#!/usr/bin/env python3
"""
Golden regression: `wrap_hex(rev20_firmware_code)` must produce a
byte-for-byte match to `firmware_stock/rev20_flasher_payload.bin`.

Rev 20 is the ONLY firmware image we know for certain has been flashed
successfully to real hardware and boots. If our wrap_hex.py can no
longer reproduce it bit-perfect from raw code bytes + a synthesized
EEPROM header, we've broken the wrap and any subsequent flash will
behave in ways not covered by the "it worked once" evidence.

Silent drift here is exactly what bricked flash #1 on 2026-07-22 —
wrap_hex had been "fixed" to pad to 8192, which happened to produce
the right output for Rev 20 (payloadSize=8174 → 8174+18=8192 exact)
but corrupted every smaller firmware. A golden test rerun after that
change would have failed noisily instead of silently working for one
input.

Runs as a pre-flash gate. Fails the build on any drift.
"""

import sys
from pathlib import Path

# Import the two builder primitives directly from wrap_hex so this test
# exercises the exact code paths a real flash uses.
sys.path.insert(0, str(Path(__file__).parent))
from wrap_hex import build_eeprom_header, emit_records  # noqa: E402


REPO = Path(__file__).parent.parent
STOCK = REPO / "firmware_stock"
REV20_CODE = STOCK / "rev20_firmware_code.bin"
REV20_WRAPPED = STOCK / "rev20_flasher_payload.bin"


def main() -> int:
    code = REV20_CODE.read_bytes()
    expected = REV20_WRAPPED.read_bytes()

    # Rev 20's header field values, extracted from the stock file:
    #   VID=0x0DBA, PID=0x1001, maxPower=500 mA (fa/2)
    # See wrap_hex.build_eeprom_header docstring for the layout.
    header = build_eeprom_header(len(code),
                                 vid=0x0DBA,
                                 pid=0x1001,
                                 max_power_mA=500)
    got = emit_records(header, code)

    if got == expected:
        # Rev 20 alone can't catch pad-to-8192 regression: its payload
        # of 8174 bytes + 18-byte header = 8192 exactly, so a buggy
        # `pad up to 8192` produces the same output as `no pad`.
        # Cross-check with a small synthetic payload where any padding
        # WOULD show up as trailing 0xFF bytes.
        small_code = bytes(range(100))     # 100 bytes, deterministic
        small_hdr  = build_eeprom_header(len(small_code),
                                         vid=0x0DBA,
                                         pid=0x1001,
                                         max_power_mA=500)
        small_out  = emit_records(small_hdr, small_code)
        # Expected size: header (18) + code (100) = 118 bytes of data,
        # emitted in records (record framing adds bytes but wrap_hex
        # emits exactly `header + code`, no padding). Reject any
        # output that's suspiciously close to a full EEPROM (>= 4 KB
        # or ends with a long 0xFF run beyond the code length).
        if len(small_out) >= 4096:
            print(f"GOLDEN FAIL: 100-byte payload wrapped to {len(small_out)}"
                  f" bytes — wrap_hex is padding output.", file=sys.stderr)
            return 1
        # A trailing run of 0xFF longer than 32 bytes past the payload
        # end indicates padding (record framing itself never emits >32
        # contiguous 0xFF unless the payload had them).
        trailing_ff = 0
        for b in reversed(small_out):
            if b == 0xFF: trailing_ff += 1
            else: break
        if trailing_ff > 32:
            print(f"GOLDEN FAIL: 100-byte payload output ends with"
                  f" {trailing_ff} trailing 0xFF bytes — padding regression.",
                  file=sys.stderr)
            return 1
        print(f"GOLDEN PASS: wrap_hex(rev20_firmware_code)"
              f" == rev20_flasher_payload ({len(got)} bytes);"
              f" 100-byte payload wraps to {len(small_out)} bytes (no pad)")
        return 0

    # Drift — narrow down where.
    print(f"GOLDEN FAIL: wrap_hex output diverges from stock", file=sys.stderr)
    print(f"  expected size: {len(expected)}", file=sys.stderr)
    print(f"  got size:      {len(got)}", file=sys.stderr)

    if len(got) != len(expected):
        print(f"  size mismatch — wrap format changed", file=sys.stderr)
        return 1

    # Same size, different bytes. Report the first diverging offset with
    # a short window for context.
    for i, (a, b) in enumerate(zip(expected, got)):
        if a != b:
            lo, hi = max(0, i - 8), min(len(expected), i + 8)
            print(f"  first diff at offset 0x{i:04x}:", file=sys.stderr)
            print(f"    expected: {expected[lo:hi].hex()}", file=sys.stderr)
            print(f"    got:      {got[lo:hi].hex()}", file=sys.stderr)
            break
    return 1


if __name__ == "__main__":
    sys.exit(main())
