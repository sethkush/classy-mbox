#!/usr/bin/env python3
"""check_release_surface -- what vendor requests does a SHIPPING unit answer?

Run against a device flashed with the release image. Every diagnostic and every
EEPROM-write request must STALL; only TLM_REQ_ENTER_DFU may answer.

WHY THIS IS A HARDWARE CHECK AND NOT A GATE ON THE BINARY. The static gates
verify what the source compiles to. This verifies what the DEVICE DOES, which
is a different claim and the one that matters for the write path: #226 added a
vendor request that makes the running application write EEPROM, and the only
thing keeping it out of a shipping unit is a preprocessor flag. A build mistake
that leaves MBOX_PROVISION defined produces an image that looks fine, passes
preflight, enumerates, streams audio -- and lets any host on the bus rewrite the
serial record. This is how you find that out.

Measured 2026-08-16 on both units, release image 0x0060: all nine STALL.

NEVER SENDS 0x12 (TLM_REQ_ENTER_DFU). That one is expected to be present and
working, and testing it costs a replug and a 2 km round trip -- see mboxtlm.py's
docstring for the same rule.
"""
import sys
import usb.core

REQ_IN, REQ_OUT = 0xC0, 0x40

# (bRequest, direction, name, expected)
PROBES = [
    (0x10, REQ_IN,  "TLM_REQ_READ (telemetry block)", "stall"),
    (0x11, REQ_OUT, "TLM_REQ_RESET (counters)",       "stall"),
    (0x13, REQ_OUT, "TLM_REQ_SET_MUX",                "stall"),
    (0x14, REQ_OUT, "TLM_REQ_SET_CLOCK",              "stall"),
    (0x17, REQ_OUT, "TLM_REQ_DIAG_MODE (recalib)",    "stall"),
    (0x19, REQ_OUT, "TLM_REQ_PROV_WRITE  <-- EEPROM WRITE", "stall"),
    (0x1A, REQ_IN,  "TLM_REQ_PROV_READ",              "stall"),
    (0x1B, REQ_OUT, "TLM_REQ_PROV_DIAG (retired)",    "stall"),
    (0x1C, REQ_IN,  "TLM_REQ_PROV_DIAGRD (retired)",  "stall"),
]

for dev in sorted(usb.core.find(find_all=True, idVendor=0x0DBA),
                  key=lambda d: d.address):
    try:
        sn = dev.serial_number
    except Exception:
        sn = None
    print("=== %s  bus %d addr %d  bcdDevice %04x ==="
          % (sn or "(no serial)", dev.bus, dev.address, dev.bcdDevice))
    bad = 0
    for req, direction, name, expect in PROBES:
        try:
            if direction == REQ_IN:
                dev.ctrl_transfer(direction, req, 0, 0, 8, 1500)
            else:
                dev.ctrl_transfer(direction, req, 0, 0, None, 1500)
            got = "ANSWERED"
        except usb.core.USBError as e:
            got = "stall" if e.errno == 32 else "err%d" % e.errno
        ok = (got == expect)
        if not ok:
            bad += 1
        print("   0x%02X %-38s %-9s %s" % (req, name, got, "OK" if ok else "<-- UNEXPECTED"))
    print("   %s\n" % ("clean: no diagnostic or EEPROM-write surface"
                       if bad == 0 else "%d UNEXPECTED" % bad))
