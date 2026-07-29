#!/usr/bin/env python3
"""
byte_census -- account for EVERY byte of both stock images as one of:
instruction, identified const-data block, or erase padding.

The annotation ledger counts *locations* (call targets, registers, IRAM bytes
and bits). That is not the same as accounting for every byte, and the
difference hid two blind spots: table-driven dispatch targets reached through
`JMP @A+DPTR` were never in the call denominator, and 561 bytes of Rev 20
const data sat outside all four categories. This tool closes the gap by
partitioning the image itself, where nothing can hide.

Padding is only called padding when it IS 0xFF. Anything else is either an
instruction the listing decoded or a data run that has to be named here.

    python3 tools/byte_census.py            # summary
    python3 tools/byte_census.py --runs     # every unclaimed run, with bytes
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware_stock")
DISASM = os.path.join(FW, "disasm")
INSN = re.compile(r"^CODE:([0-9a-f]{4})\s+([0-9a-f]+)\s+([A-Z][A-Z0-9]*)")

# Const-data runs, identified and decoded elsewhere. Each entry must cite the
# document that decodes it -- a name alone is not an account.
DATA_BLOCKS = {
    "rev20": [
        (0x0596, 402, "USB descriptor block", "disasm/rev20_descriptors_decoded.md"),
        (0x0C93,  74, "VECINT dispatch table", "decomp/gen_data_blocks.py"),
        (0x0F9C,  40, "?C_INITSEG initialised-data table", "decomp/gen_data_blocks.py"),
        (0x011F,  37, "request-code dispatch table: 11 x (16-bit BE handler, code)",
                      "disasm/DISPATCH_TABLE_011F.md"),
        (0x0A48,   8, "power-of-two bit-mask table 01 02 04 ... 80",
                      "disasm/DISPATCH_TABLE_011F.md"),
    ],
    "rev22": [
        (0x057D, 402, "USB descriptor block (Ghidra mis-decodes 3 bytes inside "
                      "it as instructions, so it appears as 4 runs)",
                      "disasm/DISPATCH_TABLE_011F.md"),
        (0x0969,   8, "power-of-two bit-mask table",
                      "disasm/DISPATCH_TABLE_011F.md"),
        (0x0C7D,  74, "VECINT dispatch table", "decomp/gen_data_blocks.py"),
        (0x0FBA,  36, "?C_INITSEG initialised-data table",
                      "decomp/gen_data_blocks.py"),
    ],
}


def load(image):
    d = open(os.path.join(FW, f"{image}_firmware_code.bin"), "rb").read()
    cov = set()
    for line in open(os.path.join(DISASM, f"{image}_ghidra.txt")):
        m = INSN.match(line)
        if m:
            a, n = int(m.group(1), 16), len(m.group(2)) // 2
            cov.update(range(a, a + n))
    return d, cov


def runs_of(d, cov):
    out, i = [], 0
    while i < len(d):
        if i in cov:
            i += 1
            continue
        j = i
        while j < len(d) and j not in cov:
            j += 1
        out.append((i, j - i))
        i = j
    return out


def main():
    show = "--runs" in sys.argv
    rc = 0
    for image in ("rev20", "rev22"):
        d, cov = load(image)
        blocks = DATA_BLOCKS.get(image, [])
        claimed = set()
        for a, n, _w, _doc in blocks:
            claimed.update(range(a, a + n))
        insn = len(cov)
        pad = data = unknown = 0
        unk_runs = []
        for a, n in runs_of(d, cov):
            for k in range(a, a + n):
                if k in claimed:
                    data += 1
                elif d[k] == 0xFF:
                    pad += 1
                else:
                    unknown += 1
            # report contiguous unknown stretches
            s = None
            for k in range(a, a + n + 1):
                bad = (k < a + n) and (k not in claimed) and (d[k] != 0xFF)
                if bad and s is None:
                    s = k
                elif not bad and s is not None:
                    unk_runs.append((s, k - s))
                    s = None
        print(f"=== {image}: {len(d)} bytes ===")
        print(f"  instructions            {insn:5d}  ({100.0*insn/len(d):5.1f}%)")
        print(f"  identified const data   {data:5d}  ({len(blocks)} block(s))")
        print(f"  0xFF erase padding      {pad:5d}")
        print(f"  UNACCOUNTED             {unknown:5d}  in {len(unk_runs)} run(s)")
        if unknown:
            rc = 1
            for a, n in unk_runs:
                print(f"    0x{a:04X}  {n:4d} bytes"
                      + (f"  {d[a:a+min(n,12)].hex()}" if show else ""))
        print()
    print("Non-zero exit means some non-0xFF byte is neither an instruction nor"
          "\na named data block. Padding counts as accounted only when it is"
          "\nactually 0xFF.")
    return rc


if __name__ == "__main__":
    sys.exit(main())
