#!/usr/bin/env python3
"""
#194 — every declared terminal and unit must name the measurement behind it.

verify_descriptors.py proves the bundle is internally CONSISTENT: sources
resolve, lengths add up, no duplicate IDs. It cannot tell that a declared
terminal corresponds to hardware that carries audio. This gate closes that gap
the same way check_citation_targets.py closes it for SFR writes -- by requiring
a citation and then checking the citation actually points somewhere.

The repo has hit this failure in both directions. The S/PDIF transmitter ran
from the first build with no descriptor (#187, found by inventory, not by a
gate). And #46 refused a Feature Unit for months because the mute pair measured
as ONE global gate -- declaring it then would have shipped a control that
silently muted the other stream; only #189 made it honest.

WHAT IS PARSED IS THE BUILT IMAGE, not descriptors.c. The question is what the
device tells a host, and a source comment cannot answer that -- the same reason
verify_descriptors walks the ROM.

Fails on:
  * a terminal or unit in the image with no row in terminal_evidence.md
  * a row whose ID is no longer declared (stale evidence outliving its subject)
  * a row citing a FINDING file that does not exist
  * a row whose declared type disagrees with the image

    python3 tools/check_terminal_evidence.py [--ihx PATH] [--map PATH]

Exit 0 = every declared terminal is backed, 1 = at least one is not.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "tools" / "terminal_evidence.md"
DECOMP = ROOT / "firmware_stock" / "decomp"

USB_DT_INTERFACE = 0x04
CS_INTERFACE = 0x24
SUBCLASS_AUDIOCONTROL = 0x01
AC_INPUT_TERMINAL = 0x02
AC_OUTPUT_TERMINAL = 0x03
AC_MIXER_UNIT = 0x04
AC_SELECTOR_UNIT = 0x05
AC_FEATURE_UNIT = 0x06

KIND = {AC_INPUT_TERMINAL: "IT", AC_OUTPUT_TERMINAL: "OT",
        AC_MIXER_UNIT: "MU", AC_SELECTOR_UNIT: "SU", AC_FEATURE_UNIT: "FU"}


def load_ihx(path):
    mem = {}
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line.startswith(":"):
            continue
        n = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        if int(line[7:9], 16) != 0:
            continue
        for i in range(n):
            mem[addr + i] = int(line[9 + 2 * i:11 + 2 * i], 16)
    return mem


def load_symbol(mapfile, want):
    for line in Path(mapfile).read_text().splitlines():
        m = re.match(r"\s*C:\s+([0-9A-Fa-f]{8})\s+_(\w+)\s", line)
        if m and m.group(2) == want:
            return int(m.group(1), 16)
    return None


def declared_units(mem, base):
    """Walk the config bundle and return {id: (kind, type_or_None)}.

    Only the AudioControl class-specific descriptors carry terminals and units,
    and they are self-delimiting by bLength, so this walk needs no length
    constant of its own -- which matters, because a length constant is exactly
    the thing that was wrong the last time these descriptors moved.
    """
    total = mem.get(base + 2, 0) | (mem.get(base + 3, 0) << 8)
    out = {}
    off = 0
    in_ac = False
    while off < total:
        blen = mem.get(base + off, 0)
        if blen == 0:
            break
        btype = mem.get(base + off + 1, 0)
        if btype == USB_DT_INTERFACE:
            # TRACK WHICH INTERFACE WE ARE INSIDE. UAC1 subtypes are namespaced
            # per interface class, and the two namespaces COLLIDE on the values
            # that matter here: AudioStreaming's FORMAT_TYPE is 0x02, the same
            # number as AudioControl's INPUT_TERMINAL. Walking the bundle
            # without this decodes the format descriptor as a terminal --
            # bFormatType reads as a terminal ID of 1 and
            # bNrChannels/bSubframeSize read as wTerminalType 0x0302. That is
            # exactly what this gate did on its first run, and what caught it
            # was the table's type cross-check disagreeing with the image.
            in_ac = mem.get(base + off + 6, 0) == SUBCLASS_AUDIOCONTROL
        elif btype == CS_INTERFACE and in_ac:
            sub = mem.get(base + off + 2, 0)
            if sub in KIND:
                tid = mem.get(base + off + 3, 0)
                ttype = None
                if sub in (AC_INPUT_TERMINAL, AC_OUTPUT_TERMINAL):
                    ttype = (mem.get(base + off + 4, 0)
                             | (mem.get(base + off + 5, 0) << 8))
                out[tid] = (KIND[sub], ttype)
        off += blen
    return out


def parse_table():
    rows = {}
    for line in TABLE.read_text().splitlines():
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) < 5 or not cells[0].isdigit():
            continue
        tid = int(cells[0])
        ttype = None if cells[2] in ("—", "-", "") else int(cells[2], 16)
        cite = cells[3].strip("`")
        rows[tid] = (cells[1], ttype, cite, cells[4])
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ihx", default=str(ROOT / "mboxfw" / "build" / "mboxfw.ihx"))
    ap.add_argument("--map", default=str(ROOT / "mboxfw" / "build" / "mboxfw.map"))
    args = ap.parse_args()

    if not Path(args.ihx).exists() or not Path(args.map).exists():
        print(f"not found: {args.ihx} / {args.map} (build mboxfw first)",
              file=sys.stderr)
        return 2

    base = load_symbol(args.map, "AppConfigDesc")
    if base is None:
        print("AppConfigDesc not in the map", file=sys.stderr)
        return 2

    declared = declared_units(load_ihx(args.ihx), base)
    rows = parse_table()
    errors = []

    for tid, (kind, ttype) in sorted(declared.items()):
        tname = "—" if ttype is None else f"0x{ttype:04X}"
        if tid not in rows:
            errors.append(
                f"{kind} id={tid} type={tname} is DECLARED but has no row in "
                f"tools/terminal_evidence.md. Every terminal a host can see "
                f"must name the measurement that proves the hardware behind "
                f"it; add the row or stop declaring the terminal.")
            continue
        rkind, rtype, cite, what = rows[tid]
        if rkind != kind:
            errors.append(f"id={tid} is a {kind} in the image but the table "
                          f"calls it a {rkind}")
        if ttype is not None and rtype is not None and rtype != ttype:
            errors.append(f"{kind} id={tid} is type {tname} in the image but "
                          f"0x{rtype:04X} in the table")
        if not (DECOMP / cite).exists():
            errors.append(f"{kind} id={tid} cites {cite}, which does not exist "
                          f"in firmware_stock/decomp/")
        if not what:
            errors.append(f"{kind} id={tid} has an empty measurement column")
        print(f"  {kind} id={tid} {tname:>7}  <- {cite}")

    for tid in sorted(set(rows) - set(declared)):
        errors.append(
            f"table has a row for id={tid} ({rows[tid][0]}) that the image no "
            f"longer declares. Stale evidence outliving its subject is how a "
            f"citation starts pointing at the wrong thing -- remove the row.")

    print()
    if errors:
        print(f"FAIL: {len(errors)} terminal(s) without sound evidence")
        for e in errors:
            print(f"  - {e}")
        return 1
    print(f"PASS: all {len(declared)} declared terminals and units cite a "
          f"measurement that exists")
    return 0


if __name__ == "__main__":
    sys.exit(main())
