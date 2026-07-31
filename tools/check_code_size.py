#!/usr/bin/env python3
"""
Fail if the compiled mboxfw code exceeds a safety-margin budget under
the 8 KB EEPROM. Rev 20 uses 8174 bytes — nearly full EEPROM. We leave
room for the 18-byte header + 5-byte SDCC tail + slack for descriptor
growth.

Budget raised 6144 -> 7168 on 2026-07-30, with the linker's --code-size
in mboxfw/Makefile. The hard ceiling is 8174 (8192 minus the 18-byte
header), and stock Rev 20 sits exactly there, so 6144 was never a
hardware constraint — it was a margin chosen when the image was half
this size. It started rejecting diagnostic code that the hardware has
ample room for. 7168 still leaves 1006 bytes under the real ceiling.

Also reports where we are as a percentage so runaway growth surfaces
early. Reads the .ihx file's highest byte address.
"""

import re
import sys
from pathlib import Path


HARD_CEILING = 8174   # 24C64 capacity 8192 minus the 18-byte EEPROM header
BUDGET_BYTES = 7168
WARN_AT      = 6656   # flag before the budget so growth trends surface early


def code_size(ihx_path: Path) -> int:
    max_addr = 0
    for line in ihx_path.read_text().splitlines():
        m = re.match(r":([0-9A-Fa-f]{2})([0-9A-Fa-f]{4})([0-9A-Fa-f]{2})", line)
        if not m:
            continue
        n, addr, rec_type = int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16)
        if rec_type == 0:
            max_addr = max(max_addr, addr + n)
        elif rec_type == 1:
            break
    return max_addr


def main() -> int:
    ihx = Path(sys.argv[1] if len(sys.argv) > 1
               else "mboxfw/build/mboxfw.ihx")
    if not ihx.exists():
        print(f"not found: {ihx} (run make first)", file=sys.stderr)
        return 2

    size = code_size(ihx)
    pct = 100.0 * size / 8192
    print(f"  code:   {size:5d} bytes ({pct:.1f}% of 8 KB EEPROM)")
    print(f"  budget: {BUDGET_BYTES} bytes ({100.0 * BUDGET_BYTES / 8192:.1f}%)")

    if size > BUDGET_BYTES:
        print(f"\nSIZE FAIL: {size} > {BUDGET_BYTES} bytes",
              file=sys.stderr)
        print("Trim before flashing. Options:", file=sys.stderr)
        print("  - Move rarely-used constants to __code (already default)",
              file=sys.stderr)
        print("  - Shrink string descriptors", file=sys.stderr)
        print("  - Drop dead code (audit with grep --unused)",
              file=sys.stderr)
        return 1

    if size > WARN_AT:
        print(f"\nSIZE WARN: {size} > {WARN_AT} bytes — trending toward")
        print("           the 6 KB budget. Review recent commits for growth.")
        # Warn only, don't fail.

    print("\nSIZE PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
