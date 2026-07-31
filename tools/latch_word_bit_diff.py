#!/usr/bin/env python3
"""
Latch-word bit-coverage gate.

The Mbox drives two serial shift chains off P1, and stock holds their state in
three IRAM bytes:

    IRAM 0x22   panel / source-mux word   (mboxfw: g_mux_state)
    IRAM 0x23   codec control word, HIGH  (mboxfw: g_codec_state_23)
    IRAM 0x25   codec control word, LOW   (mboxfw: g_codec_state_25)

Every bit of those three bytes is a physical control line. This gate asks one
question per bit: **does stock ever drive it to 1, and does mboxfw?**

It exists because that question was asked by hand exactly once, about IRAM
0x23.4, and the answer was a defect -- mboxfw never sets it, so the external
chip reset is asserted for the life of the firmware (#166). Nothing was
checking the other twenty-three bits. This checks all of them.

Stock side: byte-scan both images for every opcode that can set a bit --
SETB (D2), MOV bit,C (92), CPL (B2) -- at the bit address of each bit.
Bit address of IRAM byte B bit N is (B - 0x20) * 8 + N.

mboxfw side: parse assignments to the three mirrors out of the C source and
union the masks that can set bits. `= 0` and `&= ~m` cannot set anything;
`|= m` sets m; `= <expr>` contributes any literal masks in the expression.
This is deliberately generous -- it over-estimates what mboxfw sets, so a
reported gap is a real gap.

Exit status is 0 when every gap is listed in EXPECTED_GAPS below with a
reason, 1 otherwise. Adding a bit to that table is a claim that the gap is
understood; leaving it out makes this gate fail.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

MIRRORS = {
    0x22: ("g_mux_state", "panel / source-mux word"),
    0x23: ("g_codec_state_23", "codec control word, high byte"),
    0x25: ("g_codec_state_25", "codec control word, low byte"),
}

SET_OPCODES = {0xD2: "SETB", 0x92: "MOV bit,C", 0xB2: "CPL"}

# Bits stock drives that mboxfw does not, WITH the reason each is accepted.
# A bit listed here is a known, recorded gap. Anything not listed fails.
EXPECTED_GAPS = {
    (0x23, 0): "stock's only setter is in the mode-5 branch reachable solely "
               "from work code 0x0A, which nothing posts in either image -- "
               "dead in stock too. FINDING_codec_word_bits_resolved.md",
    (0x23, 1): "same mode-5 dead branch as 0x23.0",
    (0x23, 4): "#166 -- external-chip RESET, active low, never released by "
               "mboxfw. Held asserted for the life of the firmware.",
    (0x23, 6): "mono. mboxfw keeps it in a separate __bit g_mono, which "
               "mux_write() presents as the panel chain's 9th bit. That half "
               "works; the CODEC word's bit 6 is still always 0 where stock "
               "mirrors the live mono state.",
    (0x25, 0): "source pattern, channel field bit -- codec word low byte is "
               "never driven at all (#170)",
    (0x25, 1): "source pattern, channel field bit (#170)",
    (0x25, 2): "source pattern, channel field bit (#170)",
    (0x25, 3): "source pattern, channel field bit (#170)",
    (0x25, 4): "#159 -- UAC Selector Unit position (0 = analog, 1 = S/PDIF). "
               "Read by codec_source_changed(), set by nothing.",
    (0x25, 5): "S/PDIF receiver engaged. Read by codec_source_changed(), set "
               "by nothing (#170).",
    (0x25, 6): "bring-up-has-run guard (#170)",
    (0x25, 7): "#167 -- CS8427 chip select, active low. Never driven, so the "
               "part never sees the high->low transition that selects SPI mode.",
}


def stock_set_bits():
    """{(byte, bit): {image: [sites]}} for bits either stock image can set."""
    images = {}
    for name in ("rev20", "rev22"):
        p = ROOT / "firmware_stock" / f"{name}_firmware_code.bin"
        images[name] = p.read_bytes()

    out = {}
    for byte in MIRRORS:
        for bit in range(8):
            addr = (byte - 0x20) * 8 + bit
            per_image = {}
            for name, blob in images.items():
                sites = []
                for opcode, mnem in SET_OPCODES.items():
                    pat = bytes([opcode, addr])
                    i = blob.find(pat)
                    while i >= 0:
                        sites.append(f"{mnem}@0x{i:04x}")
                        i = blob.find(pat, i + 1)
                if sites:
                    per_image[name] = sites
            if per_image:
                out[(byte, bit)] = per_image
    return out


LITERAL = re.compile(r"0[xX][0-9a-fA-F]+|\b\d+\b")
CAST_WORDS = re.compile(r"\b(unsigned|signed|char|int|short|long|void)\b")


def _literals(rhs):
    return [int(t, 0) & 0xFF for t in LITERAL.findall(rhs)]


def _is_pure_literal(rhs):
    """True when the RHS is built only from integer literals and operators."""
    stripped = LITERAL.sub(" ", rhs)
    stripped = CAST_WORDS.sub(" ", stripped)
    return not re.search(r"[A-Za-z_]", stripped)


def mboxfw_set_masks():
    """{byte: mask} — bits mboxfw can set. Over-estimates, never under."""
    masks = {b: 0 for b in MIRRORS}
    names = {v[0]: k for k, v in MIRRORS.items()}

    for src in sorted((ROOT / "mboxfw" / "src").glob("*.c")):
        text = src.read_text()
        # Strip block and line comments so commented-out code cannot count.
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        text = re.sub(r"//[^\n]*", " ", text)
        for line in text.splitlines():
            for name, byte in names.items():
                if name not in line:
                    continue
                m = re.search(re.escape(name) + r"\s*(\|=|&=|\^=|=)([^;]*);", line)
                if not m:
                    continue
                op, rhs = m.group(1), m.group(2)
                if op == "&=":
                    continue                     # cannot set a bit
                lits = _literals(rhs)

                if not _is_pure_literal(rhs):
                    # A helper call or another variable: assume it can set
                    # anything. Generous on purpose — a reported gap stays real.
                    masks[byte] |= 0xFF
                    continue

                if op in ("|=", "^="):
                    for v in lits:
                        masks[byte] |= v
                elif "~" in rhs and lits:
                    # e.g. (0xFF & ~0x01 & ~0x08): first literal, clear the rest.
                    val = lits[0]
                    for v in lits[1:]:
                        val &= ~v & 0xFF
                    masks[byte] |= val
                else:
                    for v in lits:
                        masks[byte] |= v
    return masks


def main():
    stock = stock_set_bits()
    mine = mboxfw_set_masks()

    gaps, unexpected, stale = [], [], []
    for (byte, bit), per_image in sorted(stock.items()):
        if mine[byte] & (1 << bit):
            continue
        gaps.append((byte, bit, per_image))
        if (byte, bit) not in EXPECTED_GAPS:
            unexpected.append((byte, bit, per_image))

    for key in EXPECTED_GAPS:
        if key not in [(b, n) for b, n, _ in gaps]:
            stale.append(key)

    print("Latch-word bit coverage: stock sets it / does mboxfw?\n")
    for byte, (name, label) in MIRRORS.items():
        print(f"IRAM 0x{byte:02X}  {name}  ({label})")
        print(f"    mboxfw can set mask 0x{mine[byte]:02X}")
        for bit in range(8):
            per_image = stock.get((byte, bit))
            if not per_image:
                print(f"    .{bit}  stock never sets it            —")
                continue
            covered = bool(mine[byte] & (1 << bit))
            both = "both" if len(per_image) == 2 else "+".join(per_image)
            if covered:
                print(f"    .{bit}  stock sets ({both})            mboxfw: yes")
            else:
                why = EXPECTED_GAPS.get((byte, bit), "UNEXPLAINED")
                print(f"    .{bit}  stock sets ({both})            mboxfw: NO  — {why}")
        print()

    if stale:
        print("STALE EXPECTED_GAPS entries (mboxfw now sets these; remove them):")
        for byte, bit in stale:
            print(f"  IRAM 0x{byte:02X}.{bit}")
        return 1

    if unexpected:
        print("FAIL: bits stock drives that mboxfw does not, with no recorded reason:")
        for byte, bit, per_image in unexpected:
            print(f"  IRAM 0x{byte:02X}.{bit}  {per_image}")
        return 1

    print(f"PASS: {len(gaps)} known gaps, all recorded; no unexplained ones.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
