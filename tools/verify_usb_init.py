#!/usr/bin/env python3
"""
Verify that the compiled firmware image contains the SFR writes we
depend on for USB enumeration to succeed. Complements
verify_descriptors.py: that tool walks the descriptor payload in code
memory, this one greps the code stream for the SFR writes in usb_init
that the TAS1020A boot ROM needs to see.

We look for the 8051 idiom that SDCC emits for writing a constant to
an xdata SFR:

    90 hi lo    mov dptr, #0xhilo
    74 vv       mov a,    #0xvv
    f0          movx @dptr, a

The `mov a` may not always be adjacent (SDCC caches ACC across xdata
writes with the same value), so we also accept the pattern where a
literal `mov a, #val` appears in the code any time before a nearby
`mov dptr, #SFR; movx @dptr, a`.

Because this is a bytecode-level check, it can only catch _presence_ of
a write, not that the write is un-overridden later — but the writes we
check here (EP0 config, EP0 buffer addresses) only happen once in
usb_init and are never rewritten, so presence = correctness.

Usage: python3 tools/verify_usb_init.py [path/to/mboxfw.ihx]
"""

import sys
from pathlib import Path


def parse_ihx(text: str) -> bytes:
    chunks = {}
    max_addr = 0
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith(":"):
            continue
        n = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        rec_type = int(line[7:9], 16)
        data = bytes.fromhex(line[9:9 + 2 * n])
        if rec_type == 0x00:
            chunks[addr] = data
            max_addr = max(max_addr, addr + n)
        elif rec_type == 0x01:
            break
    out = bytearray(max_addr)
    for addr, data in chunks.items():
        out[addr:addr + len(data)] = data
    return bytes(out)


# The subset of usb_init writes whose values are enumeration-critical.
# These are Rev 20's exact values from fcn.0x0970 (Rev 22 fcn.0x0891).
# #180: this cited "rev20_flat.asm lines 1202-1226 (fcn.0x0982)". That listing
# disassembles rev20_eeprom.bin including its 18-byte header, so its addresses
# -- and the function label derived from them -- are the true address + 0x12;
# 0x0982 - 0x12 = 0x0970. Values themselves were unaffected, since the listing
# decodes the real code correctly and only its addresses are shifted.
# flat-asm-ok
#
# (sfr, expected_value, note)
CHECKS = [
    (0xFFA9, 0x42, "OEPBBAX0 — EP0 OUT buffer @ 0xFA10 (0xFA10/8)"),
    (0xFF69, 0x43, "IEPBBAX0 — EP0 IN  buffer @ 0xFA18 (0xFA18/8)"),
    (0xFFA8, 0x84, "OEPCNF0  — enable + interrupt on transaction"),
    (0xFF68, 0x84, "IEPCNF0  — enable + interrupt on transaction"),
]


def sfr_written_with(image: bytes, sfr: int, value: int) -> bool:
    """
    Return True if image contains a code sequence writing `value` into `sfr`.

    We locate every `mov dptr, #sfr` (90 hi lo) and walk forward up to
    16 bytes looking for `movx @dptr, a` (F0). Between the mov-dptr and
    the movx, or in the 16 bytes immediately preceding the mov-dptr, we
    look for a `mov a, #value` (74 vv). This accepts SDCC's tendency to
    hoist an A-load across an xdata write to a sibling register.
    """
    hi = (sfr >> 8) & 0xFF
    lo = sfr & 0xFF
    dptr = bytes([0x90, hi, lo])
    load_a = bytes([0x74, value])
    movx   = 0xF0

    i = 0
    while True:
        j = image.find(dptr, i)
        if j < 0:
            return False
        # Search up to 16 bytes ahead for the movx.
        window_after = image[j + 3:j + 3 + 16]
        movx_off = window_after.find(bytes([movx]))
        if movx_off >= 0:
            # Value could be loaded between mov dptr and movx …
            middle = window_after[:movx_off]
            if load_a in middle:
                return True
            # … or hoisted into the 16 bytes just before mov dptr.
            before = image[max(0, j - 16):j]
            if load_a in before:
                return True
        i = j + 1


def main() -> int:
    ihx_path = Path(sys.argv[1] if len(sys.argv) > 1
                    else "mboxfw/build/mboxfw.ihx")
    image = parse_ihx(ihx_path.read_text())

    fails = []
    for sfr, val, note in CHECKS:
        ok = sfr_written_with(image, sfr, val)
        marker = "OK" if ok else "MISS"
        print(f"  {marker:>4}  0x{sfr:04X} = 0x{val:02X}  {note}")
        if not ok:
            fails.append((sfr, val, note))

    if fails:
        print(f"\nFAIL: {len(fails)}/{len(CHECKS)} critical USB writes missing"
              f" from {ihx_path.name}")
        return 1
    print(f"\nPASS: all {len(CHECKS)} enumeration-critical USB writes present"
          f" in {ihx_path.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
