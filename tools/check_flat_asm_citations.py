#!/usr/bin/env python3
"""Reject citations that quote an address from rev20_flat.asm.

WHY THIS EXISTS
---------------
`firmware_stock/disasm/rev20_flat.asm` is a linear-sweep disassembly of
`rev20_eeprom.bin` -- the 8192-byte EEPROM image INCLUDING its 18-byte header
-- not of `rev20_firmware_code.bin`. Every address in it is therefore the true
code address + 0x12. See
`firmware_stock/decomp/FINDING_rev20_flat_asm_is_offset_by_18.md`.

Roughly a dozen citations across mboxfw/, safety_net/ and tools/ quoted
addresses from it. Most were off by 18; two were wrong in both directions
(safety_net's `USBFADR = 0` cited an address that is CPTCNF4 as-written and
GLOBCTL corrected, and a justification row claimed Rev 20 writes USBIMSK = 0xE5
when the 0xE5 it saw was CPTCNF2). All of them were fixed under #180.

They survived for months because they use the free-form shape
`rev20_flat.asm @ 0xNNNN` rather than the `Rev 20 fcn.0xXXXX @ 0xYYYY` shape
that `check_citation_targets.py` validates against the images. An unvalidated
citation format is an unvalidated claim, and this project has already had a
real GLOBCTL write dismissed as a "scanner artifact" on the strength of
grepping that listing.

So: quoting an address from this listing is now an error. Cite the true
address, in a form the citation gate can check.

ESCAPE HATCH
------------
A line may reference the listing WITH an address if it also carries the marker
`flat-asm-ok` -- or names task #180, which is how the existing withdrawals and
corrections are written. That covers documentation OF the defect, which
necessarily quotes the wrong addresses in order to retire them.

Bare mentions with no address (e.g. "do not use rev20_flat.asm") are always
fine; only an address next to the filename is a claim.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Files whose entire purpose is to describe the defect.
EXEMPT_FILES = {
    "tools/check_flat_asm_citations.py",
    "tools/diff_vs_rev20.py",
    "tools/diff_vs_rev20_safety_net.py",
    "firmware_stock/decomp/FINDING_rev20_flat_asm_is_offset_by_18.md",
    "firmware_stock/decomp/FINDING_globctl_bit1_missed.md",
    "CLAUDE.md",
}

SCAN_DIRS = ("mboxfw", "safety_net", "tools", "firmware_stock/decomp")
SCAN_EXT = {".c", ".h", ".py", ".md", ".sh"}

# filename, then an address within a short window -- that pairing is the claim
CITE = re.compile(r"rev2[02]_flat\.asm[^0-9A-Za-z\n]{0,24}(?:line[s]?\s*)?"
                  r"(0x[0-9A-Fa-f]{3,4}|\d{3,5}\b)")
MARKER = re.compile(r"flat-asm-ok|#180")


def main() -> int:
    bad = []
    for d in SCAN_DIRS:
        root = REPO / d
        if not root.exists():
            continue
        for p in sorted(root.rglob("*")):
            if p.suffix not in SCAN_EXT or not p.is_file():
                continue
            rel = p.relative_to(REPO).as_posix()
            if rel in EXEMPT_FILES:
                continue
            try:
                text = p.read_text(errors="ignore")
            except OSError:
                continue
            for n, line in enumerate(text.splitlines(), 1):
                if CITE.search(line) and not MARKER.search(line):
                    bad.append((rel, n, line.strip()[:110]))

    if bad:
        print("FLAT-ASM-CITE FAIL: %d citation(s) quote an address from "
              "rev20_flat.asm:\n" % len(bad))
        for rel, n, line in bad:
            print("  %s:%d\n      %s" % (rel, n, line))
        print("\nThat listing disassembles rev20_eeprom.bin (with its 18-byte")
        print("header), so its addresses are the true code address + 0x12.")
        print("Re-derive against rev20_firmware_code.bin / rev22 and cite as")
        print("    Rev 20 fcn.0xXXXX @ 0xYYYY, Rev 22 fcn.0xAAAA @ 0xBBBB")
        print("which check_citation_targets.py actually verifies.")
        print("See FINDING_rev20_flat_asm_is_offset_by_18.md.")
        print("To annotate a KNOWN-WRONG citation while retiring it, put")
        print("'flat-asm-ok' on the line.")
        return 1

    print("FLAT-ASM-CITE PASS: no address is quoted from rev20_flat.asm")
    return 0


if __name__ == "__main__":
    sys.exit(main())
