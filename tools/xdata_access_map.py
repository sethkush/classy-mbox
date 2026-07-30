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
it cannot drift from the image.

It is NOT, however, self-validating, and the line that used to sit here -- "it is
complete, so it cannot flatter" -- was false. Completeness of *sites* is easy:
every `MOV DPTR,#imm` is found by construction. Correctness of *direction* is
not, because it depends on the idiom table below being right, and on 2026-07-29
that table produced 26 wrong entries: 23 pure reads labelled as writes (the
whole SETUP-packet buffer 0xFF28-0xFF2C, which the UBM writes and the firmware
only ever reads), and 3 writes labelled as reads. A generated document is only
as honest as its classifier; treat a surprising direction here as a bug in this
file until the listing is read by hand.

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
    80 xx               SJMP: FOLLOWED, not walked past. Keil reaches a shared
                        store tail by short jump as well as by 12/02, and those
                        hops chain (Rev 22 0x0F9D SJMPs to 0x0FB6, which LCALLs
                        the bare-MOVX helper 0x0B2C). Not following it made the
                        walk run into unrelated instructions, find no F0, and
                        report a pure READ -- the same misclassification the
                        12 xxxx idiom above was added to fix, one opcode short.
                        It mislabelled OEPCNF0 |= 0x20 (stall EP0-OUT) and
                        IEPDCNTX0 = 0x01 as reads.

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

    def helper_stores(tgt, depth=3):
        """
        Does the routine at `tgt` store to the CALLER's DPTR?

        Only true if it reaches `MOVX @DPTR,A` before loading DPTR itself.
        Rev 20 0x0B5F is the counter-example that makes this necessary: it
        opens with `MOV DPTR,#0xff6b`, so it is an EP0 ack tail and says
        nothing about the register the caller had in DPTR. Reaching it from
        the wValue read at 0x02BD does not make that read a write.
        """
        if depth <= 0 or tgt not in idx:
            return False
        for k in range(idx[tgt], min(idx[tgt] + WINDOW, len(starts))):
            a2 = starts[k][0]
            op2 = d[a2]
            if op2 == 0x90:               # loads its own DPTR: not our store
                return False
            if op2 == 0xF0:
                return True
            if op2 == 0x22:               # RET without storing
                return False
            if op2 in (0x12, 0x02, 0x80):
                nxt = ((d[a2 + 1] << 8) | d[a2 + 2]) if op2 != 0x80 else \
                    a2 + 2 + (d[a2 + 1] - 256 if d[a2 + 1] > 127 else d[a2 + 1])
                return helper_stores(nxt, depth - 1)
        return False
    for i, (a, _n) in enumerate(starts):
        if d[a] != 0x90:
            continue
        reg = (d[a + 1] << 8) | d[a + 2]
        ops = []
        j = i + 1
        budget = WINDOW
        hops = 4                              # SJMP chains, bounded
        branched = False                      # a conditional branch was seen
        while budget > 0 and j < len(starts):
            addr, _ln = starts[j]
            op = d[addr]
            if op == 0x90:                    # DPTR reloaded: stop
                break
            ops.append((addr, op))
            if op in (0xF0, 0x93, 0x73, 0xA4):   # write / MOVC / jump table
                break
            if op in (0x12, 0x02):            # write delegated to a helper
                break                         # 0x02 = tail-call into a store
            if op == 0x80:                    # SJMP: follow it, don't walk past
                # ...but ONLY as a store-tail hop. Following an SJMP naively
                # invents a write out of a pure read: Rev 22 0x0177 reads
                # SETPACK_WVAL_H, CJNEs on the descriptor type, then SJMPs to
                # the matching arm -- control flow, not a store tail.
                #
                # Two criteria reject that. `branched` is the narrow one: a
                # store tail carries A to a shared MOVX and never sits behind a
                # conditional branch. helper_stores() is the general one, and
                # on both stock images it subsumes `branched` entirely --
                # removing this clause changes no entry in the generated map,
                # and no self-test site pins it. Kept as a second, independent
                # criterion, not because it is currently doing work.
                if hops <= 0 or branched:
                    break
                rel = d[addr + 1]
                tgt = addr + 2 + (rel - 256 if rel > 127 else rel)
                if tgt not in idx or not helper_stores(tgt):
                    break
                hops -= 1
                j = idx[tgt]
                budget -= 1
                continue
            # JZ/JNZ/JC/JNC/JB/JNB/JBC/CJNE/DJNZ -- see the SJMP note above.
            if op in (0x60, 0x70, 0x40, 0x50, 0x20, 0x30, 0x10, 0xD5) \
                    or 0xB4 <= op <= 0xBF or 0xD8 <= op <= 0xDF:
                branched = True
            j += 1
            budget -= 1
        kind, val = "no-access-found", None
        seq = [o for _x, o in ops]

        # A call/tail-jump only counts as delegating OUR store if the routine
        # it reaches actually stores to the caller's DPTR. Rev 20 0x02BD reads
        # wValue and then reaches 0x0B5F, which loads its own DPTR -- charging
        # that as a write to SETPACK_WVAL_L is wrong in both directions.
        helper = None
        if (0x12 in seq or 0x02 in seq) and 0xF0 not in seq:
            call_at = ops[seq.index(0x12 if 0x12 in seq else 0x02)][0]
            cand = (d[call_at + 1] << 8) | d[call_at + 2]
            if helper_stores(cand):
                helper = cand

        if 0x93 in seq:
            kind = "code-table (MOVC)"
        elif 0x73 in seq or 0xA4 in seq:
            kind = "jump table (3-byte LJMP entries)"
        elif helper is not None:
            # The value may arrive as MOV A,#imm, or as a read-modify-write
            # mask applied before the hop -- the helper only performs the MOVX.
            if 0xE0 in seq and 0x44 in seq:
                kind, val = "set-bits-via-helper 0x%04X" % helper, \
                            d[ops[seq.index(0x44)][0] + 1]
            elif 0xE0 in seq and 0x54 in seq:
                kind, val = "clr-bits-via-helper 0x%04X" % helper, \
                            d[ops[seq.index(0x54)][0] + 1]
            else:
                kind = "write-via-helper 0x%04X" % helper
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


# Sites read by hand in the 2026-07-29 classifier fix. Each one pins a distinct
# failure mode, so a regression in the idiom table trips at least one.
SELFTEST = [
    # store reached by SJMP -> LCALL -> bare MOVX; mask must survive the hops
    ("rev22", 0x0F9D, 0xFFA8, "set-bits-via-helper 0x0B2C", 0x20),
    # same helper, DIFFERENT mask -- the old map rendered these identically
    ("rev22", 0x1008, 0xFFA8, "set-bits-via-helper 0x0B2C", 0x08),
    ("rev20", 0x1010, 0xFFA8, "set-bits-via-helper 0x0B2B", 0x08),
    # SJMP behind a CJNE is control flow, NOT a store tail: must stay a read
    ("rev22", 0x0177, 0xFF2B, "read", None),
    # helper 0x0B5F loads its own DPTR, so this read must not become a write
    ("rev20", 0x02BD, 0xFF2A, "read", None),
    ("rev20", 0x02D1, 0xFF2A, "read", None),
    # pins helper_stores() in the LCALL path specifically: a plain LCALL to
    # 0x0B6E with no SJMP in between, and 0x0B6E loads DPTR from RAM 0x19:0x1A.
    # Without this site, reverting the LCALL-path check is masked by the SJMP
    # check and no self-test entry notices.
    ("rev20", 0x0173, 0xFF2B, "read", None),
    # genuine helper store the docstring cites -- must NOT regress to a read
    ("rev20", 0x034A, 0xFFDE, "write-via-helper 0x0FF4", 0xAC),
]


def selftest():
    cache = {}
    bad = []
    for img, site, reg, want_kind, want_val in SELFTEST:
        acc = cache.setdefault(img, analyse(img))
        got = [(k, v) for a, k, v in acc.get(reg, []) if a == site]
        if not got:
            bad.append(f"{img} 0x{site:04X}: no access recorded for 0x{reg:04X}")
            continue
        kind, val = got[0]
        if kind != want_kind or val != want_val:
            bad.append(f"{img} 0x{site:04X} 0x{reg:04X}: got {kind!r} "
                       f"val={val!r}, want {want_kind!r} val={want_val!r}")
    for b in bad:
        print("SELFTEST FAIL:", b)
    if bad:
        return 1
    print(f"ACCESS-MAP SELFTEST PASS: {len(SELFTEST)} hand-verified site(s)")
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
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
