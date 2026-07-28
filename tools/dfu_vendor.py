#!/usr/bin/env python3
"""Halt an mboxfw unit into DFU via the DEVICE-recipient vendor request.

    sudo ./dfu_vendor.py --pid 0x2001

The PID is REQUIRED. This script was pinned to 0x2000 while there was only
one unit; with two on the bus that default silently halted whichever unit
the author was not thinking about (done, 2026-07-28 -- it took out the
control unit of a paired experiment). Refusing to guess costs one flag;
guessing wrong costs a 2 km round trip.

Device recipient, not interface: snd-usb-audio claims the audio interfaces,
and an interface-recipient request is rejected with EBUSY by the host stack
before it ever reaches the device. The kernel driver stays attached.
"""
import argparse, sys, usb.core

TLM_REQ_ENTER_DFU = 0x12

ap = argparse.ArgumentParser()
ap.add_argument("--pid", type=lambda v: int(v, 0), required=True,
                help="which unit to halt, e.g. 0x2000 or 0x2001")
a = ap.parse_args()

d = usb.core.find(idVendor=0x0DBA, idProduct=a.pid)
if d is None:
    sys.exit("no device at 0dba:%04x" % a.pid)
print("halting 0dba:%04x (bus %d addr %d), kernel driver left attached"
      % (a.pid, d.bus, d.address))

try:
    d.ctrl_transfer(0x40, TLM_REQ_ENTER_DFU, 0, 0, None, 3000)
    print("vendor enter-DFU ACKed")
except usb.core.USBError as e:
    print("request returned: %s" % e)
