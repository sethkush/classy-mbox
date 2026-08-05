#!/usr/bin/env python3
"""
safety_net twin of tools/diff_vs_rev20.py.

Diffs safety_net's SFR writes against Rev 20's. Same category
semantics as the mboxfw diff:

  SAFETYNET_ONLY : we write it, Rev 20 doesn't. Needs a justification
                   (every "novel" write is where a new bug can live).
  REV20_ONLY     : Rev 20 writes it, safety_net doesn't. For safety_net
                   this is EXPECTED for most audio-path / streaming
                   registers — safety_net is a bare-bones recovery
                   firmware, not full audio. Each still needs a row
                   explaining why we're OK skipping it.
  CHANGED        : same addr in both, different (pattern, immediate).

Justifications live in tools/rev20_diff_justifications_safety_net.md.
Same table format as the mboxfw twin. Failing the gate on missing
rows is the whole point — without it, a deviation from working
Rev 20 silently sneaks into a recovery firmware whose only job is
to not brick.

The baseline is derived by scanning safety_net/build/*.rst directly,
NOT from a `.allowed` manifest. safety_net is small enough (~1 KB
of code) that a live scan is fast and there's no need for a
separate audit-manifest step.
"""

import re
import sys
from pathlib import Path


REPO = Path(__file__).parent.parent
SAFETY_BUILD = REPO / "safety_net" / "build"
REV20_ASM = REPO / "firmware_stock" / "disasm" / "rev20_flat.asm"
JUSTIFY = Path(__file__).parent / "rev20_diff_justifications_safety_net.md"

# Reuse scan_rst — same SDCC codegen conventions in both firmwares.
# The Rev 20 side is shared with diff_vs_rev20.py rather than reimplemented;
# the stock image is a fixed input, not a per-firmware one.
#
# #180: BOTH halves of that import matter, and one of them was missing.
#
# load_rev20_writes() is a linear scan of rev20_flat.asm, which disassembles
# rev20_eeprom.bin INCLUDING its 18-byte header, so its addresses are the true
# code address + 0x12 (FINDING_rev20_flat_asm_is_offset_by_18.md). This gate
# consumes only (SFR address, pattern, immediate) and never an instruction
# address, so the offset does not reach its output — same as diff_vs_rev20.py.
# Because the function is imported rather than copied, this gate also picked up
# the 2026-08-05 fix that stopped DPTR tracking leaking across control-flow
# edges, which had invented three Rev 20 writes into the setup packet buffer.
# Sharing the implementation is what made that fix free here.
#
# load_rev20_helper_writes() was NOT being imported, and that is a real gap:
# stock performs some writes from a callee against the caller's DPTR (the
# caller loads DPTR, LCALLs a shared helper, the helper issues the MOVX). A
# linear listing cannot see those at all. Without them the Rev 20 baseline is
# short by five (addr, pattern, immediate) tuples across four addresses —
# ACGDCTL, CPTRXCNF4, CPTCNF3 and ACG2FRQ0 — every one of which would be
# reported as a divergence that does not exist, in the direction that reads as
# "stock does this and safety_net does not". Ported here so both gates see the
# same stock image.
sys.path.insert(0, str(Path(__file__).parent))
from audit_sfr_writes import scan_rst  # noqa: E402
from diff_vs_rev20 import (  # noqa: E402
    load_rev20_writes,
    load_rev20_helper_writes,
)


def load_safety_net_writes() -> set[tuple[str, str, str]]:
    """
    Scan every .rst in safety_net/build and dedupe by (addr, pattern,
    immediate) — the same tuple the manifest / justifications key on.
    """
    writes: set[tuple[str, str, str]] = set()
    for rst in sorted(SAFETY_BUILD.glob("*.rst")):
        for w in scan_rst(rst):
            writes.add((w.addr, w.pattern, w.immediate))
    return writes


def load_justifications() -> set[tuple[str, str, str]]:
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
        if all(set(c) <= set("-: ") for c in cells):
            continue
        addr = cells[0].lower().removeprefix("0x")
        pattern = cells[1].lower()
        imm = cells[2].lower()
        if not re.fullmatch(r"[0-9a-f]{4}", addr):
            continue
        out.add((addr, pattern, imm))
    return out


def main() -> int:
    safety = load_safety_net_writes()
    if not safety:
        print(f"ERROR: no writes scanned from {SAFETY_BUILD} — run"
              f" `make -C safety_net` first?", file=sys.stderr)
        return 2

    rev20 = load_rev20_writes() | load_rev20_helper_writes()
    if not rev20:
        print(f"ERROR: no writes extracted from Rev 20 disasm — is"
              f" {REV20_ASM} present and non-empty?", file=sys.stderr)
        return 2

    matched   = safety & rev20
    only_sn   = safety - rev20
    only_rev  = rev20  - safety

    addrs_sn = {a: set() for a, _, _ in safety}
    for a, p, i in safety:
        addrs_sn[a].add((p, i))
    addrs_rev = {a: set() for a, _, _ in rev20}
    for a, p, i in rev20:
        addrs_rev[a].add((p, i))
    changed_addrs = [
        a for a in addrs_sn
        if a in addrs_rev and addrs_sn[a] != addrs_rev[a]
    ]
    changed_tuples: set[tuple[str, str, str, str]] = set()
    for a in changed_addrs:
        for p, i in addrs_sn[a] - addrs_rev[a]:
            changed_tuples.add((a, p, i, "sn"))
        for p, i in addrs_rev[a] - addrs_sn[a]:
            changed_tuples.add((a, p, i, "rev"))
    only_sn  = {t for t in only_sn  if t[0] not in changed_addrs}
    only_rev = {t for t in only_rev if t[0] not in changed_addrs}

    justify = load_justifications()

    unjustified: list[tuple[str, str, str, str]] = []
    for t in sorted(only_sn):
        if (t[0], t[1], t[2]) not in justify:
            unjustified.append((t[0], t[1], t[2], "SAFETYNET_ONLY"))
    for t in sorted(only_rev):
        if (t[0], t[1], t[2]) not in justify:
            unjustified.append((t[0], t[1], t[2], "REV20_ONLY"))
    for a, p, i, side in sorted(changed_tuples):
        if (a, p, i) not in justify:
            tag = f"CHANGED_{'SN' if side == 'sn' else 'REV'}"
            unjustified.append((a, p, i, tag))

    print(f"  matches      : {len(matched)}")
    print(f"  sn-only      : {len(only_sn)}")
    print(f"  rev20-only   : {len(only_rev)}")
    print(f"  changed addrs: {len(changed_addrs)}")

    if not unjustified:
        print(f"\nREV20-DIFF (safety_net) PASS: all diffs justified in"
              f" {JUSTIFY.name}")
        return 0

    print(f"\nREV20-DIFF (safety_net) FAIL: {len(unjustified)} diff(s)"
          f" without a justification row in {JUSTIFY.name}:",
          file=sys.stderr)
    for addr, pattern, imm, cat in unjustified:
        print(f"  {cat:18} 0x{addr}  {pattern:8}  {imm:6}",
              file=sys.stderr)
    print(f"\nAdd a table row to {JUSTIFY.name} for each:", file=sys.stderr)
    print(f"    | 0xADDR | PATTERN | IMMEDIATE | source-file | reason |",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
