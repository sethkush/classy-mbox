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
# relocatable operands come out as 00 00 with an r-record; detect by mnemonic
RELOC_OPS = ("lcall", "ljmp", "acall", "ajmp", "sjmp", "jz", "jnz", "jc", "jnc",
             "jb", "jnb", "jbc", "cjne", "djnz", "mov\tdptr")

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


def extract(lst, func):
    """Return (bytes, [(offset, mnemonic)]) for one function in the listing."""
    out, notes, inside = bytearray(), [], False
    for line in open(lst):
        if re.match(rf"^\s+[0-9A-F]{{6}}\s.*\b_{re.escape(func)}:", line) or \
           re.search(rf"\b_{re.escape(func)}:\s*$", line):
            inside = True
            continue
        if not inside:
            continue
        if re.search(r"^\s+[0-9A-F]{6}\s+\d+\s+_\w+:", line):
            break
        m = LST.match(line)
        if not m:
            continue
        raw = bytes.fromhex(m.group(2).replace(" ", ""))
        mn = line.split("\t", 1)[1].strip() if "\t" in line else ""
        notes.append((len(out), mn, len(raw)))
        out += raw
        if mn.startswith("ret"):
            break
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
        got, notes = extract(lst, func)

        n = max(len(want), len(got))
        bad = []
        for i in range(n):
            wb = want[i] if i < len(want) else None
            gb = got[i] if i < len(got) else None
            if wb != gb:
                reloc = any(o <= i < o + sz and mn.startswith(RELOC_OPS)
                            for o, mn, sz in notes)
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
