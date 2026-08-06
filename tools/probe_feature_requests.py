#!/usr/bin/env python3
"""#188 on hardware — do the feature-request stalls behave, and does the device
survive them?

sim_ep0_requests.py already runs these four cases under ucSim. This exists
because the behaviour #188 replaced was justified by HOST behaviour ("a stall
here makes some hosts abandon the device"), so a passing simulator is necessary
and not sufficient.

WHY THE DRIVER IS DETACHED. The two endpoint-recipient cases target EP 0x81,
which lives on interface 2. While snd-usb-audio owns that interface the HOST
STACK refuses the transfer with EIO before it ever reaches the device, and that
errno is indistinguishable from a device stall unless you look for it. The
first run of this probe reported those two cases as a device divergence from
the simulator; they had simply never been sent. Detaching for the duration is
what makes the question answerable.

Classify by errno, not by message: a stall is EPIPE (32). Anything else is the
host stack talking, not the device.

Reattach can fail with "Entity not found" (seen 2026-08-05). The card kept
working -- capture and playback were both verified afterwards, and telemetry
kept answering -- but if anything looks wrong, replug the unit.

Result 2026-08-05, unit A, build 0x0032: all four cases match the simulator.
"""
import sys, usb.core
d = usb.core.find(idVendor=0x0DBA, custom_match=lambda x: (x.serial_number or "")=="RK10874600Q")
if d is None: sys.exit("unit A not found")

# EP 0x81 lives on interface 2 (capture). An endpoint-recipient control from
# userspace is refused by the HOST STACK with EIO while snd-usb-audio owns that
# interface -- which looks identical to a device stall if you only read errno.
IFACE = 2
detached = False
try:
    if d.is_kernel_driver_active(IFACE):
        d.detach_kernel_driver(IFACE); detached = True
        print("detached snd-usb-audio from interface %d" % IFACE)

    def t(name, bmreq, breq, wval, widx, expect):
        try:
            d.ctrl_transfer(bmreq, breq, wval, widx, 0, 2000)
            got = "ACK"
        except usb.core.USBError as e:
            got = "STALL" if e.errno == 32 else "ERR(errno=%s)" % e.errno
        ok = "OK " if got == expect else "MISMATCH"
        print("  %-38s %-14s expect %-6s %s" % (name, got, expect, ok))
        return got == expect

    print("#188 endpoint-recipient cases, driver detached:")
    r = []
    r.append(t("SET_FEATURE halt on iso EP 0x81", 0x02, 0x03, 0x0000, 0x0081, "STALL"))
    r.append(t("CLEAR_FEATURE halt on EP 0x81",   0x02, 0x01, 0x0000, 0x0081, "ACK"))
    print("\n%s" % ("MATCHES SIMULATOR" if all(r) else "DIVERGENCE"))
    b = d.ctrl_transfer(0xC0, 0x10, 0, 0, 8, 2000)
    print("device still answering EP0: build 0x%04X" % (b[0] | (b[1]<<8)))
finally:
    if detached:
        usb.util.dispose_resources(d)
        try:
            d.attach_kernel_driver(IFACE); print("reattached snd-usb-audio")
        except Exception as e:
            print("REATTACH FAILED (%s) -- replug the unit" % e)
