#!/usr/bin/env python3
"""
Fail if the compiled mboxfw code exceeds the TAS1020B's PROGRAM RAM.

6016 bytes (0x1780) is a HARDWARE limit, not a margin. The part has 6016
bytes of program RAM -- TAS1020B datasheet, features list and section 1
overview, stated twice; see ramloader/DESIGN.md "Verified constraints". The
boot ROM copies the EEPROM payload into that RAM and runs it there, so an
image larger than 6016 bytes cannot exist on this part, no matter how much
EEPROM sits behind it.

REVERTED 6144 -> 7168 -> 6016 on 2026-08-03. The 2026-07-30 raise argued
that "Rev 20 uses 8174 bytes -- nearly full EEPROM", so 6144 "was never a
hardware constraint". That premise was wrong: 8174 is the FF-padded EEPROM
region. Stock Rev 20's last non-0xFF byte is at 0x103E -- 4159 bytes of real
code; Rev 22 is 4150. Both sit about 1850 bytes UNDER 6016. Padding was read
as code, the limit was declared imaginary, and the guard came off.

Cost: on 2026-08-03 a 6448-byte mboxfw passed this gate, passed the other
31, flashed cleanly, reached DFU manifest, and came up silent on USB --
432 bytes had nowhere to live. See BRICK_LOG.md. Keep this number matched
to mboxfw/Makefile's --code-size.

The 8 KB EEPROM ceiling is a SEPARATE and larger constraint. It is reported
alongside so a future reader does not confuse the two the way that raise did.

Reads the .ihx file's highest byte address.
"""

import re
import sys
from pathlib import Path


PROGRAM_RAM  = 6016   # 0x1780 -- TAS1020B datasheet, stated twice. HARDWARE.
EEPROM_MAX   = 8174   # 24C64 capacity 8192 minus the 18-byte header. Not the
                      # binding limit -- the image must fit program RAM first.
BUDGET_BYTES = PROGRAM_RAM
WARN_AT      = 5600   # flag before the ceiling so growth trends surface early


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
