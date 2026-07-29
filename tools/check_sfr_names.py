#!/usr/bin/env python3
"""
Cross-consistency gate for SFR register names.

The existing gates check that every SFR write has a citation
(check_sfr_citations.py) and that every diff against Rev 20 has a
justification row (diff_vs_rev20.py). Neither checks that the rows agree with
each other, or with the datasheet.

That gap is not theoretical. Three separate bugs in this project came from a
register being given a name that belonged to a different address:

  * 0xFFE1 called DMACTL1 when it is ACGCTL, so `|= 0xC0` was believed to arm
    the DMA. DMAEN on 0xFFE8/0xFFEE had never been set at all, and telemetry
    read back the wrong register and reported success.
  * 0xFFE5-7 called DMASRC0_* when they are ACG1FRQ2/1/0.
  * 0xFFD4 called CPTCTL when it is CPTRXCNF4. CPTCTL is 0xFFDC. One
    justification row offered "Rev-20-empirical usage" as authority for the
    wrong name; the same file elsewhere had it right, and nothing noticed.

Checks
------
1. NAME/ADDRESS AGREEMENT. On any line carrying at least one SFR address and
   at least one canonical register name, every name present must have its own
   canonical address present too. A line may legitimately discuss several
   registers, and may legitimately say "X is not 0xNNNN" -- as long as it also
   states X's real address, the line is self-consistent.
2. RETIRED NAMES. Names known to have been invented and withdrawn are flagged
   wherever they appear, unless the line is explicitly retiring them.
3. EDITING ARTIFACTS. Leftovers from interrupted edits ("-OLD" rows, spliced
   "Original text follows:", conflict markers) in the justification files.

Two structural exemptions, both principled rather than convenient:

  * A markdown table whose header declares a wrong-name column ("Invented
    name", "WRONG", "old name") is documenting the errors on purpose.
  * Fenced code blocks in markdown are quotations -- evidence being discussed,
    not a claim the document is making. Live code is still checked, because
    regs.h and mboxfw/src are scanned outside any fence.

Suppress an individual line with the marker `sfr-names-ok`.

Exit 0 = clean, 1 = violations.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CANON_HDR = os.path.join(
    ROOT, "reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h")

# STRICT: a register name on these lines is a load-bearing claim that a gate
# or the build depends on, so every name must carry its own address. These are
# the files where the historical bugs actually lived.
STRICT = [
    "tools/rev20_diff_justifications.md",
    "tools/rev20_diff_justifications_safety_net.md",
    "mboxfw/include/regs.h",
]
# ADVISORY: reverse-engineering narrative. Cross-referencing another register
# by name without repeating its address is normal and readable prose here, so
# only the retired-name and artifact checks apply.
ADVISORY = [
    "firmware_stock/disasm/NOTES.md",
    "firmware_stock/disasm/rev20_ANNOTATED.md",
    "firmware_stock/disasm/rev22_ANNOTATED.md",
    "firmware_stock/decomp/README.md",
]
ADVISORY_GLOBS = [
    ("mboxfw/src", ".c"),
    ("firmware_stock/decomp/cand", ".c"),
    ("firmware_stock/decomp/cand", ".h"),
]

# Names invented during this project and since withdrawn. Flagged on sight so
# they cannot creep back in through a copied comment.
RETIRED = {
    "DMACTL2": "0xFFE2 is ACGDCTL/ACG1DCTL, not a DMA register",
    "DMASRC0_LO": "0xFFE5-7 are ACG1FRQ2/1/0",
    "DMASRC0_MID": "0xFFE5-7 are ACG1FRQ2/1/0",
    "DMASRC0_HI": "0xFFE5-7 are ACG1FRQ2/1/0",
    "DMASRC2_LO": "0xFFF7-9 are ACG2FRQ2/1/0",
    "DMASRC2_MID": "0xFFF7-9 are ACG2FRQ2/1/0",
    "DMASRC2_HI": "0xFFF7-9 are ACG2FRQ2/1/0",
    # Invented during the Rev 22 decompilation for 0xFF9B/0xFF9F. TI calls them
    # OEPDCNTX2/OEPDCNTY2; "OEPBCT" appears nowhere in reference/tas1020a/. The
    # name/address check did not catch these because cand/ is advisory, so they
    # are listed here instead, where a bare mention is enough to fail.
    "OEPBCTX2": "0xFF9B is OEPDCNTX2",
    "OEPBCTY2": "0xFF9F is OEPDCNTY2",
    "IEPBCTX1": "0xFF63 is IEPDCNTX1",
    "IEPBCTY1": "0xFF67 is IEPDCNTY1",
}
# A line that is *narrating* a past naming error legitimately contains the bad
# name next to the address it never belonged to. Recognising that is what keeps
# the historical record in regs.h and the justification files from tripping the
# gate that exists because of it.
HISTORICAL = ("invented", "retired", "stale", "withdrawn", "corrected",
              "formerly", "previously", "was named", "was called", "used to",
              "no longer", "old name", "those names", "not a dma", "no such",
              "renamed", "misnamed", "never writes", "wrongly")

ARTIFACTS = [
    (re.compile(r"\|\s*0x[0-9a-fA-F]{4}-OLD\s*\|"),
     "leftover '-OLD' table row from an interrupted edit"),
    (re.compile(r"Original text follows:"),
     "spliced 'Original text follows:' -- two conclusions in one row"),
    (re.compile(r"^(<{7}|={7}|>{7})"), "merge conflict marker"),
]
ARTIFACT_FILES = ("rev20_diff_justifications.md",
                  "rev20_diff_justifications_safety_net.md")

SUPPRESS = "sfr-names-ok"

RE_ADDR = re.compile(r"0x(F{2}[0-9a-fA-F]{2})\b", re.I)


def load_canonical():
    """name -> address, from TI's header."""
    if not os.path.exists(CANON_HDR):
        sys.exit(f"missing canonical header: {CANON_HDR}")
    canon = {}
    pat = re.compile(r"#define\s+(\w+)\s+stc_sfr\(0x([0-9A-Fa-f]{4})\)")
    for line in open(CANON_HDR):
        m = pat.search(line)
        if m:
            canon[m.group(1).upper()] = int(m.group(2), 16)
    if not canon:
        sys.exit(f"parsed no register names from {CANON_HDR}")
    return canon


def files_to_scan():
    """-> [(path, strict?)]"""
    out = []
    for rel in STRICT:
        p = os.path.join(ROOT, rel)
        if os.path.exists(p):
            out.append((p, True))
    for rel in ADVISORY:
        p = os.path.join(ROOT, rel)
        if os.path.exists(p):
            out.append((p, False))
    for d, ext in ADVISORY_GLOBS:
        dp = os.path.join(ROOT, d)
        if not os.path.isdir(dp):
            continue
        for fn in sorted(os.listdir(dp)):
            if fn.endswith(ext):
                out.append((os.path.join(dp, fn), False))
    return out


def main():
    canon = load_canonical()
    name_re = re.compile(r"\b(" + "|".join(sorted(canon, key=len, reverse=True))
                         + r")\b")
    retired_re = re.compile(r"\b(" + "|".join(RETIRED) + r")\b")

    violations = []
    scanned = files_to_scan()
    for path, strict in scanned:
        rel = os.path.relpath(path, ROOT)
        is_justif = os.path.basename(path) in ARTIFACT_FILES
        is_md = path.endswith(".md")
        in_fence = False
        exempt_table = False
        all_lines = open(path, errors="replace").read().split("\n")
        for n, line in enumerate(all_lines, 1):
            low = line.lower()
            # A comment block narrating a past naming error often puts the
            # signal word a line or two above the bad name, so read a small
            # window rather than a single line.
            window = " ".join(all_lines[max(0, n - 4):n + 1]).lower()

            if is_md:
                if line.lstrip().startswith("```"):
                    in_fence = not in_fence
                    continue
                if in_fence:
                    continue
                # A table header that declares a wrong-name column exempts the
                # rows beneath it; the table ends at the next blank line.
                if line.startswith("|"):
                    if any(k in low for k in
                           ("invented name", "wrong, do not use", "old name")):
                        exempt_table = True
                elif not line.strip():
                    exempt_table = False
                if exempt_table:
                    continue

            if SUPPRESS in line:
                continue

            # -- check 3: editing artifacts -----------------------------
            if is_justif:
                for pat, why in ARTIFACTS:
                    if pat.search(line):
                        violations.append((rel, n, why, line.strip()[:100]))

            # -- check 2: retired names ---------------------------------
            historical = any(c in window for c in HISTORICAL)

            for m in retired_re.finditer(line):
                if historical:
                    continue
                violations.append(
                    (rel, n, f"retired name {m.group(1)!r} "
                             f"({RETIRED[m.group(1)]})", line.strip()[:100]))

            # -- check 1: name/address agreement (STRICT files only) ----
            if not strict or historical:
                continue
            addrs = {int(a, 16) for a in RE_ADDR.findall(line)}
            if not addrs:
                continue
            for m in name_re.finditer(line):
                nm = m.group(1).upper()
                want = canon[nm]
                if want not in addrs:
                    violations.append(
                        (rel, n,
                         f"{nm} paired with {'/'.join(f'0x{a:04X}' for a in sorted(addrs))} "
                         f"but {nm} is 0x{want:04X}",
                         line.strip()[:100]))

    if violations:
        print(f"SFR-NAME FAIL: {len(violations)} inconsistency(ies)\n")
        for rel, n, why, ctx in violations:
            print(f"  {rel}:{n}")
            print(f"      {why}")
            print(f"      | {ctx}")
        print("\nA register name next to an address is a factual claim. If the")
        print("line legitimately mentions several registers, include each name's")
        print("own address on that line. To suppress, add the marker "
              f"'{SUPPRESS}'.")
        return 1

    ns = sum(1 for _, st in scanned if st)
    print(f"SFR-NAME PASS: {len(canon)} canonical names, "
          f"{ns} strict + {len(scanned)-ns} advisory files, no inconsistencies")
    return 0


if __name__ == "__main__":
    sys.exit(main())
