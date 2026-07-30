#!/usr/bin/env python3
"""
verify_reachability -- prove that boot-critical writes are reached ON THE BOOT
PATH, not merely present somewhere in the image.

Why this exists
---------------
Every other gate in preflight.sh is whole-image: it asks "does the firmware
anywhere write this register / set this bit". None of them ask "is that write
reached on the path that needs it". `WHAT_REMAINS_UNKNOWN.md` §5 named this as
the one method gap the inventory did not close.

The gap is not hypothetical, and the TR0 bug is the exact shape of it. `TR0`
had never been set anywhere, Timer 0 never counted, and six gates stayed green.
After the fix there are two `TR0 = 1` sites: one in `main()` and one on the
resume path in `power.c`. Mutation-testing `sfr_direct_diff` then showed the
limit -- deleting the `main()` site ALONE leaves it green, because the resume
path still satisfies "somewhere in the image". But resume only runs after a
suspend, and a suspend requires an already-configured device, so a firmware
whose only `TR0` is on the resume path has no frame clock until the host
suspends and wakes it. That is a real defect no existing gate can see.

Method
------
Build a call graph from the SDCC .rst listings: `_name:` labels delimit
functions, `lcall _x` / `ljmp _x` are edges (ljmp covers SDCC's tail calls).
Entry points are `_main` and each `_isr_*` named in the vector table.

For each property below, find the functions containing the write, then check
reachability from `_main` in a graph with the resume entry point REMOVED. A
property satisfied only from `power.c` therefore fails, which is precisely the
mutation that slipped past before.

Interrupt handlers count as reachable: they are entered by hardware, not by a
call, and their edges are followed from the vector-table entries. What is
excluded is only the resume path.

    python3 tools/verify_reachability.py           # gate
    python3 tools/verify_reachability.py --graph   # print the reachable set
"""
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent
BUILD = ROOT / "mboxfw" / "build"

# The function that only runs after a bus suspend: it drives PCON.IDL and then
# re-initialises everything on wake. Removing it from the graph is what makes
# "reachable" mean "reachable on the boot path". It IS reachable from _main via
# _work_dispatch, so excluding it is the whole point -- naming a function that
# does not exist makes this gate pass vacuously, which is how the first version
# of this file was wrong.
RESUME_ENTRY = "_do_suspend"

# Interrupt handlers are entered by hardware. SDCC emits the vector table under
# a real label, so its ljmps parse as calls from here; treating this as a root
# makes every ISR (and everything an ISR calls) reachable, which is correct.
VECTOR_LABEL = "__interrupt_vect"

# Functions allowed to be emitted-but-uncalled, with the reason. Keep this
# list at zero-or-tiny: every entry is a place the orphan check is not looking.
ORPHAN_ALLOWED = {
    # Deliberately an empty body -- cs8427.c documents that natural instruction
    # timing at 12 MHz already meets the CS8427's SCL spec, so there is nothing
    # to emit and SDCC drops the call. Unlike the other three delays, this one
    # is a no-op BY DESIGN rather than by accident. The claim that no explicit
    # delay is needed rests on the comment, not on a measurement.
    "_bit_delay": "empty by design (cs8427.c) -- compiles to a bare ret",
}

LABEL = re.compile(r"^\s+[0-9A-F]{6}\s+\d+\s+(_\w+):\s*$")
# SDCC emits ";\t function <name>" before each real function body. That is the
# only reliable way to tell a function from a data symbol or a `_fn_PARM_2`
# argument slot, both of which are plain labels too.
FN_MARKER = re.compile(r";\s*function\s+(\w+)\s*$")
# The mnemonic tail sits after the line number, separated by a tab.
BODY = re.compile(r"^\s+[0-9A-F]{6}\s+[0-9A-F ]*\[[\d ]+\]\s+\d+\s+\t(.*)$")
CALL = re.compile(r"\b(?:lcall|ljmp)\s+(_\w+)")


# Bit properties: (label, bit symbol, containing SFR symbol, bit index).
#
# A bit counts as SET by any of: `setb <bit>`, `mov <SFR>,#imm` with the bit set
# in imm, or `orl <SFR>,#imm` likewise. Matching only `setb` is wrong and was
# wrong here first: hw_init enables both EX0 and ET0 with the byte write
# `IE = 0x03`, so an EX0 check that only looked for `setb _EX0` reported the
# boot path as missing it. This is the mirror of the trap sfr_direct_diff
# documents -- there, a byte write of 0x00 must NOT count as setting a bit; here,
# a byte write of 0x03 must count.
BIT_PROPERTIES = [
    ("Timer 0 running (TR0 set)",           "_TR0", "_TCON", 4),
    ("Interrupts globally enabled (EA set)", "_EA",  "_IE",   7),
    ("External interrupt 0 enabled (EX0)",   "_EX0", "_IE",   0),
    ("Timer 0 interrupt enabled (ET0)",      "_ET0", "_IE",   1),
]

# XDATA properties, matched by the DPTR load that reaches them.
XDATA_PROPERTIES = [
    ("USB attach (USBCTL 0xFFFD touched)",
     re.compile(r"mov\s+dptr,#0xfffd", re.I)),
]

# Properties satisfied by a function being reached at all.
FUNCTION_PROPERTIES = {
    "_usb_ep0_setup": "EP0 configured (usb_ep0_setup reached)",
    "_hw_init":       "hw_init reached",
    "_usb_init":      "usb_init reached",
    "_usb_attach":    "usb_attach reached",
}


def sets_bit(insns, bit_sym, sfr_sym, bit):
    """Does any instruction here SET `bit` of `sfr_sym`?"""
    mask = 1 << bit
    setb = re.compile(r"\bsetb\b\s+" + re.escape(bit_sym) + r"\b", re.I)
    byte = re.compile(r"\b(mov|orl)\b\s+" + re.escape(sfr_sym)
                      + r"\s*,\s*#(0x[0-9a-f]+|\d+)", re.I)
    for i in insns:
        if setb.search(i):
            return True
        m = byte.search(i)
        if m and int(m.group(2), 0) & mask:
            return True
    return False


def parse():
    """Return (graph, bodies, vectors, real_fns)."""
    graph, bodies, vectors, real = {}, {}, set(), set()
    for p in sorted(BUILD.glob("*.rst")):
        cur = None
        for raw in p.read_text(errors="ignore").splitlines():
            fm = FN_MARKER.search(raw)
            if fm:
                real.add("_" + fm.group(1))
                continue
            m = LABEL.match(raw)
            if m:
                cur = m.group(1)
                graph.setdefault(cur, set())
                bodies.setdefault(cur, [])
                continue
            b = BODY.match(raw)
            if not b:
                continue
            insn = b.group(1).strip()
            # The interrupt vector table sits below any function label: its
            # ljmps are hardware entry points, not calls.
            if cur is None:
                c = CALL.search(insn)
                if c:
                    vectors.add(c.group(1))
                continue
            bodies[cur].append(insn)
            c = CALL.search(insn)
            if c:
                graph.setdefault(cur, set()).add(c.group(1))
    return graph, bodies, vectors, real


def reach(graph, roots, excluded=frozenset()):
    seen, stack = set(), [r for r in roots if r not in excluded]
    while stack:
        fn = stack.pop()
        if fn in seen or fn in excluded:
            continue
        seen.add(fn)
        stack.extend(graph.get(fn, ()))
    return seen


def main():
    graph, bodies, vectors, real = parse()
    if "_main" not in graph:
        print("REACHABILITY FAIL: no _main in the build listings")
        return 1

    roots = {"_main"} | {v for v in vectors if v.startswith("_isr")}
    if VECTOR_LABEL in graph:
        roots.add(VECTOR_LABEL)
    else:
        print(f"REACHABILITY FAIL: no {VECTOR_LABEL} label found -- the ISR "
              f"entry points cannot be established, so a pass would be vacuous")
        return 1
    if RESUME_ENTRY not in graph:
        print(f"REACHABILITY FAIL: {RESUME_ENTRY} not in the build listings; "
              f"excluding a non-existent function makes this gate vacuous")
        return 1
    boot = reach(graph, roots, excluded={RESUME_ENTRY})
    whole = reach(graph, roots | {RESUME_ENTRY})

    if "--graph" in sys.argv:
        print(f"entry points: {sorted(roots)}")
        print(f"reachable on boot path ({len(boot)}):")
        for f in sorted(boot):
            print("   ", f)
        only = sorted(whole - boot)
        print(f"reachable ONLY via {RESUME_ENTRY} ({len(only)}):")
        for f in sorted(only):
            print("   ", f)
        return 0

    fails, checked = [], 0

    def report(label, on_boot, anywhere):
        if on_boot:
            return
        if anywhere:
            fails.append(f"{label}: satisfied in {anywhere} but NONE is "
                         f"reachable from _main with {RESUME_ENTRY} excluded "
                         f"-- resume-path-only, so it does not hold at boot")
        else:
            fails.append(f"{label}: not satisfied anywhere in the image")

    for label, bit_sym, sfr_sym, bit in BIT_PROPERTIES:
        checked += 1
        hit = [f for f in graph if sets_bit(bodies.get(f, ()), bit_sym,
                                            sfr_sym, bit)]
        report(label, sorted(f for f in hit if f in boot), sorted(hit))

    for label, rx in XDATA_PROPERTIES:
        checked += 1
        hit = [f for f in graph if any(rx.search(i) for i in bodies.get(f, ()))]
        report(label, sorted(f for f in hit if f in boot), sorted(hit))

    # Orphaned function bodies. A real function that is emitted but called from
    # nowhere is how the delay-elision bug hid: SDCC proved each delay helper
    # observationally pure, deleted every CALL SITE, and left the body in the
    # image -- so `grep` found the function, the listing looked normal, and the
    # delay simply never happened. Three were live at once (short_delay in the
    # boot panel sequence, inter_reg_delay x9 in the CS8427 init,
    # eeprom_write_hold after every EEPROM write).
    checked += 1
    called = {c for cs in graph.values() for c in cs}
    orphans = sorted(f for f in real
                     if f in graph and f not in called
                     and f not in roots and f != RESUME_ENTRY
                     and f not in ORPHAN_ALLOWED
                     and bodies.get(f))
    if orphans:
        fails.append(f"function bodies emitted but never called: {orphans} -- "
                     f"if these are busy-wait delays, their loop counter needs "
                     f"to be volatile or SDCC deletes the call site")

    for fn, label in FUNCTION_PROPERTIES.items():
        checked += 1
        if fn not in boot:
            where = "absent from the build" if fn not in graph \
                else f"present but only reachable via {RESUME_ENTRY}"
            fails.append(f"{label}: {fn} {where}")

    for f in fails:
        print("REACHABILITY FAIL:", f)
    if fails:
        return 1
    print(f"REACHABILITY PASS: {checked} boot-path propert(ies) verified over "
          f"{len(boot)} function(s) reachable from _main "
          f"({RESUME_ENTRY} excluded)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
