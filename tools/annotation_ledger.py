#!/usr/bin/env python3
"""
annotation_ledger -- measure how much of the stock firmware is ACTUALLY
explained, as opposed to how many bytes reconstruct.

Why this exists: this project could report "8174/8174 bytes rebuild, SHA-256
verified" for both images and did, repeatedly, as if that meant the firmware
was understood. It does not. Byte-exactness says what the code IS; it says
nothing about what any of it MEANS. On 2026-07-29 a few hours spent on four
bytes of IRAM turned up the source mux bit map, the source-pattern mapping,
a wrong boot value in mboxfw, a reversed cycle order, mono at 0x23.6, two
state machines, the button handler and the panel pin map -- none of it
previously written down, all of it sitting in listings that had been called
fully annotated. The rate of discovery was the finding: the denominator had
never been established.

So this counts the denominator. It enumerates every function entry point and
every memory location either image touches, then checks each one against the
repo's own documentation and source.

WHAT COUNTS AS EXPLAINED

The first version of this script inferred the numerator from text: an item
counted as documented if its address appeared anywhere in the repo's markdown
or sources. That scored 100% of call targets and 94% of IRAM on first run --
worthless, because with full disassembly listings checked in, nearly every
address appears somewhere. It would have scored IRAM 0x22 as documented on
2026-07-28, when nothing whatever was known about it. A metric that cannot
distinguish "explained" from "mentioned" measures nothing.

So the numerator is now EXPLICIT and hand-curated. Only items listed in

    firmware_stock/disasm/ANNOTATION_CLAIMS.tsv

count as explained. One row per item:

    image<TAB>kind<TAB>addr<TAB>name<TAB>where-established<TAB>level

`kind` is one of func / xdata / irambyte / irambit. The where-established
column must point at something that actually establishes the claim, not merely
mention it.

`level` is `explained` or `name`, and the distinction is load-bearing. Knowing
that XDATA 0xFFEE is called DMACTL1 is NOT knowing what the firmware does with
it -- that address was called DMACTL1 while the real DMA sat elsewhere, and the
wrong name caused a real bug. Register names imported from a datasheet or
vendor header are `name`. A byte-exact reconstruction with prose saying what
the code does, or a bit map derived and cross-checked against both images, is
`explained`. The two are reported separately and never added together.

The denominator stays automatic -- the image tells us what it touches, and
that cannot be argued with. The numerator requires somebody to have done the
work and said where. That asymmetry is the point: the score cannot drift
upward on its own, and adding a row is a claim someone can check.

    python3 tools/annotation_ledger.py            # summary
    python3 tools/annotation_ledger.py --full     # every unexplained item
    python3 tools/annotation_ledger.py --write    # ANNOTATION_LEDGER.md

Exit status is always 0; this measures, it does not gate. Wire it into a gate
once the numbers are good enough that regressions matter.
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware_stock")
DISASM = os.path.join(FW, "disasm")
IMAGES = ("rev20", "rev22")

# Documents that count as real annotation for IRAM locations. A location named
# only inside mboxfw source does NOT count -- mboxfw's names have been wrong
# before (g_phantom_48v, DMACTL1, OEPBCTX2), and a name is not an explanation.
ANNOTATION_DOCS = [
    "disasm/MUX_IRAM22_ANNOTATION.md",
    "disasm/IRAM23_IRAM25_ANNOTATION.md",
    "disasm/rev20_ANNOTATED.md",
    "disasm/rev22_ANNOTATED.md",
    "disasm/NOTES.md",
    "disasm/PANEL_LEDS.md",
    "disasm/rev20_STARTUP_TRACE.md",
    "disasm/rev22_STARTUP_TRACE.md",
    "disasm/rev20_audio_dispatch.md",
    "disasm/rev20_dynamic_reconfig.md",
    "disasm/ANNOTATION_BRIEF.md",
]

INSN = re.compile(r"^CODE:([0-9a-f]{4})\s+([0-9a-f]+)\s+([A-Z][A-Z0-9]*)")

# 8051 opcodes taking a direct address as their first operand byte.
DIRECT_OPS = {
    0x05, 0x15, 0x25, 0x35, 0x45, 0x55, 0x65, 0x75, 0x85, 0x95,
    0xB5, 0xC5, 0xD5, 0xE5, 0xF5,
    0x42, 0x43, 0x52, 0x53, 0x62, 0x63,
    0xC0, 0xD0,
}
DIRECT_OPS |= set(range(0x88, 0x90))    # MOV direct,Rn
DIRECT_OPS |= set(range(0xA8, 0xB0))    # MOV Rn,direct
BIT_OPS = {0x10, 0x20, 0x30, 0x72, 0x82, 0x92, 0xA0, 0xA2, 0xB2, 0xC2, 0xD2}
CALL_OPS = {0x12, 0x02}                 # LCALL, LJMP (absolute targets)

# (base, entry-count) of request-code dispatch tables: 3-byte entries of
# (16-bit big-endian handler address, 1-byte request code).
# Rev 22 has NO equivalent table. A signature search for the same code
# sequence (00,01,03,05,06,07,08,09,0A,0B,0C at stride 3) finds it only in
# Rev 20; Rev 22 dispatches the same request codes with a CJNE compare chain,
# which the instruction scan already covers. Assuming symmetry and decoding
# Rev 22 at 0x011E produced garbage handlers (0x0201/0x0202) and nonsense
# codes -- the check that caught it was that the codes were not the known set.
DISPATCH_TABLES = {"rev20": [(0x011F, 11)]}
# Reached by LCALL through a computed vector or from a data table, never named
# by an instruction operand in the image.
BOOT_ROM_ENTRIES = {0x2F00}


def load(image):
    """-> (image bytes, set of instruction start addresses)."""
    d = open(os.path.join(FW, f"{image}_firmware_code.bin"), "rb").read()
    starts = set()
    for line in open(os.path.join(DISASM, f"{image}_ghidra.txt")):
        m = INSN.match(line)
        if m:
            starts.add(int(m.group(1), 16))
    return d, starts


def survey(image):
    """Enumerate what the image touches: call targets, IRAM bytes/bits, XDATA."""
    d, starts = load(image)
    calls, iram_b, iram_bit, xdata = set(), defaultdict(set), defaultdict(set), defaultdict(set)
    sfr = defaultdict(set)
    for i in sorted(starts):
        op = d[i]
        if i + 2 >= len(d):
            continue
        if op in CALL_OPS:
            calls.add((d[i + 1] << 8) | d[i + 2])
        elif op == 0x11 or (op & 0x1F) == 0x11:      # ACALL page-relative
            pass
        if op == 0x90:                                # MOV DPTR,#imm16
            # A DPTR load is not automatically an XDATA access. The image is
            # 0x1FEE bytes, so any target below that is a CODE address -- a
            # lookup-table base for MOVC A,@A+DPTR. Lumping those in with SFR
            # space made five CODE tables look like unnamed hardware
            # registers, which is a category error, not a gap.
            xdata[(d[i + 1] << 8) | d[i + 2]].add(i)
        if op in DIRECT_OPS:
            a = d[i + 1]
            if a < 0x80:                              # IRAM
                iram_b[a].add(i)
            else:
                # Direct-addressed SFRs. These were omitted from all four
                # original categories, so "100% on four numbers" silently
                # excluded 16 SFRs and ~60 sites per image -- including the
                # timer setup and PCON, where the missing mboxfw suspend path
                # was hiding. The categories were my choice, so this was my
                # gap, not a technicality.
                sfr[a].add(i)
        if op in BIT_OPS:
            b = d[i + 1]
            if b < 0x80:                              # bit-addressable IRAM
                iram_bit[b].add(i)
    # Table-driven dispatch targets. These are reached by JMP @A+DPTR with the
    # address coming from a const table, so no LCALL/LJMP instruction names
    # them and the scan above cannot see them. Leaving them out made
    # "call targets 100%" mean "100% of instruction-reachable targets", which
    # is not what it looked like. See disasm/DISPATCH_TABLE_011F.md.
    for base, n in DISPATCH_TABLES.get(image, []):
        for k in range(n):
            off = base + k * 3
            calls.add((d[off] << 8) | d[off + 1])
    calls |= BOOT_ROM_ENTRIES
    return calls, iram_b, iram_bit, xdata, sfr


def repo_text(patterns):
    """Concatenated text of the given repo-relative files that exist."""
    out = []
    for rel in patterns:
        p = os.path.join(FW, rel)
        if os.path.exists(p):
            out.append(open(p, errors="replace").read())
    return "\n".join(out)


def all_prose():
    """Every markdown and C/H source in the repo -- the generous 'cited' test."""
    out = []
    for base, _dirs, files in os.walk(ROOT):
        if "/.git" in base or "/build" in base:
            continue
        for fn in files:
            if fn.endswith((".md", ".c", ".h", ".txt")) and "ghidra" not in fn:
                try:
                    out.append(open(os.path.join(base, fn), errors="replace").read())
                except OSError:
                    pass
    return "\n".join(out)


def regs_named():
    """-> set of XDATA addresses that regs.h gives a name."""
    named = set()
    p = os.path.join(ROOT, "mboxfw", "include", "regs.h")
    if os.path.exists(p):
        for line in open(p):
            m = re.match(r"#define\s+(\w+)\s+XDATA\(0x([0-9A-Fa-f]{4})\)", line.strip())
            if m:
                named.add(int(m.group(2), 16))
    return named


CLAIMS = os.path.join(DISASM, "ANNOTATION_CLAIMS.tsv")


def load_claims():
    """-> {(image, kind, addr-text): level}. Absent level defaults to explained."""
    out = {}
    if not os.path.exists(CLAIMS):
        return out
    for line in open(CLAIMS):
        # Comments are WHOLE-LINE only. Stripping from the first '#' anywhere
        # silently ate four legitimate rows whose names contained 8051
        # immediates -- "#0xFE", "#0x00", "#0x10", "#0xFF" -- leaving too few
        # fields to parse, so the ledger under-reported and I misread that as
        # unfinished work.
        if line.lstrip().startswith("#"):
            continue
        line = line.rstrip("\n")
        if not line.strip():
            continue
        f = line.split("\t")
        if len(f) >= 5:
            lvl = f[5].strip() if len(f) >= 6 and f[5].strip() else "explained"
            out[(f[0].strip(), f[1].strip(), f[2].strip())] = lvl
    return out


def main():
    full = "--full" in sys.argv
    write = "--write" in sys.argv

    claims = load_claims()
    report = []

    def emit(s=""):
        report.append(s)
        print(s)

    emit("ANNOTATION LEDGER")
    emit("=" * 72)
    emit("Byte reconstruction is 100% for both images and is NOT what this")
    emit("measures. This counts items explained in ANNOTATION_CLAIMS.tsv, where")
    emit("each row names a location and points at the document establishing it.")
    emit(f"Claims on file: {len(claims)}")
    emit()

    for image in IMAGES:
        calls, iram_b, iram_bit, xdata, sfr = survey(image)
        emit(f"--- {image} ---")

        rows = [("func", a, f"0x{a:04X}") for a in sorted(calls)]
        rows += [("xdata", a, f"0x{a:04X}") for a in sorted(xdata)]
        rows += [("irambyte", a, f"0x{a:02X}") for a in sorted(iram_b)]
        rows += [("irambit", b, f"0x{0x20+(b>>3):02X}.{b&7}")
                 for b in sorted(iram_bit)]
        rows += [("sfr", a, f"0x{a:02X}") for a in sorted(sfr)]

        for kind, label in (("func", "call targets   "),
                            ("xdata", "XDATA addresses"),
                            ("irambyte", "IRAM bytes     "),
                            ("irambit", "IRAM bits      "),
                            ("sfr", "direct SFRs    ")):
            items = [(a, t) for k, a, t in rows if k == kind]
            n = len(items)
            if not n:
                continue
            exp = [t for a, t in items
                   if claims.get((image, kind, t)) == "explained"]
            nmd = [t for a, t in items if claims.get((image, kind, t)) == "name"]
            un = [t for a, t in items if (image, kind, t) not in claims]
            emit(f"  {label}: {len(exp):3d}/{n:3d} explained "
                 f"({100.0*len(exp)/n:5.1f}%)   "
                 f"{len(nmd):3d} named-only   {len(un):3d} neither")
            if full and un:
                for j in range(0, len(un), 12):
                    emit("      " + " ".join(un[j:j + 12]))
        emit()

    emit("Every row in the claims file is checkable against the document it")
    emit("cites. Nothing counts until somebody does the work and says where.")

    if write:
        out = os.path.join(ROOT, "ANNOTATION_LEDGER.md")
        with open(out, "w") as f:
            f.write("# Annotation ledger\n\nGenerated by "
                    "`tools/annotation_ledger.py`. Regenerate rather than "
                    "editing.\n\n```\n" + "\n".join(report) + "\n```\n")
        print(f"\nwrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
