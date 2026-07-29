#!/usr/bin/env python3
"""
xdata_access_map -- for every XDATA/SFR address either stock image touches,
enumerate what the firmware actually DOES with it: every site, the direction,
and the value where it is statically known.

This exists because `regs.h` names 157 registers and a name is not an
explanation. The annotation ledger scores a named register as `level=name`
precisely because knowing 0xFFEE is called DMACTL1 tells you nothing about what
the firmware writes there -- and that address carried the DMACTL1 name while the
real DMA channels sat elsewhere, which caused a real bug.

An exhaustive access map IS the explanation at the level that matters for a
reimplementation: every read, every write, every constant. It is generated, so
it cannot drift from the image, and it is complete, so it cannot flatter.

Recognised idioms after `MOV DPTR,#addr`:

    74 nn / F0          write of immediate nn
    E0 / 44 nn / F0     read-modify-write, OR with nn   (set bits)
    E0 / 54 nn / F0     read-modify-write, AND with nn  (clear bits)
    E4 / F0             write zero
    E0 (no F0 nearby)   read only
    F0 (A not a known immediate) write of a computed value
    74 nn / 12 xxxx     write of nn performed inside the helper at xxxx --
                        Rev 20 0x0FF4 is such a helper, and missing this idiom
                        left 14 sites classified as bare reads when they are
                        writes (it is how CPTCNF3 = 0xAC at 0x034A is emitted)
    93                  MOVC A,@A+DPTR: a CODE lookup table, not a register
    73 / A4             JMP @A+DPTR, or MUL AB computing a table stride: a
                        jump table. Keil emits `MOV B,#3 / MUL AB` because each
                        entry is a 3-byte LJMP.
    12 xxxx (no 74)     write performed by the helper at xxxx, which supplies
                        the value itself -- Rev 20 0x0E18 writes 0x10 to the
                        caller's DPTR and then to ACG2DCTL
    02 xxxx             same, but tail-called: Keil LJMPs into a shared tail
                        that performs the MOVX (Rev 22 0x0103 and 0x016F reach
                        the store this way)

    python3 tools/xdata_access_map.py            # summary counts
    python3 tools/xdata_access_map.py --write    # XDATA_ACCESS_MAP.md
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware_stock")
DISASM = os.path.join(FW, "disasm")
INSN = re.compile(r"^CODE:([0-9a-f]{4})\s+([0-9a-f]+)\s+([A-Z][A-Z0-9]*)")
WINDOW = 10          # instructions to look ahead for the access


def load(image):
    d = open(os.path.join(FW, f"{image}_firmware_code.bin"), "rb").read()
    starts = []
    for line in open(os.path.join(DISASM, f"{image}_ghidra.txt")):
        m = INSN.match(line)
        if m:
            starts.append((int(m.group(1), 16), len(m.group(2)) // 2))
    return d, starts


def regs_h():
    out = {}
    p = os.path.join(ROOT, "mboxfw", "include", "regs.h")
    for line in open(p):
        m = re.match(r"#define\s+(\w+)\s+XDATA\(0x([0-9A-Fa-f]{4})\)", line.strip())
        if m:
            out[int(m.group(2), 16)] = m.group(1)
    return out


def analyse(image):
    d, starts = load(image)
    idx = {a: i for i, (a, _n) in enumerate(starts)}
    acc = {}
    for i, (a, _n) in enumerate(starts):
        if d[a] != 0x90:
            continue
        reg = (d[a + 1] << 8) | d[a + 2]
        ops = []
        for j in range(i + 1, min(i + 1 + WINDOW, len(starts))):
            addr, _ln = starts[j]
            op = d[addr]
            if op == 0x90:                    # DPTR reloaded: stop
                break
            ops.append((addr, op))
            if op in (0xF0, 0x93, 0x73, 0xA4):   # write / MOVC / jump table
                break
            if op in (0x12, 0x02):            # write delegated to a helper
                break                         # 0x02 = tail-call into a store
        kind, val = "no-access-found", None
        seq = [o for _x, o in ops]
        if 0x93 in seq:
            kind = "code-table (MOVC)"
        elif 0x73 in seq or 0xA4 in seq:
            kind = "jump table (3-byte LJMP entries)"
        elif (0x12 in seq or 0x02 in seq) and 0xF0 not in seq:
            tgt = ops[seq.index(0x12 if 0x12 in seq else 0x02)][0]
            kind = "write-via-helper 0x%04X" % ((d[tgt + 1] << 8) | d[tgt + 2])
            if 0x74 in seq:
                val = d[ops[seq.index(0x74)][0] + 1]
        elif 0xF0 in seq:
            pos = seq.index(0xF0)
            pre = seq[:pos]
            if 0xE0 in pre and 0x44 in pre:
                kind, val = "set-bits", d[ops[pre.index(0x44)][0] + 1]
            elif 0xE0 in pre and 0x54 in pre:
                kind, val = "clr-bits", d[ops[pre.index(0x54)][0] + 1]
            elif 0x74 in pre:
                kind, val = "write", d[ops[pre.index(0x74)][0] + 1]
            elif 0xE4 in pre:
                kind, val = "write", 0
            elif 0xE0 in pre:
                kind = "read-modify-write"
            else:
                kind = "write-computed"
        elif 0xE0 in seq:
            kind = "read"
        acc.setdefault(reg, []).append((a, kind, val))
    return acc


def main():
    write = "--write" in sys.argv
    names = regs_h()
    out = ["# XDATA / SFR access map",
           "",
           "Generated by `tools/xdata_access_map.py`. Regenerate rather than",
           "editing. For every address either stock image touches, this lists",
           "every access site, its direction, and the constant where one is",
           "statically determinable.",
           "",
           "A register NAME is not an explanation; this is what the firmware",
           "does with each one. `set-bits` and `clr-bits` are read-modify-write",
           "with OR/AND of the given mask.",
           ""]
    for image in ("rev20", "rev22"):
        acc = analyse(image)
        out += [f"## {image}", "", f"{len(acc)} addresses touched.", ""]
        for reg in sorted(acc):
            nm = names.get(reg, "(unnamed)")
            out.append(f"### 0x{reg:04X}  {nm}")
            for a, kind, val in sorted(acc[reg]):
                v = "" if val is None else f" 0x{val:02X}"
                out.append(f"    0x{a:04X}  {kind}{v}")
            out.append("")
        print(f"{image}: {len(acc)} addresses, "
              f"{sum(len(v) for v in acc.values())} access sites")
    if write:
        p = os.path.join(DISASM, "XDATA_ACCESS_MAP.md")
        open(p, "w").write("\n".join(out) + "\n")
        print("wrote", p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
