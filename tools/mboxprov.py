#!/usr/bin/env python3
"""
mboxprov -- write a unit's serial number into EEPROM, at the desk, once.

WHAT THIS REPLACES. tools/mkserial.py appended the serial record to the flash
image and let the boot ROM's DFU write it. That is REFUTED: dfuDnloadData()
seeds dataRemain from the header's payloadSize and answers the byte after it
with errFILE, then stops -- so the padded image never completes, dataType is
left at EEPROM_APPCODE_UPDATING, and the unit boots to app-DFU instead of
running firmware. That is the 2026-07-22 brick pattern, and it cost two
flashes on 2026-08-16 before UsbDfu.c was read. See FINDING_226.

WHY THE SAME CODE MAKES THIS WORK. Because DFU stops at payloadSize, a flash
touches offsets 18..payloadSize and NOTHING above it. The record at 0x1F00 is
therefore unreachable by any flash, which is exactly the property provisioning
wants: write it once, and it survives every future reflash.

So the record goes in through the RUNNING APPLICATION, not through DFU:

    1.  Flash the provisioning image:
            cd mboxfw && make clean
            make MBOX_PID=0x2000 MBOX_RELEASE=1 MBOX_PROVISION=1
            sudo tools/mboxflash_linux.py flash mboxfw/build/mboxfw_flasher.bin
    2.  Replug.
    3.  sudo tools/mboxprov.py --addr <bus>:<addr> write RK10874600Q
    4.  Flash the shipping image:
            cd mboxfw && make clean
            make MBOX_PID=0x2000 MBOX_RELEASE=1 MBOX_SERIAL_EEPROM=1
            sudo tools/mboxflash_linux.py flash mboxfw/build/mboxfw_flasher.bin
    5.  Replug. lsusb -v now shows iSerialNumber, read from EEPROM at boot.

NEVER LEAVE THE PROVISIONING IMAGE ON A UNIT. It is the only build in which
the host can cause an EEPROM write at all. The write is bounded IN FIRMWARE to
the 27 bytes of the record -- the host sends an offset, never an address, and
the firmware supplies the 0x1F00 base -- but the shipping image has no write
path whatsoever, and that is the state a unit should be left in.

--addr, NOT --serial. The provisioning image serves no iSerialNumber (that is
the whole point: it is what runs BEFORE the unit has one), so it cannot be
selected by serial. With two units on the bench, `list` first and pass --addr.
Selecting the wrong unit here writes the wrong serial onto it -- recoverable by
re-provisioning, but only from the desk.
"""
import argparse
import sys
import time

try:
    import usb.core
    import usb.util
    HAVE_USB = True
except ImportError:
    HAVE_USB = False

MBOX_VID = 0x0DBA
AUDIO_PIDS = (0x1000,) + tuple(range(0x2000, 0x2010))

TLM_REQ_PROV_WRITE = 0x19   # bmRequestType 0x40, wValue = offset, wIndex = byte
TLM_REQ_PROV_READ  = 0x1A   # bmRequestType 0xC0, wValue = offset, 8 bytes back
REQ_IN  = 0xC0
REQ_OUT = 0x40

# Must match mboxfw/include/serialno.h. Checked by --selftest below, which is
# the only thing standing between a header edit and a record the firmware
# silently refuses to read.
MAGIC = b"MBSN"
VERSION = 1
MAX_CHARS = 20
HDR_LEN = 7                 # magic[4], version, nchar, xor
XOR_OFFSET = 6
RECORD_LEN = HDR_LEN + MAX_CHARS
BLOCK = 8


def build_record(serial):
    """The exact bytes serialno_load() validates. Mirrors it field for field."""
    raw = serial.encode("ascii")
    if not 1 <= len(raw) <= MAX_CHARS:
        sys.exit("serial must be 1..%d characters (got %d)"
                 % (MAX_CHARS, len(raw)))
    for i, c in enumerate(raw):
        # The firmware rejects anything outside printable ASCII and serves NO
        # serial at all if it finds one, so catching it here turns a silent
        # nothing-happened into a message naming the character.
        if c < 0x20 or c > 0x7E:
            sys.exit("serial character %d (0x%02x) is not printable ASCII; "
                     "the firmware would reject the whole record" % (i, c))

    rec = bytearray(MAGIC) + bytes([VERSION, len(raw), 0]) + raw
    x = 0
    for i, b in enumerate(rec):
        if i != XOR_OFFSET:
            x ^= b
    rec[XOR_OFFSET] = x
    return bytes(rec)


def decode_record(raw):
    """Read a record back the way the firmware does. Returns (serial, why)."""
    if len(raw) < HDR_LEN:
        return None, "short read (%d bytes)" % len(raw)
    if bytes(raw[0:4]) != MAGIC:
        return None, "no MBSN magic (got %s)" % raw[0:4].hex()
    if raw[4] != VERSION:
        return None, "version %d, expected %d" % (raw[4], VERSION)
    n = raw[5]
    if n == 0 or n > MAX_CHARS:
        return None, "nchar %d out of range" % n
    if len(raw) < HDR_LEN + n:
        return None, "record claims %d chars, only %d bytes read" % (n, len(raw))
    x = 0
    for i in range(HDR_LEN + n):
        if i != XOR_OFFSET:
            x ^= raw[i]
    if x != raw[XOR_OFFSET]:
        return None, "xor %02x, record says %02x" % (x, raw[XOR_OFFSET])
    chars = raw[HDR_LEN:HDR_LEN + n]
    for c in chars:
        if c < 0x20 or c > 0x7E:
            return None, "non-printable character 0x%02x" % c
    return chars.decode("ascii"), "ok"


def find_devices():
    if not HAVE_USB:
        sys.exit("pyusb not installed. On the void box: ~/mbox-venv/bin/python")
    found = []
    for pid in AUDIO_PIDS:
        for dev in usb.core.find(find_all=True, idVendor=MBOX_VID,
                                 idProduct=pid):
            found.append((dev, pid))
    return found


def _serial_of(dev):
    try:
        return usb.util.get_string(dev, dev.iSerialNumber) if dev.iSerialNumber else None
    except Exception:
        return None


def select_device(addr):
    found = find_devices()
    if not found:
        sys.exit("no audio-mode Mbox found (%04x:%s)"
                 % (MBOX_VID, "/".join("%04x" % p for p in AUDIO_PIDS)))
    if addr is not None:
        found = [(d, p) for d, p in found if (d.bus, d.address) == addr]
        if not found:
            sys.exit("no audio-mode Mbox at bus %d addr %d" % addr)
    if len(found) > 1:
        # Same refuse-to-guess rule as mboxtlm.py and mboxflash_linux.py, and
        # here the cost of a wrong guess is a unit labelled as the other one.
        lines = ["    %04x:%04x  bus %d addr %d  serial %s"
                 % (MBOX_VID, p, d.bus, d.address, _serial_of(d) or "(none)")
                 for d, p in found]
        sys.exit("%d audio-mode Mboxes attached and none was selected:\n%s\n"
                 "Pick one with --addr <bus>:<addr>. Refusing to guess: this "
                 "command WRITES a serial." % (len(found), "\n".join(lines)))
    return found[0][0]


def prov_read(dev, offset, timeout=2000):
    """8 bytes from the record region. The firmware bounds offset+8 <= 27."""
    data = dev.ctrl_transfer(REQ_IN, TLM_REQ_PROV_READ, offset, 0, BLOCK, timeout)
    if len(data) != BLOCK:
        sys.exit("PROV_READ at offset %d returned %d bytes, expected %d"
                 % (offset, len(data), BLOCK))
    return bytes(data)


def read_record(dev):
    """The whole 27-byte region, from overlapping 8-byte reads."""
    out = bytearray(RECORD_LEN)
    for off in (0, 8, 16, RECORD_LEN - BLOCK):
        out[off:off + BLOCK] = prov_read(dev, off)
    return bytes(out)


def prov_write_byte(dev, offset, value, retries=20):
    """One byte. A STALL means the previous program cycle is still running.

    The firmware STALLs rather than dropping the request precisely so this
    retry is possible: a silently-dropped byte would leave a record that fails
    its own checksum, which reads back as "no serial" with nothing to say why.
    """
    for attempt in range(retries):
        try:
            dev.ctrl_transfer(REQ_OUT, TLM_REQ_PROV_WRITE, offset, value,
                              None, 2000)
            return
        except usb.core.USBError as e:
            if e.errno == 32:           # EPIPE: a real STALL from the device
                time.sleep(0.01)        # one program cycle is ~5 ms
                continue
            raise
    sys.exit("offset %d still STALLing after %d attempts -- the device is "
             "either not running a provisioning image, or its I2C write path "
             "is failing. Read block 0 / check the build." % (offset, retries))


def check_is_provisioning_image(dev):
    """Fail early and by name, rather than 27 STALLs deep."""
    try:
        prov_read(dev, 0)
    except usb.core.USBError as e:
        if e.errno == 32:
            sys.exit("this unit STALLs TLM_REQ_PROV_READ, so it is NOT running "
                     "a provisioning image.\nBuild one with:\n"
                     "    cd mboxfw && make clean && "
                     "make MBOX_PID=0x2000 MBOX_RELEASE=1 MBOX_PROVISION=1")
        raise


def cmd_list(args):
    found = find_devices()
    if not found:
        print("no audio-mode Mbox attached")
        return 0
    for d, p in found:
        print("%04x:%04x  bus %d addr %d  serial %s"
              % (MBOX_VID, p, d.bus, d.address, _serial_of(d) or "(none)"))
    return 0


def cmd_show(args):
    dev = select_device(args.addr)
    check_is_provisioning_image(dev)
    raw = read_record(dev)
    print("record at 0x1F00:", raw.hex())
    serial, why = decode_record(raw)
    if serial is None:
        print("NO VALID RECORD: %s" % why)
        return 1
    print("serial: %s" % serial)
    return 0


def cmd_write(args):
    dev = select_device(args.addr)
    check_is_provisioning_image(dev)

    existing, _ = decode_record(read_record(dev))
    if existing is not None and existing != args.serial and not args.force:
        sys.exit("this unit already carries serial %r. Pass --force to "
                 "overwrite it -- and check you selected the right unit, "
                 "because that is what this usually means." % existing)

    rec = build_record(args.serial)
    print("writing %d bytes to 0x1F00..0x%04X" % (len(rec), 0x1F00 + len(rec) - 1))
    for i, b in enumerate(rec):
        prov_write_byte(dev, i, b)
        print("\r  %d/%d" % (i + 1, len(rec)), end="", flush=True)
    print()

    # VERIFY OFF THE PART, not against what we think we sent. Every byte went
    # through an I2C program cycle that can fail, and eeprom_write_byte's
    # return value is discarded in firmware (main() cannot report it).
    raw = read_record(dev)
    got, why = decode_record(raw)
    if got != args.serial:
        print("record read back: %s" % raw.hex(), file=sys.stderr)
        sys.exit("VERIFY FAILED: read back %r (%s), expected %r"
                 % (got, why, args.serial))
    print("verified from EEPROM: %s" % got)
    print("\nNow flash the SHIPPING image and replug:")
    print("    cd mboxfw && make clean && "
          "make MBOX_PID=0x2000 MBOX_RELEASE=1 MBOX_SERIAL_EEPROM=1")
    print("    sudo tools/mboxflash_linux.py flash "
          "mboxfw/build/mboxfw_flasher.bin")
    return 0


def selftest():
    """Round-trip build/decode, and check the geometry against serialno.h.

    The geometry check is the point: these constants are duplicated from the
    firmware header, and a record built to the wrong layout does not fail
    loudly -- the firmware just serves no serial, which looks identical to an
    unprovisioned unit.
    """
    import os
    import re
    ok = True

    here = os.path.dirname(os.path.abspath(__file__))
    hdr = os.path.join(here, "..", "mboxfw", "include", "serialno.h")
    text = open(hdr).read()
    want = {"SERIAL_VERSION": VERSION, "SERIAL_MAX_CHARS": MAX_CHARS,
            "SERIAL_HDR_LEN": HDR_LEN}
    for name, mine in want.items():
        m = re.search(r"#define\s+%s\s+(\S+)" % name, text)
        if not m:
            print("FAIL: %s not found in serialno.h" % name)
            ok = False
            continue
        theirs = int(m.group(1), 0)
        if theirs != mine:
            print("FAIL: %s is %d in serialno.h, %d here" % (name, theirs, mine))
            ok = False
    for i, ch in enumerate(MAGIC.decode()):
        m = re.search(r"#define\s+SERIAL_MAGIC%d\s+'(.)'" % i, text)
        if not m or m.group(1) != ch:
            print("FAIL: SERIAL_MAGIC%d disagrees with %r" % (i, ch))
            ok = False

    for s in ("A", "RK10874600Q", "X" * MAX_CHARS, "0123456789"):
        rec = build_record(s)
        padded = rec + b"\xff" * (RECORD_LEN - len(rec))
        got, why = decode_record(padded)
        if got != s:
            print("FAIL: %r round-tripped to %r (%s)" % (s, got, why))
            ok = False

    # Every single-bit corruption of the header must be REJECTED. This is what
    # the xor is for -- a half-written record from an interrupted flash.
    rec = bytearray(build_record("RK10874600Q"))
    for i in range(len(rec)):
        for bit in range(8):
            bad = bytearray(rec)
            bad[i] ^= (1 << bit)
            padded = bytes(bad) + b"\xff" * (RECORD_LEN - len(bad))
            got, _ = decode_record(padded)
            if got == "RK10874600Q":
                print("FAIL: flipping bit %d of byte %d still decoded clean"
                      % (bit, i))
                ok = False

    print("selftest: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def parse_addr(s):
    try:
        bus, addr = s.split(":")
        return (int(bus), int(addr))
    except ValueError:
        raise argparse.ArgumentTypeError("expected BUS:ADDR, e.g. 2:14")


def main():
    p = argparse.ArgumentParser(
        description=__doc__.split("\n")[1],
        epilog=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--addr", type=parse_addr, metavar="BUS:ADDR",
                   help="select a unit by USB bus:address (see `list`)")
    p.add_argument("--selftest", action="store_true",
                   help="check the record format against serialno.h; no device")
    sub = p.add_subparsers(dest="cmd")

    sub.add_parser("list", help="show attached audio-mode Mboxes")
    sub.add_parser("show", help="read and decode the record at 0x1F00")
    w = sub.add_parser("write", help="write a serial into EEPROM")
    w.add_argument("serial")
    w.add_argument("--force", action="store_true",
                   help="overwrite a record that already holds a different serial")

    args = p.parse_args()
    if args.selftest:
        return selftest()
    if args.cmd == "list":
        return cmd_list(args)
    if args.cmd == "show":
        return cmd_show(args)
    if args.cmd == "write":
        return cmd_write(args)
    p.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
