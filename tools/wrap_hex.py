#!/usr/bin/env python3
"""
Convert SDCC's Intel HEX output to Digi's TI-style record format
(what mboxflash --flash consumes).

TI record format (per record):
    u32 length_BE
    u32 addr_BE
    u32 type_BE     (0 = data, 1 = EOF)
    length bytes of data

Layout of a complete EEPROM image (matches Rev 20 stock byte-for-byte):
    Records 0..N: 32-byte page-aligned chunks starting at address 0.
                  The 18-byte EEPROM header occupies the first 18 bytes
                  of record 0; code fills the remaining 14 bytes and
                  continues into subsequent records.
    (No explicit EOF marker — Rev 20 stock ends on the last data record
     and mboxflash's parser stops when it can't read another 12-byte
     record header.)

The 18-byte EEPROM header is the TAS1020A boot-ROM's expected preamble.
See firmware_stock/disasm/NOTES.md "EEPROM_HEADER_STRUCT" for the field
map. This script assembles it from the raw firmware size + a fixed
template so we don't have to hand-edit binary blobs.

Autodetect signature checked by mboxflash (payload.m:MBoxPayload_Autodetect):
    00 00 00 20   len = 32 BE
    00 00 00 00   addr = 0
    00 00 00 00   type = 0
    60 12 12 34   header chksum=0x60, size=0x12, sig=0x12 0x34
    0d ba         Digi VID
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
    # chksum byte = additive (NOT complemented) sum of the remaining 17
    # bytes, mod 256. Verified against stock rev20_flasher_payload.bin:
    # its header is 60 12 12 34 0d ba 10 01 01 01 04 fa 02 20 01 00 1f ee
    # and sum(bytes[1:]) & 0xFF = 0x60 = the stored chksum. TI's docs
    # describe a "complemented" chksum but the flasher payload uses raw
    # additive — matches what the TAS1020A boot ROM actually verifies.
    chksum = sum(body) & 0xFF
    return bytes([chksum]) + body


# ---- TI record stream ------------------------------------------------ #

EEPROM_SIZE = 8192   # TAS1020A boot ROM programs the full 8 KB per download.


def emit_records(header: bytes,
                 code: bytes,
                 page: int = 32) -> bytes:
    """
    Emit stock-compatible records: the whole image (header ++ code) is
    split into page-sized chunks starting at address 0. Each chunk becomes
    one type-0 record. There is no explicit EOF; mboxflash's parser stops
    when it runs out of bytes (matches rev20_flasher_payload.bin exactly).

    Pads to the FULL EEPROM_SIZE (not just page-aligned): the TAS1020A
    boot ROM won't transition dfuDNLOAD_IDLE → dfuMANIFEST until it has
    received the whole 8 KB. An unpadded short image leaves the device
    stuck in DFU on the next power cycle, treating the flash as
    incomplete (empirically observed 2026-07-18).
    """
    image = bytearray(header + code)
    if len(image) > EEPROM_SIZE:
        raise ValueError(f"image {len(image)} bytes exceeds EEPROM budget"
                         f" {EEPROM_SIZE}")
    # Fill the rest with 0xFF (matches erased-EEPROM state) so the boot
    # ROM sees a complete 8 KB stream and can commit.
    image += b"\xff" * (EEPROM_SIZE - len(image))

    out = bytearray()
    for addr in range(0, len(image), page):
        piece = bytes(image[addr:addr + page])
        out += struct.pack(">III", len(piece), addr, 0) + piece
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

    # payloadSize in the EEPROM header names the code-only size (matches
    # stock rev20: header field = 0x1FEE = 8174, and its code image is
    # exactly 8174 bytes). Emit_records() pages the whole (header+code)
    # blob afterwards.
    header = build_eeprom_header(len(code),
                                 vid=args.vid, pid=args.pid,
                                 max_power_mA=args.max_power_mA)
    stream = emit_records(header, code)
    args.out.write_bytes(stream)

    # Sanity: assert the first 18 bytes of the stream's record-0 data
    # match mboxflash's autodetect signature. If this ever trips we've
    # broken the wire format.
    sig = b"\x00\x00\x00\x20\x00\x00\x00\x00\x00\x00\x00\x00"  # len32 addr0 type0
    assert stream[:12] == sig, "record 0 does not match len=32/addr=0/type=0 signature"
    assert stream[12] == (sum(header[1:]) & 0xFF), "header chksum mismatch"
    assert stream[13] == 18 and stream[14:16] == b"\x12\x34", "header sig mismatch"
    assert stream[16:18] == b"\x0d\xba", "VID field not 0x0DBA"

    print(f"code       : {len(code):>5} bytes ({len(code)/1024:.1f} KB)")
    print(f"header     : {len(header):>5} bytes")
    print(f"records    : {len(stream)//44:>5} × 44B = {len(stream)} bytes total")
    return 0


if __name__ == "__main__":
    sys.exit(main())
