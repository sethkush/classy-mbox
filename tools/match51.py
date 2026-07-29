#!/usr/bin/env python3
"""
Byte-match harness for the Mbox stock firmware decompilation.

Compiles a candidate C function with SDCC, extracts the emitted bytes, and
diffs them against the corresponding bytes in a stock image.

    python3 tools/match51.py cand/dma0_disable.c

Each candidate .c file carries its target in a header comment:

    // MATCH: image=rev20 addr=0x1001 len=8 func=dma0_disable

`len` is optional; the stock length is taken from the Ghidra function table
when omitted. Symbols that the linker would relocate (LCALL/LJMP to other
functions) are compared loosely: the opcode must match, the address operand
is reported but not counted as a mismatch unless --strict is given.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware_stock")
DIS = os.path.join(FW, "disasm")

IMAGES = {"rev20": os.path.join(FW, "rev20_firmware_code.bin"),
          "rev22": os.path.join(FW, "rev22_firmware_code.bin")}
GHIDRA = {"rev20": os.path.join(DIS, "rev20_ghidra.txt"),
          "rev22": os.path.join(DIS, "rev22_ghidra.txt")}

HDR = re.compile(r"//\s*MATCH:\s*(.*)")
# listing rows: "      000000 90 FF E8         [24]  107 \tmov\tdptr,#_X"
LST = re.compile(r"^\s+([0-9A-F]{6})((?:\s[0-9A-F]{2})+)\s")
# Only calls/jumps to *external* symbols are left unresolved by the assembler
# (as 00 00 plus a relocation record); everything else, including local
# branches, is resolved at assembly time and must compare strictly.
#
# Mask ONLY the two address operand bytes, never the opcode. Masking whole
# instructions hides real mismatches: "jbc".startswith("jb") would skip the
# opcode byte that distinguishes JBC (0x10) from JNB (0x30), and masking
# `mov dptr,#...` would hide a wrong SFR address entirely.
RELOC_MNEMONICS = ("lcall", "ljmp")
RELOC_OPERAND_BYTES = (1, 2)

# LJMP/LCALL to a LOCAL label encodes an absolute address, so a function
# compiled standalone at 0x0000 differs from the same function linked at its
# real address by exactly that base. Rather than excusing those bytes, relocate
# ours and compare properly -- a genuinely wrong local target still fails.
ABS_MNEMONICS = ("lcall", "ljmp")


def relocate_local_jumps(got, notes, base):
    """Add `base` to absolute operands of LJMP/LCALL targeting local labels."""
    out = bytearray(got)
    for off, mn, sz in notes:
        parts = mn.split()
        if len(parts) < 2 or parts[0] not in ABS_MNEMONICS or sz != 3:
            continue
        if parts[1].startswith("_"):      # external symbol: linker resolves it
            continue
        if off + 2 >= len(out):
            continue
        tgt = (out[off + 1] << 8) | out[off + 2]
        tgt = (tgt + base) & 0xFFFF
        out[off + 1] = tgt >> 8
        out[off + 2] = tgt & 0xFF
    return bytes(out)

CFLAGS = ["-mmcs51", "--model-small", "--std-c99", "--opt-code-size",
          "--no-xinit-opt", "-S"]


def stock_bytes(image, addr, length):
    data = open(IMAGES[image], "rb").read()
    return data[addr:addr + length]


def stock_fn_len(image, addr):
    want = f"@ CODE:{addr:04x}"
    cur = None
    total = 0
    for line in open(GHIDRA[image]):
        if "======== FUNCTION" in line:
            if cur:
                return total
            cur = want in line
            total = 0
            continue
        m = re.match(r"CODE:[0-9a-f]{4}\s+([0-9a-f]+)\s", line)
        if m and cur:
            total += len(m.group(1)) // 2
    return total or None


def compile_candidate(cpath, extra):
    d = tempfile.mkdtemp(prefix="match51_")
    asm = os.path.join(d, "c.asm")
    r = subprocess.run(["sdcc"] + CFLAGS + extra + [cpath, "-o", asm],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr, file=sys.stderr)
        sys.exit(f"sdcc failed on {cpath}")
    r = subprocess.run(["sdas8051", "-l", "-o", asm], capture_output=True, text=True,
                       cwd=d)
    if r.returncode:
        print(r.stdout + r.stderr, file=sys.stderr)
        sys.exit("sdas8051 failed")
    return os.path.join(d, "c.lst")


def extract(lst, func, span=False):
    """Return (bytes, [(offset, mnemonic)]) for one function in the listing.

    With `span`, keep going past subsequent function labels to the end of the
    code area. That is for candidates holding more than one function, where the
    point of the candidate is the run of bytes the two produce together --
    Keil's merged tails and adjacent-function short jumps only reproduce when
    the functions are compiled in the original order in one unit.
    """
    out, notes, inside = bytearray(), [], False
    for line in open(lst):
        if re.match(rf"^\s+[0-9A-F]{{6}}\s.*\b_{re.escape(func)}:", line) or \
           re.search(rf"\b_{re.escape(func)}:\s*$", line):
            inside = True
            continue
        if not inside:
            continue
        # Stop at the next function label or the end of the code area.
        # Do NOT stop at the first `ret` — functions with early returns have
        # several, and truncating there silently under-reports the body.
        if not span and re.search(r"\b_\w+:\s*$", line) \
                and not line.lstrip().startswith(";"):
            break
        if ".area" in line and "CSEG" not in line:
            break
        m = LST.match(line)
        if not m:
            continue
        raw = bytes.fromhex(m.group(2).replace(" ", ""))
        mn = line.split("\t", 1)[1].strip() if "\t" in line else ""
        notes.append((len(out), mn, len(raw)))
        out += raw
    return bytes(out), notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("candidates", nargs="+")
    ap.add_argument("--strict", action="store_true",
                    help="count relocatable address operands as mismatches")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    tot_b = tot_m = 0
    results = []
    for c in a.candidates:
        src = open(c).read()
        m = HDR.search(src)
        if not m:
            sys.exit(f"{c}: no // MATCH: header")
        kv = dict(p.split("=", 1) for p in m.group(1).split())
        image, addr, func = kv["image"], int(kv["addr"], 0), kv["func"]
        extra = kv.get("cflags", "").replace(",", " ").split()
        length = int(kv["len"], 0) if "len" in kv else stock_fn_len(image, addr)
        want = stock_bytes(image, addr, length)

        lst = compile_candidate(c, extra)
        got, notes = extract(lst, func, kv.get("span") == "1")
        got = relocate_local_jumps(got, notes, addr)

        n = max(len(want), len(got))
        bad = []
        for i in range(n):
            wb = want[i] if i < len(want) else None
            gb = got[i] if i < len(got) else None
            if wb != gb:
                # A byte is excusable only if it is an address operand of an
                # LCALL/LJMP to an external symbol, which the linker resolves.
                # mn may be tab- or space-separated: compiler output uses
                # tabs, hand-written inline asm typically does not.
                reloc = any(o <= i < o + sz
                            and mn.split()[0] in RELOC_MNEMONICS
                            and (i - o) in RELOC_OPERAND_BYTES
                            and "_" in mn
                            for o, mn, sz in notes if mn.split())
                if reloc and not a.strict:
                    continue
                bad.append((i, wb, gb))
        status = "MATCH" if not bad and len(want) == len(got) else "DIFF"
        tot_b += len(want)
        tot_m += len(want) - len(bad)
        results.append((status, func, image, addr, len(want), len(got), bad, notes))

    for status, func, image, addr, lw, lg, bad, notes in results:
        mark = "\033[32mMATCH\033[0m" if status == "MATCH" else "\033[31mDIFF \033[0m"
        size = f"{lw} B" if lw == lg else f"{lw}->{lg} B"
        print(f"  0x{addr:04X} {func:<32} {size:>10}  {mark}"
              + (f"  {len(bad)} byte(s)" if bad else ""))
        if bad and a.verbose:
            for off, wb, gb in bad[:12]:
                w = f"{wb:02x}" if wb is not None else "--"
                g = f"{gb:02x}" if gb is not None else "--"
                mn = next((mn for o, mn, sz in notes if o <= off < o + sz), "")
                print(f"        +0x{off:03x}  stock {w}  ours {g}   {mn}")
    pct = 100.0 * tot_m / tot_b if tot_b else 0
    nm = sum(1 for r in results if r[0] == "MATCH")
    print(f"\n  matched {nm}/{len(results)} functions, "
          f"{tot_m}/{tot_b} bytes ({pct:.1f}%)")


if __name__ == "__main__":
    main()
