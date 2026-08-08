"""Send TLM_REQ_ENTER_DFU to ONE unit, selected by its physical USB port.

enter_dfu_serial.py selects by iSerialNumber and refuses to guess, for the
reason given in its docstring: this request breaks the EEPROM header checksum,
so picking the wrong unit wedges it until someone walks to it.

That tool cannot be used on a build with no serial strings. Build 0x0041 is
one: the #197 gate probe fits in 6016 bytes only with the 42-byte per-unit
serial descriptor removed, and a build that cannot be put into DFU by name is
a build that must not be flashed without a replacement selector.

The port path is the replacement, and it is strictly the better identifier for
this job anyway -- it is where the unit physically is, so it stays correct
across a reflash, a firmware that reports the wrong serial, and a firmware
that reports none. Read it from `lsusb -t` or:

    for d in /sys/bus/usb/devices/*/; do
        [ "$(cat $d/idVendor 2>/dev/null)" = 0dba ] && basename $d
    done

BENCH_WIRING.md records which port holds which unit. Verify it there before
running this -- the whole point of refusing to guess is lost if the map is
stale.
"""
import sys, usb.core

if len(sys.argv) != 2:
    sys.exit("usage: enter_dfu_port.py <PORT>     e.g. 2-1.4")

want = sys.argv[1]


def path_of(d):
    """Kernel-style path: '<bus>-<port>.<port>...', matching sysfs names."""
    try:
        ports = d.port_numbers
    except (AttributeError, NotImplementedError):
        return None
    if not ports:
        return None
    return "%d-%s" % (d.bus, ".".join(str(p) for p in ports))


found = []
for pid in (0x1000,) + tuple(range(0x2000, 0x2010)):
    for d in usb.core.find(find_all=True, idVendor=0x0DBA, idProduct=pid):
        if path_of(d) == want:
            found.append((d, pid))

if not found:
    seen = []
    for pid in (0x1000,) + tuple(range(0x2000, 0x2010)):
        for d in usb.core.find(find_all=True, idVendor=0x0DBA, idProduct=pid):
            seen.append("%s (%04x:%04x)" % (path_of(d), d.idVendor, d.idProduct))
    sys.exit("no 0dba device at port %s; present: %s"
             % (want, ", ".join(seen) or "none"))

# Same hard error as the serial tool. Two devices cannot share a port path, so
# this can only fire on a pyusb that reports port_numbers inconsistently --
# which is exactly the case where guessing is least defensible.
if len(found) > 1:
    sys.exit("ambiguous: %d devices report port %s" % (len(found), want))

dev, pid = found[0]
print("port %s -> %04x:%04x, sending TLM_REQ_ENTER_DFU (0x12)" % (want, 0x0DBA, pid))

# bmRequestType 0x40 (host->device, vendor, DEVICE recipient), bRequest 0x12.
# Device recipient on purpose -- an interface-recipient request is rejected
# with EBUSY by the host stack once snd-usb-audio has claimed the interfaces.
dev.ctrl_transfer(0x40, 0x12, 0, 0, None, timeout=1000)

print("sent. The firmware halts in the handler, so the unit DROPS OFF THE BUS")
print("while still physically plugged in. It must be REPLUGGED before it will")
print("appear in DFU.")
