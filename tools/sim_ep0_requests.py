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
from sim_p1_waveform import Sim, ROOT, decode           # noqa: E402

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
    # #165. Block 10 runs a CS8427 READ over the SPI control port -- novel
    # code with no stock address to cite, so it gets executed before it is
    # ever flashed rather than after.
    ("telemetry read block 10", setup(0xC0, 0x10, 10,     0,  8), ARMED),
]


def symbols(mapfile):
    """CODE-space symbols. Rows carry a `C:` area tag."""
    out = {}
    for line in Path(mapfile).read_text().splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "C:" and parts[2].startswith("_"):
            out[parts[2]] = int(parts[1], 16)
    return out


def data_symbols(mapfile):
    """DSEG symbols, which are listed WITHOUT an area tag:

        C:   000017CD  _AppDevDesc      descriptors     <- code
             0000000B  _g_codec_state_25  codec         <- data

    Reusing symbols() for these silently returns nothing, which is how the
    suspend check first came out as a crash rather than a result. Code and
    data addresses are kept apart because they are different spaces and 0x0B
    is a legal address in both.
    """
    out = {}
    for line in Path(mapfile).read_text().splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0].startswith("0") and \
                parts[1].startswith("_"):
            try:
                out[parts[1]] = int(parts[0], 16)
            except ValueError:
                pass
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


def run_transfer(ihx, packet, entry, ret_to, settle_bp, max_chunks=64):
    """Drive a MULTI-PACKET control read to completion in one session.

    Deliver the SETUP, then hand back a VEC_IEP0 ("the packet you armed has
    gone") over and over, exactly as the hardware would, collecting each
    chunk the firmware stages. This is the path the ~12% EP0 IN packet loss
    measured on hardware lives on, and the one #148's Y-count work was
    about. It has never been executed.

    State has to persist across chunks -- g_ep0_reply_src and
    g_ep0_reply_remaining are the whole mechanism -- so unlike run_request()
    this uses a SINGLE simulator session for the entire transfer.

    Returns (chunks, counts) where chunks is the reassembled byte string.
    """
    return run_session(ihx, [(packet, max_chunks, True)], entry, ret_to,
                       settle_bp)[0]


def run_session(ihx, steps, entry, ret_to, settle_bp):
    """Drive several control transfers through ONE simulator session.

    State that survives a transfer is the whole point. `usb.c` records the
    bug this catches: macOS asks for the device descriptor with wLength=64,
    the firmware clamps to 18 and ships 8, leaving g_ep0_reply_remaining =
    10. macOS only wanted bMaxPacketSize0, abandons, and sends SET_ADDRESS.
    If the new SETUP does not clear that counter, the status ZLP is replaced
    by 8 leftover descriptor bytes, EP0 desynchronises, and enumeration
    never completes. Shipped once, found by an LED canary, fixed 2026-07-26.
    A per-transfer harness cannot see it.

    steps is [(packet, max_chunks, drain)]. `drain` stops at the first
    short packet, which is how a control read ends; without it the step
    takes exactly max_chunks calls, which is what the abandoned-transfer
    case needs -- its evidence arrives on the continuation AFTER a
    zero-length status stage.

    Returns [(bytes, counts)] per step.
    """
    sim = Ep0Sim(ihx)
    sim.cmd(f"break {settle_bp}")
    sim.run_to_stop()
    sim.cmd("delete 0")
    sim.cmd(f"break 0x{ret_to:04X}")

    results = []

    def one_call():
        # SP is back where it started after each RET, so re-push every time.
        sp = sim.peek_sfr(0x81)
        if sp is None:
            raise RuntimeError("could not read SP")
        sim.cmd(f"fill iram 0x{sp + 1:02x} 0x{sp + 1:02x} 0x{ret_to & 0xFF:02x}")
        sim.cmd(f"fill iram 0x{sp + 2:02x} 0x{sp + 2:02x} "
                f"0x{(ret_to >> 8) & 0xFF:02x}")
        sim.cmd(f"fill sfr 0x81 0x81 0x{sp + 2:02x}")
        sim.cmd(f"pc 0x{entry:04X}")
        sim.run_to_stop()

    def harvest():
        cnt = sim.peek(IEPDCNTX0)
        bbax = sim.peek(IEPBBAX0)
        if not cnt or not bbax or cnt[0] == POISON:
            return None
        n = cnt[0] & 0x7F          # bit 7 is NAK, not part of the count
        base = STC_BUFFER_BASE + (bbax[0] << 3)
        data = sim.peek(base, 8) or []
        return n, bytes(data[:n])

    for packet, max_chunks, drain in steps:
        collected = bytearray()
        counts = []
        sim.cmd(f"fill xram 0x{IEPDCNTX0:04x} 0x{IEPDCNTX0:04x} 0x{POISON:02x}")
        sim.deliver(packet)
        one_call()
        got = harvest()
        if got:
            counts.append(got[0])
            collected += got[1]

        # NOT bounded by "nothing new was armed": once a transfer has
        # drained, every further VEC_IEP0 arms a ZERO-LENGTH packet, so the
        # poison is always overwritten and that test never fires. Measured,
        # after it ran the config descriptor out to 64 calls.
        while len(counts) < max_chunks:
            if drain and counts and counts[-1] < 8:
                break
            sim.cmd(f"fill xram 0x{IEPDCNTX0:04x} 0x{IEPDCNTX0:04x} "
                    f"0x{POISON:02x}")
            sim.poke(VECINT, 0x08)      # VEC_IEP0 -- "your packet went out"
            one_call()
            got = harvest()
            if got is None:
                break
            counts.append(got[0])
            collected += got[1]
        results.append((bytes(collected), counts))

    sim.close()
    return results


EP0_OUT_BUF = 0xFA10
VEC_OEP0    = 0x00
VEC_RESR    = 0x15
VEC_SUSR    = 0x16


def run_scenario(ihx, actions, entry, ret_to, settle_bp, watch_iram=None):
    """Drive an arbitrary sequence of USB events through one session.

    An action is a dict:
        {"setup": [8 bytes]}   deliver a SETUP packet
        {"vec": code}          hand over any other VECINT (IEP0/OEP0/SUSR/RESR)
        {"call": addr}         enter another function instead of usb_service
        {"break_iram_w": addr} stop on a write to that IRAM byte, for code
                               that never returns (do_suspend() blocks in
                               PCON idle until the host resumes it)
        {"out": [bytes]}       stage bytes in the EP0 OUT buffer first
        {"label": str}         for the report

    Returns one observation per action: (count, staged[8], iram, write_seen).
    `write_seen` is False when a break_iram_w was asked for and the run never
    stopped on it -- i.e. the expected write never happened. Without that
    distinction a missing write looks like a failed read, which is how the
    suspend mutation first reported "could not read the mirror" instead of
    "do_suspend never touched it".
    `count` is None when nothing was armed -- for an OUT data stage or a
    suspend that is the correct answer, not a failure.

    State persists across actions because that is the entire point: alt
    settings, a pending SET_CUR data stage and the suspend/resume flag all
    live between requests.
    """
    sim = Ep0Sim(ihx)
    sim.cmd(f"break {settle_bp}")
    sim.run_to_stop()
    sim.cmd("delete 0")
    sim.cmd(f"break 0x{ret_to:04X}")

    obs = []
    for act in actions:
        sim.cmd(f"fill xram 0x{IEPDCNTX0:04x} 0x{IEPDCNTX0:04x} 0x{POISON:02x}")
        for i, b in enumerate(act.get("out", [])):
            sim.poke(EP0_OUT_BUF + i, b)
        if "setup" in act:
            sim.deliver(act["setup"])
        if "vec" in act:
            sim.poke(VECINT, act["vec"])

        sp = sim.peek_sfr(0x81)
        sim.cmd(f"fill iram 0x{sp + 1:02x} 0x{sp + 1:02x} 0x{ret_to & 0xFF:02x}")
        sim.cmd(f"fill iram 0x{sp + 2:02x} 0x{sp + 2:02x} "
                f"0x{(ret_to >> 8) & 0xFF:02x}")
        sim.cmd(f"fill sfr 0x81 0x81 0x{sp + 2:02x}")
        biw = act.get("break_iram_w")
        if biw is not None:
            sim.cmd(f"break iram w 0x{biw:02x}")
        sim.cmd(f"pc 0x{act.get('call', entry):04X}")
        stopped = sim.run_to_stop()
        write_seen = True
        if biw is not None:
            write_seen = stopped is not None and stopped != ret_to
            sim.cmd("delete")
            sim.cmd(f"break 0x{ret_to:04X}")

        cnt = sim.peek(IEPDCNTX0)
        cnt = cnt[0] if cnt else None
        staged = None
        if cnt is not None and cnt != POISON:
            bbax = sim.peek(IEPBBAX0)
            if bbax:
                staged = sim.peek(STC_BUFFER_BASE + (bbax[0] << 3), 8)
        else:
            cnt = None
        iram = None
        if watch_iram is not None:
            sim._send(f"dump iram 0x{watch_iram:02x} 0x{watch_iram:02x}")
            line = sim._read_until(re.compile(rf"^0x{watch_iram:02x}\s"))
            if line:
                m = re.search(rf"^0x{watch_iram:02x}\s+([0-9a-f]{{2}})", line)
                iram = int(m.group(1), 16) if m else None
        obs.append((cnt, staged, iram, write_seen))
    sim.close()
    return obs


def trace_request_p1(ihx, packet, entry, ret_to, settle_bp):
    """Deliver a request and capture the P1 waveform it produces.

    #165's read is the first code in this firmware that clocks the CS8427
    control port in a direction stock never used, so "it returned 8 bytes" is
    not enough -- the framing has to be right too. Breaking on writes to P1
    for the duration of one request, and decoding the result with the same
    decoder sim_p1_waveform.py validates against both stock images, checks
    the transaction the part would actually see.
    """
    sim = Ep0Sim(ihx)
    sim.cmd(f"break {settle_bp}")
    sim.run_to_stop()
    sim.cmd("delete 0")

    sim.deliver(packet)
    sp = sim.peek_sfr(0x81)
    sim.cmd(f"fill iram 0x{sp + 1:02x} 0x{sp + 1:02x} 0x{ret_to & 0xFF:02x}")
    sim.cmd(f"fill iram 0x{sp + 2:02x} 0x{sp + 2:02x} 0x{(ret_to >> 8) & 0xFF:02x}")
    sim.cmd(f"fill sfr 0x81 0x81 0x{sp + 2:02x}")

    sim.cmd("break sfr w 0x90")
    sim.cmd(f"break 0x{ret_to:04X}")
    sim.cmd(f"pc 0x{entry:04X}")

    samples = [sim.read_p1()]
    for _ in range(20000):
        pc = sim.run_to_stop()
        if pc is None or pc == ret_to:
            break
        v = sim.read_p1()
        if v is None:
            break
        samples.append(v)
    sim.close()
    return samples


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

    # ---- the multi-packet path ----
    cfg_addr = syms.get("_AppConfigDesc")
    total = (rom[cfg_addr + 2] | (rom[cfg_addr + 3] << 8)) if cfg_addr else 0
    print(f"  multi-packet: GET_DESCRIPTOR config, wLength={total} "
          f"(wTotalLength from ROM)")
    data, counts = run_transfer(
        ihx, setup(0x80, 0x06, 0x0200, 0, total), service,
        ret_to=loop, settle_bp=f"0x{loop:04X}")
    want = bytes(rom[cfg_addr:cfg_addr + total]) if cfg_addr else b""
    print(f"    {len(counts)} chunks, counts {counts}")
    print(f"    reassembled {len(data)} of {total} bytes")
    if args.verbose:
        print("    " + " ".join(f"{b:02X}" for b in data))
    expect_chunks = (total + 7) // 8
    if len(counts) != expect_chunks:
        fails.append(
            f"the transfer took {len(counts)} chunks; {total} bytes at 8 per "
            f"packet is {expect_chunks}. A short count means the continuation "
            f"stopped early; a long one means a chunk was repeated.")
    if data != want:
        first = next((i for i in range(min(len(data), len(want)))
                      if data[i] != want[i]), min(len(data), len(want)))
        fails.append(
            f"the reassembled transfer does not match _AppConfigDesc: "
            f"{len(data)} bytes vs {len(want)}, first difference at offset "
            f"{first}. The bytes the firmware ships across packet boundaries "
            f"are not the descriptor it holds.")
    print()

    # ---- #165: the framing of the CS8427 read, before it is flashed ----
    samples = trace_request_p1(ihx, setup(0xC0, 0x10, 10, 0, 8), service,
                               ret_to=loop, settle_bp=f"0x{loop:04X}")
    r = decode(samples)
    data_tx = [(by, n) for by, n in r["transactions"] if n]
    print(f"  #165 CS8427 read framing: {len(samples)} P1 writes, "
          f"{len(r['transactions'])} CS-low periods")
    for by, n in r["transactions"]:
        print("      " + (f"select pulse, {n} clocks" if not by else
              f"{n:3d} clocks  " + " ".join(f"{b:02X}" for b in by)))
    if not data_tx:
        fails.append("#165: the block-10 read clocked no CS-framed transaction "
                     "at all -- the probe never reached the control port.")
    else:
        by, n = data_tx[0]
        if n != 24:
            fails.append(f"#165: the read is {n} clocks, not 24. DS477F5 s9.1 "
                         f"is address + MAP + eight read clocks.")
        if by[0] != 0x21:
            fails.append(f"#165: the read opens with 0x{by[0]:02X}, not 0x21. "
                         f"Bit 0 is the R/W bit and it must be SET for a read; "
                         f"0x20 would be a write and would clobber the register.")
        if len(by) > 1 and by[1] != 0x04:
            fails.append(f"#165: the MAP byte is 0x{by[1]:02X}, not 0x04 "
                         f"(CLOCKSOURCE) -- the wrong register is being read.")
    print()

    # ---- the abandoned-transfer regression ----
    # Exactly the sequence usb.c documents having shipped broken: a
    # wLength=64 device-descriptor request (clamped to 18, one chunk of 8
    # shipped, 10 left over), abandoned, then SET_ADDRESS. The status stage
    # must be a zero-length packet, not the 8 stale descriptor bytes.
    steps = run_session(
        ihx,
        [(setup(0x80, 0x06, 0x0100, 0, 64), 1, False),   # ask, take one chunk
         (setup(0x00, 0x05, 3, 0, 0), 2, False)],        # abandon; SET_ADDRESS 3
        service, ret_to=loop, settle_bp=f"0x{loop:04X}")
    (first_data, first_counts), (addr_data, addr_counts) = steps
    print("  abandoned transfer then SET_ADDRESS "
          "(the enumeration bug usb.c records shipping)")
    print(f"    wLength=64 device request: counts {first_counts}, "
          + " ".join(f"{b:02X}" for b in first_data))
    print(f"    SET_ADDRESS status stage : counts {addr_counts}")
    if first_counts != [8]:
        fails.append(f"a wLength=64 device-descriptor request shipped "
                     f"{first_counts}; the first chunk is 8 bytes.")
    if len(addr_counts) < 2:
        fails.append(f"SET_ADDRESS produced {addr_counts}; the scenario needs "
                     f"the status stage AND the continuation after it.")
    elif addr_counts[0] != 0:
        fails.append(
            f"the SET_ADDRESS status stage shipped {addr_counts[0]} bytes, "
            f"not a zero-length packet.")
    elif addr_counts[1] != 0:
        # The status stage itself is fine either way; the leftover goes out
        # on the NEXT completion, which is where this has to look.
        fails.append(
            f"after the SET_ADDRESS status stage the firmware shipped "
            f"{addr_counts[1]} more bytes. That is the leftover "
            f"g_ep0_reply_remaining from the abandoned transfer going out "
            f"behind the status stage -- EP0 desynchronises and enumeration "
            f"never completes. usb.c documents shipping exactly this once, "
            f"found by an LED canary and fixed 2026-07-26.")
    print()

    # ---- #40: SET_INTERFACE alt gating, across a session ----
    # Alt settings are STATE. One request cannot show that setting 1 sticks,
    # that reading it back agrees, or that returning to 0 takes. All four
    # steps run in one session for that reason.
    obs = run_scenario(ihx, [
        {"setup": setup(0x01, 0x0B, 1, 1, 0)},    # SET_INTERFACE iface 1 alt 1
        {"setup": setup(0x81, 0x0A, 0, 1, 1)},    # GET_INTERFACE iface 1
        {"setup": setup(0x01, 0x0B, 0, 1, 0)},    # back to alt 0
        {"setup": setup(0x81, 0x0A, 0, 1, 1)},    # GET_INTERFACE again
    ], service, ret_to=loop, settle_bp=f"0x{loop:04X}")
    got = [(c, (s[0] if s else None)) for c, s, _, _ in obs]
    print(f"  #40 SET_INTERFACE alt gating: {got}")
    if got[0][0] != 0:
        fails.append(f"SET_INTERFACE iface 1 alt 1 did not answer with a "
                     f"zero-length status stage (count={got[0][0]}).")
    if got[1][0] != 1 or got[1][1] != 1:
        fails.append(f"GET_INTERFACE after alt 1 returned count={got[1][0]} "
                     f"value={got[1][1]}; expected 1 byte reading 1. The alt "
                     f"setting did not stick.")
    if got[3][0] != 1 or got[3][1] != 0:
        fails.append(f"GET_INTERFACE after returning to alt 0 read "
                     f"{got[3][1]}; the interface never went back down, so "
                     f"the streaming endpoints would stay armed.")
    print()

    # ---- #41: the 3-byte SET_CUR sample rate, round trip ----
    # A control-OUT with a DATA STAGE: the SETUP alone carries no rate. The
    # bytes arrive later on VEC_OEP0, which is why this needs the scenario
    # runner and not a single request. Rev 20 reads only src[0] and so
    # accepts nonsense; mboxfw parses all 24 bits and stalls what its
    # descriptors do not advertise, so both halves are checked.
    RATE = [0x44, 0xAC, 0x00]                       # 44100 = 0x00AC44
    obs = run_scenario(ihx, [
        {"setup": setup(0x22, 0x01, 0x0100, 0x81, 3)},   # SET_CUR, ep 0x81
        {"vec": VEC_OEP0, "out": RATE},                  # the data stage
        {"setup": setup(0xA2, 0x81, 0x0100, 0x81, 3)},   # GET_CUR
        {"setup": setup(0x22, 0x01, 0x0100, 0x81, 3)},   # SET_CUR again
        {"vec": VEC_OEP0, "out": [0xE0, 0x93, 0x04]},    # 300000 -- not ours
        {"setup": setup(0xA2, 0x81, 0x0100, 0x81, 3)},   # GET_CUR again
    ], service, ret_to=loop, settle_bp=f"0x{loop:04X}")
    back = obs[2][1][:3] if obs[2][1] else None
    after = obs[5][1][:3] if obs[5][1] else None
    print(f"  #41 SET_CUR 44100 -> GET_CUR {back}, "
          f"after a 300000 attempt -> {after}")
    if back != RATE:
        fails.append(f"GET_CUR returned {back} after SET_CUR 44100 "
                     f"({RATE}). The 24-bit little-endian parse or the data "
                     f"stage is wrong.")
    if after != RATE:
        fails.append(f"after a 300000 Hz SET_CUR the rate reads {after}. A "
                     f"rate outside the descriptor list must be refused, not "
                     f"accepted -- Rev 20 reads only the low byte and would "
                     f"take it.")
    print()

    # ---- #149: suspend clears the bring-up guard ----
    # VEC_SUSR does NOT suspend. It posts a work code and returns -- stock's
    # own split (Rev 20's whole SUSR handler is `MOV 0x0A,#0x0E; RET` at
    # 0x0006), because everything suspend does is slow and PCON idle inside
    # an ISR cannot be woken by the interrupt that would resume it. The work
    # runs later from work_dispatch() in the main loop.
    #
    # Delivering VEC_SUSR alone and reading the mirror therefore shows
    # nothing changed -- which this check first reported as a firmware
    # defect. It was the scenario: the deferred work had never been run.
    # do_suspend() zeroes the codec word, and bit 6 of the low byte IS the
    # "bring-up already ran" flag, so zeroing it is what re-arms
    # cs8427_boot_init() for the resume. If suspend stopped clearing it, the
    # external RESET would stay asserted after a resume and the part would
    # be dead with nothing in the log to say so.
    mirror25 = data_symbols(mapf).get("_g_codec_state_25")
    if mirror25 is None:
        fails.append("_g_codec_state_25 not found in the map, so the suspend "
                     "check could not run. A check that cannot run is not a "
                     "check.")
        obs = []
    else:
        dispatch = syms.get("_work_dispatch")
        obs = run_scenario(ihx, [
            {"setup": setup(0x00, 0x09, 1, 0, 0)},   # SET_CONFIGURATION 1
            {"vec": VEC_SUSR},                       # posts WORK_SUSPEND
            # do_suspend() ends in PCON idle and never returns, so this
            # stops on the write to the mirror instead of on a return.
            {"call": dispatch, "break_iram_w": mirror25},
        ], service, ret_to=loop, settle_bp=f"0x{loop:04X}",
            watch_iram=mirror25)
    states = [o[2] for o in obs]
    if obs:
        print(f"  #149 g_codec_state_25 @ IRAM 0x{mirror25:02X} "
              f"after [SET_CONFIG, SUSR, work_dispatch]: "
              + " ".join("--" if s is None else f"0x{s:02X}" for s in states))
    if not obs:
        pass
    elif not obs[2][3]:
        fails.append(
            "do_suspend() never wrote the codec word mirror. The bring-up "
            "guard (0x25.6) is what re-arms cs8427_boot_init(), so if suspend "
            "stops clearing it a resume never releases the external RESET "
            "again and the CS8427 stays dead with nothing in the log to say "
            "so.")
    elif states[2] is None:
        fails.append("could not read the codec word mirror after suspend.")
    elif states[2] & 0x40:
        fails.append(
            f"after suspend the bring-up guard (0x25.6) is still SET "
            f"(0x{states[2]:02X}). do_suspend() is supposed to zero the codec "
            f"word, which re-arms cs8427_boot_init(); leaving it set means a "
            f"resume never releases the external RESET again.")
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
