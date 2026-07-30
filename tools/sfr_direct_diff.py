#!/usr/bin/env python3
"""
sfr_direct_diff -- diff mboxfw's DIRECT-addressed SFR writes against both
stock images.

Why this exists
---------------
`audit_sfr_writes.py` and `diff_vs_rev20.py` scan for `MOV DPTR,#0xFFxx`
followed by `MOVX @DPTR,A`. That covers the TAS1020B's memory-mapped UIFR
registers and NOTHING ELSE. The 8051 core SFR space at 0x80-0xFF -- TCON,
TMOD, IE, IP, PCON, TH0/TL0, P1, P3, PSW, SP -- is direct-addressed, and every
bit-addressable SFR bit (TR0, EA, EX0, IT0, ...) is reached with SETB/CLR/CPL.
None of those instructions contain a MOVX. So none of them were ever compared
against stock.

That is not hypothetical. On 2026-07-29, `TR0` turned out never to have been
set anywhere in mboxfw: `hw_init` writes `TCON = 0x00`, which clears it, and
nothing turned it back on. Timer 0 never counted, its ISR never fired once, and
`g_timer0_ticks` read 0 on every telemetry dump since it was added. Both stock
images set it two instructions before enabling interrupts (Rev 20 0x0AC8,
Rev 22 0x0A72). Six existing gates were green across that whole period, because
`SETB 0x8C` has no MOVX in it and lives in a register space nothing scanned.

Method
------
Stock side: decode opcode bytes from the recursive-traversal Ghidra listings
(NOT rev20_flat.asm, which is known bad -- see project notes). Byte-level
decoding rather than mnemonic matching, so listing formatting cannot cause a
miss.

mboxfw side: parse the SDCC .rst listings, which carry both the `_NAME = 0xNN`
equates and symbolic instructions. Using mnemonics here avoids mis-decoding
`.db` descriptor tables as instructions.

Reads are reported separately and are advisory: reading an SFR stock never
reads is not a defect, but stock reading one mboxfw ignores can be (that is how
DMABCNT0 surfaced).

    python3 tools/sfr_direct_diff.py           # summary + diffs
    python3 tools/sfr_direct_diff.py --all     # every site, both sides
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISASM = os.path.join(ROOT, "firmware_stock", "disasm")
BUILD = os.path.join(ROOT, "mboxfw", "build")

INSN = re.compile(r"^CODE:([0-9a-f]{4})\s+([0-9a-f]+)\s+([A-Z][A-Z0-9]*)")

# 8051 bit-addressable SFR bytes. A bit address >= 0x80 belongs to the SFR at
# (bitaddr & 0xF8); only these bytes are bit-addressable.
BIT_SFR_BASES = {0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8,
                 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8}

# Canonical core-SFR names (8051 + TAS1020B additions). Address -> name.
CORE = {
    0x80: "P0",   0x81: "SP",   0x82: "DPL",  0x83: "DPH",  0x87: "PCON",
    0x88: "TCON", 0x89: "TMOD", 0x8A: "TL0",  0x8B: "TL1",  0x8C: "TH0",
    0x8D: "TH1",  0x90: "P1",   0x98: "SCON", 0x99: "SBUF", 0xA0: "P2",
    0xA8: "IE",   0xB0: "P3",   0xB8: "IP",   0xC8: "T2CON",
    0xD0: "PSW",  0xE0: "ACC",  0xF0: "B",
}

# Named bits worth calling out by name in the report.
BITNAME = {
    0x8C: "TR0", 0x8E: "TR1", 0x88: "IT0", 0x8A: "IT1",
    0x89: "IE0", 0x8B: "IE1", 0x8D: "TF0", 0x8F: "TF1",
    0xA8: "EX0", 0xA9: "ET0", 0xAA: "EX1", 0xAB: "ET1",
    0xAC: "ES",  0xAD: "ET2", 0xAF: "EA",
    0xD7: "CY",  0xD3: "RS0", 0xD4: "RS1",
}

# Write forms. opcode -> (total_len, index_of_direct_operand, kind)
#   0x85 is MOV dst,src encoded `85 src dst`, so the DESTINATION is byte 2.
DIRECT_W = {
    0x75: (3, 1, "assign-imm"),
    0xF5: (2, 1, "assign-A"),
    0x85: (3, 2, "assign-direct"),
    0x42: (2, 1, "or-A"),
    0x43: (3, 1, "or-imm"),
    0x52: (2, 1, "and-A"),
    0x53: (3, 1, "and-imm"),
    0x62: (2, 1, "xor-A"),
    0x63: (3, 1, "xor-imm"),
    0x05: (2, 1, "inc"),
    0x15: (2, 1, "dec"),
    0xD0: (2, 1, "pop"),
}
for _op in range(0x88, 0x90):          # MOV direct,Rn
    DIRECT_W[_op] = (2, 1, "assign-Rn")

DIRECT_R = {0xE5: (2, 1), 0xC0: (2, 1)}   # MOV A,direct ; PUSH direct

BIT_W = {0xD2: "setb", 0xC2: "clr", 0xB2: "cpl", 0x92: "mov-bit-from-C"}
BIT_R = {0x20: "jb", 0x30: "jnb", 0x10: "jbc", 0xA2: "mov-C-from-bit"}


def stock_sites(image):
    """Return (writes, reads) dicts: addr -> set of (kind, imm_or_None)."""
    path = os.path.join(DISASM, f"{image}_ghidra.txt")
    data = open(os.path.join(ROOT, "firmware_stock",
                             f"{image}_firmware_code.bin"), "rb").read()
    writes, reads = defaultdict(set), defaultdict(set)
    for line in open(path):
        m = INSN.match(line)
        if not m:
            continue
        a = int(m.group(1), 16)
        op = data[a]

        if op in DIRECT_W:
            ln, idx, kind = DIRECT_W[op]
            tgt = data[a + idx]
            if tgt >= 0x80:
                imm = data[a + 2] if kind.endswith("-imm") else None
                writes[tgt].add((kind, imm))
        elif op in DIRECT_R:
            _ln, idx = DIRECT_R[op]
            tgt = data[a + idx]
            if tgt >= 0x80:
                reads[tgt].add(("read", None))
        elif op in BIT_W:
            b = data[a + 1]
            if b >= 0x80 and (b & 0xF8) in BIT_SFR_BASES:
                writes[b & 0xF8].add((BIT_W[op] + f" bit{b & 7}", None))
        elif op in BIT_R:
            b = data[a + 1]
            if b >= 0x80 and (b & 0xF8) in BIT_SFR_BASES:
                reads[b & 0xF8].add((f"test bit{b & 7}", None))
    return writes, reads


RST_EQU = re.compile(r"^\s+([0-9A-F]{6})\s+\d+\s+(\w+)\s*=\s*0x([0-9a-fA-F]{2,4})\s*$")
RST_INSN = re.compile(r"^\s+[0-9A-F]{6}\s+(?:[0-9A-F]{2} )+\s*\[\s*\d+\]\s+\d+\s+\t(.*)$")


def mboxfw_sites():
    """Parse every .rst in the build dir. Returns (writes, reads)."""
    equ, writes, reads = {}, defaultdict(set), defaultdict(set)
    rsts = [f for f in os.listdir(BUILD) if f.endswith(".rst")]
    if not rsts:
        sys.exit("no .rst files in mboxfw/build -- run `make` first")

    # Pass 1: collect symbol -> address equates across all modules.
    for f in rsts:
        for line in open(os.path.join(BUILD, f)):
            m = RST_EQU.match(line)
            if m:
                equ[m.group(2)] = int(m.group(3), 16)

    # Bit symbols: SDCC equates e.g. _TR0 = 0x008c (a BIT address, not a byte).
    # Distinguish by instruction form rather than by value, below.
    def resolve(tok):
        tok = tok.strip()
        return equ.get(tok)

    for f in rsts:
        for line in open(os.path.join(BUILD, f)):
            m = RST_INSN.match(line)
            if not m:
                continue
            text = m.group(1).split(";")[0].strip()
            parts = re.split(r"\t+|\s{1,}", text, maxsplit=1)
            mn = parts[0].lower()
            ops = parts[1] if len(parts) > 1 else ""

            if mn in ("setb", "clr", "cpl") and ops.strip().startswith("_"):
                b = resolve(ops)
                if b is not None and b >= 0x80 and (b & 0xF8) in BIT_SFR_BASES:
                    writes[b & 0xF8].add((f"{mn} bit{b & 7}", None))
                continue
            if mn in ("jb", "jnb", "jbc") and ops.strip().startswith("_"):
                b = resolve(ops.split(",")[0])
                if b is not None and b >= 0x80 and (b & 0xF8) in BIT_SFR_BASES:
                    reads[b & 0xF8].add((f"test bit{b & 7}", None))
                continue

            if "," in ops:
                dst, src = [x.strip() for x in ops.split(",", 1)]
                d = resolve(dst)
                kind = {"mov": "assign", "orl": "or", "anl": "and",
                        "xrl": "xor"}.get(mn)
                if kind and d is not None and 0x80 <= d <= 0xFF:
                    imm = None
                    if src.startswith("#0x"):
                        try:
                            imm = int(src[3:], 16)
                        except ValueError:
                            imm = None
                        kind += "-imm"
                    elif src.lower() == "a":
                        kind += "-A"
                    writes[d].add((kind, imm))
                s = resolve(src)
                if mn == "mov" and s is not None and 0x80 <= s <= 0xFF \
                        and dst.lower() == "a":
                    reads[s].add(("read", None))
            elif mn in ("inc", "dec", "push", "pop"):
                d = resolve(ops)
                if d is not None and 0x80 <= d <= 0xFF:
                    (reads if mn == "push" else writes)[d].add((mn, None))
    return writes, reads


# SFRs the compiler owns. A difference here is codegen strategy, not firmware
# policy, and flagging it would make this gate noise. Each needs a reason, and
# each reason was checked rather than assumed.
EXEMPT = {
    0x81: "SP -- set by SDCC's crt0, which has no .rst in the build dir. "
          "Verified directly in the linked image (see the SP check below).",
    0x82: "DPL -- DPTR half, written by every XDATA access the compiler emits.",
    0x83: "DPH -- likewise.",
    0xE0: "ACC -- accumulator; PUSH/POP ACC is ISR save/restore.",
    0xF0: "B -- MUL/DIV scratch and ISR save/restore.",
    0xD0: "PSW -- stock's INT0 handler selects register bank 2 "
          "(MOV PSW,#0x10 at Rev 20 0x0DB6 / Rev 22 0x0DE9); SDCC's ISRs "
          "instead push the registers they use. Different strategy for the "
          "same guarantee, and SDCC's is the one its codegen assumes.",
}


def name(addr):
    return CORE.get(addr, f"SFR_0x{addr:02X}")


def linked_sp():
    """MOV SP,#imm from the linked image -- crt0 has no .rst to parse."""
    path = os.path.join(BUILD, "mboxfw.ihx")
    if not os.path.exists(path):
        return None, None
    img = {}
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        n, addr, t = (int(line[1:3], 16), int(line[3:7], 16),
                      int(line[7:9], 16))
        if t != 0:
            continue
        for i in range(n):
            img[addr + i] = int(line[9 + 2 * i:11 + 2 * i], 16)
    if not img:
        return None, None
    data = bytes(img.get(i, 0xFF) for i in range(max(img) + 1))
    for i in range(len(data) - 2):
        if data[i] == 0x75 and data[i + 1] == 0x81:
            return data[i + 2], i
    return None, None


def fmt(entries):
    out = []
    for kind, imm in sorted(entries, key=lambda e: (e[0], -1 if e[1] is None
                                                    else e[1])):
        out.append(kind if imm is None else f"{kind} 0x{imm:02X}")
    return ", ".join(out)


def main():
    show_all = "--all" in sys.argv
    r20w, r20r = stock_sites("rev20")
    r22w, r22r = stock_sites("rev22")
    mbw, mbr = mboxfw_sites()

    stock_w = set(r20w) | set(r22w)
    stock_r = set(r20r) | set(r22r)

    print("=== DIRECT-ADDRESSED SFR WRITES (core space 0x80-0xFF) ===\n")
    print(f"  rev20 writes {len(r20w)} SFRs, rev22 {len(r22w)}, "
          f"mboxfw {len(mbw)}\n")

    both = sorted(stock_w | set(mbw))
    if show_all:
        for a in both:
            print(f"  0x{a:02X} {name(a):6}")
            print(f"      rev20 : {fmt(r20w.get(a, set())) or '-'}")
            print(f"      rev22 : {fmt(r22w.get(a, set())) or '-'}")
            print(f"      mboxfw: {fmt(mbw.get(a, set())) or '-'}")
        print()

    rc = 0

    sp, spat = linked_sp()
    r20sp = [i for k, i in r20w.get(0x81, set()) if k == "assign-imm"]
    print("  STACK POINTER (crt0, not in any .rst):")
    if sp is None:
        rc = 1
        print("    NO `MOV SP,#imm` in the linked image -- SP is whatever "
              "reset left it (0x07), so the stack overlaps register bank 1.")
    else:
        print(f"    mboxfw: MOV SP,#0x{sp:02X} at 0x{spat:04X} "
              f"-> stack starts 0x{sp + 1:02X}")
        print(f"    stock : MOV SP,#0x{r20sp[0]:02X} "
              f"-> stack starts 0x{r20sp[0] + 1:02X}"
              if r20sp else "    stock : (none found)")
        print(f"    mboxfw stack has {0x100 - (sp + 1)} bytes of IRAM above it")
    print()

    missing = sorted(a for a in stock_w if a not in mbw and a not in EXEMPT)
    exempted = sorted(a for a in stock_w if a not in mbw and a in EXEMPT)
    if missing:
        rc = 1
        print("  STOCK WRITES IT, MBOXFW NEVER DOES:")
        for a in missing:
            src = "both" if a in r20w and a in r22w else (
                "rev20" if a in r20w else "rev22")
            print(f"    0x{a:02X} {name(a):6} ({src}): "
                  f"{fmt(r20w.get(a) or r22w.get(a))}")
        print()
    if exempted and show_all:
        print("  EXEMPT (compiler-owned; reason recorded in EXEMPT):")
        for a in exempted:
            print(f"    0x{a:02X} {name(a):6}: {EXEMPT[a]}")
        print()

    # Per-bit comparison, by DIRECTION.
    #
    # A byte-level match hides missing bits -- that is the TR0 case, since both
    # firmwares "write TCON". But the first cut of this check went wrong the
    # other way: it treated any byte-wide assignment as covering every bit, so
    # `TCON = 0x00` counted as covering stock's `SETB TR0`. It does the
    # opposite -- it clears the bit stock sets. That version passed its own
    # mutation test with TR0 deleted, i.e. it was useless.
    #
    # The rule that works: a byte assignment of V can SET only the bits where V
    # has a 1, and can CLEAR only the bits where V has a 0. So
    #   mboxfw can set bit b   <=>  it SETBs b, or assigns some V with bit b = 1
    #   mboxfw can clear bit b <=>  it CLRs b,  or assigns some V with bit b = 0
    # and the gap that fails the gate is a bit stock ever SETs that mboxfw
    # cannot set. The clear direction is advisory: reset state is already 0, so
    # a missing clear is usually harmless where a missing set is not.
    # The two sides must treat an UNPROVABLE write (computed value: `MOV
    # TCON,A`, `POP IP`, RMW with a register) asymmetrically, or the check is
    # garbage in one direction or the other:
    #
    #   stock side  (proven=True): a computed write does NOT prove stock sets
    #     any particular bit, so it contributes nothing. Counting it as
    #     "sets all 8" invents requirements -- the first version of this did
    #     exactly that and reported 15 bogus FAILs on TCON and IP.
    #   mboxfw side (proven=False): a computed write MIGHT set the bit, so it
    #     counts as capability. Otherwise the gate fails on anything the
    #     compiler routes through the accumulator.
    #
    # Both directions err toward silence, which is the right bias for a gate
    # whose output is a claim that stock does something we don't.
    def caps(d, a, proven):
        cs = cc = 0
        for kind, imm in d.get(a, set()):
            if kind.startswith(("setb", "mov-bit")):
                cs |= 1 << int(kind.split("bit")[1])
            elif kind.startswith("clr"):
                cc |= 1 << int(kind.split("bit")[1])
            elif kind.startswith("cpl"):
                b = 1 << int(kind.split("bit")[1])
                cs |= b
                cc |= b
            elif kind == "assign-imm" and imm is not None:
                cs |= imm
                cc |= 0xFF & ~imm
            elif not proven:
                cs = cc = 0xFF
        return cs, cc

    print("  BIT-LEVEL by direction (a missing SET fails; missing CLEAR is "
          "advisory):")
    bitgap = False
    for a in sorted(BIT_SFR_BASES & (stock_w | set(mbw))):
        if a in EXEMPT:
            continue
        s_set = caps(r20w, a, True)[0] | caps(r22w, a, True)[0]
        s_clr = caps(r20w, a, True)[1] | caps(r22w, a, True)[1]
        m_set, m_clr = caps(mbw, a, False)
        miss_set = s_set & ~m_set
        miss_clr = s_clr & ~m_clr
        if not (miss_set or miss_clr):
            continue
        for b in range(8):
            nm = BITNAME.get(a + b, f"bit{b}")
            if miss_set >> b & 1:
                bitgap = True
                rc = 1
                print(f"    FAIL 0x{a:02X} {name(a):6} bit {b} ({nm}): stock "
                      f"SETS it; mboxfw cannot -- it neither SETBs it nor "
                      f"assigns a value with that bit high")
            elif miss_clr >> b & 1:
                print(f"    note 0x{a:02X} {name(a):6} bit {b} ({nm}): stock "
                      f"clears it; mboxfw does not (reset state is 0 anyway)")
    if not bitgap:
        print("    no missing SETs")
    print()

    only = sorted(a for a in mbw if a not in stock_w)
    if only:
        print("  MBOXFW WRITES IT, NEITHER STOCK IMAGE DOES:")
        for a in only:
            print(f"    0x{a:02X} {name(a):6}: {fmt(mbw[a])}")
        print()

    rmissing = sorted(a for a in stock_r if a not in mbr
                      and a not in mbw and a not in EXEMPT)
    if rmissing:
        print("  ADVISORY -- stock READS it, mboxfw neither reads nor writes:")
        for a in rmissing:
            print(f"    0x{a:02X} {name(a):6}: {fmt(r20r.get(a) or r22r.get(a))}")
        print()

    print("Non-zero exit means stock touches a core SFR, or a bit of one,\n"
          "that mboxfw never does. That is not automatically a defect --\n"
          "but every one needs a reason, and TR0 did not have one.")
    return rc


if __name__ == "__main__":
    sys.exit(main())
