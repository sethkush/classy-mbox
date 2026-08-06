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
    # These entries were briefly reworded on 2026-08-04 to claim 0x0A is a HOST
    # command index and the branch therefore live. That was wrong and is
    # withdrawn; the original wording was correct. 0x0A really is an internal
    # work code in IRAM 0x0A, dispatched through event_jump_table @ 0x0300
    # (index = code - 1, so 0x0300 + 3*9 = 0x031B). The reachability claim is
    # now backed by a complete scan over EVERY addressing mode that can write
    # IRAM 0x0A in both images, not just the MOV direct,#imm idiom:
    #   13 immediates: 0x01-0x08 and 0x0B-0x0E -- 0x09 and 0x0A absent
    #   MOV 0x0a,A @ 0x0565: preceded by CLR A (dispatch epilogue) -> writes 0
    #   MOV 0x0a,A @ 0x0A06: A cleared at 0x09F5, not reloaded  -> writes 0
    # No site can post 0x0A, so the branch is genuinely unreachable.
    # (An apparent `INC 0x0a` at 0x0EE1 is a scanning artifact: the bytes are
    # `20 05 0a` = JB 0x05,0x0eed. Decode from instruction boundaries.)
    # See FINDING_capture_works_anyway.md.
    (0x23, 0): "stock's only setter is Rev 20 0x07B8 / Rev 22 0x0796, inside "
               "the clock-mode-5 branch of audio_clock_mode_apply, reachable "
               "only from work code 0x0A -- which no site in either image can "
               "post (all addressing modes scanned). Dead in stock too. "
               "FINDING_codec_word_bits_resolved.md",
    (0x23, 1): "same unreachable work-code-0x0A branch as 0x23.0 "
               "(Rev 20 0x07BA, Rev 22 0x0798)",
    # 0x25.0-.3 were gaps until #170 (2026-08-03). codec_source_changed() now
    # derives all four from g_mux_state on every publish.
    # 0x25.4 was a gap until #177 (2026-08-04). selector_set_source() in usb.c
    # now drives it from the UAC1 Selector Unit control, as stock's cmd4/cmd5
    # do (Rev 20 0x0454 / 0x0466, Rev 22 0x045A / 0x0469).
    (0x25, 5): "S/PDIF receiver engaged. Stock's only setter is `SETB 0x2d` at "
               "Rev 20 0x04CA / Rev 22 0x04C0, inside work code 0x0B -- which "
               "is UNREACHABLE: it fires only when P3.1 falls, and P3.1 is TXD "
               "on an unconfigured UART, measured to sit high with an S/PDIF "
               "carrier present and absent (FINDING_p31_is_txd.md, 2026-08-04). "
               "So the byte exists in both images and never executes in either. "
               "mboxfw not setting it matches stock's BEHAVIOUR, not its bytes. "
               "Consequence: codec_source_changed()'s 0x25.5 test is "
               "constant-false and its else branch always runs -- which is "
               "exactly what stock does at runtime too.",
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
DEFINE = re.compile(r"^\s*#define\s+(CODEC2[35]_[A-Z0-9_]+)\s+(0[xX][0-9a-fA-F]+|\d+)",
                    re.M)


def bit_macros():
    """The named codec-word bit macros from codec.h, as {NAME: value}.

    Without this, `g_codec_state_25 |= CODEC25_BRINGUP_DONE` reads as a
    non-literal RHS and the estimator conservatively assumes 0xFF, which
    silently hides every remaining gap. The macros are literals by another
    name, so resolve them.
    """
    text = (ROOT / "mboxfw" / "include" / "codec.h").read_text()
    return {name: int(val, 0) & 0xFF for name, val in DEFINE.findall(text)}


MACROS = bit_macros()


def _expand(rhs):
    """Substitute the codec-word bit macros for their values.

    WORD-BOUNDARY matching, and it matters. A plain str.replace() rewrites
    prefixes: with CODEC23_MUTE_PAIR defined, CODEC23_MUTE_PAIR_ALL became
    `0x0c_ALL`, which is not a literal, so the store was credited with all
    eight bits and the documented 0x23.0/0x23.1 gaps read as stale. The gate
    failed for a reason that had nothing to do with the firmware, and the
    failure pointed at the wrong file. Found 2026-08-06 adding #190.

    This is strictly more correct than the old behaviour, not more permissive:
    every name that resolved before still resolves, and names that merely
    START with another name now resolve instead of silently corrupting.
    """
    for name, val in MACROS.items():
        rhs = re.sub(r"\b" + re.escape(name) + r"\b", hex(val), rhs)
    return rhs


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

    sources = sorted((ROOT / "mboxfw" / "src").glob("*.c"))
    # Headers too: MONO_ON()/MONO_OFF() are macros over g_codec_state_23 that
    # live in codec.h, so a src-only scan would miss bit 0x23.6 entirely.
    sources += sorted((ROOT / "mboxfw" / "include").glob("*.h"))
    for src in sources:
        text = src.read_text()
        # Strip block and line comments so commented-out code cannot count.
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        text = re.sub(r"//[^\n]*", " ", text)
        # Two passes, because the two kinds of assignment terminate
        # differently and a single line-based scan gets one of them wrong.
        #
        # 2026-08-03: this WAS a single line-based scan with the terminator
        # `[^;\n]*`, i.e. "semicolon or end of line". That silently truncates
        # any statement wrapped across lines: #170's assignment to
        # g_codec_state_25 begins `= (unsigned char)` and continues on the
        # next line, so the captured RHS was the bare cast — no literals, and
        # no letters left after CAST_WORDS stripping, so it passed
        # _is_pure_literal and contributed 0. The function's contract is
        # "over-estimates, never under"; that path under-estimated, which is
        # the one direction that turns a real gap into a silent pass.
        # The two passes must run on DISJOINT text. A macro body carries no
        # semicolon of its own, so a ';'-terminated scan over the whole file
        # runs past the end of the #define and swallows everything up to the
        # next semicolon — for codec.h that is several lines of MONO_SET's
        # do/while, whose identifiers then trip the not-pure-literal branch
        # and mask the byte to 0xFF. Over-estimating to 0xFF is "safe" for the
        # gap check but destroys it as a signal: every bit reads as driven.
        macro_lines, code_lines = [], []
        for line in text.splitlines():
            if line.lstrip().startswith("#"):
                macro_lines.append(line)
                code_lines.append("")       # keep the split lossless
            else:
                macro_lines.append("")
                code_lines.append(line)
        code_text = "\n".join(code_lines)

        stmts = []
        for name, byte in names.items():
            if name not in text:
                continue
            # Statements: terminated by ';' only, so newlines are spanned and
            # a wrapped assignment is captured whole.
            for m in re.finditer(
                    re.escape(name) + r"\s*(\|=|&=|\^=|=)([^;]*);", code_text):
                stmts.append((byte, m.group(1), m.group(2)))
            # Macro bodies stay line-terminated:
            #   #define MONO_ON() (g_codec_state_23 |= CODEC23_MONO)
            for line in macro_lines:
                if name not in line:
                    continue
                m = re.search(re.escape(name) + r"\s*(\|=|&=|\^=|=)([^;\n]*)",
                              line)
                if m:
                    stmts.append((byte, m.group(1), m.group(2)))

        for byte, op, rhs in stmts:
            if op == "&=":
                continue                     # cannot set a bit
            rhs = _expand(rhs)
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
