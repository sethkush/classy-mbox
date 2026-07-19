#!/usr/bin/env python3
"""
Verify that the compiled firmware contains the CS8427 boot sequence
Rev 20 uses at fcn.0x080B. The bit-banger serialises three bytes per
register write over P1.3/P1.4 (start / addr 0x20 / reg / value / stop),
so we can't grep for a `mov dptr` idiom — the registers and values
flow through the code as immediate-load constants that get shifted out
bit-by-bit.

Instead: for each expected (reg, value) pair we grep the code image
for the pattern SDCC emits when `cs8427_write(reg, value)` is called
with two literal args. SDCC on mcs51 passes the first arg in DPL and
spills the second to a static IRAM slot `_cs8427_write_PARM_2`, so a
call to `cs8427_write(0x04, 0x00)` compiles to exactly:

    75 <slot> <value>   mov _cs8427_write_PARM_2, #value
    75 82    <reg>      mov dpl, #reg
    12 <hi>  <lo>       lcall _cs8427_write

We resolve the PARM_2 IRAM address from the .map file and grep for
the two-load pair (in either order) within a short window.

Reference: NOTES.md § "CS8427 boot sequence" table, mboxfw/src/cs8427.c.

Usage: python3 tools/verify_cs8427.py [path/to/mboxfw.ihx]
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


# Rev 20 fcn.0x080B / mboxfw cs8427_boot_init() — the 10-register
# CS8427 boot sequence. Each entry is (reg_byte, value_byte, note).
CS8427_BOOT_SEQ = [
    (0x04, 0x00, "Clock Source Ctrl — reset (RUN=0)"),
    (0x13, 0x10, "Channel Status Byte format"),
    (0x04, 0x00, "Clock Source Ctrl — still reset (re-armed)"),
    (0x04, 0x40, "Clock Source Ctrl — RUN=1, clock enabled"),
    (0x01, 0x01, "Chip Control 2"),
    (0x02, 0x20, "Data Flow Control"),
    (0x03, 0x0C, "Clock Source Control 3"),
    (0x05, 0x05, "Serial Audio Input Format"),
    (0x06, 0x05, "Serial Audio Output Format"),
    (0x11, 0xFF, "Interrupt Mask (enable all)"),
]


def resolve_parm2_slot(map_path: Path) -> int:
    """Extract the IRAM address SDCC assigned to _cs8427_write_PARM_2.

    Falls back to 0x09 (empirically what SDCC picked on 2026-07) if the
    symbol isn't in the map file — the fallback lets the verifier still
    run on older map files, at the cost of possibly missing renames.
    """
    if not map_path.exists():
        return 0x09
    for line in map_path.read_text().splitlines():
        # Map lines look like: "  DEF  _cs8427_write_PARM_2  0009"
        if "_cs8427_write_PARM_2" not in line:
            continue
        parts = line.split()
        for tok in reversed(parts):
            try:
                return int(tok, 16)
            except ValueError:
                continue
    return 0x09


def find_call_with_args(image: bytes, reg: int, value: int, slot: int) -> bool:
    """
    Return True if the image contains a 2-load pattern (dpl, PARM_2)
    for (reg, value) within a 12-byte window, in either order.
    """
    load_reg = bytes([0x75, 0x82, reg])          # mov dpl, #reg
    load_val = bytes([0x75, slot, value])        # mov _PARM_2, #value

    i = 0
    while True:
        j = image.find(load_reg, i)
        if j < 0:
            break
        window = image[max(0, j - 12):j + 3 + 12]
        if load_val in window:
            return True
        i = j + 1
    return False


def main() -> int:
    ihx_path = Path(sys.argv[1] if len(sys.argv) > 1
                    else "mboxfw/build/mboxfw.ihx")
    map_path = ihx_path.with_suffix(".map")
    image = parse_ihx(ihx_path.read_text())
    slot = resolve_parm2_slot(map_path)
    print(f"  _cs8427_write_PARM_2 slot resolved to IRAM 0x{slot:02X}")

    fails = []
    for i, (reg, val, note) in enumerate(CS8427_BOOT_SEQ, 1):
        ok = find_call_with_args(image, reg, val, slot)
        marker = "OK" if ok else "MISS"
        print(f"  {marker:>4}  #{i:2d}  reg 0x{reg:02X} = 0x{val:02X}  {note}")
        if not ok:
            fails.append((i, reg, val, note))

    if fails:
        print(f"\nFAIL: {len(fails)}/{len(CS8427_BOOT_SEQ)} CS8427 boot writes"
              f" missing from {ihx_path.name}")
        for i, reg, val, note in fails:
            print(f"       #{i:2d}  0x{reg:02X} = 0x{val:02X}  ({note})")
        return 1
    print(f"\nPASS: all {len(CS8427_BOOT_SEQ)} CS8427 boot writes present in"
          f" {ihx_path.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
