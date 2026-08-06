#!/usr/bin/env python3
"""Halt an mboxfw unit into DFU via the DEVICE-recipient vendor request.

    sudo ./dfu_vendor.py --serial RK10874600Q

TARGETING MUST BE UNAMBIGUOUS, and the PID no longer provides that.

This script was pinned to 0x2000 while there was only one unit; with two on
the bus that default silently halted whichever unit the author was not
thinking about (done, 2026-07-28 -- it took out the control unit of a paired
experiment). The fix then was to require --pid, which worked only while the
two units carried DIFFERENT PIDs.

They no longer do. Both moved to 0x2000 on 2026-08-04, deliberately: the PID
says which PRODUCT this is, not which unit, and it changes driver binding --
the exact variable an A/B measurement has to hold still (BENCH_WIRING.md,
"Why not the PID"). That change silently disarmed the safety this flag was
providing: --pid 0x2000 now matches BOTH units and usb.core.find() returns
whichever the bus enumerated first.

So target by serial, which is the identity (BENCH_WIRING.md: "Trust the
serial"), or by bus:addr when no serial is being served. Ambiguity is a hard
error, matching mboxflash_linux.py's discipline -- a wrong guess costs a 2 km
round trip.

Device recipient, not interface: snd-usb-audio claims the audio interfaces,
and an interface-recipient request is rejected with EBUSY by the host stack
before it ever reaches the device. The kernel driver stays attached.
"""
import argparse, sys, usb.core

TLM_REQ_ENTER_DFU = 0x12

ap = argparse.ArgumentParser()
ap.add_argument("--serial", default=None,
                help="which unit to halt, e.g. RK10874600Q (preferred)")
ap.add_argument("--addr", metavar="BUS:ADDR", default=None,
                help="fallback when no serial is served, e.g. --addr 2:4")
ap.add_argument("--pid", type=lambda v: int(v, 0), default=None,
                help="narrow by PID; does NOT disambiguate two units, both "
                     "of which are 0x2000")
a = ap.parse_args()

if not a.serial and not a.addr:
    sys.exit("refusing to guess: pass --serial (preferred) or --addr BUS:ADDR.\n"
             "Both units answer to --pid 0x2000, so the PID cannot pick one.")

want_addr = None
if a.addr:
    _b, _a = a.addr.split(":")
    want_addr = (int(_b), int(_a))


def matches(d):
    if a.pid is not None and d.idProduct != a.pid:
        return False
    if want_addr is not None and (d.bus, d.address) != want_addr:
        return False
    if a.serial:
        try:
            if (d.serial_number or "") != a.serial:
                return False
        except Exception:
            # A unit serving no serial cannot be the one asked for by serial.
            return False
    return True


devs = [d for d in usb.core.find(find_all=True, idVendor=0x0DBA) if matches(d)]

if not devs:
    sys.exit("no matching Mbox found")
if len(devs) > 1:
    listing = "\n".join(
        "    bus %d addr %d  pid 0x%04x  serial %s"
        % (d.bus, d.address, d.idProduct, (d.serial_number or "<none>"))
        for d in devs)
    sys.exit("AMBIGUOUS -- %d units match; refusing to halt one at random:\n%s"
             % (len(devs), listing))

d = devs[0]
print("halting 0dba:%04x serial %s (bus %d addr %d), kernel driver left attached"
      % (d.idProduct, (d.serial_number or "<none>"), d.bus, d.address))

try:
    d.ctrl_transfer(0x40, TLM_REQ_ENTER_DFU, 0, 0, None, 3000)
    print("vendor enter-DFU ACKed")
except usb.core.USBError as e:
    print("request returned: %s" % e)
