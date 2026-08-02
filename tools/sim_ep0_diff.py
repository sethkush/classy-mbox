#!/usr/bin/env python3
"""
Differential EP0 gate — mboxfw against the image that actually enumerated.

`sim_ep0_requests.py` asks mboxfw a question and checks the answer against
expectations THIS PROJECT WROTE. That is worth having, and it is not the same
as checking against an implementation the hardware accepted.

`safety_net` is that implementation. It is mboxfw's sibling — same
architecture, same EP0 buffer at 0xFA18, same handler shape, its own
`EP0_DIFF_vs_REV20.md` — and it is the ONLY image in this repository whose USB
behaviour is confirmed on the real device: it enumerated on 2026-07-26 with
bcdDevice 0xDEAD visible on the bus. Until this gate existed it had never been
run through any executed check at all.

WHY IT TOOK FIVE ASKINGS TO GET HERE. `sim_ep0_requests.py` deliberately has no
stock arm, because Rev 20 defers standard requests to its work-code dispatcher
and answers every packet identically — measured, and recorded there. That
reasoning is correct and it is also the whole mistake: having established that
*stock* was a useless comparator, nothing went on to ask whether a *different*
one existed. The capability was already in the tree.

WHAT THIS IS EVIDENCE OF, AND WHAT IT IS NOT. safety_net is minimal — no audio,
no UAC, no telemetry — so the comparison only covers the shared surface:
enumeration, EP0 mechanics, descriptor plumbing. And "safety_net enumerated" is
evidence about safety_net's code. Where mboxfw differs deliberately the diff
raises a question; it does not settle it. Divergences are therefore RECORDED
with reasons rather than silently tolerated, and an unrecorded one fails.

Several bugs were found in safety_net first and hand-ported to mboxfw — the
`IEPCNF0 |= 0x08` stall inversion, the TOGGLE pair, the abandoned-transfer
flush. Hand-porting is exactly what a differential polices.

Usage:  python3 tools/sim_ep0_diff.py [--verbose]
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sim_ep0_requests import (                              # noqa: E402
    Ep0Sim, POISON, IEPDCNTX0, IEPBBAX0, IEPCNF0, STALL_BIT,
    STC_BUFFER_BASE, VECINT, VEC_SETUP, SETPACK, setup, symbols,
)
from sim_p1_waveform import ROOT                            # noqa: E402

# The state both images reach once EP0 is configured, and the precondition
# every measurement here depends on:
#   IEPBBAX0 = (0xFA18 - 0xF800) >> 3
#   IEPCNF0  = UBME | UBMIE, no STALL bit
# Asserting BOTH is not belt-and-braces. ucSim leaves XDATA uninitialised, and
# at safety_net's earlier settle point IEPCNF0 read 0x2A -- garbage that
# happens to have bit 3 set. The stall detector compares "STALL set now" with
# "STALL set before", so a genuine stall read as no-response and looked like a
# real divergence from mboxfw. One unasserted precondition, two wrong results.
EXPECTED_BBAX = 0x43
EXPECTED_CNF0 = 0x84


class Image:
    """One firmware under test: where to settle, and where to hand it a vector.

    `settle` is a ucSim breakpoint expression. The two images need different
    ones and the difference is load-bearing: mboxfw settles at its main loop,
    but safety_net has no such symbol and — measured, not assumed — programs
    IEPBBAX0 only at 0x05A5, AFTER its first write to USBCTL. Settling on the
    USBCTL write reads the endpoint base before the firmware has set it, which
    silently yields a garbage buffer address and eight bytes of uninitialised
    XDATA that look exactly like a descriptor that failed to appear.

    That is why check_settled() exists rather than a comment saying "be
    careful". The precondition is asserted every run.
    """

    def __init__(self, name, ihx, mapfile, entry_sym, entry_addr, settle):
        self.name = name
        self.ihx = ROOT / ihx
        self.map = ROOT / mapfile
        self.settle = settle
        syms = symbols(self.map) if self.map.exists() else {}
        self.entry = syms.get(entry_sym, entry_addr)
        self.loop = syms.get("_buttons_poll")


IMAGES = [
    Image("mboxfw", "mboxfw/build/mboxfw.ihx", "mboxfw/build/mboxfw.map",
          "_usb_service", None, None),
    # safety_net's usb_service() is static, so the map has no symbol for it;
    # _usb_isr is its only caller and reaches it directly.
    Image("safety_net", "safety_net/build/safety_net.ihx",
          "safety_net/build/safety_net.map", "_usb_isr", 0x0617,
          # NOT the IEPBBAX0 write: IEPCNF0 is programmed later still, at
          # 0x05C7. Settle on the last of the two.
          f"xram w 0x{IEPCNF0:04x}"),
]

ARMED, STALL, NONE = "armed", "stall", "no-response"

# The shared surface. Nothing here is audio, UAC or telemetry — safety_net has
# none of that, and a request only one image implements compares nothing.
REQUESTS = [
    ("GET_DESCRIPTOR device", setup(0x80, 0x06, 0x0100, 0, 18)),
    ("GET_DESCRIPTOR config", setup(0x80, 0x06, 0x0200, 0, 9)),
    ("GET_STATUS device",     setup(0x80, 0x00, 0, 0, 2)),
    ("SET_ADDRESS 3",         setup(0x00, 0x05, 3, 0, 0)),
    ("undefined bReq 0x0C",   setup(0x80, 0x0C, 0, 0, 2)),
]

# Divergences between mboxfw and safety_net, each with the reason it is
# accepted. Anything not listed fails; anything listed that no longer diverges
# fails too, because a stale entry is a claim that stopped being true.
EXPECTED_DIVERGENCE = {
    "GET_DESCRIPTOR config":
        "safety_net advertises a minimal configuration and mboxfw a three-"
        "interface audio one, so wTotalLength and the interface count differ "
        "by design. The framing -- 9-byte header, bDescriptorType 0x02, same "
        "packet count -- is what this gate checks for that request, and that "
        "still has to match.",
}


def run(image, packet, want_bbax=True):
    """Deliver one SETUP to one image and report what it staged."""
    sim = Ep0Sim(image.ihx)
    if image.settle:
        sim.cmd(f"break {image.settle}")
    else:
        sim.cmd(f"break 0x{image.loop:04X}")
    sim.run_to_stop()
    sim.cmd("delete")

    bbax = sim.peek(IEPBBAX0)
    bbax = bbax[0] if bbax else None
    cnf0 = sim.peek(IEPCNF0)
    cnf0 = cnf0[0] if cnf0 else None
    if want_bbax and (bbax != EXPECTED_BBAX or cnf0 != EXPECTED_CNF0):
        sim.close()
        return ("unsettled", (bbax, cnf0), bbax)

    # A sentinel return address when the image has no main-loop symbol.
    # safety_net's usb_service() is static and it has no _buttons_poll, so
    # the first version bounded the run on the IEPCNF0 write instead -- and
    # stopped AT that write, reading the register before the store landed.
    # A stall then read as "no-response", which looked like a real difference
    # from mboxfw. Bounding both images the same way removes the asymmetry:
    # 0x0000 is the reset vector, never reached during one service call, so a
    # fetch break there means "the call returned".
    ret_to = image.loop if image.loop else 0x0000
    sim.cmd(f"fill xram 0x{IEPDCNTX0:04x} 0x{IEPDCNTX0:04x} 0x{POISON:02x}")
    cnf_before = sim.peek(IEPCNF0)
    for i, b in enumerate(packet):
        sim.poke(SETPACK + i, b)
    sim.poke(VECINT, VEC_SETUP)

    sp = sim.peek_sfr(0x81)
    sim.cmd(f"fill iram 0x{sp + 1:02x} 0x{sp + 1:02x} 0x{ret_to & 0xFF:02x}")
    sim.cmd(f"fill iram 0x{sp + 2:02x} 0x{sp + 2:02x} "
            f"0x{(ret_to >> 8) & 0xFF:02x}")
    sim.cmd(f"fill sfr 0x81 0x81 0x{sp + 2:02x}")
    sim.cmd(f"break 0x{ret_to:04X}")
    sim.cmd(f"pc 0x{image.entry:04X}")
    sim.run_to_stop()

    cnt = sim.peek(IEPDCNTX0)
    cnt = cnt[0] if cnt else None
    cnf = sim.peek(IEPCNF0)
    cnf = cnf[0] if cnf else None
    before = cnf_before[0] if cnf_before else 0
    staged = None
    if cnt is not None and cnt != POISON and bbax is not None:
        staged = sim.peek(STC_BUFFER_BASE + (bbax << 3), 8)
    sim.close()

    if cnt is not None and cnt != POISON:
        return (ARMED, (cnt, staged), bbax)
    if cnf is not None and (cnf & STALL_BIT) and not (before & STALL_BIT):
        return (STALL, None, bbax)
    return (NONE, None, bbax)


def summarise(outcome, detail):
    if outcome != ARMED:
        return outcome
    cnt, staged = detail
    body = " ".join(f"{b:02X}" for b in (staged or [])[:cnt])
    return f"armed({cnt}) {body}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    for im in IMAGES:
        if not im.ihx.exists():
            print(f"not found: {im.ihx} (build it first)", file=sys.stderr)
            return 2

    print("The same SETUP packets into mboxfw and into the image that "
          "enumerated.\n")

    fails, results = [], {}
    for name, packet in REQUESTS:
        row = {}
        for im in IMAGES:
            outcome, detail, bbax = run(im, packet)
            row[im.name] = (outcome, detail)
            if outcome == "unsettled":
                gb, gc = detail
                fails.append(
                    f"{im.name}: after settling IEPBBAX0="
                    f"{'--' if gb is None else f'0x{gb:02X}'} IEPCNF0="
                    f"{'--' if gc is None else f'0x{gc:02X}'}, expected "
                    f"0x{EXPECTED_BBAX:02X}/0x{EXPECTED_CNF0:02X}. EP0 is not "
                    f"configured yet, so every register read is uninitialised "
                    f"XDATA dressed up as a result.")
        results[name] = row
        a = summarise(*row["mboxfw"])
        b = summarise(*row["safety_net"])
        agree = "same" if a == b else "DIFFER"
        print(f"  {name:<24} {agree}")
        print(f"      mboxfw      {a}")
        print(f"      safety_net  {b}")
    print()

    seen_div = set()
    for name, row in results.items():
        a = summarise(*row["mboxfw"])
        b = summarise(*row["safety_net"])
        if a == b:
            continue
        seen_div.add(name)
        if name not in EXPECTED_DIVERGENCE:
            fails.append(
                f"{name}: mboxfw answers `{a}` where the image that enumerated "
                f"answers `{b}`, with no recorded reason. safety_net is not "
                f"automatically right -- but a difference on the enumeration "
                f"surface between our firmware and the one the device accepted "
                f"is a question that has to be answered, not skipped.")
        else:
            print(f"  recorded divergence, {name}:")
            print(f"      {EXPECTED_DIVERGENCE[name]}")

    for name in EXPECTED_DIVERGENCE:
        if name not in seen_div:
            fails.append(
                f"EXPECTED_DIVERGENCE lists `{name}` but the two images now "
                f"agree there. A stale entry is a claim that stopped being "
                f"true; remove it.")

    # Framing has to match even where content is allowed to differ: both
    # config descriptors are 9-byte headers of type 0x02 in one 8-byte packet.
    for im in ("mboxfw", "safety_net"):
        outcome, detail = results["GET_DESCRIPTOR config"][im]
        if outcome != ARMED or not detail or not detail[1]:
            fails.append(f"{im}: GET_DESCRIPTOR config staged nothing.")
            continue
        cnt, staged = detail
        if cnt != 8 or staged[1] != 0x02:
            fails.append(
                f"{im}: config descriptor first packet is count={cnt} "
                f"type=0x{staged[1]:02X}; expected 8 bytes of type 0x02. The "
                f"contents may differ between the images, the framing may not.")
    print()

    if fails:
        print("FAIL:")
        for f in fails:
            print(f"  - {f}")
        return 1

    print("PASS: mboxfw and the image that enumerated answer the shared "
          "enumeration surface identically, apart from recorded divergences.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
