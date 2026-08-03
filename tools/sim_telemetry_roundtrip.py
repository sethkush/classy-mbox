#!/usr/bin/env python3
"""
Telemetry round-trip gate — run the firmware, decode its blocks with the
tool that will read them on the bench.

WHAT THIS EXISTS FOR. Every executed gate in this tree runs the FIRMWARE.
sim_p1_waveform.py decodes what it puts on the pins; sim_ep0_requests.py
reads what it stages on EP0; sim_ep0_diff.py compares that against the image
the hardware accepted. Not one of them executes a line of the HOST side.

`tools/mboxtlm.py` is ~570 lines of decoders that turn eight bytes into the
sentences a human acts on, and until this gate existed it had never been run
against a single byte of input, on hardware or off. The firmware writes fields
at offsets in telemetry.c; the host reads fields at offsets in mboxtlm.py.
Those are two independently-maintained layouts with nothing between them, and
a disagreement does not fail loudly -- it produces a confident wrong reading,
which is precisely the failure the whole telemetry path was built to avoid.
One power cycle costs a 2 km round trip. Spending it to discover that the
reader is six blocks behind the firmware is the worst way to find out.

That is not hypothetical. `tools/mbox_telemetry.py` was a complete, working,
unreferenced second reader carrying current PID handling (touched 2026-07-28)
that read `range(5)` blocks when there were 11. Anything from block 5 on --
including block 10, the CS8427 readback the #165 power cycle is being spent
on -- silently did not exist. It has been retired; its one unique feature
(`--ep0-test`) is now `mboxtlm.py ep0test`. This gate is what keeps the next
one from accumulating.

WHAT IT CHECKS, all against the EXECUTING firmware, not against source text:

  1. Constants agree. TLM_NUM_BLOCKS / TLM_BUILD_ID / the three request codes
     are parsed out of the firmware header and compared against mboxtlm.py's
     copies. This is the `range(5)` class of bug, caught statically.
  2. Every declared block answers. Blocks 0..NUM_BLOCKS-1 are requested from
     the running image over EP0 and must each stage exactly 8 bytes.
  3. The boundary holds. Block NUM_BLOCKS must NOT be served. If the firmware
     answers a block the host never asks for, the host is behind.
  4. Every decoder runs on those real bytes without raising, and produces
     output. A decoder that crashes on the bench is a lost power cycle.
  5. The endianness seam. The host's decoded build id must equal TLM_BUILD_ID
     from the header -- firmware put16 against host u16, on a value whose two
     bytes differ, so a byte swap cannot pass.
  6. The decoders discriminate. Different blocks must decode differently, or
     the gate is reporting a constant.
  7. mboxtlm and mboxflash_linux agree on the audio PID alias range, which a
     comment in mboxtlm.py claims is "kept in step" and nothing checked.

WHAT IT DOES NOT SHOW. That the device is on the bus, that pyusb works, or
that any transport succeeded. It shows that the bytes the firmware produces
mean, to the reader, what the firmware meant by them.

Usage:  python3 tools/sim_telemetry_roundtrip.py [--verbose]
"""

import argparse
import contextlib
import importlib.util
import io
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import sim_ep0_requests as ep0     # noqa: E402  (path set above)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def header_defines(path):
    """#define NAME VALUE from a C header, integers only."""
    out = {}
    pat = re.compile(r"^\s*#define\s+(\w+)\s+((?:0[xX][0-9a-fA-F]+)|\d+)\b")
    for line in path.read_text().splitlines():
        m = pat.match(line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


def read_block(tlm, ihx, index, entry, ret_to, settle_bp):
    """Ask the running firmware for one telemetry block over EP0.

    The request is built from mboxtlm's OWN constants, not from a copy pasted
    in here. If the tool's request code or packet size drifts from the
    firmware's, this arm stops getting answers -- so the executed check
    stands on its own rather than leaning on the static comparison above.
    """
    packet = ep0.setup(tlm.REQ_IN, tlm.TLM_REQ_READ, wval=index,
                       wlen=tlm.BLOCK_SIZE)
    return ep0.run_request(ihx, packet, entry, ret_to=ret_to,
                           settle_bp=settle_bp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    build = ROOT / "mboxfw" / "build"
    ihx, mapf = build / "mboxfw.ihx", build / "mboxfw.map"
    if not ihx.exists() or not mapf.exists():
        print(f"not found: {ihx} / {mapf} (build mboxfw first)",
              file=sys.stderr)
        return 2

    tlm = load("mboxtlm", ROOT / "tools" / "mboxtlm.py")
    flash = load("mboxflash_linux", ROOT / "tools" / "mboxflash_linux.py")
    hdr = header_defines(ROOT / "mboxfw" / "include" / "telemetry.h")

    fails = []

    # ---- 1. constants: the firmware header against the host's copies -------
    print("Firmware header vs the host reader.\n")
    pairs = [
        ("block count", "TLM_NUM_BLOCKS", tlm.NUM_BLOCKS),
        ("read request", "TLM_REQ_READ", tlm.TLM_REQ_READ),
        ("reset request", "TLM_REQ_RESET", tlm.TLM_REQ_RESET),
        ("setmux request", "TLM_REQ_SET_MUX", tlm.TLM_REQ_SET_MUX),
    ]
    for label, define, host in pairs:
        fw = hdr.get(define)
        agree = fw is not None and fw == host
        print(f"  {label:<16} {define} = "
              f"{'--' if fw is None else f'0x{fw:02X}'}   "
              f"mboxtlm = 0x{host:02X}   {'ok' if agree else 'DISAGREE'}")
        if fw is None:
            fails.append(f"{define} is not in telemetry.h, so the host's "
                         f"0x{host:02X} is checked against nothing")
        elif not agree:
            fails.append(
                f"{label}: the firmware says {define} = 0x{fw:02X} and the "
                f"host reader uses 0x{host:02X}. On the bench that reads as "
                f"a device fault, not a tool that is out of step.")

    nblocks = hdr.get("TLM_NUM_BLOCKS", tlm.NUM_BLOCKS)
    have = set(tlm.DECODERS)
    want = set(range(nblocks))
    if have != want:
        missing = sorted(want - have)
        extra = sorted(have - want)
        fails.append(
            f"the firmware serves {nblocks} blocks and the host has decoders "
            f"for {sorted(have)}. Missing: {missing or 'none'}; decodes "
            f"blocks the firmware does not serve: {extra or 'none'}.")
    print(f"  decoders          {len(have)} for {nblocks} served blocks   "
          f"{'ok' if have == want else 'MISMATCH'}")

    # BLOCK_FIRST_BUILD is what separates "this block reads all-0xFF" from
    # "this build does not serve this block" -- byte-identical on the wire.
    # A device running 0x0011 reported "NO PIN ANSWERED. CDOUT is not wired"
    # for block 10 on 2026-08-03 because the host guessed from its own block
    # count instead. The newest block must be gated on the CURRENT build id,
    # or the next block added inherits a stale gate and reads as served on
    # every older device.
    first = getattr(tlm, "BLOCK_FIRST_BUILD", {})
    if set(first) != want:
        fails.append(
            f"BLOCK_FIRST_BUILD covers {sorted(first)} but the firmware "
            f"serves {sorted(want)}. A block with no entry cannot be told "
            f"from the out-of-range sentinel on any older build.")
    else:
        newest = first[nblocks - 1]
        cur = hdr.get("TLM_BUILD_ID")
        if cur is not None and newest != cur:
            fails.append(
                f"block {nblocks - 1} is the newest block and "
                f"BLOCK_FIRST_BUILD says it arrived in 0x{newest:04X}, but "
                f"TLM_BUILD_ID is 0x{cur:04X}. Bumping TLM_NUM_BLOCKS without "
                f"recording the build that did it makes the new block read as "
                f"served on devices that do not have it.")
        if sorted(first.values()) != list(first.values()):
            fails.append(
                "BLOCK_FIRST_BUILD is not monotonic in block index; blocks "
                "are only ever appended, so a later block cannot predate an "
                "earlier one.")
    print(f"  block-origin map  newest block 0x{first.get(nblocks - 1, 0):04X} "
          f"vs TLM_BUILD_ID 0x{hdr.get('TLM_BUILD_ID', 0):04X}   "
          f"{'ok' if first.get(nblocks - 1) == hdr.get('TLM_BUILD_ID') else 'MISMATCH'}")

    # mboxtlm.py's comment claims this is kept in step with the flasher.
    # It also legitimately includes 0x1000 (the quirked default PID, where
    # EP0 telemetry works but snd-usb-audio never binds), so the invariant
    # is over the ALIAS RANGE, not the whole tuple.
    tlm_aliases = tuple(p for p in tlm.AUDIO_PIDS if p != 0x1000)
    if tlm_aliases != flash.AUDIO_PID_ALIASES:
        fails.append(
            f"mboxtlm's audio PID aliases {tlm_aliases} and the flasher's "
            f"{flash.AUDIO_PID_ALIASES} disagree, but mboxtlm.py's comment "
            f"says they are kept in step. A tool that cannot see the unit "
            f"the other tool just flashed looks like a dead device.")
    print(f"  PID aliases       {len(tlm_aliases)} shared with the flasher   "
          f"{'ok' if tlm_aliases == flash.AUDIO_PID_ALIASES else 'DISAGREE'}")

    # ---- 2..6. execute the firmware and decode what it produces ------------
    want_id = hdr.get("TLM_BUILD_ID")
    syms = ep0.symbols(mapf)
    loop, service = syms.get("_buttons_poll"), syms.get("_usb_service")
    if loop is None or service is None:
        print("could not resolve _buttons_poll / _usb_service from the map",
              file=sys.stderr)
        return 2

    print("\nEvery block, read out of the running firmware and decoded by "
          "the bench tool.\n")
    decoded = {}
    for i in range(nblocks):
        outcome, staged, cnt, _cnf = read_block(
            tlm, ihx, i, service, ret_to=loop, settle_bp=f"0x{loop:04X}")
        raw = " ".join(f"{b:02X}" for b in staged) if staged else "--"
        if outcome != ep0.ARMED or cnt != 8:
            fails.append(
                f"block {i}: the firmware answered {outcome} with count="
                f"{cnt}, not 8 bytes. mboxtlm.read_block() treats a short "
                f"read as an error, so this block is unreadable on the bench.")
            print(f"  block {i:<2} {outcome} count={cnt}")
            continue
        # Route through show(), not DECODERS[i]. An earlier version of this
        # gate called the decoder directly and passed while the bench tool
        # printed nothing of the sort: show() intercepted all-0xFF as the
        # unknown-block sentinel, so block 10's decoder was unreachable for an
        # all-0xFF reading. Checking the layer under the one
        # that runs on the bench is not checking the bench tool.
        try:
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                # Pass the build id the firmware itself reported, the same way
                # mboxtlm's own commands do. Without it every all-0xFF block
                # reads back AMBIGUOUS and the served/not-served logic never
                # runs -- the gate would be checking a path the bench never
                # takes.
                tlm.show(i, staged, device_build=want_id)
            printed = buf.getvalue()
            lines = [ln.strip() for ln in printed.splitlines()[1:] if ln.strip()]
        except Exception as e:                       # noqa: BLE001
            fails.append(
                f"block {i}: {type(e).__name__}: {e} — the host decoder "
                f"crashes on bytes the firmware actually produces. That is "
                f"a lost power cycle, discovered here instead.")
            print(f"  block {i:<2} {raw}   DECODER RAISED {type(e).__name__}")
            continue
        if not lines:
            fails.append(f"block {i}: the decoder produced no output at all")
        # The sentinel names an index the firmware does not serve. Printing it
        # for one it does means a real reading has been reported as a tool
        # error -- and the block's own decoder never ran.
        if any("unknown block index" in ln for ln in lines):
            fails.append(
                f"block {i} is served by the firmware, but the bench tool "
                f"prints the unknown-block-index sentinel for the bytes it "
                f"produced ({raw}). Its decoder never runs, so a real "
                f"measurement reads as a tool/firmware mismatch.")
        decoded[i] = lines
        print(f"  block {i:<2} {raw}")
        if args.verbose:
            for ln in lines:
                print(f"           {ln}")
        else:
            print(f"           {lines[0]}")

    # ---- 5. the endianness seam, on a value with two distinct bytes -------
    if want_id is None:
        fails.append("TLM_BUILD_ID is not in telemetry.h")
    elif (want_id & 0xFF) == (want_id >> 8):
        fails.append(
            f"TLM_BUILD_ID is 0x{want_id:04X}, whose two bytes are equal, so "
            f"the round-trip cannot distinguish put16 from a byte swap. Bump "
            f"it to a value with distinct bytes.")
    elif 0 in decoded:
        got = next((ln for ln in decoded[0] if ln.startswith("build id:")),
                   None)
        m = re.search(r"0x([0-9A-Fa-f]{4})", got or "")
        got_id = int(m.group(1), 16) if m else None
        print(f"\n  build id   firmware 0x{want_id:04X}   "
              f"host read 0x{got_id:04X}" if got_id is not None
              else f"\n  build id   host produced no readable value")
        if got_id != want_id:
            fails.append(
                f"the firmware's TLM_BUILD_ID is 0x{want_id:04X} and the host "
                f"decodes block 0 as 0x{got_id if got_id is None else got_id:04X}"
                f". Block 0 is how you prove which image is running; if it "
                f"reads wrong, every later reading is attributed to the "
                f"wrong build.")

    # ---- 3. the boundary: an undeclared block must not be served ----------
    outcome, staged, cnt, _ = read_block(
        tlm, ihx, nblocks, service, ret_to=loop, settle_bp=f"0x{loop:04X}")
    served = outcome == ep0.ARMED and cnt == 8 and staged is not None \
        and not all(b == 0xFF for b in staged)
    print(f"  boundary   block {nblocks} -> {outcome} count={cnt}   "
          f"{'SERVED' if served else 'not served, as expected'}")
    if served:
        fails.append(
            f"the firmware serves block {nblocks}, which TLM_NUM_BLOCKS says "
            f"does not exist. mboxtlm reads 0..{nblocks - 1}, so whatever "
            f"that block reports is invisible on the bench — the exact shape "
            f"of the retired mbox_telemetry.py bug.")

    # ---- 6. the decoders have to discriminate ----------------------------
    shapes = {tuple(v) for v in decoded.values()}
    print(f"  discrimination  {len(shapes)} distinct decodings across "
          f"{len(decoded)} blocks")
    if len(decoded) > 1 and len(shapes) < 2:
        fails.append(
            "every block decoded to the same text, so this gate is reporting "
            "a constant rather than reading the firmware.")

    print()
    if fails:
        print("FAIL:")
        for f in fails:
            print(f"  - {f}")
        return 1
    print("PASS: the firmware's telemetry blocks and the tool that reads them "
          "on the bench agree, executed end to end.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
