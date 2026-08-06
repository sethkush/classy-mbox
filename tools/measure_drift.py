#!/usr/bin/env python3
"""Measure the device's actual sample rate against the host clock — #181/#182.

THE QUESTION. Both iso endpoints declare SYNC_ADAPTIVE. Adaptive means the
endpoint slaves its converter to the other end of the link. Ours are clocked by
the TAS1020B Adaptive Clock Generator from a fixed 24-bit frequency word, and
nothing in this project has established whether that generator locks to the USB
SOF or free-runs from the crystal. If it free-runs, the declaration is wrong and
the symptom is drift over minutes — invisible to every measurement taken so far,
all of which were seconds long.

TWO REJECTED DESIGNS, because each produced a confident wrong answer first.

  1. `arecord -d T`, then frames/T. The count runs from process start to kill,
     and arecord's startup costs an unknown ~50-200 ms. Over 1800 s a 100 ms
     skew is 55 ppm, and the effect sought is tens of ppm. Pure noise.

  2. Timestamp the stream at its first and last chunk and divide. Better, and
     still not enough — it was the first version of this file. Two failures,
     both measured:

     A FIXED STARTUP DEFICIT. The same pair of units read -196 ppm over 60 s
     and -10 to -15 ppm over 1800 s. One artifact explains both: roughly 600
     frames missing at the head of every capture, which is -196 ppm when spread
     over 60 s and -7 ppm over 1800 s. The endpoint method cannot see it,
     because it has no interior points to notice the head is anomalous.

     A NOISE FLOOR THAT WAS GUESSED. The docstring claimed ~0.02 ppm from chunk
     quantisation. That is the quantisation term only; the real limit is
     scheduling jitter on when the reader wakes after read() returns. A few ms
     at t1 is a few ppm over 1800 s — the same size as the 4.47 ppm
     differential the tool then declared decisive. A measurement whose error
     bar is invented cannot adjudicate anything.

WHAT THIS DOES INSTEAD. Sample (time, cumulative frames) about once a second
and fit a straight line. The slope is the rate.

  * Jitter at any single sample is averaged over hundreds of samples rather
    than being the whole measurement, falling as 1/sqrt(N).
  * A warm-up is DISCARDED before the fit starts, so the startup deficit lands
    outside the fitted region instead of tilting it.
  * The fit yields the STANDARD ERROR OF THE SLOPE. That is the honest error
    bar: it is computed from the residuals actually observed, not asserted.
    Every verdict below is a comparison against it rather than against a
    threshold picked by hand.

THE DECISIVE READING IS DIFFERENTIAL. Both units sit on one host controller and
therefore one SOF. Two independent crystals differ by tens of ppm and cannot
agree by accident; two generators locked to the same SOF agree exactly. The
differential also cancels error in the host's own timebase, which the absolute
reading cannot — and the absolute reading is further suspect because "the host
clock" and "the USB SOF" need not be the same oscillator.

Pure stdlib on purpose — the void box venv has no numpy, and this runs there.

Usage:
    measure_drift.py --card 2 --rate 48000 --seconds 1800 --label A --out A.json
    measure_drift.py --compare A.json B.json
"""
import argparse
import json
import subprocess
import sys
import time


BYTES_PER_FRAME = 6          # 2ch x 24-bit packed (S24_3LE)
CHUNK = 4096
SAMPLE_EVERY_S = 1.0
DEFAULT_WARMUP_S = 60.0


def linfit(xs, ys):
    """Least-squares slope, intercept, and standard error of the slope.

    Written out rather than imported: the void box has no numpy, and the
    standard error is the entire point of doing this at all.
    """
    n = len(xs)
    if n < 3:
        raise SystemExit("need at least 3 samples to fit a line, got %d" % n)
    mx = sum(xs) / n
    my = sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        raise SystemExit("all samples share one timestamp — cannot fit")
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx
    intercept = my - slope * mx
    # Residual standard deviation, then the slope's standard error.
    resid = [y - (intercept + slope * x) for x, y in zip(xs, ys)]
    if n > 2:
        s2 = sum(r * r for r in resid) / (n - 2)
    else:
        s2 = 0.0
    se_slope = (s2 / sxx) ** 0.5 if sxx > 0 else 0.0
    return slope, intercept, se_slope


def measure(card, rate, seconds, warmup=DEFAULT_WARMUP_S, chunk=CHUNK):
    # hw: NOT plughw: -- plughw would silently insert a rate converter and hand
    # back exactly the nominal rate no matter what the device did, which is the
    # one result that would make this whole measurement meaningless.
    cmd = ["arecord", "-D", "hw:%d,0" % card,
           "-f", "S24_3LE", "-c", "2", "-r", str(rate), "-t", "raw"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    t_start = None
    fit_t0 = None
    frames_at_fit_start = None
    frames = 0
    xs, ys = [], []
    next_sample = None

    try:
        while True:
            b = proc.stdout.read(chunk)
            if not b:
                break
            now = time.monotonic()
            frames += len(b) / float(BYTES_PER_FRAME)
            if t_start is None:
                t_start = now
                continue
            # Everything before the warm-up expires is thrown away: the
            # startup deficit lives in there, and including it tilts the fit.
            if now - t_start < warmup:
                continue
            if fit_t0 is None:
                fit_t0 = now
                frames_at_fit_start = frames
                next_sample = now + SAMPLE_EVERY_S
                continue
            if now >= next_sample:
                xs.append(now - fit_t0)
                ys.append(frames - frames_at_fit_start)
                next_sample = now + SAMPLE_EVERY_S
            if now - t_start >= seconds:
                break
    finally:
        proc.terminate()
        try:
            err = proc.stderr.read().decode("utf-8", "replace")
        except Exception:
            err = ""
        proc.wait()

    if len(xs) < 3:
        raise SystemExit("card %d: only %d fit samples — did the stream start?"
                         % (card, len(xs)))

    slope, _icept, se = linfit(xs, ys)
    ppm = (slope - rate) / float(rate) * 1e6
    ppm_se = se / float(rate) * 1e6
    overruns = err.lower().count("overrun")
    return {
        "card": card, "nominal": rate,
        "fit_span_s": xs[-1], "samples": len(xs),
        "measured_hz": slope, "se_hz": se,
        "ppm": ppm, "ppm_se": ppm_se,
        "overruns": overruns, "warmup_s": warmup,
        "stderr_tail": err[-400:],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", type=int)
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--seconds", type=float, default=1800.0)
    ap.add_argument("--warmup", type=float, default=DEFAULT_WARMUP_S)
    ap.add_argument("--label", default="")
    ap.add_argument("--out", default=None)
    ap.add_argument("--compare", nargs=2, metavar="JSON")
    args = ap.parse_args()

    if args.compare:
        a = json.load(open(args.compare[0]))
        b = json.load(open(args.compare[1]))
        for r in (a, b):
            print("  %-6s %12.4f Hz  %+8.3f +/- %.3f ppm  %d samples over "
                  "%.0f s  overruns %d"
                  % (r.get("label", "?"), r["measured_hz"], r["ppm"],
                     r["ppm_se"], r["samples"], r["fit_span_s"], r["overruns"]))
        d = a["ppm"] - b["ppm"]
        # Errors add in quadrature; this is the uncertainty ON THE DIFFERENCE,
        # which is what the verdict must be judged against.
        de = (a["ppm_se"] ** 2 + b["ppm_se"] ** 2) ** 0.5
        print("  differential %+.3f +/- %.3f ppm" % (d, de))
        print()
        if de == 0:
            print("  VERDICT  no uncertainty estimate — cannot adjudicate.")
            return 0
        sigma = abs(d) / de
        print("  separation   %.1f sigma" % sigma)
        if sigma < 3.0:
            print("  VERDICT  the two units agree within the measurement's own")
            print("           error -> consistent with both slaved to the")
            print("           shared SOF. SYNC_ADAPTIVE is defensible.")
        else:
            print("  VERDICT  the units differ by %.1f sigma -> independent" % sigma)
            print("           clocks, i.e. free-running. SYNC_ADAPTIVE is")
            print("           wrong; see tasks #185 and #186.")
        return 0

    if args.card is None:
        raise SystemExit("--card is required unless --compare is given")
    r = measure(args.card, args.rate, args.seconds, args.warmup)
    r["label"] = args.label
    print("  %s card %d: %.4f Hz (%+.3f +/- %.3f ppm), %d samples, %d overruns"
          % (args.label or "?", r["card"], r["measured_hz"], r["ppm"],
             r["ppm_se"], r["samples"], r["overruns"]))
    if args.out:
        json.dump(r, open(args.out, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
