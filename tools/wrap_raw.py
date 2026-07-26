#!/usr/bin/env python3
"""
Wrap a raw `header + code` binary into the TI-record stream format that
mboxflash expects. Companion to wrap_hex.py: wrap_hex takes an SDCC .ihx
input and (re)generates its own 18-byte EEPROM header + wraps; wrap_raw
takes a binary that ALREADY has a valid 18-byte header baked in (e.g.
the payload extracted from Digidesign's stock updater .app resource
fork) and just re-emits it as records.

Use case: Rev 22 firmware payload from `firmware_stock/rev22_flasher_
payload_raw.bin`, which is 8192 bytes = 18 header + 8174 code.
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from wrap_hex import emit_records, EEPROM_SIZE

HEADER_SIZE = 18


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("raw", type=Path,
                    help="input file: 18-byte EEPROM header + code")
    ap.add_argument("-o", "--out", type=Path, required=True,
                    help="output file: TI-record stream, ready for mboxflash")
    args = ap.parse_args()

    blob = args.raw.read_bytes()
    if len(blob) < HEADER_SIZE:
        print(f"FAIL: {args.raw} is {len(blob)} bytes, needs at least"
              f" {HEADER_SIZE}", file=sys.stderr)
        return 1
    if len(blob) > EEPROM_SIZE:
        print(f"FAIL: {args.raw} is {len(blob)} bytes, exceeds EEPROM"
              f" capacity {EEPROM_SIZE}", file=sys.stderr)
        return 1

    header = blob[:HEADER_SIZE]
    code = blob[HEADER_SIZE:]

    # Sanity: signature bytes at header offset 2..3 should be 0x12 0x34
    if header[2:4] != b"\x12\x34":
        print(f"WARN: header signature bytes at offset 2..3 are "
              f"{header[2]:#x} {header[3]:#x}, expected 0x12 0x34",
              file=sys.stderr)

    stream = emit_records(header, code)
    args.out.write_bytes(stream)
    print(f"code       : {len(code):>5} bytes ({len(code)/1024:.1f} KB)")
    print(f"header     : {len(header):>5} bytes")
    print(f"records    : {len(stream)//44:>5} × 44B = {len(stream)} bytes total")
    return 0


if __name__ == "__main__":
    sys.exit(main())
