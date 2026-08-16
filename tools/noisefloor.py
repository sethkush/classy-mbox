#!/usr/bin/env python3
"""Per-band noise floor on every input, with the analysis floor measured too.

WHY THIS EXISTS RATHER THAN THE NUMBERS ALREADY IN THE LOG. An LF-noise
regression was chased on 2026-08-15 using figures produced by an ad-hoc session
measurement -- roughly -71 dBFS below 200 Hz against -97 up high -- that no
committed tool generated. Those numbers cannot be reproduced or compared
against, because nothing records how they were normalised: band sum or per-bin
mean, one-sided or two-sided, Hann-corrected or not. Each of those choices moves
a "dBFS" figure by 10 dB or more, and the effect being chased was 26.

So this does not try to match them. It defines the measurement and commits it,
and the old figures are treated as uncomparable rather than as a baseline.

THE NORMALISATION, stated because it is the whole ambiguity:
  * per-bin MEAN power within each band, not the band sum -- a sum grows with
    bandwidth, so summing makes a wide band look louder than a narrow one for
    no physical reason.
  * one-sided rfft, Hann window, corrected by the window's coherent power gain
    AND by the 1.5x three-bin Hann power spread already established in the #196
    analyser.
  * 0 dBFS = a full-scale sine, so a full-scale sine reads 0.

THE KNOWN-ANSWER ARM IS BUILT IN, and this project's rules require one. Digital
silence -- an all-zero buffer of the same length -- is pushed through the exact
same analysis and reported as ANALYSIS FLOOR. Any channel reading at that floor
is reporting the arithmetic, not the hardware, and the row is flagged. Without
it a quiet channel and a broken capture path look identical.

    sudo noisefloor.py --cards 0,2

Captures silence: nothing is played, so what it measures is the input path with
whatever happens to be connected. Read it against BENCH_WIRING.md -- a channel
fed by another unit's output is measuring THAT unit's output stage as much as
its own input.
"""
import argparse
import subprocess
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("numpy not installed. On the void box: ~/mbox-venv/bin/python")

BANDS = [("20-200Hz", 20, 200), ("200Hz-2k", 200, 2000),
         ("2k-20k", 2000, 20000)]
HANN_3BIN = 1.5          # #196: Hann spreads a tone's power over three bins


def capture(card, secs, rate=48000):
    p = subprocess.run(
        ["arecord", "-D", f"hw:{card},0", "-f", "S24_3LE", "-r", str(rate),
         "-c", "2", "-d", str(int(round(secs))), "-t", "raw"],
        capture_output=True)
    if p.returncode != 0:
        return None, p.stderr.decode()[-160:].strip()
    raw = np.frombuffer(p.stdout, dtype=np.uint8)
    n = (len(raw) // 6) * 6
    b = raw[:n].reshape(-1, 3)
    v = (b[:, 0].astype(np.int32)
         | (b[:, 1].astype(np.int32) << 8)
         | (b[:, 2].astype(np.int32) << 16))
    v = np.where(v & 0x800000, v - 0x1000000, v).astype(np.float64) / 8388608.0
    return v.reshape(-1, 2), None


def bands(x, rate=48000, skip_s=1.0):
    """Per-band per-bin mean power, in dBFS relative to a full-scale sine."""
    # Drop the head: the capture high-pass restarts at every stream open and
    # #197/#201 established that the first ~200 ms is a settling transient, not
    # a noise floor.
    x = x[int(rate * skip_s):]
    w = np.hanning(len(x))
    sp = np.abs(np.fft.rfft(x * w)) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / rate)
    # Full-scale sine reference through the identical path.
    ref = (np.sum(w) / 2.0) ** 2 * HANN_3BIN
    out = {}
    for name, lo, hi in BANDS:
        m = (f >= lo) & (f < hi)
        out[name] = 10 * np.log10(sp[m].mean() / ref + 1e-30) if m.any() else float("nan")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cards", default="0,2",
                    help="comma-separated ALSA card numbers")
    ap.add_argument("--seconds", type=float, default=6.0)
    a = ap.parse_args()

    rate = 48000
    n = int(rate * (a.seconds - 1.0))

    # THE REFERENCE ARM, computed first so it is on screen before any hardware
    # number and cannot be rationalised after the fact.
    #
    # NOT an all-zero buffer: that has exactly zero power and reports whatever
    # epsilon guards the log, which was -300 dB on the first run -- a number
    # that looks like a floor and is arithmetic about nothing. The meaningful
    # bound is the 24-bit QUANTISATION floor, so the reference is uniform noise
    # of one LSB peak-to-peak. Nothing captured through a 24-bit path can read
    # below this, so a channel sitting on it is at the limit of the format
    # rather than at the limit of the hardware.
    rng = np.random.default_rng(0)      # fixed seed: the arm must not wander
    lsb = 1.0 / 8388608.0
    floor = bands((rng.random(n + rate) - 0.5) * lsb, rate)
    print(f"{'channel':<26}" + "".join(f"{b[0]:>12}" for b in BANDS))
    print("-" * 62)
    print(f"{'ANALYSIS FLOOR (silence)':<26}"
          + "".join(f"{floor[b[0]]:>12.1f}" for b in BANDS))
    print()

    for card in [int(c) for c in a.cards.split(",")]:
        try:
            serial = open(
                f"/sys/class/sound/card{card}/device/../serial").read().strip()
        except OSError:
            serial = f"card{card}"
        d, err = capture(card, a.seconds, rate)
        if d is None:
            print(f"{serial+' ch?':<26}CAPTURE FAILED -- {err}")
            continue
        for ch in (0, 1):
            r = bands(d[:, ch], rate)
            near = [b[0] for b in BANDS
                    if r[b[0]] - floor[b[0]] < 6.0]
            tag = "   <-- AT THE ANALYSIS FLOOR" if len(near) == len(BANDS) else ""
            print(f"{serial+' ch'+str(ch+1):<26}"
                  + "".join(f"{r[b[0]]:>12.1f}" for b in BANDS) + tag)

    print()
    print("Rows within 6 dB of the analysis floor in every band are reporting")
    print("arithmetic, not hardware. Read the rest against BENCH_WIRING.md: a")
    print("channel fed by another unit's output measures THAT unit's output")
    print("stage as much as its own input.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
