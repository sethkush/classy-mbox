#!/usr/bin/env python3
"""
Diff mboxfw's SFR writes against Rev 20's. Rev 20 is the only firmware
we know for certain boots and enumerates on real hardware; anywhere
mboxfw deviates without justification is exactly where tonight's bricks
came from (USBCTL=0xC0 assignment vs. Rev 20's OR-in of just CONN).

Report categories:
  MATCH       : same (addr, pattern, immediate) tuple in both.
  MBOXFW_ONLY : mboxfw writes it, Rev 20 doesn't — every one needs a
                justification. Novel state changes are the only place
                new bugs can live.
  REV20_ONLY  : Rev 20 writes it, mboxfw doesn't — we may be missing
                some init step that Rev 20 relies on. Especially
                dangerous for anything the boot ROM assumes we set.
  CHANGED     : same address, different pattern OR different immediate.
                Highest-priority review — this is the exact shape of
                the USBCTL=0xC0 vs |=0x80 bug.

Justifications live in tools/rev20_diff_justifications.md as a table:
    addr | pattern | immediate | source | reason
The gate fails if any diff is missing a justification row.
"""

import re
import sys
from pathlib import Path


REPO = Path(__file__).parent.parent
# WHAT THIS FILE ACTUALLY IS, measured 2026-08-05.
#
# rev20_flat.asm is a linear-sweep disassembly of rev20_EEPROM.bin -- the full
# 8192-byte EEPROM image INCLUDING its 18-byte header -- not of
# rev20_firmware_code.bin. Every instruction in it matches rev20_eeprom.bin at
# the address it claims (4096/4096), and 4086/4086 of the instructions at
# address >= 18 match the CODE image at (address - 0x12).
#
# So: **every address in rev20_flat.asm is the true code address + 0x12.**
#
# The instruction DECODING of the real code is nevertheless correct, by luck:
# the 18 header bytes happen to decode as exactly 18 bytes of junk
# instructions (60 12 | 12 34 0d | ba 10 01 | 01 01 | 04 | fa | 02 20 01 | 00 |
# 1f | ee), so the sweep lands byte-aligned on the reset vector and never
# desynchronises. That is why the write-address set extracted below is sound:
# checked against XDATA_ACCESS_MAP.md (built from the binary plus the Ghidra
# recursive listing) it misses ZERO of the 52 addresses stock writes.
#
# This gate only ever consumes (SFR address, pattern, immediate) and never an
# instruction address, so the +0x12 offset does not affect its output. It DOES
# affect anything that quotes an address from this listing -- see
# FINDING_rev20_flat_asm_is_offset_by_18.md.
REV20_ASM = REPO / "firmware_stock" / "disasm" / "rev20_flat.asm"
JUSTIFY = Path(__file__).parent / "rev20_diff_justifications.md"

# Reuse audit_sfr_writes primitives — the scanner already handles SDCC's
# dpl/inc-dptr walk conventions. Rev 20's radare2 flat disasm uses the
# same 8051 mnemonics with slightly different formatting, so the regexes
# still hit. Import the manifest loader too so the "mboxfw side" list is
# whatever the current manifest promises we emit.
sys.path.insert(0, str(Path(__file__).parent))
from audit_sfr_writes import scan_rst, load_manifest  # noqa: E402


# --- Rev 20 side --------------------------------------------------- #

def load_rev20_helper_writes() -> set[tuple[str, str, str]]:
    """Rev 20 writes performed BY A CALLEE against the caller's DPTR.

    The linear scan below cannot see these. The caller loads DPTR and calls a
    shared helper; the helper issues the MOVX. In a linear listing the
    instructions following the LCALL are the caller's continuation, not the
    helper's, so there is nothing to attribute the write to -- and once DPTR
    tracking correctly stops at control-flow edges, the write vanishes
    entirely. Before that fix these sites were "found" only because a stale
    DPTR was carried across branches until some unrelated MOVX turned up; for
    ACGDCTL that happened to yield the right value, which is luck, not method.

    XDATA_ACCESS_MAP.md models them explicitly as `write-via-helper <target>
    [<value>]`, built from the binary plus the Ghidra recursive listing. Where
    the map records the value, use it. Where it does not, the value lives in
    the helper's own body -- decode the leading `MOV A,#imm` straight from the
    image rather than assuming one.
    """
    MAP = REPO / "firmware_stock" / "disasm" / "XDATA_ACCESS_MAP.md"
    IMG = REPO / "firmware_stock" / "rev20_firmware_code.bin"
    if not MAP.exists() or not IMG.exists():
        return set()
    img = IMG.read_bytes()
    text = MAP.read_text(errors="ignore")
    rev20 = text.split("## rev20", 1)[1].split("## rev22", 1)[0]

    out: set[tuple[str, str, str]] = set()
    cur = None
    for line in rev20.splitlines():
        m = re.match(r"### 0x([0-9A-Fa-f]{4})", line)
        if m:
            cur = m.group(1).lower()
            continue
        m = re.search(r"write-via-helper\s+0x([0-9A-Fa-f]{4})"
                      r"(?:\s+0x([0-9A-Fa-f]{1,2}))?", line)
        if not m or cur is None:
            continue
        imm = m.group(2)
        if imm is None:
            tgt = int(m.group(1), 16)
            # helper body: 74 <imm> f0  ->  MOV A,#imm ; MOVX @DPTR,A
            if tgt + 2 < len(img) and img[tgt] == 0x74 and img[tgt + 2] == 0xF0:
                imm = "%02x" % img[tgt + 1]
            else:
                # value not statically determinable; record the write without
                # claiming an immediate rather than inventing one
                out.add((cur, "rmw", "-"))
                continue
        out.add((cur, "assign", "0x" + imm.lower().zfill(2)))
    return out


def load_rev20_writes() -> set[tuple[str, str, str]]:
    """
    Scan Rev 20's flat r2 disasm for every XDATA write. Same state
    machine as scan_rst — SDCC-emit and r2-decompile use the same 8051
    instruction mnemonics. r2 output has a `; comment` after some lines
    that we strip before matching.
    """
    text = REV20_ASM.read_text(errors="ignore")
    # r2 lines look like:
    #   0x00000345      f0             movx @dptr, a
    # normalize to something scan_rst expects (leading spaces + mnemonic
    # column). Just write to a temp .rst-shaped list and reuse scan_rst.
    # Actually scan_rst reads a Path — we'll do the scan inline instead
    # so we don't dump temp files.
    RE_DPTR     = re.compile(r"mov\s+dptr\s*,\s*#0x([0-9a-fA-F]{2,4})")
    RE_MOVX_W   = re.compile(r"movx\s+@dptr\s*,\s*a")
    RE_MOVX_R   = re.compile(r"movx\s+a\s*,\s*@dptr")
    RE_ORL_IMM  = re.compile(r"orl\s+a\s*,\s*#0x([0-9a-fA-F]{1,2})")
    RE_ANL_IMM  = re.compile(r"anl\s+a\s*,\s*#0x([0-9a-fA-F]{1,2})")
    RE_MOV_IMM  = re.compile(r"mov\s+a\s*,\s*#0x([0-9a-fA-F]{1,2})")
    RE_CLR_A    = re.compile(r"clr\s+a\b")
    RE_INC_DPL  = re.compile(r"inc\s+dpl\b")
    RE_DEC_DPL  = re.compile(r"dec\s+dpl\b")
    RE_INC_DPTR = re.compile(r"inc\s+dptr\b")
    RE_MOV_DPL  = re.compile(r"mov\s+dpl\s*,\s*#0x([0-9a-fA-F]{1,2})")
    RE_MOV_DPH  = re.compile(r"mov\s+dph\s*,\s*#0x([0-9a-fA-F]{1,2})")

    # A DPTR load is only good until control flow leaves the straight-line run.
    # Without this the scanner carried last_dptr across `ret` and branches and
    # attributed a MOVX in a completely different function to whatever address
    # was loaded last. That invented three Rev 20 writes -- 0xFF29, 0xFF2B,
    # 0xFF2C -- which are the SETUP packet buffer (bRequest / wValue / wIndex).
    # The MCU only ever READS those; the UBM writes them. A phantom Rev 20
    # write is the dangerous direction of error: it surfaces as REV20_ONLY,
    # which reads as "stock does this and we do not" and invites copying a
    # write that does not exist. CLAUDE.md states the rule this restores:
    # track straight-line only, end the run at any control-flow edge.
    # NOTE what is deliberately ABSENT: lcall / acall.
    #
    # A call does NOT end the run, because this firmware uses the callee to
    # perform the caller's write -- the caller loads DPTR, calls a shared
    # helper, and the helper issues the MOVX against the DPTR it inherited.
    # ACGDCTL is the documented instance: 0x0736 loads #0xFFE2 and LCALLs
    # 0x0E18, which writes 0x10 to it (see the long comment in streaming.c).
    # XDATA_ACCESS_MAP.md classifies that site as "write-via-helper 0x0E18".
    # Ending the run at LCALL loses it, and the gate then reports mboxfw
    # writing ACGDCTL where stock does not -- a false divergence in the
    # opposite direction from the phantoms.
    RE_FLOW = re.compile(r"\b(ret|reti|ljmp|sjmp|ajmp|jmp|jz|jnz|jc|jnc|jb|jnb|jbc|cjne|djnz)\b")

    writes: set[tuple[str, str, str]] = set()
    last_dptr = None
    saw_read = False
    op_class = None
    op_imm = None

    for raw in text.splitlines():
        # Strip r2's leading tree chars and address column; keep mnemonic tail.
        line = raw.lower()

        m = RE_DPTR.search(line)
        if m:
            last_dptr = m.group(1).zfill(4)
            saw_read = False
            op_class = None
            op_imm = None
            continue

        # End the straight-line run. Checked BEFORE the MOVX handling below so
        # a branch cannot carry a stale DPTR into another function.
        if RE_FLOW.search(line):
            last_dptr = None
            saw_read = False
            op_class = None
            op_imm = None
            continue

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
            op_imm = "0x" + m.group(1).zfill(2)
            continue
        m = RE_ANL_IMM.search(line)
        if m:
            op_class = "and_not"
            op_imm = "0x" + m.group(1).zfill(2)
            continue
        m = RE_MOV_IMM.search(line)
        if m:
            op_class = "assign"
            op_imm = "0x" + m.group(1).zfill(2)
            continue
        if RE_CLR_A.search(line):
            op_class = "assign"
            op_imm = "0x00"
            continue

        if RE_MOVX_W.search(line):
            if not last_dptr or not last_dptr.startswith("ff"):
                continue
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
            writes.add((last_dptr, pattern, imm))
            saw_read = False
            op_class = None
            op_imm = None

    return writes


# --- Justifications ------------------------------------------------ #

def load_justifications() -> set[tuple[str, str, str]]:
    """
    Parse the justifications table. Rows are markdown pipe-tables:
        | 0xffff | assign | 0xnn | source | reason |
    We accept any row whose first three cells parse as (addr, pattern, imm).
    """
    if not JUSTIFY.exists():
        return set()
    out: set[tuple[str, str, str]] = set()
    for raw in JUSTIFY.read_text().splitlines():
        line = raw.strip()
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 3:
            continue
        # Skip separator rows like |---|---|---|
        if all(set(c) <= set("-: ") for c in cells):
            continue
        addr = cells[0].lower().removeprefix("0x")
        pattern = cells[1].lower()
        imm = cells[2].lower()
        # Header row: addr won't be a hex literal
        if not re.fullmatch(r"[0-9a-f]{4}", addr):
            continue
        out.add((addr, pattern, imm))
    return out


# --- Main ---------------------------------------------------------- #

def main() -> int:
    mboxfw = load_manifest()  # what we currently emit (baseline)
    if not mboxfw:
        print("ERROR: tools/sfr_writes.allowed is empty — run"
              " tools/audit_sfr_writes.py --update first", file=sys.stderr)
        return 2

    rev20 = load_rev20_writes() | load_rev20_helper_writes()
    if not rev20:
        print("ERROR: no writes extracted from Rev 20 disasm — is"
              f" {REV20_ASM} present and non-empty?", file=sys.stderr)
        return 2

    matched   = mboxfw & rev20
    only_mbox = mboxfw - rev20
    only_rev  = rev20  - mboxfw

    # `changed` = same address in both, but different (pattern, imm).
    # Compute by grouping by addr and finding addrs present in both
    # sides with divergent tuples.
    addrs_mbox = {a: set() for a, _, _ in mboxfw}
    for a, p, i in mboxfw:
        addrs_mbox[a].add((p, i))
    addrs_rev = {a: set() for a, _, _ in rev20}
    for a, p, i in rev20:
        addrs_rev[a].add((p, i))
    changed_addrs = [
        a for a in addrs_mbox
        if a in addrs_rev and addrs_mbox[a] != addrs_rev[a]
    ]
    # Move CHANGED tuples out of the only_* buckets so they are reported once.
    changed_tuples = set()
    for a in changed_addrs:
        for p, i in addrs_mbox[a] - addrs_rev[a]:
            changed_tuples.add((a, p, i, "mbox"))
        for p, i in addrs_rev[a] - addrs_mbox[a]:
            changed_tuples.add((a, p, i, "rev"))
    only_mbox = {t for t in only_mbox if t[0] not in changed_addrs}
    only_rev  = {t for t in only_rev  if t[0] not in changed_addrs}

    justify = load_justifications()

    # Anything in only_mbox, only_rev, or changed_tuples must have an
    # entry in the justifications table.
    unjustified: list[tuple[str, str, str, str]] = []
    for t in sorted(only_mbox):
        if (t[0], t[1], t[2]) not in justify:
            unjustified.append((t[0], t[1], t[2], "MBOXFW_ONLY"))
    for t in sorted(only_rev):
        if (t[0], t[1], t[2]) not in justify:
            unjustified.append((t[0], t[1], t[2], "REV20_ONLY"))
    for a, p, i, side in sorted(changed_tuples):
        if (a, p, i) not in justify:
            unjustified.append((a, p, i, f"CHANGED_{side.upper()}"))

    # Summary
    print(f"  matches      : {len(matched)}")
    print(f"  mboxfw-only  : {len(only_mbox)}")
    print(f"  rev20-only   : {len(only_rev)}")
    print(f"  changed addrs: {len(changed_addrs)}")

    if not unjustified:
        print(f"\nREV20-DIFF PASS: all diffs justified in"
              f" {JUSTIFY.name}")
        return 0

    print(f"\nREV20-DIFF FAIL: {len(unjustified)} diff(s) without a"
          f" justification row in {JUSTIFY.name}:", file=sys.stderr)
    for addr, pattern, imm, cat in unjustified:
        print(f"  {cat:15} 0x{addr}  {pattern:8}  {imm:6}",
              file=sys.stderr)
    print(f"\nAdd a table row to {JUSTIFY.name} for each:", file=sys.stderr)
    print(f"    | 0xADDR | PATTERN | IMMEDIATE | source-file | reason |",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
