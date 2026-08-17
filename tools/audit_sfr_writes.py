#!/usr/bin/env python3
"""
Lock every XDATA (SFR-mapped) write in the compiled mboxfw against a
citation manifest. Fails the build on any unauthorized write, immediate
value, or write pattern (assignment vs RMW).

Why this exists — 2026-07-22 flashes:
  Flash #1: wrap_hex.py padded past payloadSize. NOT caught by any
            audit because it was in tooling, not firmware — that's why
            we also have tools/test_wrap_hex_golden.py (task #49).
  Flash #2: `USBCTL = 0xC0` assignment clobbered CONN/FEN/SDW state
            the boot ROM had configured. Every SFR write should have a
            cited reference (Rev 20 disasm address OR TI reference
            file:line), and RMW-vs-assignment is part of the citation.
            If a new commit introduces a write we didn't previously
            authorize, or changes the pattern of an existing write,
            this script fails the build.

Manifest lives in tools/sfr_writes.allowed. Each line:
    ADDR  PATTERN  IMMEDIATE  CITATION
where ADDR is a 4-hex XDATA address (or `runtime` for computed),
PATTERN is one of {assign, or, and_not, rmw}, IMMEDIATE is the byte
value or `-` for computed values, and CITATION is a short
free-form reference (e.g. "Rev20 fcn.0x08CB @ 0x08E5" or "TI UsbEng.c
engUsbInit").

Run manually after intentional changes to update the manifest.
"""

import re
import sys
from pathlib import Path
from typing import NamedTuple


REPO = Path(__file__).parent.parent
BUILD = REPO / "mboxfw" / "build"
MANIFEST = Path(__file__).parent / "sfr_writes.allowed"
# Writes that some build tiers legitimately do not emit. Exempt from the
# REMOVED check ONLY -- see the header of the file itself.
TIER_OPTIONAL = Path(__file__).parent / "sfr_writes.tier_optional"


class Write(NamedTuple):
    addr: str        # "ffxx" (4 hex, lowercase) or "runtime"
    pattern: str     # "assign" | "or" | "and_not" | "rmw"
    immediate: str   # "0xNN" or "-"
    source_hint: str # e.g. "usb.c:415 usb_init"


# SDCC 8051 emits three tell-tale patterns we recognize:
#   mov dptr,#0xADDR
#   [optional temp shuffle]
#   [op] a,#0xIMM        ; anl / orl / mov / etc — determines pattern
#   movx @dptr,a
# For OR-in: op is `orl a, #0xIMM`; for AND-NOT: `anl a, #0xIMM`; for
# assign: `mov a, #0xIMM` (no prior read). RMW is a broader "read a
# first, then modify, then write" pattern we tag when there's an
# intervening `movx a,@dptr` before the write.

RE_DPTR     = re.compile(r"mov\s+dptr\s*,\s*#0x([0-9a-fA-F]{2,4})")
RE_MOVX_W   = re.compile(r"movx\s+@dptr\s*,\s*a")
RE_MOVX_R   = re.compile(r"movx\s+a\s*,\s*@dptr")
RE_ORL_IMM  = re.compile(r"orl\s+a\s*,\s*#0x([0-9a-fA-F]{1,2})")
RE_ANL_IMM  = re.compile(r"anl\s+a\s*,\s*#0x([0-9a-fA-F]{1,2})")
RE_MOV_IMM  = re.compile(r"mov\s+a\s*,\s*#0x([0-9a-fA-F]{1,2})")
RE_CLR_A    = re.compile(r"clr\s+a")
# SDCC often walks dptr with inc/dec dpl (0x15/0x05 82) or explicit
# dpl/dph loads. Track those so we get the write address right.
RE_INC_DPL  = re.compile(r"inc\s+dpl\b")
RE_DEC_DPL  = re.compile(r"dec\s+dpl\b")
RE_INC_DPTR = re.compile(r"inc\s+dptr\b")
RE_MOV_DPL  = re.compile(r"mov\s+dpl\s*,\s*#0x([0-9a-fA-F]{1,2})")
RE_MOV_DPH  = re.compile(r"mov\s+dph\s*,\s*#0x([0-9a-fA-F]{1,2})")


def scan_rst(path: Path) -> list[Write]:
    """
    Walk a single .rst and yield every write we can identify against
    an FFxx (SFR-space) address.
    """
    lines = path.read_text(errors="ignore").splitlines()
    writes: list[Write] = []
    src_hint = path.stem

    # State machine — keep the last-seen dptr address, whether we've
    # seen a preceding read (which classifies write as RMW), the
    # last-seen a-load immediate + op class.
    last_dptr = None    # "ffxx" or None
    saw_read  = False   # movx a,@dptr since last dptr load
    op_class  = None    # "or" | "and_not" | "assign" | None
    op_imm    = None    # "0xNN" or None

    for raw in lines:
        line = raw.lower()
        # Strip line-number prefix + hex machine code prefix. Keep the
        # mnemonic tail.
        m = RE_DPTR.search(line)
        if m:
            last_dptr = m.group(1).zfill(4)
            saw_read  = False
            op_class  = None
            op_imm    = None
            continue

        # dptr byte-level nudges — SDCC uses these to walk through a
        # run of adjacent XDATA registers without re-emitting the full
        # 3-byte mov dptr,#... instruction.
        if last_dptr is not None:
            if RE_INC_DPL.search(line) or RE_INC_DPTR.search(line):
                last_dptr = f"{(int(last_dptr, 16) + 1) & 0xFFFF:04x}"
                continue
            if RE_DEC_DPL.search(line):
                last_dptr = f"{(int(last_dptr, 16) - 1) & 0xFFFF:04x}"
                continue
            m = RE_MOV_DPL.search(line)
            if m:
                high = int(last_dptr, 16) & 0xFF00
                last_dptr = f"{high | int(m.group(1), 16):04x}"
                continue
            m = RE_MOV_DPH.search(line)
            if m:
                low = int(last_dptr, 16) & 0x00FF
                last_dptr = f"{(int(m.group(1), 16) << 8) | low:04x}"
                continue

        if RE_MOVX_R.search(line):
            saw_read = True
            continue

        m = RE_ORL_IMM.search(line)
        if m:
            op_class = "or"
            op_imm   = "0x" + m.group(1).zfill(2)
            continue

        m = RE_ANL_IMM.search(line)
        if m:
            op_class = "and_not"
            op_imm   = "0x" + m.group(1).zfill(2)
            continue

        m = RE_MOV_IMM.search(line)
        if m:
            op_class = "assign"
            op_imm   = "0x" + m.group(1).zfill(2)
            continue

        if RE_CLR_A.search(line):
            op_class = "assign"
            op_imm   = "0x00"
            continue

        if RE_MOVX_W.search(line):
            # Only care about FFxx SFR-space writes.
            if not last_dptr or not last_dptr.startswith("ff"):
                continue

            # Classify pattern. Saw_read wins if present (that's RMW
            # regardless of the op class). Then op_class if we captured
            # one before the write. Otherwise "runtime" (unknown value
            # in accumulator from a prior computation).
            if saw_read:
                pattern = op_class if op_class in ("or", "and_not") else "rmw"
            elif op_class == "or":
                pattern = "or"
            elif op_class == "and_not":
                pattern = "and_not"
            elif op_class == "assign":
                pattern = "assign"
            else:
                pattern = "runtime"

            imm = op_imm if op_imm and pattern != "rmw" else "-"
            writes.append(Write(last_dptr, pattern, imm, src_hint))
            # Don't clear last_dptr — SDCC often reuses dptr across
            # sequential writes to the same address.
            saw_read = False
            op_class = None
            op_imm   = None

    return writes


def load_manifest(path: Path = None) -> set[tuple[str, str, str]]:
    path = MANIFEST if path is None else path
    if not path.exists():
        return set()
    out: set[tuple[str, str, str]] = set()
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split(None, 3)
        if len(parts) < 3:
            continue
        addr = parts[0].lower().removeprefix("0x")
        pattern = parts[1].lower()
        imm = parts[2].lower()
        out.add((addr, pattern, imm))
    return out


def main() -> int:
    if not BUILD.exists():
        print(f"FAIL: {BUILD} does not exist — run `make` first", file=sys.stderr)
        return 1

    all_writes: list[Write] = []
    for rst in sorted(BUILD.glob("*.rst")):
        all_writes.extend(scan_rst(rst))

    # Deduplicate by (addr, pattern, immediate). Aggregate source
    # hints for reporting.
    unique: dict[tuple[str, str, str], set[str]] = {}
    for w in all_writes:
        key = (w.addr, w.pattern, w.immediate)
        unique.setdefault(key, set()).add(w.source_hint)

    allowed = load_manifest()
    unauthorized = [(k, srcs) for k, srcs in sorted(unique.items())
                    if k not in allowed]

    # --update mode: rewrite the manifest with the current write set
    # as the new baseline. Use ONLY after intentional review. Committed
    # baseline is the anchor future runs diff against.
    if "--update" in sys.argv[1:]:
        lines = [
            "# SFR-write manifest — regenerated by audit_sfr_writes.py --update.",
            "# Every entry: ADDR  PATTERN  IMMEDIATE  # source-file(s)",
            "# Any diff between the current .rst set and this file fails the",
            "# audit gate. Update ONLY after reviewing what changed and",
            "# confirming the new write is intentional.",
            "#",
            "# Bug precedent — 2026-07-22 flash #2 was `USBCTL = 0xC0`",
            "# (assign,0xc0) at 0xfffc when Rev 20 has (or,0x80). A baseline",
            "# generated from a working image would have flagged that",
            "# specific tuple as a diff before flash.",
            "",
        ]
        for (addr, pattern, imm), srcs in sorted(unique.items()):
            srcs_s = ",".join(sorted(srcs))
            lines.append(f"0x{addr}  {pattern:8}  {imm:6}  # {srcs_s}")
        MANIFEST.write_text("\n".join(lines) + "\n")
        print(f"wrote {len(unique)} entries to {MANIFEST}")
        return 0

    # Also detect entries in the manifest that are NO LONGER produced by
    # the build — a removed write is also a drift worth surfacing.
    # A write the build does not emit is drift worth surfacing -- EXCEPT for
    # entries that are known to be tier-conditional. Without this exemption the
    # only way to green the gate on a non-baselined tier is `--update`, which
    # re-baselines onto whatever is in build/ and would erase a real removal.
    removed = allowed - {k for k in unique} - load_manifest(TIER_OPTIONAL)

    if not unauthorized and not removed:
        print(f"SFR AUDIT PASS: {len(unique)} unique writes, all match manifest"
              f" ({MANIFEST.name})")
        return 0

    if unauthorized:
        print(f"SFR AUDIT FAIL: {len(unauthorized)} write(s) new since last"
              f" baseline — not in {MANIFEST.name}:", file=sys.stderr)
        for (addr, pattern, imm), srcs in unauthorized:
            srcs_s = ", ".join(sorted(srcs))
            print(f"  + 0x{addr}  {pattern:8}  {imm:6}  # {srcs_s}",
                  file=sys.stderr)
    if removed:
        print(f"SFR AUDIT FAIL: {len(removed)} write(s) in {MANIFEST.name}"
              f" no longer emitted by the build:", file=sys.stderr)
        for addr, pattern, imm in sorted(removed):
            print(f"  - 0x{addr}  {pattern:8}  {imm:6}", file=sys.stderr)
    print(f"\nIf all diffs are intentional, review each and re-baseline:",
          file=sys.stderr)
    print(f"    python3 tools/audit_sfr_writes.py --update", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
