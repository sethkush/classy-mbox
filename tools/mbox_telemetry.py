#!/usr/bin/env python3
"""Read mboxfw telemetry over EP0. See mboxfw/TELEMETRY.md for the block map.

    sudo ./mbox_telemetry.py            # dump all blocks
    sudo ./mbox_telemetry.py --reset    # zero the counters
    sudo ./mbox_telemetry.py --ep0-test # measure EP0 continuation loss

Every read is a single 8-byte EP0 packet, deliberately: the defect under
investigation is the multi-packet continuation path, so the instrument must
not depend on it.
"""
import argparse, struct, sys

try:
    import usb.core
except ImportError:
    sys.exit("pyusb not installed:  pip3 install pyusb")

VID = 0x0DBA
# 0x1000 is the original PID; 0x2000 is the quirk-free build (see
# verify_descriptors.py for why). Try each so one tool covers both.
PIDS = (0x2000, 0x1000, 0x1001)
REQ_READ, REQ_RESET = 0x10, 0x11

PHASES = [(0x01, "usb_init"), (0x02, "hw_init"), (0x04, "attach"),
          (0x08, "cs8427"), (0x10, "codec"), (0x20, "main_loop")]


def dev():
    d = None
    for _pid in PIDS:
        d = usb.core.find(idVendor=VID, idProduct=_pid)
        if d is not None:
            break
    if d is None:
        sys.exit("no mboxfw on the bus (looked for 0dba:2000/1000/1001)")
    return d


def read_block(d, i):
    r = d.ctrl_transfer(0xC0, REQ_READ, i, 0, 8, 2000)
    if len(r) != 8:
        raise IOError("block %d returned %d bytes, expected 8" % (i, len(r)))
    return bytes(r)


def dump(d):
    b = [read_block(d, i) for i in range(5)]

    bid, stage, phases, loops, rstr = struct.unpack("<HBBHH", b[0])
    print("build 0x%04x  stage %d  loop_count %d  bus_resets %d" % (bid, stage, loops, rstr))
    print("  phases: " + " ".join(n for m, n in PHASES if phases & m) or "  phases: (none)")

    setups, iep0, chunks, drains = struct.unpack("<HHHH", b[1])
    print("EP0: setups=%d iep0_ints=%d chunks=%d drains=%d" % (setups, iep0, chunks, drains))
    if chunks and iep0 < chunks - 1:
        print("  !! iep0_ints < chunks-1 — interrupts are being LOST")
    # NB: chunks counts PACKETS, drains counts TRANSFERS - different units, so
    # chunks-drains is meaningless. An earlier version subtracted them and
    # reported a bogus "transfers never drained" count.
    if drains:
        print("  avg %.1f packets per completed transfer" % (chunks / drains))

    bmreq, breq, wval, widx, wlen = struct.unpack("<BBHHH", b[2])
    print("last SETUP: bmReq=0x%02x bReq=0x%02x wValue=0x%04x wIndex=0x%04x wLength=%d"
          % (bmreq, breq, wval, widx, wlen))

    print("VECINT: setup=%d iep0=%d oep0=%d rstr=%d none=%d other=%d" % tuple(b[3][:6]))

    ee, cs, co, stalls = b[4][:4]
    print("periph: eeprom=0x%02x cs8427=0x%02x codec=0x%02x  stalls=%d" % (ee, cs, co, stalls))

    b5 = read_block(d, 5)
    sof = b5[0] | (b5[1] << 8)
    print("isoc: sof=%d iep1=%d oep2=%d" % (sof, b5[2], b5[3]))
    seen = b5[6]
    print("  live IEPCNF1=0x%02x OEPCNF2=0x%02x  alt_seen: playback=%d capture=%d"
          % (b5[4], b5[5], (seen >> 0) & 1, (seen >> 1) & 1))
    print("  last SET_INTERFACE: iface=%d alt=%d" % (b5[7] >> 4, b5[7] & 0x0F))

    b6 = read_block(d, 6)
    print("dma/cport: DMACTL1=0x%02x (armed=%d) CPTSTA=0x%02x CPTCNF1=0x%02x ACGDCTL=0x%02x"
          % (b6[0], 1 if (b6[0] & 0xC0) == 0xC0 else 0, b6[1], b6[2], b6[3]))
    print("  IEPCNF1=0x%02x IEPBCTX1=0x%02x IEPBSIZ1=0x%02x OEPBCTX2=0x%02x"
          % (b6[4], b6[5], b6[6], b6[7]))


def ep0_test(d, trials=40):
    """Cross-check the device's own chunk counter against host-visible success.

    This is the measurement the VECINT fix has to pass. For an N-packet
    reply the device should push N chunks per successful transfer; a
    shortfall means it stopped being asked."""
    print("%-28s %-6s %-9s %s" % ("transfer", "pkts", "host ok", "device chunks"))
    for label, wv, wlen, pk in [("DEVICE  wLen=8",  0x0100,  8, 1),
                                ("DEVICE  wLen=18", 0x0100, 18, 3),
                                ("CONFIG  wLen=64", 0x0200, 64, 8)]:
        d.ctrl_transfer(0x40, REQ_RESET, 0, 0, None, 2000)
        ok = 0
        for _ in range(trials):
            try:
                d.ctrl_transfer(0x80, 0x06, wv, 0, wlen, 400)
                ok += 1
            except Exception:
                pass
        chunks = struct.unpack("<HHHH", read_block(d, 1))[2]
        expect = ok * pk
        flag = "" if chunks >= expect else "   <-- SHORTFALL, packets lost"
        print("%-28s %-6d %2d/%-6d %d (expect >= %d)%s"
              % (label, pk, ok, trials, chunks, expect, flag))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reset", action="store_true", help="zero the counters")
    ap.add_argument("--ep0-test", action="store_true", help="measure EP0 continuation loss")
    a = ap.parse_args()
    d = dev()
    if a.reset:
        d.ctrl_transfer(0x40, REQ_RESET, 0, 0, None, 2000)
        print("counters cleared")
        return 0
    if a.ep0_test:
        ep0_test(d)
        return 0
    dump(d)
    return 0


if __name__ == "__main__":
    sys.exit(main())
