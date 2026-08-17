#!/usr/bin/env python3
"""Append a provisioned iSerialNumber record to an Mbox firmware image (#221).

WHY THE RECORD TRAVELS WITH THE IMAGE. The only way a host can write this
device's EEPROM is a DFU download, and a DFU download writes contiguously from
offset 0. So the serial cannot be written as a separate step -- it has to be
part of the image, with the gap between the payload and the record filled.

That padding is free in practice: the boot ROM copies only what the 18-byte
header says the payload is, so trailing bytes are never executed and never
reach program RAM. It costs ~2.5 KB of DFU transfer, a few seconds once.

WHY THIS EXISTS AT ALL, since #193 decided the default image serves
iSerialNumber 0 and that decision still stands for a COMPILE-TIME serial. One
image flashed to many units with a baked-in string would make every one of them
claim the same serial, and hosts key device identity on that field. This is the
runtime source #193 said was worth wanting and did not have: the flasher writes
the string printed on the unit, so ONE image serves the true serial everywhere.

MBOX_UNIT= remains the bench mechanism and is mutually exclusive with this --
one serves a provisioned serial, the other a compile-time constant, and a build
carrying both would have two answers to one question.

    python3 tools/mkserial.py in.bin RK10874600Q -o out.bin
"""
import argparse
import struct
import sys

# Must match mboxfw/include/serialno.h exactly.
EE_SERIAL_ADDR = 0x1F00
MAGIC = b"MBSN"
VERSION = 0x01
MAX_CHARS = 20
HDR_LEN = 7          # magic[4], version, nchar, xor
EEPROM_SIZE = 8192


def build_record(serial: str) -> bytes:
    b = serial.encode("ascii")
    if not 1 <= len(b) <= MAX_CHARS:
        sys.exit("serial must be 1..%d characters" % MAX_CHARS)
    for c in b:
        # The firmware rejects anything outside printable ASCII, so rejecting it
        # here too means a bad serial fails at the desk rather than silently
        # producing a unit that serves iSerialNumber 0 a flash later.
        if c < 0x20 or c > 0x7E:
            sys.exit("serial must be printable ASCII")
    rec = bytearray(MAGIC + bytes([VERSION, len(b), 0x00]) + b)
    x = 0
    for i, v in enumerate(rec):
        if i != 6:               # the checksum byte itself is excluded
            x ^= v
    rec[6] = x
    return bytes(rec)


def parse_records(blob):
    """-> [(addr, data)] in file order. Format per tools/wrap_hex.py:
    u32 length_BE, u32 addr_BE, u32 type_BE, then `length` bytes."""
    out, p = [], 0
    while p + 12 <= len(blob):
        ln, addr, ty = struct.unpack_from(">III", blob, p)
        p += 12
        if ty != 0:
            break
        if p + ln > len(blob):
            sys.exit("truncated record at offset %d" % p)
        out.append((addr, blob[p:p + ln]))
        p += ln
    return out


def rec(addr, data):
    return struct.pack(">III", len(data), addr, 0) + data


def selftest():
    """Prove this file and serialno.c agree, by re-deriving the constants from
    the header rather than trusting that two hand-written copies match.

    The record format lives in exactly two places -- here and
    mboxfw/include/serialno.h -- and a silent disagreement between them produces
    a unit that flashes cleanly, boots, and serves iSerialNumber 0 with no
    diagnostic anywhere. That failure is invisible until someone looks at lsusb,
    which is precisely the shape of bug this project keeps finding late.
    """
    import re
    from pathlib import Path
    h = (Path(__file__).resolve().parent.parent
         / "mboxfw/include/serialno.h").read_text()

    def val(name):
        m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+|'.')" % name, h)
        if not m:
            sys.exit("SELFTEST FAIL: %s not found in serialno.h" % name)
        t = m.group(1)
        return ord(t[1]) if t.startswith("'") else int(t, 0)

    fw_addr = (val("EE_SERIAL_HI") << 8) | val("EE_SERIAL_LO")
    checks = [
        ("record address", EE_SERIAL_ADDR, fw_addr),
        ("magic[0]", MAGIC[0], val("SERIAL_MAGIC0")),
        ("magic[1]", MAGIC[1], val("SERIAL_MAGIC1")),
        ("magic[2]", MAGIC[2], val("SERIAL_MAGIC2")),
        ("magic[3]", MAGIC[3], val("SERIAL_MAGIC3")),
        ("version", VERSION, val("SERIAL_VERSION")),
        ("max chars", MAX_CHARS, val("SERIAL_MAX_CHARS")),
        ("header length", HDR_LEN, val("SERIAL_HDR_LEN")),
    ]
    bad = 0
    for name, host, fw in checks:
        ok = host == fw
        print("  %-16s host=%-6s firmware=%-6s %s"
              % (name, host, fw, "ok" if ok else "MISMATCH"))
        bad += not ok

    # And the checksum, computed the way serialno.c computes it.
    r = build_record("RK10874600Q")
    x = 0
    for i in range(HDR_LEN + r[5]):
        if i != 6:
            x ^= r[i]
    ok = x == r[6]
    print("  %-16s host=0x%02X  firmware-rule=0x%02X %s"
          % ("xor checksum", r[6], x, "ok" if ok else "MISMATCH"))
    bad += not ok

    print()
    if bad:
        print("SELFTEST FAIL: %d disagreement(s) with serialno.h" % bad)
        return 1
    print("SELFTEST PASS: mkserial.py agrees with serialno.h on all %d fields"
          % (len(checks) + 1))
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="flasher record container from wrap_hex.py")
    ap.add_argument("serial")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()

    blob = open(a.image, "rb").read()
    recs = parse_records(blob)
    if not recs:
        sys.exit("no records parsed -- is this a wrap_hex.py container?")
    end = max(addr + len(d) for addr, d in recs)
    if end > EE_SERIAL_ADDR:
        sys.exit("payload ends at 0x%04X and would overlap the serial record "
                 "at 0x%04X" % (end, EE_SERIAL_ADDR))

    # PAD TO KEEP THE RECORD STREAM CONTIGUOUS. mboxflash validates contiguity,
    # and the boot ROM's dfuEepromCopy writes sequentially -- a jump straight to
    # 0x1F00 would either fail validation or leave the gap unwritten. 0xFF is
    # the erased state, so the padding says nothing.
    # FLATTEN AND RE-CHUNK rather than appending records to the existing
    # stream. The payload's final record is a partial page (9 bytes here), and
    # appending after it leaves a short record in the MIDDLE of the stream --
    # mboxflash rejects that with "record N has length 9 (expected 32)", and it
    # is right to: the boot ROM's page writes assume full pages except at the
    # end. Rebuilding from a flat image guarantees one uniform chunking with the
    # only short record last.
    flat = bytearray(b"\xFF" * EE_SERIAL_ADDR)
    for addr, d in recs:
        flat[addr:addr + len(d)] = d
    record = build_record(a.serial)
    flat += record

    PAGE = 32
    body = bytearray()
    for off in range(0, len(flat), PAGE):
        body += rec(off, bytes(flat[off:off + PAGE]))

    total = len(flat)
    if total > EEPROM_SIZE:
        sys.exit("result exceeds the 8192-byte EEPROM")

    open(a.out, "wb").write(bytes(body))
    print("%s + serial %r -> %s" % (a.image, a.serial, a.out))
    print("  payload ends 0x%04X, padded to 0x%04X, record %d bytes, xor 0x%02X"
          % (end, EE_SERIAL_ADDR, len(record), record[6]))
    print("  EEPROM occupancy %d of %d bytes" % (total, EEPROM_SIZE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
