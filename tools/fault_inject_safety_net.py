#!/usr/bin/env python3
"""
safety_net twin of tools/fault_inject.py.

Corrupt one random byte of the compiled safety_net.ihx at a time,
run in the sim, categorize the outcome. Same intent as the mboxfw
twin: surface bit-flips that let the firmware complete to its idle
loop but with USBCTL's CONN bit not set (silent, un-recoverable
brick).

Differences from the mboxfw fault_inject:
  - No phase-canary check. safety_net doesn't emit canaries; adding
    them would inflate the ~1 KB image we deliberately keep small.
  - Loop entry resolved from _main's tail sjmp (same heuristic as
    sim_smoke_safety_net.sh), not by grepping for a specific 00NNN$
    label — that label number varies across builds.
  - Sim timeout bumped to 8s (safety_net's 65k-iter settle loop
    eats real wall-clock in s51). Iterations default is reduced
    since each run is slower.

Categories:
  REACHED_OK  - hit loop_entry AND USBCTL bit 7 set (CONN)
  REACHED_BAD - hit loop_entry but USBCTL bit 7 CLEAR — the
                dangerous silent-brick case we're hunting
  HUNG        - didn't reach loop_entry (timed out)
  TRAP        - sim halted with an error message
"""

import os
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path


REPO = Path(__file__).parent.parent
IHX  = REPO / "safety_net" / "build" / "safety_net.ihx"
RST  = REPO / "safety_net" / "build" / "main.rst"

# Each sim run of safety_net is ~2-4s in s51 (dominated by the settle
# loop). Keep the default iteration count small; bump via CLI arg or
# FAULT_SEED for a stress run.
DEFAULT_N = 20
SIM_TIMEOUT_S = 8


def parse_ihx_addrs(text: str) -> list[tuple[int, bytearray]]:
    records = []
    for line in text.splitlines():
        if not line.startswith(":"):
            continue
        n     = int(line[1:3], 16)
        addr  = int(line[3:7], 16)
        rtype = int(line[7:9], 16)
        if rtype == 0:
            records.append((addr, bytearray.fromhex(line[9:9 + 2 * n])))
        elif rtype == 1:
            break
    return records


def write_ihx(records: list[tuple[int, bytearray]], out_path: Path) -> None:
    with out_path.open("w") as f:
        for addr, data in records:
            n = len(data)
            body = bytes([n, (addr >> 8) & 0xFF, addr & 0xFF, 0x00]) + bytes(data)
            chksum = ((-sum(body)) & 0xFF)
            f.write(":" + body.hex().upper() + f"{chksum:02X}\n")
        f.write(":00000001FF\n")


def resolve_loop_entry() -> str:
    """
    Find the self-jump (`80 FE`) at the tail of _main. safety_net's
    idle loop is a `for(;;) {}` compiled to `sjmp .` (label pointing
    at itself). The label number (00XXX$) shifts across builds — we
    resolve by matching the address of the sjmp to the label right
    above it, while scanning inside the _main function's span only.
    """
    lines = RST.read_text().splitlines()
    in_main = False
    last_addr = None
    label_re = re.compile(r'^\s+([0-9A-F]{4,6})\s+\d+\s+00\d+\$:\s*$')
    sjmp_re  = re.compile(r'^\s+([0-9A-F]{4,6})\s+80\s+FE\b')
    main_re  = re.compile(r'^\s+[0-9A-F]{4,6}\s+\d+\s+_main:')
    for line in lines:
        if main_re.match(line):
            in_main = True
            continue
        if not in_main:
            continue
        m = label_re.match(line)
        if m:
            last_addr = m.group(1)
            continue
        m = sjmp_re.match(line)
        if m and m.group(1) == last_addr:
            return "0x" + m.group(1).lower()
    raise SystemExit("could not locate _main idle sjmp in safety_net main.rst")


def run_sim(ihx_path: Path, loop_entry: str,
            timeout_s: int = SIM_TIMEOUT_S) -> str:
    cmd = ["timeout", str(timeout_s), "s51", "-q", str(ihx_path)]
    stdin_text = (
        f"run 0 {loop_entry}\n"
        "dx 0xfffc 0xfffc\n"
        "q\n"
    )
    proc = subprocess.run(cmd, input=stdin_text, capture_output=True,
                          text=True)
    return proc.stdout + proc.stderr


def classify_run(out: str, loop_entry: str) -> str:
    """
    Same buckets as the mboxfw fault_inject twin, MINUS the canary
    check — safety_net has no phase canaries. REACHED_OK requires
    USBCTL bit 7 set; REACHED_BAD is the silent-brick case.
    """
    needle = f"stop at {loop_entry.lower()}"
    if needle not in out.lower():
        if "invalid" in out.lower() or "trap" in out.lower():
            return "TRAP"
        return "HUNG"
    m = re.search(r"^0xfffc\s+([0-9a-f]{2})", out, re.MULTILINE)
    usbctl = int(m.group(1), 16) if m else 0
    return "REACHED_OK" if (usbctl & 0x80) else "REACHED_BAD"


def main() -> int:
    if not IHX.exists() or not RST.exists():
        print("run `make -C safety_net` first", file=sys.stderr)
        return 2

    n_iterations = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_N
    seed = int(os.environ.get("FAULT_SEED", "42"))
    random.seed(seed)

    loop_entry = resolve_loop_entry()
    baseline_records = parse_ihx_addrs(IHX.read_text())
    byte_positions: list[tuple[int, int]] = []
    for ri, (_, data) in enumerate(baseline_records):
        for off in range(len(data)):
            byte_positions.append((ri, off))

    with tempfile.NamedTemporaryFile("w", suffix=".ihx", delete=False) as tf:
        base_ihx = Path(tf.name)
    write_ihx(baseline_records, base_ihx)
    baseline_out = run_sim(base_ihx, loop_entry)
    baseline_cls = classify_run(baseline_out, loop_entry)
    if baseline_cls != "REACHED_OK":
        print(f"BASELINE FAIL: unmutated safety_net classified as"
              f" {baseline_cls}", file=sys.stderr)
        print(baseline_out[-1000:], file=sys.stderr)
        return 1
    print(f"  baseline (no mutation): REACHED_OK"
          f" ({len(baseline_records)} records,"
          f" {sum(len(d) for _, d in baseline_records)} bytes)")
    print(f"  loop_entry: {loop_entry}")
    print(f"  iterations: {n_iterations}, seed: {seed},"
          f" sim timeout: {SIM_TIMEOUT_S}s")
    print()

    counters = {"REACHED_OK": 0, "REACHED_BAD": 0, "HUNG": 0, "TRAP": 0}
    bad_details = []

    for it in range(n_iterations):
        mutated = [(a, bytearray(d)) for a, d in baseline_records]
        ri, off = random.choice(byte_positions)
        original = mutated[ri][1][off]
        bit = random.randint(0, 7)
        mutated[ri][1][off] = original ^ (1 << bit)
        abs_addr = mutated[ri][0] + off

        with tempfile.NamedTemporaryFile("w", suffix=".ihx", delete=False) as tf:
            mpath = Path(tf.name)
        write_ihx(mutated, mpath)
        out = run_sim(mpath, loop_entry)
        cls = classify_run(out, loop_entry)
        counters[cls] += 1

        marker = {"REACHED_OK": ".", "REACHED_BAD": "!",
                  "HUNG":       "h", "TRAP":        "T"}[cls]
        sys.stdout.write(marker)
        sys.stdout.flush()
        if cls == "REACHED_BAD":
            bad_details.append((abs_addr, original, mutated[ri][1][off], bit))
        mpath.unlink(missing_ok=True)

    print()
    base_ihx.unlink(missing_ok=True)
    print()
    print("Legend: . = REACHED_OK, ! = REACHED_BAD, h = HUNG, T = TRAP")
    print()
    for k, v in counters.items():
        pct = 100.0 * v / n_iterations
        print(f"  {k:12s} : {v:3d}  ({pct:.0f}%)")
    print()

    if bad_details:
        print("UNSAFE mutations (reached loop but CONN bit cleared):")
        for addr, orig, new, bit in bad_details:
            print(f"  0x{addr:04X}  bit {bit}: 0x{orig:02X} → 0x{new:02X}")
        print()
        print("Each row is a bit-flip that did NOT hang the sim but left")
        print("safety_net idling with USBCTL CONN unset. Real-hardware")
        print("equivalent: firmware appears to boot but is silent on USB —")
        print("un-recoverable soft-brick.")

    return 1 if counters["REACHED_BAD"] > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
