#!/usr/bin/env python3
"""
Convert SDCC's Intel HEX output to Digi's TI-style record format
(what mboxflash --flash consumes).

TI record format (per record):
    u32 length_BE
    u32 addr_BE
    u32 type_BE     (0 = data, 1 = EOF)
    length bytes of data

Layout of a complete EEPROM image (matches Rev 20 / v22):
    Record 0: EEPROM header (18 bytes at addr 0)
    Record 1..N: firmware code, up to 32 bytes each
    Final record: EOF marker (length=0, type=1)

The 18-byte EEPROM header is the TAS1020A boot-ROM's expected preamble.
See firmware_stock/disasm/NOTES.md "EEPROM_HEADER_STRUCT" for the field
map. This script assembles it from the raw firmware size + a fixed
template so we don't have to hand-edit binary blobs.
"""

import argparse
import struct
import sys
from pathlib import Path


# ---- Intel HEX ------------------------------------------------------- #

def parse_ihx(text: str) -> bytes:
    """Return the concatenated code bytes from an SDCC .ihx file."""
    max_addr = 0
    chunks = {}
    for line_no, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line.startswith(":"):
            continue
        n = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        rec_type = int(line[7:9], 16)
        data = bytes.fromhex(line[9:9 + 2 * n])
        # Checksum byte follows; skip validation for now.
        if rec_type == 0x00:  # data record
            chunks[addr] = data
            max_addr = max(max_addr, addr + n)
        elif rec_type == 0x01:  # EOF
            break
        elif rec_type in (0x02, 0x04, 0x05):
            # extended addressing / entry point — we don't need them
            # for the 64 KB single-bank 8051 case
            continue
        else:
            print(f"warn: line {line_no} unknown record type 0x{rec_type:02x}",
                  file=sys.stderr)

    if not chunks:
        raise ValueError("no data records found in .ihx")

    out = bytearray(max_addr)
    for addr, data in chunks.items():
        out[addr:addr + len(data)] = data
    return bytes(out)


# ---- EEPROM header --------------------------------------------------- #

def build_eeprom_header(payload_size: int,
                        vid: int = 0x0DBA,
                        pid: int = 0x1001,
                        max_power_mA: int = 500) -> bytes:
    """
    Build the 18-byte TAS1020A EEPROM_HEADER_STRUCT.
    Fields (per TI's ROM/eeprom.h):
        u8  headerChksum       = complemented sum of remaining bytes
        u8  headerSize         = 18
        u8  signatures[2]      = { 0x12, 0x34 }
        u16 vendorId (BE)
        u16 productId (BE)
        u8  productVersion     = 0x01
        u8  FirmwareVersion    = 0x01
        u8  usbAttribute       = 0x04 (self-powered flag — matches Rev 20)
        u8  maxPower           = mA / 2
        u8  attribute          = 0x02 (EEPROM_HEADER_OVERWRITE)
        u8  wPageSize          = 32
        u8  dataType           = 0x01 (APPCODE)
        u8  rPageSize          = 0
        u16 payloadSize (BE)
    """
    body = struct.pack(
        ">BBBHHBBBBBBBBH",
        18,          # headerSize
        0x12, 0x34,  # signatures
        vid, pid,
        0x01, 0x01,  # productVersion, FirmwareVersion
        0x04,        # usbAttribute
        max_power_mA // 2,
        0x02,        # attribute
        32,          # wPageSize
        0x01,        # dataType = APPCODE
        0x00,        # rPageSize
        payload_size,
    )
    chksum = (-sum(body)) & 0xFF
    return bytes([chksum]) + body


# ---- TI record stream ------------------------------------------------ #

def emit_records(header: bytes,
                 code: bytes,
                 chunk: int = 32) -> bytes:
    """
    Emit the full stream:
        record 0: header @ addr 0
        record 1..N: code in `chunk`-byte pieces starting at addr len(header)
        final: EOF marker
    """
    out = bytearray()

    def rec(addr: int, rtype: int, data: bytes) -> bytes:
        return struct.pack(">III", len(data), addr, rtype) + data

    out += rec(0, 0, header)
    off = len(header)
    for i in range(0, len(code), chunk):
        piece = code[i:i + chunk]
        out += rec(off + i, 0, piece)
    out += rec(0, 1, b"")   # EOF
    return bytes(out)


# ---- entry ----------------------------------------------------------- #

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ihx", type=Path, help="SDCC .ihx input")
    ap.add_argument("-o", "--out", type=Path, required=True,
                    help="output flasher-payload .bin")
    ap.add_argument("--vid", type=lambda s: int(s, 0), default=0x0DBA)
    ap.add_argument("--pid", type=lambda s: int(s, 0), default=0x1001)
    ap.add_argument("--max-power-mA", type=int, default=500)
    args = ap.parse_args()

    code = parse_ihx(args.ihx.read_text())
    # Pad code to page-boundary — the TAS1020A EEPROM programmer expects
    # writes aligned to wPageSize (32).
    pad = (-len(code)) & 31
    code += b"\xff" * pad

    header = build_eeprom_header(len(code),
                                 vid=args.vid, pid=args.pid,
                                 max_power_mA=args.max_power_mA)
    stream = emit_records(header, code)
    args.out.write_bytes(stream)

    print(f"code       : {len(code):>5} bytes ({len(code)/1024:.1f} KB)")
    print(f"header     : {len(header):>5} bytes")
    print(f"records    : {len(stream):>5} bytes total")
    return 0


if __name__ == "__main__":
    sys.exit(main())
