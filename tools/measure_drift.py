#!/usr/bin/env python3
"""Measure the device's actual sample rate against the host clock — #181/#182.

THE QUESTION. Both iso endpoints declare SYNC_ADAPTIVE. Adaptive means the
endpoint slaves its converter to the other end of the link. Ours are clocked by
the TAS1020B Adaptive Clock Generator from a fixed 24-bit frequency word, and
nothing in this project has ever established whether that generator locks to the
USB SOF or free-runs from the crystal. If it free-runs, the declaration is wrong
and the symptom is drift over minutes — invisible to every measurement taken so
far, all of which were seconds long.

WHY NOT THE OBVIOUS VERSION. The tempting test is `arecord -d 1800`, then
frames/1800. That does not work: the count runs from process start to process
kill, and arecord's startup costs an unknown ~50-200 ms. Over 1800 s a 100 ms
skew is 55 ppm of error, and the effect we are trying to see — a crystal
mismatch — is itself tens of ppm. The measurement would be pure noise wearing a
number's clothes.

So timestamp the SAMPLE STREAM, not the process. arecord writes raw frames to a
pipe; we take a monotonic timestamp after the first chunk and after the last,
and count only the frames BETWEEN those two instants. Both endpoints are then
the same event ("a chunk just finished arriving"), so the pipe latency that
biases t0 biases t1 equally and cancels. Residual error is about one chunk at
each end — 14 ms at 48 kHz with the default chunk — which over 1800 s is under
0.02 ppm. The clock, not the harness, becomes the limit.

TWO READINGS, AND THE SECOND IS THE DECISIVE ONE.

  ABSOLUTE — each unit's rate against the host's monotonic clock. Tells you the
  ppm error, but "the host clock" and "the USB SOF" are not guaranteed to be the
  same oscillator, so a small reading here is suggestive rather than conclusive.

  DIFFERENTIAL — unit A against unit B, run simultaneously on the same host
  controller (both units MUST be on one controller; they share its SOF). This is
  the one that answers the question. Two independent crystals differ by tens of
  ppm and cannot agree by accident. Two generators both locked to the same SOF
  agree exactly. So:

      |rate_A - rate_B| ~ 0 ppm    -> SOF-locked. SYNC_ADAPTIVE is honest.
      |rate_A - rate_B| ~ 10s ppm  -> free-running. The declaration is wrong.

  The differential also cancels any error in the host's own timebase, which the
  absolute reading cannot.

Pure stdlib on purpose — the void box venv has no numpy, and this runs there.

Usage (on the host holding the units):
    measure_drift.py --card 2 --rate 48000 --seconds 1800 --label A
Run one instance per unit, concurrently, then compare with --compare.
"""
import argparse
import json
import subprocess
import sys
import time


BYTES_PER_FRAME = 6          # 2ch x 24-bit packed (S24_3LE)
CHUNK = 4096


def measure(card, rate, seconds, chunk=CHUNK):
    """Run arecord on hw:<card>,0 and time the sample stream itself."""
    cmd = ["arecord", "-D", "hw:%d,0" % card,
           "-f", "S24_3LE", "-c", "2", "-r", str(rate),
           "-t", "raw"]
    # hw: NOT plughw: -- plughw would silently insert a rate converter and
    # hand back exactly the nominal rate no matter what the device did, which
    # is the one result that would make this whole measurement meaningless.
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)

    t0 = None
    t1 = None
    frames = 0
    deadline = None
    try:
        while True:
            b = proc.stdout.read(chunk)
            if not b:
                break
            now = time.monotonic()
            if t0 is None:
                # First chunk: start the clock here and count nothing yet, so
                # t0 and t1 are both "a chunk just finished arriving" and their
                # pipe latency cancels.
                t0 = now
                deadline = t0 + seconds
                continue
            frames += len(b) / float(BYTES_PER_FRAME)
            t1 = now
            if now >= deadline:
                break
    finally:
        proc.terminate()
        try:
            err = proc.stderr.read().decode("utf-8", "replace")
        except Exception:
            err = ""
        proc.wait()

    if t0 is None or t1 is None or t1 <= t0:
        raise SystemExit("card %d: no samples arrived — is the device streaming?"
                         % card)

    elapsed = t1 - t0
    measured = frames / elapsed
    ppm = (measured - rate) / float(rate) * 1e6
    # arecord announces every overrun on stderr. A drifting clock against a
    # fixed-size host buffer produces these at a steady interval, so the count
    # is a second, independent signature of the same defect.
    overruns = err.lower().count("overrun")
    return {
        "card": card, "nominal": rate, "elapsed_s": elapsed,
        "frames": frames, "measured_hz": measured, "ppm": ppm,
        "overruns": overruns, "stderr_tail": err[-400:],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", type=int)
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--seconds", type=float, default=1800.0)
    ap.add_argument("--label", default="")
    ap.add_argument("--out", default=None, help="write the reading as JSON")
    ap.add_argument("--compare", nargs=2, metavar="JSON",
                    help="two --out files: report the differential")
    args = ap.parse_args()

    if args.compare:
        a = json.load(open(args.compare[0]))
        b = json.load(open(args.compare[1]))
        d = a["measured_hz"] - b["measured_hz"]
        dppm = d / float(a["nominal"]) * 1e6
        print("  %-10s %12.4f Hz  %+8.2f ppm  overruns %d"
              % (a.get("label", "A"), a["measured_hz"], a["ppm"], a["overruns"]))
        print("  %-10s %12.4f Hz  %+8.2f ppm  overruns %d"
              % (b.get("label", "B"), b["measured_hz"], b["ppm"], b["overruns"]))
        print("  differential %+.2f ppm" % dppm)
        print()
        # 2 ppm is comfortably above this harness's ~0.02 ppm floor and far
        # below any real crystal mismatch, so it separates the two hypotheses
        # without sitting near either.
        if abs(dppm) < 2.0:
            print("  VERDICT  the two units agree -> both slaved to the shared")
            print("           SOF. SYNC_ADAPTIVE is an honest declaration.")
        else:
            print("  VERDICT  the units disagree by more than two independent")
            print("           crystals could agree by accident -> free-running.")
            print("           SYNC_ADAPTIVE is wrong; see task #185.")
        return 0

    if args.card is None:
        raise SystemExit("--card is required unless --compare is given")
    r = measure(args.card, args.rate, args.seconds)
    r["label"] = args.label
    print("  %s card %d: %.4f Hz (%+.2f ppm), %.1f s, %d overruns"
          % (args.label or "?", r["card"], r["measured_hz"], r["ppm"],
             r["elapsed_s"], r["overruns"]))
    if args.out:
        json.dump(r, open(args.out, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
