#!/usr/bin/env python3
"""
Whole-image link for the Mbox stock firmware decompilation.

match51.py verifies one function at a time, compiled standalone at address
zero. That is a necessary check but a weak one: it proves each function's bytes
in isolation, and deliberately excuses the address operands of calls to other
functions because a standalone compile cannot know where they land.

This tool removes that excuse. Every matched candidate is compiled into its own
code segment, the segments are placed at their real stock addresses, and the
linker resolves every inter-function reference for real. What comes out is
compared against the stock image byte for byte, in place.

Three classes of defect only become visible here:

  * WRONG CALL TARGET. A standalone match reports the opcode and shrugs at the
    operand. Linked, the operand is a real address and either equals stock or
    does not.
  * OVERRUN. A function that compiles longer than its stock counterpart looks
    like a per-function length difference in match51; linked, it runs into its
    neighbour, and that is reported as an overlap rather than silently
    truncated.
  * LAYOUT DEPENDENCE. Keil's function ordering is part of the encoding --
    short jumps and merged tails only work at the original addresses. Placing
    segments at stock addresses is what lets those forms reproduce at all.

Symbols for functions not yet decompiled are supplied as absolute equates
generated from the Ghidra function table, so a matched function may call an
unmatched one and still link.

    python3 tools/link51.py rev20
    python3 tools/link51.py rev20 -v      # per-function byte diffs

Exit 0 = every placed byte matches stock, 1 = otherwise.
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware_stock")
DIS = os.path.join(FW, "disasm")
CAND = os.path.join(FW, "decomp", "cand")

IMAGES = {"rev20": os.path.join(FW, "rev20_firmware_code.bin"),
          "rev22": os.path.join(FW, "rev22_firmware_code.bin")}
GHIDRA = {"rev20": os.path.join(DIS, "rev20_ghidra.txt"),
          "rev22": os.path.join(DIS, "rev22_ghidra.txt")}

HDR = re.compile(r"//\s*MATCH:\s*(.*)")
FUNC_HDR = re.compile(r"======== FUNCTION (\S+) .*?@ CODE:([0-9a-f]{4})")
# An instruction row carries contiguous hex followed by an uppercase mnemonic.
# Hex-dump rows (data tables, 0xFF erase fill) space their bytes out and have no
# mnemonic, so this pattern counts code and only code.
INSN = re.compile(r"^CODE:([0-9a-f]{4})\s+([0-9a-f]+)\s+([A-Z])")

CFLAGS = ["-mmcs51", "--model-small", "--std-c99", "--opt-code-size",
          "--no-xinit-opt", "-c"]

SYMMAP = os.path.join(FW, "decomp", "symbols.map")

# Areas SDCC emits that hold no placed content for these candidates. They are
# parked above the image so that if anything ever does land in one, it shows up
# as an obvious out-of-range address rather than quietly overwriting code.
# Only areas that actually appear in the .rel files get based -- sdld treats
# basing an absent area as an error.
PARK = {"DSEG": 0x0030, "OSEG": 0x0030, "ISEG": 0x0080, "SSEG": 0x0080,
        "BSEG": 0x0000, "PSEG": 0x0000, "XSEG": 0xF000, "XABS": 0xF000,
        "CSEG": 0x1F00, "CONST": 0x1F00, "CABS": 0x1F00, "HOME": 0x1F00,
        "GSINIT": 0x1F00, "GSFINAL": 0x1F00, "XINIT": 0x1F00, "XISEG": 0xF000}


# Filled in at build time with `entry=1` candidates, which need equates rather
# than segments for exactly the same reason the symbols.map rows do.
SYM_EXTRA = {}


def load_symmap():
    """-> {c_name: addr} for callees whose C name differs from Ghidra's."""
    out = {}
    if not os.path.exists(SYMMAP):
        return out
    for line in open(SYMMAP):
        line = line.split("#")[0].split()
        if len(line) == 2:
            out[line[0]] = int(line[1], 0)
    return out


def areas_in(rels):
    """Area names declared by any .rel, so we only base what exists."""
    found = set()
    for r in rels:
        for line in open(r):
            if line.startswith("A "):
                found.add(line.split()[1].split("(")[0])
    return found


def ghidra_functions(image):
    """-> {addr: (name, insn_byte_count)} from the Ghidra listing."""
    fns, cur = {}, None
    for line in open(GHIDRA[image]):
        m = FUNC_HDR.search(line)
        if m:
            cur = int(m.group(2), 16)
            fns[cur] = [m.group(1), 0]
            continue
        m2 = INSN.match(line)
        if m2 and cur is not None:
            fns[cur][1] += len(m2.group(2)) // 2
    return {a: tuple(v) for a, v in fns.items()}


def candidates(image):
    """-> ([placed], [entry]), each [(path, addr, func, length, cflags)].

    An `entry=1` candidate is an alternate entry point into a tail that another
    candidate already covers -- Keil merged the two, so the bytes exist once in
    stock and belong to whichever function contains them. Placing such a
    candidate as its own segment would ask the linker to write the same
    addresses twice. It is linked as an absolute equate instead, so call sites
    resolve to the right address without duplicating bytes.
    """
    placed, entry = [], []
    fns = ghidra_functions(image)
    for path in sorted(glob.glob(os.path.join(CAND, "*.c"))):
        m = HDR.search(open(path).read())
        if not m:
            continue
        kv = dict(p.split("=", 1) for p in m.group(1).split())
        if kv["image"] != image:
            continue
        addr = int(kv["addr"], 0)
        length = int(kv["len"], 0) if "len" in kv else fns.get(addr, (None, 0))[1]
        rec = (path, addr, kv["func"], length,
               kv.get("cflags", "").replace(",", " ").split())
        (entry if kv.get("entry") == "1" else placed).append(rec)
    key = lambda c: c[1]
    return sorted(placed, key=key), sorted(entry, key=key)


def make_stubs(image, defined, work):
    """Absolute equates for every Ghidra function we have not decompiled.

    A matched function calling an unmatched one still has to link, and the
    address it calls has to be the real one or the comparison is meaningless.
    """
    lines = [";; generated by link51.py -- stock addresses of functions",
             ";; not yet decompiled, so live call sites resolve correctly.",
             "\t.module stubs"]
    syms = {name: addr for addr, (name, _) in ghidra_functions(image).items()}
    syms.update(load_symmap())      # C-side names for merged-tail entry points
    syms.update(SYM_EXTRA)          # entry=1 candidates
    n = 0
    for name, addr in sorted(syms.items(), key=lambda kv: kv[1]):
        if name in defined:
            continue
        lines += [f"\t.globl _{name}", f"_{name}\t=\t0x{addr:04X}"]
        n += 1
    path = os.path.join(work, "stubs.asm")
    open(path, "w").write("\n".join(lines) + "\n")
    r = subprocess.run(["sdas8051", "-l", "-o", path], capture_output=True,
                       text=True, cwd=work)
    if r.returncode:
        sys.exit("stub assembly failed:\n" + r.stdout + r.stderr)
    return os.path.join(work, "stubs.rel"), n


def build(image, work, verbose):
    cands, entries = candidates(image)
    # Entry points are *not* defined by a placed segment, so they fall through
    # to the equate list along with the functions we have not decompiled.
    defined = {c[2] for c in cands}
    for _, addr, func, _, _ in entries:
        SYM_EXTRA[func] = addr
    stub_rel, n_stub = make_stubs(image, defined, work)

    rels, segs = [], []
    for path, addr, func, length, extra in cands:
        seg = f"S{addr:04X}"
        rel = os.path.join(work, f"{func}.rel")
        r = subprocess.run(
            ["sdcc"] + CFLAGS + [f"--codeseg", seg] + extra + [path, "-o", rel],
            capture_output=True, text=True)
        if r.returncode:
            print(r.stdout + r.stderr, file=sys.stderr)
            sys.exit(f"sdcc failed on {path}")
        rels.append(rel)
        segs.append((seg, addr))

    ihx = os.path.join(work, "image.ihx")
    present = areas_in([stub_rel] + rels)
    cmd = ["sdld", "-n", "-m", "-i", ihx]
    for seg, addr in segs:
        cmd += ["-b", f"{seg}=0x{addr:04X}"]
    for area, base in sorted(PARK.items()):
        if area in present:
            cmd += ["-b", f"{area}=0x{base:04X}"]
    cmd += [stub_rel] + rels
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=work)
    if r.returncode or "?ASlink-Error" in (r.stdout + r.stderr):
        print(r.stdout + r.stderr, file=sys.stderr)
        sys.exit("link failed")
    # sdld bumps an area forward rather than honouring a base that would overlap
    # an already-placed area, so a segment landing anywhere but its stock
    # address means two candidates claim the same bytes. Silently accepting the
    # bump would compare the wrong function against the wrong stock bytes.
    mapf = os.path.join(work, "image.map")
    moved = []
    want = dict(segs)
    for line in open(mapf):
        m = re.match(r"^(S[0-9A-F]{4})\s+([0-9A-F]{8})\s", line)
        if m and m.group(1) in want:
            got = int(m.group(2), 16)
            if got != want[m.group(1)]:
                moved.append((m.group(1), want[m.group(1)], got))
    if moved:
        print("\n  SEGMENT DISPLACED -- the linker could not honour these bases,")
        print("  which means two candidates claim overlapping stock bytes:")
        for seg, w, g in moved:
            nm = next(c[2] for c in cands if c[1] == w)
            print(f"    {nm:<32} wanted 0x{w:04X}, landed 0x{g:04X}")
        sys.exit(1)
    return cands, entries, ihx, n_stub, mapf


def read_ihx(path):
    """-> {addr: byte}. Sparse on purpose: only what the linker actually placed."""
    mem = {}
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        b = bytes.fromhex(line[1:])
        n, addr, typ = b[0], (b[1] << 8) | b[2], b[3]
        if typ == 0:
            for i in range(n):
                mem[addr + i] = b[4 + i]
    return mem


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", choices=sorted(IMAGES))
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    stock = open(IMAGES[a.image], "rb").read()
    work = tempfile.mkdtemp(prefix="link51_")
    cands, entries, ihx, n_stub, mapf = build(a.image, work, a.verbose)
    mem = read_ihx(ihx)

    # Expected extent of each placed function, for overrun detection.
    owner = {}
    for _, addr, func, length, _ in cands:
        for i in range(addr, addr + length):
            owner[i] = func

    rows, bad_total, placed = [], 0, 0
    for _, addr, func, length, _ in cands:
        got = [mem.get(addr + i) for i in range(length)]
        bad = [(i, stock[addr + i], got[i])
               for i in range(length) if got[i] != stock[addr + i]]
        rows.append((addr, func, length, bad))
        bad_total += len(bad)
        placed += length

    # Bytes the linker emitted outside any declared function extent: either a
    # function compiled longer than stock, or an area landed somewhere it should
    # not have. Either way it is a real defect, not a rounding error.
    stray = sorted(k for k in mem if k not in owner and k < len(stock))
    overruns = {}
    for k in stray:
        prev = max((c[1] for c in cands if c[1] <= k), default=None)
        if prev is not None:
            overruns.setdefault(prev, []).append(k)

    # An entry point that does not land inside a placed function is an
    # unbacked address claim: nothing verifies those bytes, so say so.
    unbacked = [e for e in entries if e[1] not in owner]

    print(f"whole-image link: {a.image}  ({len(cands)} placed, "
          f"{len(entries)} merged-tail entry point(s), "
          f"{n_stub} stock-address stubs)\n")
    for addr, func, length, bad in rows:
        mark = "\033[32mMATCH\033[0m" if not bad else "\033[31mDIFF \033[0m"
        note = f"  {len(bad)} byte(s)" if bad else ""
        print(f"  0x{addr:04X} {func:<32} {length:4d} B  {mark}{note}")
        if bad and a.verbose:
            for off, w, g in bad[:12]:
                gs = f"{g:02x}" if g is not None else "--"
                print(f"        +0x{off:03x}  stock {w:02x}  ours {gs}")

    if overruns:
        print("\n  OVERRUN -- bytes emitted past a function's stock extent:")
        for base, ks in sorted(overruns.items()):
            nm = next(c[2] for c in cands if c[1] == base)
            print(f"    {nm} (0x{base:04X}) spills "
                  f"0x{ks[0]:04X}..0x{ks[-1]:04X} ({len(ks)} B)")

    if entries:
        print("\n  merged-tail entry points (equated, bytes owned by the "
              "container):")
        for _, addr, func, length, _ in entries:
            host = owner.get(addr)
            print(f"    0x{addr:04X} {func:<30} {length:3d} B  "
                  + (f"inside {host}" if host else "\033[31mNOT COVERED\033[0m"))

    total = sum(v[1] for v in ghidra_functions(a.image).values())
    nm_ok = sum(1 for r in rows if not r[3])
    if placed:
        print(f"\n  linked {nm_ok}/{len(rows)} functions exact, "
              f"{placed - bad_total}/{placed} placed bytes "
              f"({100.0 * (placed - bad_total) / placed:.1f}%)")
        print(f"  image coverage: {placed}/{total} instruction bytes "
              f"({100.0 * placed / total:.1f}% of {a.image})")
    else:
        print("\n  nothing placed")
    print(f"  map: {mapf}")
    return 1 if (bad_total or overruns or unbacked) else 0


if __name__ == "__main__":
    sys.exit(main())
