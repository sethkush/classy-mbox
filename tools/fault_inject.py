#!/usr/bin/env python3
"""
Fault-injection test: corrupt one random byte of the compiled mboxfw
.ihx at a time, run in the sim, verify one of:
  (a) firmware still reaches main loop with USBCTL CONN set (byte was
      in dead code / didn't matter)
  (b) firmware traps / halts / infinite-loops (sim times out)

What we're LOOKING for: corruption in a critical byte causing UNSAFE
behavior — a value assignment that clobbers a critical SFR, a jump
target that lands in the middle of an instruction and executes as a
different opcode with different effects, etc.

Case (b) is FINE for our purposes — the sim hangs means the real
device would hang too, and our early-USB / DFU recovery paths would
handle it.

Case (a) is FINE — corruption was in unused space.

The FAILURE mode we want to surface: firmware completes to main loop
but writes a WRONG value somewhere important (e.g., USBCTL to a value
that detaches from bus, EEPROM_APPCODE_UPDATING sticks, etc.).

For a first pass we just count: how many single-byte flips break the
firmware's reach-main-loop-with-CONN invariant vs how many are silent.
"""

import os
import random
import subprocess
import sys
import tempfile
import re
from pathlib import Path


REPO   = Path(__file__).parent.parent
IHX    = REPO / "mboxfw" / "build" / "mboxfw.ihx"
RST    = REPO / "mboxfw" / "build" / "main.rst"

# How many random flips to try. Each is a full sim run (~1-2s) so
# keep small for CI, bump up for a stress run.
DEFAULT_N = 30


def parse_ihx_addrs(text: str) -> list[tuple[int, bytearray]]:
    """Return list of (base_addr, bytes) for each data record."""
    records = []
    for line in text.splitlines():
        if not line.startswith(":"):
            continue
        n     = int(line[1:3], 16)
        addr  = int(line[3:7], 16)
        rtype = int(line[7:9], 16)
        if rtype == 0:
            data = bytearray.fromhex(line[9:9 + 2 * n])
            records.append((addr, data))
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
    for line in RST.read_text().splitlines():
        if "00102$:" in line:
            m = re.match(r"\s*([0-9A-Fa-f]{4,6})\s", line)
            if m:
                return "0x" + m.group(1)
    raise SystemExit("could not find loop entry in main.rst")


def run_sim(ihx_path: Path, loop_entry: str, timeout_s: int = 5) -> str:
    """Run sim for up to timeout_s. Return combined stdout+stderr."""
    cmd = ["timeout", str(timeout_s), "s51", "-q", str(ihx_path)]
    stdin_text = (
        f"run 0 {loop_entry}\n"
        "dx 0xfffc 0xfffc\n"
        "dx 0xfa00 0xfa05\n"
        "q\n"
    )
    proc = subprocess.run(cmd, input=stdin_text, capture_output=True,
                          text=True)
    return proc.stdout + proc.stderr


def classify_run(out: str, loop_entry: str) -> str:
    """
    Categorize a sim run's outcome:
      REACHED_OK   - hit loop_entry AND USBCTL bit 7 set AND canary 5 (a6) set
      REACHED_BAD  - hit loop_entry but USBCTL wrong OR canary missing
      HUNG         - timed out (didn't reach loop_entry)
      TRAP         - sim halted with an error message
    """
    needle = f"stop at {loop_entry.lower()}"
    reached = needle in out.lower()
    if not reached:
        if "invalid" in out.lower() or "trap" in out.lower():
            return "TRAP"
        return "HUNG"
    # extract USBCTL and last canary
    m = re.search(r"^0xfffc\s+([0-9a-f]{2})", out, re.MULTILINE)
    usbctl = int(m.group(1), 16) if m else 0
    m = re.search(r"^0xfa00\s+([0-9a-f\s]+?)\.", out, re.MULTILINE)
    canary5 = 0
    if m:
        parts = m.group(1).split()
        if len(parts) >= 6:
            canary5 = int(parts[5], 16)
    if (usbctl & 0x80) and canary5 == 0xA6:
        return "REACHED_OK"
    return "REACHED_BAD"


def main() -> int:
    if not IHX.exists() or not RST.exists():
        print("run `make` in mboxfw/ first", file=sys.stderr)
        return 2

    n_iterations = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_N
    seed = int(os.environ.get("FAULT_SEED", "42"))
    random.seed(seed)

    loop_entry = resolve_loop_entry()
    baseline_records = parse_ihx_addrs(IHX.read_text())

    # Flatten address→byte map so we can pick random positions.
    byte_positions: list[tuple[int, int]] = []   # (record_index, offset_in_record)
    for ri, (_, data) in enumerate(baseline_records):
        for off in range(len(data)):
            byte_positions.append((ri, off))

    # First, verify baseline PASSES.
    with tempfile.NamedTemporaryFile("w", suffix=".ihx", delete=False) as tf:
        base_ihx = Path(tf.name)
    write_ihx(baseline_records, base_ihx)
    baseline_out = run_sim(base_ihx, loop_entry)
    baseline_cls = classify_run(baseline_out, loop_entry)
    if baseline_cls != "REACHED_OK":
        print(f"BASELINE FAIL: unmutated firmware classified as {baseline_cls}",
              file=sys.stderr)
        print(baseline_out[-1000:], file=sys.stderr)
        return 1
    print(f"  baseline (no mutation): REACHED_OK ({len(baseline_records)}"
          f" records, {sum(len(d) for _, d in baseline_records)} bytes)")
    print(f"  iterations: {n_iterations}, seed: {seed}")
    print()

    counters = {"REACHED_OK": 0, "REACHED_BAD": 0, "HUNG": 0, "TRAP": 0}
    bad_details = []

    for it in range(n_iterations):
        # Deep copy
        mutated = [(a, bytearray(d)) for a, d in baseline_records]
        ri, off = random.choice(byte_positions)
        original = mutated[ri][1][off]
        # Flip a random bit
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
            bad_details.append((abs_addr, original, mutated[ri][1][off], cls, bit))
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
        print(f"UNSAFE mutations (reached loop but broke CONN or canary):")
        for addr, orig, new, cls, bit in bad_details:
            print(f"  0x{addr:04X}  bit {bit}: 0x{orig:02X} → 0x{new:02X}")
        print()
        print("Each row is a bit-flip that DID NOT hang the sim but left the")
        print("firmware in a state where USBCTL CONN or the loop canary is")
        print("wrong. Real-hardware equivalent: silent, un-recoverable brick.")

    # HUNG is fine — real hardware would hang too, and USB-up-early
    # would still let recovery happen.
    # TRAP is fine — sim halted, no ambiguity.
    # REACHED_BAD is the only genuinely worrying category.

    if counters["REACHED_BAD"] > 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
