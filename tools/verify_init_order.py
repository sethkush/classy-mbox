#!/usr/bin/env python3
"""
verify_init_order -- compare the ORDER of mboxfw's hw_init register writes
against both stock images' equivalent boot-init function.

Why this exists
---------------
Every other gate answers a set question: which registers are written, which
bits, with which values, and (since verify_reachability) whether the write is
reached at boot. None answers a SEQUENCE question. `WHAT_REMAINS_UNKNOWN.md` §5
names ordering as the dimension still unchecked, and the delay-elision bug
(`FINDING_delay_calls_elided.md`) lived in exactly that dimension: the values
written by hw_init's panel sequence matched stock exactly, while the gap stock
leaves between them had been deleted by the compiler.

Ordering matters concretely on this part. GLOBCTL bit 0 (CPTEN) must be set
AFTER the CPTCNF/CPTBR/CPTCTL codec-port registers are configured, not before --
hw_init.c says so in a comment, and nothing checked it. Enabling a peripheral
before configuring it is a class of bug that a set-comparison cannot see.

Correspondence
--------------
    Rev 20  hw_master_init      @ 0x08CB
    Rev 22  hw_clock_codec_init @ 0x07EC

Not assumed from the names, which are ours. Both are the second-action writer of
USBCTL 0xFFFC (0x08D0 / 0x07F1), and the surrounding functions pair up the same
way: suspend 0x0526/0x0525, main 0x0A95/0x0A3F, rstr 0x0F43/0x0F64.

Method
------
Extract each side's writes in program order, then compare the relative order of
every register both sides write. Reported as INVERSIONS: pairs (A, B) where
stock writes A before B and mboxfw writes B before A. A register written more
than once uses its FIRST write.

First-touch keying is NOT sufficient on its own, and saying otherwise here was
wrong: GLOBCTL is written twice for unrelated reasons (bit 1 early, CPTEN last),
so its first touch is the early one and moving CPTEN to before the codec block
changed nothing in the comparison. A mutation test caught this gate failing to
catch the exact property the paragraph above claims it protects. CPTEN ordering
is therefore asserted explicitly, in check_cpten_last().

Inversions that are understood are listed in ORDER_EXEMPT with a reason;
anything else fails. Registers only one side writes are ignored here -- that is
diff_vs_rev20's job, and mixing the two questions is what made earlier gates
hard to reason about.

    python3 tools/verify_init_order.py          # gate
    python3 tools/verify_init_order.py --dump   # both sequences side by side
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent
DISASM = ROOT / "firmware_stock" / "disasm"
BUILD = ROOT / "mboxfw" / "build"

STOCK_INIT = {"rev20": 0x08CB, "rev22": 0x07EC}

# Order differences that are understood and deliberate. Each key is the
# unordered pair; the value says why swapping them is harmless. Anything NOT
# listed here fails, which is the point -- this is the ordering equivalent of
# rev20_diff_justifications.md, not a way to silence the gate.
#
# mboxfw's hw_init runs: timers -> IE/IP -> ports -> MEMCFG -> codec -> GLOBCTL.
# Stock runs:            MEMCFG -> ports -> timers -> IE/IP -> codec -> GLOBCTL.
# The codec block and GLOBCTL agree, which is the part that matters.
ORDER_EXEMPT = {
    # GLOBCTL's first touch differs DELIBERATELY and the reason is a hardware
    # measurement, not an omission: stock writes GLOBCTL = 0x06 before the codec
    # block, mboxfw must NOT, because setting bit 1 after usb_init() makes the
    # device silent on USB (build 0x0010 silent vs 0x0011 attaching, bisected
    # 2026-07-29). So mboxfw's first GLOBCTL touch is CPTEN, after the block.
    # See the block comment in hw_init.c and FINDING_globctl_bit1_missed.md.
    frozenset(("GLOBCTL", "CPTCNF1")):
        "deliberate: GLOBCTL bit 1 after usb_init() silences USB (measured)",
    frozenset(("GLOBCTL", "CPTCNF2")):
        "deliberate: GLOBCTL bit 1 after usb_init() silences USB (measured)",
    frozenset(("GLOBCTL", "CPTCNF3")):
        "deliberate: GLOBCTL bit 1 after usb_init() silences USB (measured)",
    frozenset(("GLOBCTL", "CPTCNF4")):
        "deliberate: GLOBCTL bit 1 after usb_init() silences USB (measured)",
    frozenset(("GLOBCTL", "CPTSTA")):
        "deliberate: GLOBCTL bit 1 after usb_init() silences USB (measured)",
    frozenset(("GLOBCTL", "CPTRXCNF2")):
        "deliberate: GLOBCTL bit 1 after usb_init() silences USB (measured)",
    frozenset(("GLOBCTL", "CPTRXCNF3")):
        "deliberate: GLOBCTL bit 1 after usb_init() silences USB (measured)",
    frozenset(("GLOBCTL", "CPTRXCNF4")):
        "deliberate: GLOBCTL bit 1 after usb_init() silences USB (measured)",
    frozenset(("MEMCFG", "IE")):
        "MEMCFG |= 0x01 sets SDW, which the boot ROM's UtilResetCPU already "
        "set; the write is idempotent, so its position carries no state",
    frozenset(("MEMCFG", "P3")):   "same: SDW is already set, write idempotent",
    frozenset(("MEMCFG", "TH0")):  "same: SDW is already set, write idempotent",
    frozenset(("MEMCFG", "TMOD")): "same: SDW is already set, write idempotent",
    frozenset(("P3", "IE")):
        "EA stays clear for all of hw_init -- main.c calls hw_init() at :238 "
        "and sets EA = 1 only at :264 -- so unmasking individual IE bits "
        "before the port pins are driven cannot deliver an interrupt",
    frozenset(("P3", "TH0")):
        "port state and timer reload are independent; no timer runs yet "
        "(hw_init writes TCON = 0, and TR0 is set later in main)",
    frozenset(("P3", "TMOD")):     "same: no timer runs during hw_init",
    frozenset(("TH0", "TMOD")):
        "reload-then-mode vs mode-then-reload is equivalent while the timer "
        "is stopped (TCON = 0 throughout hw_init)",
}

INSN = re.compile(r"^CODE:([0-9a-f]{4})\s+([0-9a-f]+)\s+([A-Z][A-Z0-9]*)")
FUNC_HDR = re.compile(r"^; ======== FUNCTION .* @ CODE:([0-9a-f]{4})")

# Core SFRs we care about ordering for, by direct address.
CORE = {0x87: "PCON", 0x88: "TCON", 0x89: "TMOD", 0x8A: "TL0", 0x8B: "TL1",
        0x8C: "TH0", 0x8D: "TH1", 0x90: "P1", 0xA8: "IE", 0xB0: "P3",
        0xB8: "IP"}
# Bit-addressable SFR bytes: a bit address >= 0x80 belongs to SFR (bit & 0xF8).
BIT_BASES = {0x88: "TCON", 0x90: "P1", 0xA8: "IE", 0xB0: "P3", 0xB8: "IP"}


def regs_h():
    out = {}
    for line in (ROOT / "mboxfw" / "include" / "regs.h").read_text().splitlines():
        m = re.match(r"#define\s+(\w+)\s+XDATA\(0x([0-9A-Fa-f]{4})\)", line.strip())
        if m:
            out[int(m.group(2), 16)] = m.group(1)
    return out


NAMES = regs_h()


def name(addr, xdata=True):
    if xdata:
        return NAMES.get(addr, "0x%04X" % addr)
    return CORE.get(addr, "SFR_0x%02X" % addr)


def stock_sequence(image):
    """
    Ordered [(site, register-name)] written by the boot-init function.

    XDATA writes come from xdata_access_map.analyse() rather than a fresh scan.
    That classifier already handles every store idiom in these images --
    including stores delegated to a helper, which is how Rev 20 writes
    CPTRXCNF4 (`MOV DPTR,#0xffd4 / MOV A,#3 / LCALL 0x0deb` at 0x0929). A
    naive "look for MOVX @DPTR,A" scan reported that register as never written
    by stock, which contradicted hw_init.c's own citation. Reusing the
    self-tested classifier is what keeps the two tools from disagreeing.

    Core-SFR (direct-addressed) writes are decoded here, since analyse() is
    XDATA-only by design.
    """
    sys.path.insert(0, str(Path(__file__).parent))
    from xdata_access_map import analyse            # noqa: E402

    data = (ROOT / "firmware_stock" / f"{image}_firmware_code.bin").read_bytes()
    start = STOCK_INIT[image]
    ins, bounds = [], []
    for line in (DISASM / f"{image}_ghidra.txt").read_text().splitlines():
        h = FUNC_HDR.match(line)
        if h:
            bounds.append(int(h.group(1), 16))
            continue
        m = INSN.match(line)
        if m:
            ins.append(int(m.group(1), 16))
    end = min([b for b in sorted(bounds) if b > start], default=0xFFFF)

    # (a) Helper-delegated stores ONLY, from the self-tested classifier. Its
    # direct-store sites are left out because the linear walk below finds those
    # too, and counting both would list every register twice (analyse() reports
    # the DPTR-load address, the walk reports the MOVX).
    seq = []
    for reg, sites in analyse(image).items():
        for site, kind, _val in sites:
            if start <= site < end and "via-helper" in kind:
                seq.append((site, name(reg)))

    # (b) Direct MOVX stores, from a LINEAR walk that tracks DPTR across the
    # whole function. analyse() looks a bounded number of instructions ahead of
    # each `MOV DPTR,#imm`, which cannot connect a load to a store an arbitrary
    # distance later. Rev 20 loads DPTR = 0xFFB0 at 0x08D4 and then, 27
    # instructions later, does `INC DPTR / MOV A,#6 / MOVX @DPTR,A` at 0x08FB --
    # GLOBCTL = 0x06. Every windowed scanner in this repo misses that write, and
    # it was dismissed in rev20_diff_justifications.md as a scanner artifact on
    # the strength of the direct form not existing. Within one straight-line
    # init function a linear walk is exact where a window is not.
    seen = {s for s, _r in seq}
    dptr = None
    for a in ins:
        if not (start <= a < end):
            continue
        op = data[a]
        if op == 0x90:
            dptr = (data[a + 1] << 8) | data[a + 2]
        elif op == 0xA3:                                # INC DPTR
            if dptr is not None:
                dptr += 1
        elif op == 0x15 and data[a + 1] == 0x82:        # DEC DPL
            if dptr is not None:
                dptr -= 1
        elif op == 0x75 and data[a + 1] == 0x82:        # MOV DPL,#imm
            if dptr is not None:
                dptr = (dptr & 0xFF00) | data[a + 2]
        elif op == 0xF0:                                # MOVX @DPTR,A
            if dptr is not None and a not in seen:
                seq.append((a, name(dptr)))

    for a in ins:
        if not (start <= a < end):
            continue
        op = data[a]
        if op in (0x75, 0x43, 0x53):                    # MOV/ORL/ANL dir,#imm
            d = data[a + 1]
            if d in CORE:
                seq.append((a, name(d, xdata=False)))
        elif op in (0xD2, 0xC2):                        # SETB/CLR bit
            b = data[a + 1]
            if b >= 0x80 and (b & 0xF8) in BIT_BASES:
                seq.append((a, BIT_BASES[b & 0xF8]))
    return sorted(seq)


RST_DPTR = re.compile(r"mov\s+dptr,#0x([0-9a-f]{4})")
RST_DIR = re.compile(r"\b(?:mov|orl|anl)\s+_(\w+)\s*,\s*#")
RST_BIT = re.compile(r"\b(?:setb|clr)\s+_(\w+)\b")
# SDCC bit symbol -> containing SFR, for the bits hw_init touches.
BIT_OWNER = {"TR0": "TCON", "TR1": "TCON", "IT0": "TCON", "IT1": "TCON",
             "EA": "IE", "EX0": "IE", "ET0": "IE", "EX1": "IE", "ET1": "IE"}


def mboxfw_sequence(fn="_hw_init"):
    """
    Ordered [(line, register-name, immediate)] written inside `fn`.

    The immediate is whatever last landed in A before the store (`mov a,#imm`
    or `orl a,#imm`), or None if computed. It is needed to tell GLOBCTL's two
    distinct writes apart -- bit 1 early, CPTEN last.
    """
    seq, dptr, inside = [], None, False
    acc = None                       # last immediate loaded into / ORed with A
    regmask = {}                     # rN -> immediate, for SDCC's ar7 routing
    for p in sorted(BUILD.glob("*.rst")):
        for raw in p.read_text(errors="ignore").splitlines():
            m = re.match(r"^\s+[0-9A-F]{6}\s+\d+\s+(_\w+):\s*$", raw)
            if m:
                inside = (m.group(1) == fn)
                continue
            if not inside:
                continue
            low = raw.lower()
            d = RST_DPTR.search(low)
            if d:
                dptr = int(d.group(1), 16)
                continue
            # SDCC walks adjacent registers by adjusting DPL and keeping DPH.
            # hw_init's codec block is written that way -- CPTCNF1 down to
            # CPTSTA at 0xFFE0..0xFFDC with `dec dpl` between stores -- so a
            # parser that only tracks full DPTR loads reports five writes to
            # CPTCNF1 instead of one each to five different registers.
            if dptr is not None:
                if re.search(r"\bdec\s+dpl\b", low):
                    dptr -= 1
                    continue
                if re.search(r"\binc\s+dptr\b", low):
                    dptr += 1
                    continue
                dl = re.search(r"\bmov\s+dpl,#0x([0-9a-f]{2})", low)
                if dl:
                    dptr = (dptr & 0xFF00) | int(dl.group(1), 16)
                    continue
            am = re.search(r"\b(?:mov|orl|anl)\s+a,#0x([0-9a-f]{1,2})", low)
            if am:
                acc = int(am.group(1), 16)
                continue
            # SDCC sometimes routes an RMW mask through a register instead of A
            # (`orl ar7,#0x02` ... `mov a,r7`), which it does when it fuses two
            # adjacent RMWs on neighbouring addresses. Missing this form made
            # the CPTEN check report "unverifiable" instead of naming the real
            # violation, so the gate failed for the wrong reason.
            rm = re.search(r"\b(?:mov|orl|anl)\s+ar(\d),#0x([0-9a-f]{1,2})", low)
            if rm:
                regmask[rm.group(1)] = int(rm.group(2), 16)
                continue
            mv = re.search(r"\bmov\s+a,r(\d)\b", low)
            if mv and mv.group(1) in regmask:
                acc = regmask[mv.group(1)]
                continue
            if "movx" in low and "@dptr,a" in low:
                if dptr is not None:
                    seq.append((raw.strip()[:6], name(dptr), acc))
                acc = None
                continue
            b = RST_BIT.search(raw)
            if b and b.group(1) in BIT_OWNER:
                seq.append((raw.strip()[:6], BIT_OWNER[b.group(1)], None))
                continue
            dr = RST_DIR.search(raw)
            if dr and dr.group(1) in CORE.values():
                seq.append((raw.strip()[:6], dr.group(1), None))
    return seq


def check_cpten_last(mbox_events):
    """
    CPTEN (GLOBCTL bit 0) must be set AFTER every codec-port register.

    Checked explicitly rather than inferred from the stock-vs-mboxfw sequence
    diff, because that diff keys on a register's FIRST write and GLOBCTL is
    written twice for different reasons (bit 1 early, CPTEN last). With
    first-touch keying alone, moving CPTEN to before the codec block changed
    nothing in the comparison -- a mutation test caught the gate failing to
    catch exactly the property its docstring claims to protect.
    """
    cpt = [i for i, (_s, r, _op) in enumerate(mbox_events)
           if r.startswith("CPT")]
    cpten = [i for i, (_s, r, op) in enumerate(mbox_events)
             if r == "GLOBCTL" and op is not None and op & 0x01]
    if not cpt or not cpten:
        return ("CPTEN ordering unverifiable: found %d codec-port write(s) and "
                "%d GLOBCTL bit-0 set(s)" % (len(cpt), len(cpten)))
    if min(cpten) < max(cpt):
        return ("GLOBCTL bit 0 (CPTEN) is set at event %d, before the last "
                "codec-port write at event %d -- the codec port is enabled "
                "while still being configured" % (min(cpten), max(cpt)))
    return None


def first_index(seq):
    out = {}
    for i, item in enumerate(seq):
        out.setdefault(item[1], i)
    return out


def main():
    mbox = mboxfw_sequence()
    if not mbox:
        print("INIT-ORDER FAIL: no writes found in _hw_init -- parser or build "
              "is wrong, and a pass would be vacuous")
        return 1

    mi = first_index(mbox)
    fails = []
    cpten = check_cpten_last(mbox)
    if cpten:
        fails.append(cpten)
    for image in ("rev20", "rev22"):
        stock = stock_sequence(image)
        if not stock:
            print(f"INIT-ORDER FAIL: no writes extracted from {image} "
                  f"0x{STOCK_INIT[image]:04X}")
            return 1
        si = first_index(stock)
        common = sorted(set(si) & set(mi))
        if "--dump" in sys.argv:
            print(f"\n=== {image} 0x{STOCK_INIT[image]:04X} "
                  f"({len(stock)} writes) ===")
            for s, r in stock:
                print(f"  0x{s:04X}  {r}{'' if r in mi else '   (mboxfw: -)'}")
            print(f"=== mboxfw _hw_init ({len(mbox)} writes) ===")
            for s, r, op in mbox:
                v = "" if op is None else f" (0x{op:02X})"
                print(f"  {s}  {r}{v}"
                      f"{'' if r in si else f'   ({image}: -)'}")
            print(f"common registers: {len(common)}")
        inversions = [(a, b) for i, a in enumerate(common)
                      for b in common[i + 1:]
                      if (si[a] < si[b]) != (mi[a] < mi[b])]
        unexplained = [p for p in inversions
                       if frozenset(p) not in ORDER_EXEMPT]
        for a, b in unexplained:
            first_s, first_m = (a, b) if si[a] < si[b] else (b, a)
            fails.append(f"{image}: stock writes {first_s} before {first_m}; "
                         f"mboxfw writes {first_m} before {first_s} -- add a "
                         f"reason to ORDER_EXEMPT or fix the order")
        exempt = len(inversions) - len(unexplained)
        if not unexplained:
            print(f"  {image}: {len(common)} common register(s); "
                  f"{exempt} known-benign inversion(s), none unexplained")

    if "--dump" in sys.argv:
        return 0
    for f in fails:
        print("INIT-ORDER FAIL:", f)
    if fails:
        return 1
    print("INIT-ORDER PASS: hw_init write order agrees with both stock images")
    return 0


if __name__ == "__main__":
    sys.exit(main())
