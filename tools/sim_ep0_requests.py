#!/usr/bin/env python3
"""
EP0 request gate — feed the firmware a SETUP packet and read the reply.

Every executed check this project has ever run drives the boot path with NO
INPUT. `sim_smoke.sh` asks "did it reach the main loop". `sim_p1_waveform.py`
asks "what did it emit on the pins while starting up". Neither hands the
firmware a request, so nothing has ever confirmed that mboxfw ANSWERS one.

The reason that gap survived is a bad inference, recorded here because it cost
a round of "this needs hardware": *the USB engine is not modelled* was read as
*the request path cannot be executed*. Those are different claims. The SIE and
the UBM are not modelled -- but the firmware's request handling is ordinary
8051 code reading a SETUP packet out of XDATA at 0xFF28, and ucSim models XDATA
as plain RAM. The missing piece was never the model. It was the stimulus.

    write the 8 SETUP bytes to 0xFF28
    write VECINT (0xFFB2) = 0x12          -- VEC_SETUP
    enter usb_service()
    break on the write to IEPDCNTX0       -- "packet armed"
    read the staged bytes back out of the EP0 IN buffer

WHAT THIS DOES NOT SHOW. That the SIE delivered the packet, that the UBM
handed it over, that anything happened on the wire, or any timing. It shows
what the firmware replies once a SETUP has landed in the buffer. #165 on real
hardware is still the only thing that proves the CS8427 heard us.

VALIDATION, three ways, because a harness that delivers nothing looks exactly
like firmware that answers nothing.

  1. Two independent readings must agree. The descriptor bytes the handler
     STAGES are compared against the bytes sitting in ROM at the address the
     linker map gives for `_AppDevDesc` / `_AppConfigDesc` -- executed output
     against static table, neither derived from the other. A harness that
     delivered nothing could not produce those exact bytes.

  2. A NO-STIMULUS control. The identical sequence runs with the SETUP packet
     and VECINT left alone, and nothing may be staged. This is what separates
     "the firmware answered the request" from "the firmware was going to write
     that buffer anyway".

  3. The replies must DISCRIMINATE. Different requests have to produce
     different staged bytes and different counts, or the gate is reporting a
     constant.

There is deliberately no stock arm. Rev 20's `usb_ev_setup` (0x0026) reads
0xFF28 and handles class requests inline, but defers standard requests to the
work-code dispatcher, so it stages nothing at 0x0026 and writes IEPDCNTX0 = 0
unconditionally. Measured: GET_DESCRIPTOR, a class OUT, a class IN and an
all-0xFF garbage packet all produce byte-identical results. A stock arm built
on that would pass no matter what the harness did -- which is worse than
having none.

Usage:  python3 tools/sim_ep0_requests.py [--verbose]
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sim_p1_waveform import Sim, ROOT                   # noqa: E402

SETPACK      = 0xFF28       # bmRequestType, bRequest, wValueL/H, wIndexL/H, wLengthL/H
VECINT       = 0xFFB2
VEC_SETUP    = 0x12
IEPCNF0      = 0xFF68
IEPBBAX0     = 0xFF69
IEPDCNTX0    = 0xFF6B
STC_BUFFER_BASE = 0xF800    # EP_BBAX in regs.h: (addr - 0xF800) >> 3
STALL_BIT    = 0x08         # IEPCNF0 bit 3

ARMED  = "armed"
STALL  = "stall"


def setup(bmreq, breq, wval=0, widx=0, wlen=0):
    return [bmreq, breq, wval & 0xFF, (wval >> 8) & 0xFF,
            widx & 0xFF, (widx >> 8) & 0xFF, wlen & 0xFF, (wlen >> 8) & 0xFF]


REQUESTS = [
    ("GET_DESCRIPTOR device",  setup(0x80, 0x06, 0x0100, 0, 18), ARMED),
    ("GET_DESCRIPTOR config",  setup(0x80, 0x06, 0x0200, 0,  9), ARMED),
    ("GET_STATUS device",      setup(0x80, 0x00, 0,      0,  2), ARMED),
    ("telemetry read block 0", setup(0xC0, 0x10, 0,      0,  8), ARMED),
    # bRequest 0x0C is not a defined standard request. STALL is the
    # spec-correct answer and usb.c says so at the default case.
    ("undefined bRequest 0x0C", setup(0x80, 0x0C, 0,     0,  2), STALL),
]


def symbols(mapfile):
    out = {}
    for line in Path(mapfile).read_text().splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "C:" and parts[2].startswith("_"):
            out[parts[2]] = int(parts[1], 16)
    return out


def rom_bytes(ihx):
    """Flatten an Intel-HEX file into a code-space byte array."""
    data = bytearray(0x10000)
    for line in Path(ihx).read_text().splitlines():
        line = line.strip()
        if not line.startswith(":"):
            continue
        n = int(line[1:3], 16)
        a = int(line[3:7], 16)
        t = int(line[7:9], 16)
        if t != 0:
            continue
        data[a:a + n] = bytes.fromhex(line[9:9 + n * 2])
    return data


class Ep0Sim(Sim):
    """Sim plus the few XDATA pokes a request needs."""

    def poke(self, addr, value):
        self.cmd(f"fill xram 0x{addr:04x} 0x{addr:04x} 0x{value:02x}")

    def peek(self, addr, count=1):
        self._send(f"dx 0x{addr:04x} 0x{addr + count - 1:04x}")
        line = self._read_until(re.compile(rf"^0x{addr:04x}\s"))
        if line is None:
            return None
        toks = line.split()[1:1 + count]
        try:
            return [int(t, 16) for t in toks]
        except ValueError:
            return None

    def peek_sfr(self, addr):
        self._send(f"dump sfr 0x{addr:02x} 0x{addr:02x}")
        line = self._read_until(re.compile(rf"^0x{addr:02x}\s"))
        if line is None:
            return None
        m = re.search(r"(0x[0-9a-f]{2})\s+'", line)
        return int(m.group(1), 16) if m else None

    def deliver(self, packet):
        if packet is None:
            return                      # the no-stimulus control
        for i, b in enumerate(packet):
            self.poke(SETPACK + i, b)
        self.poke(VECINT, VEC_SETUP)


POISON = 0xEE       # written to IEPDCNTX0 before every run


def run_request(ihx, packet, entry, ret_to, settle_bp=None):
    """Deliver one SETUP and report what the firmware staged.

    Returns (outcome, staged_bytes, count, iepcnf0).

    The call is given a REAL RETURN ADDRESS. Entering usb_service() with
    `pc` alone leaves the stack holding whatever was there, so the RET
    wandered into unrelated code that went on writing USB registers -- the
    no-stimulus control came back "armed" on a run where nothing had been
    delivered at all. Pushing `ret_to` and breaking there bounds the run to
    exactly one call.

    Outcome is decided by whether IEPDCNTX0 CHANGED from POISON, not by
    whether the simulator stopped. Stopping means a breakpoint was hit; it
    says nothing about what the firmware did.
    """
    sim = Ep0Sim(ihx)
    if settle_bp is not None:
        # Let init finish first: descriptors are served out of a buffer whose
        # base register usb_ep0_setup() writes, so a request delivered before
        # that would be answered into an address the firmware has not chosen.
        sim.cmd(f"break {settle_bp}")
        sim.run_to_stop()
        sim.cmd("delete 0")

    # Poison the count register: without this a leftover value reads as a
    # response, and "nothing was staged" is indistinguishable from "something
    # was staged before I looked".
    sim.cmd(f"fill xram 0x{IEPDCNTX0:04x} 0x{IEPDCNTX0:04x} 0x{POISON:02x}")
    cnf_before = sim.peek(IEPCNF0)
    sim.deliver(packet)

    sp = sim.peek_sfr(0x81)
    if sp is None:
        sim.close()
        raise RuntimeError("could not read SP")
    sim.cmd(f"fill iram 0x{sp + 1:02x} 0x{sp + 1:02x} 0x{ret_to & 0xFF:02x}")
    sim.cmd(f"fill iram 0x{sp + 2:02x} 0x{sp + 2:02x} 0x{(ret_to >> 8) & 0xFF:02x}")
    sim.cmd(f"fill sfr 0x81 0x81 0x{sp + 2:02x}")

    sim.cmd(f"break 0x{ret_to:04X}")
    sim.cmd(f"pc 0x{entry:04X}")
    sim.run_to_stop()

    cnf = sim.peek(IEPCNF0)
    bbax = sim.peek(IEPBBAX0)
    cnt = sim.peek(IEPDCNTX0)
    staged = None
    if bbax:
        base = STC_BUFFER_BASE + (bbax[0] << 3)
        staged = sim.peek(base, 8)
    sim.close()

    cnf = cnf[0] if cnf else None
    cnt = cnt[0] if cnt else None
    before = cnf_before[0] if cnf_before else 0

    if cnt is not None and cnt != POISON:
        return ARMED, staged, cnt, cnf
    if cnf is not None and (cnf & STALL_BIT) and not (before & STALL_BIT):
        return STALL, staged, cnt, cnf
    return "no-response", staged, cnt, cnf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    build = ROOT / "mboxfw" / "build"
    ihx = build / "mboxfw.ihx"
    mapf = build / "mboxfw.map"
    if not ihx.exists() or not mapf.exists():
        print(f"not found: {ihx} / {mapf} (build mboxfw first)", file=sys.stderr)
        return 2

    syms = symbols(mapf)
    rom = rom_bytes(ihx)
    loop = syms.get("_buttons_poll")
    service = syms.get("_usb_service")
    if loop is None or service is None:
        print("could not resolve _buttons_poll / _usb_service from the map",
              file=sys.stderr)
        return 2

    print("Delivering SETUP packets to the running firmware and reading the "
          "replies.\n")
    print(f"    _usb_service  0x{service:04X}    _buttons_poll  0x{loop:04X}\n")

    fails = []
    results = []
    for name, packet, expect in REQUESTS:
        outcome, staged, cnt, cnf = run_request(
            ihx, packet, service, ret_to=loop, settle_bp=f"0x{loop:04X}")
        results.append((name, packet, expect, outcome, staged, cnt, cnf))
        shown = " ".join(f"{b:02X}" for b in staged) if staged else "--"
        print(f"  {name:<24} {' '.join(f'{b:02X}' for b in packet)}")
        print(f"  {'':<24} -> {outcome:<12} count={cnt} IEPCNF0="
              f"{'--' if cnf is None else f'0x{cnf:02X}'}  staged: {shown}")
        if outcome != expect:
            fails.append(f"{name}: expected {expect}, got {outcome}")
    print()

    # ---- executed output vs the static table, two independent readings ----
    for name, sym, req in (("device", "_AppDevDesc", "GET_DESCRIPTOR device"),
                           ("config", "_AppConfigDesc", "GET_DESCRIPTOR config")):
        addr = syms.get(sym)
        if addr is None:
            fails.append(f"{sym} not in the map, so the {name} descriptor "
                         f"expectation cannot be derived independently")
            continue
        want = list(rom[addr:addr + 8])
        got = next((r[4] for r in results if r[0] == req), None)
        print(f"  {sym} @ 0x{addr:04X} in ROM : "
              + " ".join(f"{b:02X}" for b in want))
        print(f"  {'staged by the handler':<24} : "
              + (" ".join(f"{b:02X}" for b in got) if got else "--"))
        if got is None:
            fails.append(f"{req} staged nothing to compare against {sym}")
        elif got != want:
            fails.append(
                f"{req}: the handler staged "
                f"{' '.join(f'{b:02X}' for b in got)} but {sym} in ROM holds "
                f"{' '.join(f'{b:02X}' for b in want)}. The table and what "
                f"goes out on the wire disagree.")
    print()

    # ---- control 1: no stimulus, no reply ----
    # The same sequence with the SETUP packet and VECINT left alone. If
    # anything is staged here, the "replies" above are not replies.
    outcome, staged, cnt, cnf = run_request(
        ihx, None, service, ret_to=loop, settle_bp=f"0x{loop:04X}")
    shown = " ".join(f"{b:02X}" for b in staged) if staged else "--"
    print(f"  control, no stimulus delivered -> {outcome}, count={cnt}, "
          f"staged: {shown}")
    if outcome != "no-response":
        fails.append(
            f"with NO SETUP packet and NO VECINT the firmware still reached "
            f"{outcome} (count={cnt}). Nothing above is caused by the "
            f"stimulus, so nothing above is a reply.")

    # ---- control 2: the replies must discriminate ----
    replies = [(r[0], tuple(r[4] or ()), r[5]) for r in results if r[3] == ARMED]
    distinct = {(b, c) for _, b, c in replies}
    print(f"  control, {len(replies)} armed replies, {len(distinct)} distinct")
    if len(replies) > 1 and len(distinct) == 1:
        fails.append(
            "every armed request produced identical bytes and an identical "
            "count, so this gate is reporting a constant rather than a "
            "response. That is how a vacuous check looks from the outside.")
    print()

    if fails:
        print("FAIL:")
        for f in fails:
            print(f"  - {f}")
        return 1

    print("PASS: every request answered as expected, the staged descriptors "
          "match the ROM table, no reply without a stimulus, and the "
          "replies discriminate.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
