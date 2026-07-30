#!/usr/bin/env python3
"""
Verify that a Rev-20 citation points at code that touches the register it cites.

tools/check_sfr_citations.py checks that every SFR write in mboxfw CARRIES a
citation. It never checks that the citation POINTS anywhere. That gap let a
whole block of streaming.c drift: `IEPCNF1 = 0xC5` was annotated
`Rev 20 fcn.0x0728 @ 0x07F6`, and 0x07F6 is `f5 2f` -- a MOV to IRAM, not an
endpoint register. The real write is at 0x07E4. `OEPDCNTX2 = 0` was cited at
0x07EE, which is a SETB of a panel bit.

A wrong citation is worse than no citation. It reads as verified, so nobody
re-derives it, and the next person to trust it inherits the error. Two of the
seven this tool first found were Rev 22 addresses written as Rev 20, and one
pointed into the ?C_INITSEG data table.

The check: for a line of the form

    REG = value;   /* Rev 20 fcn.0xNNNN @ 0xAAAA */

confirm that address AAAA in the Rev 20 image actually accesses REG -- either
through `MOV DPTR,#<addr of REG>` within a short window, or, for the SFRs that
are directly addressable, a direct read/write of that SFR byte.

Citations on prose lines (no `REG =` on the line) are checked more loosely:
the cited address must at least be inside a function, not in a data table.

    python3 tools/check_citation_targets.py

Exit 0 = every citation lands, 1 = at least one does not.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware_stock")
REGS_H = os.path.join(ROOT, "mboxfw", "include", "regs.h")

# Both stock images. This gate checked ONLY Rev 20 until 2026-07-29, which was
# fine while every ported routine came from Rev 20 -- and stopped being fine the
# moment streaming.c gained five `Rev 22 fcn.0x0D58 @ ...` citations for the
# playback SOF watchdog, a routine that exists only in Rev 22. Those citations
# were unverified by any gate, which is exactly the condition this tool was
# written to eliminate. Note the irony recorded in the docstring above: two of
# the seven errors it first caught were Rev 22 addresses mislabelled as Rev 20,
# so the tool already knew the two images were easy to confuse.
IMAGES = {
    "20": (os.path.join(FW, "rev20_firmware_code.bin"),
           os.path.join(FW, "disasm", "rev20_ghidra.txt")),
    "22": (os.path.join(FW, "rev22_firmware_code.bin"),
           os.path.join(FW, "disasm", "rev22_ghidra.txt")),
}

CITE = re.compile(r"Rev\s*(20|22)\s+fcn\.0x([0-9A-Fa-f]{4})\s*@\s*0x([0-9A-Fa-f]{4})")
ASSIGN = re.compile(r"\b([A-Z][A-Z0-9_]{2,})\s*(?:=|\|=|&=|\^=)")
# How far from the cited address to look for the DPTR load that selects the
# register. Keil emits `MOV DPTR,#reg` then the access within a few bytes, and
# a citation may reasonably anchor on either.
WINDOW = 8
SUPPRESS = "citation-target-ok"


def load_regs():
    """-> {NAME: addr} for XDATA-mapped SFRs declared in mboxfw's regs.h."""
    out = {}
    if not os.path.exists(REGS_H):
        return out
    for line in open(REGS_H):
        m = re.match(r"#define\s+(\w+)\s+XDATA\(0x([0-9A-Fa-f]{4})\)", line.strip())
        if m:
            out[m.group(1)] = int(m.group(2), 16)
    return out


def code_extents(ghidra):
    """Address ranges the Ghidra listing decodes as instructions."""
    ok = set()
    insn = re.compile(r"^CODE:([0-9a-f]{4})\s+([0-9a-f]+)\s+[A-Z]")
    for line in open(ghidra):
        m = insn.match(line)
        if m:
            a, n = int(m.group(1), 16), len(m.group(2)) // 2
            ok.update(range(a, a + n))
    return ok


def main():
    imgs = {}
    for rev, (binp, ghp) in IMAGES.items():
        if not os.path.exists(binp):
            sys.exit(f"missing {binp}")
        imgs[rev] = (open(binp, "rb").read(), code_extents(ghp))
    regs = load_regs()

    bad, checked = [], 0
    per_rev = {r: 0 for r in IMAGES}
    for sub in ("mboxfw/src", "mboxfw/include", "safety_net/src"):
        base = os.path.join(ROOT, sub)
        if not os.path.isdir(base):
            continue
        for fn in sorted(os.listdir(base)):
            if not fn.endswith((".c", ".h")):
                continue
            path = os.path.join(base, fn)
            rel = os.path.relpath(path, ROOT)
            for n, line in enumerate(open(path, errors="replace"), 1):
                if SUPPRESS in line:
                    continue
                m = CITE.search(line)
                if not m:
                    continue
                rev = m.group(1)
                at = int(m.group(3), 16)
                d, code = imgs[rev]
                checked += 1
                per_rev[rev] += 1

                if at >= len(d) or at not in code:
                    bad.append((rel, n, f"Rev {rev} 0x{at:04X} is not "
                                        f"instruction bytes (data table or "
                                        f"erase fill)",
                                line.strip()[:90]))
                    continue

                am = ASSIGN.search(line)
                name = am.group(1) if am else None
                if not name or name not in regs:
                    continue          # prose citation: extent check was enough

                want = regs[name]
                lo, hi = max(0, at - WINDOW), min(len(d), at + WINDOW + 3)
                sel = bytes([0x90, (want >> 8) & 0xFF, want & 0xFF])
                if sel in d[lo:hi]:
                    continue
                # Directly addressable SFRs (0x80..0xFF) reached without DPTR.
                if want >= 0xFF80 and any(
                        d[i] in (0xF5, 0xE5, 0x75) and d[i + 1] == (want & 0xFF)
                        for i in range(lo, hi - 1)):
                    continue
                # Last resort: ask the access map. A register can be reached
                # without any `MOV DPTR,#imm` nearby -- stock writes GLOBCTL at
                # Rev 20 0x08FE with DPTR arrived at by INC DPTR from the
                # MEMCFG write 27 instructions earlier. Byte-matching the
                # immediate load cannot see that, and rejecting such a citation
                # is how a REAL stock write came to be recorded as a scanner
                # artifact in rev20_diff_justifications.md.
                sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
                from xdata_access_map import analyse      # noqa: E402
                if any(lo <= site <= hi
                       for site, _k, _v in analyse(f"rev{rev}").get(want, ())):
                    continue

                where = [f"0x{i:04X}" for i in range(len(d) - 2)
                         if d[i:i + 3] == sel][:4]
                bad.append((rel, n,
                            f"{name} (0x{want:04X}) is not accessed at "
                            f"Rev {rev} 0x{at:04X}; in that image it is "
                            f"accessed at "
                            + (", ".join(where) if where else "no site found"),
                            line.strip()[:90]))

    if bad:
        print(f"CITATION-TARGET FAIL: {len(bad)} citation(s) do not land\n")
        for rel, n, why, ctx in bad:
            print(f"  {rel}:{n}\n      {why}\n      | {ctx}")
        print("\nA citation that points at the wrong address reads as verified and")
        print("is worse than no citation. Fix the address, or drop the claim.")
        return 1
    print(f"CITATION-TARGET PASS: {checked} citation(s) "
          f"(Rev 20: {per_rev['20']}, Rev 22: {per_rev['22']}) land on code "
          f"that touches the register they name")
    return 0


if __name__ == "__main__":
    sys.exit(main())
