#!/usr/bin/env python3
"""#147 pass 1: does a rigid slot-periodic artifact still exist in capture?

The 3-in-8 rail pattern was last seen 2026-07-31, on an image whose analog
path was DEAD -- capture RMS was -3.5 dBFS, about 90 dB hotter than silence,
which FINDING_147_the_capture_stream_is_noise.md identified as an undriven
CDATI pin rather than audio. #166/#167/#168 have shipped since, #197 explained
the settling transient, and #196 measured a real loopback. That finding made a
falsifiable prediction -- fix the dead path and the pattern goes with it -- and
nobody has checked it. This checks it.

Two statistics, deliberately different in what they can be fooled by:

  RAIL CHI-SQUARE is the specific one. It folds only the samples at or beyond
  |x| > 0.98 FS onto each candidate period and asks whether they cluster by
  phase. This is the original artifact's own signature: 6 contiguous slots in
  every 16 pinned to a static logic level. A test tone cannot trigger it,
  because mktone.py's default is 0.5 FS and never reaches the threshold.

  ENVELOPE F is the general one. One-way ANOVA on |x| folded by phase, so it
  flags ANY periodic amplitude structure. It IS fooled by the tone -- a 1 kHz
  sine at 48 kHz is period 48, so P=48 and its divisors will light up in a
  tone-bearing arm, and that is expected, not a finding. Read it on the silent
  arm, and use it on the tone arm only as evidence the tone is present.

Both skip the head of the stream. #197: the ADC's high-pass restarts at every
stream open and decays with tau = 171 ms, so the first several hundred ms are a
settling transient with real periodicity-free structure. Default skip is 1.0 s.

    analyse_147.py arm2_silent.wav
    analyse_147.py arm1_tone.wav --tone 1000
"""
import argparse
import math
import sys
import wave

FS24 = 8388607.0


def decode(path, skip_s, want_frames):
    """Return (rate, nchan, [ch0, ch1, ...]) as lists of ints, already
    windowed. Handles the 3-byte subframes the device actually streams; 2 and
    4 byte widths are accepted so a converted file is not silently misread."""
    w = wave.open(path, "rb")
    rate, nch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
    total = w.getnframes()
    skip = int(rate * skip_s)
    if skip >= total:
        sys.exit(f"{path}: only {total} frames, less than the {skip}-frame "
                 f"settling skip; capture longer than {skip_s}s")
    w.setpos(skip)
    n = min(want_frames, total - skip)
    raw = w.readframes(n)
    w.close()

    chans = [[] for _ in range(nch)]
    step = sw * nch
    if sw == 3:
        for c in range(nch):
            base = c * 3
            out = chans[c]
            for i in range(base, len(raw), step):
                v = raw[i] | (raw[i + 1] << 8) | (raw[i + 2] << 16)
                out.append(v - 0x1000000 if v & 0x800000 else v)
    elif sw in (2, 4):
        shift = 8 * (sw - 1)
        sign = 1 << (8 * sw - 1)
        full = 1 << (8 * sw)
        for c in range(nch):
            base = c * sw
            out = chans[c]
            for i in range(base, len(raw), step):
                v = int.from_bytes(raw[i:i + sw], "little")
                v = v - full if v & sign else v
                # normalise to the 24-bit scale everything else here uses
                out.append(v << (24 - 8 * sw) if sw == 2 else v >> 8)
    else:
        sys.exit(f"{path}: unsupported sample width {sw}")
    return rate, nch, chans, total, skip


def goertzel(x, rate, hz):
    """Single-bin DFT magnitude in dBFS. Cheaper than an FFT and there is only
    ever one frequency of interest here."""
    n = len(x)
    k = round(n * hz / rate)
    if k == 0 or k >= n // 2:
        return None
    wr = 2.0 * math.cos(2.0 * math.pi * k / n)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + wr * s1 - s2
        s2, s1 = s1, s0
    mag = math.sqrt(s1 * s1 + s2 * s2 - wr * s1 * s2) * 2.0 / n
    return 20 * math.log10(mag / FS24) if mag > 0 else -999.0


def rail_chi2(x, thresh, max_p):
    """Fold the rail samples onto each period and chi-square against uniform.

    Only rail INDICES are folded, not the whole stream, so this stays fast even
    in pure Python when the rails are dense -- and costs nothing when, as we
    hope, there are none."""
    lim = thresh * FS24
    idx = [i for i, v in enumerate(x) if v >= lim or v <= -lim]
    if len(idx) < 64:
        return len(idx), []
    out = []
    n = len(idx)
    for p in range(2, max_p + 1):
        bins = [0] * p
        for i in idx:
            bins[i % p] += 1
        exp = n / p
        chi = sum((b - exp) ** 2 for b in bins) / exp
        # normalise by degrees of freedom: ~1.0 when the rails are uniform in
        # phase, regardless of P, so periods are comparable to each other.
        out.append((chi / (p - 1), p, bins))
    out.sort(reverse=True)
    return len(idx), out


def envelope_f(x, max_p):
    """One-way ANOVA F on |x| folded by phase. F ~ 1.0 means no periodic
    amplitude structure at that period."""
    a = [v if v >= 0 else -v for v in x]
    n = len(a)
    grand = sum(a) / n
    sst = sum((v - grand) ** 2 for v in a)
    out = []
    for p in range(2, max_p + 1):
        sums = [0.0] * p
        cnts = [0] * p
        for i, v in enumerate(a):
            sums[i % p] += v
            cnts[i % p] += 1
        means = [sums[i] / cnts[i] for i in range(p)]
        ssb = sum(cnts[i] * (means[i] - grand) ** 2 for i in range(p))
        ssw = sst - ssb
        if ssw <= 0:
            continue
        f = (ssb / (p - 1)) / (ssw / (n - p))
        out.append((f, p))
    out.sort(reverse=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--tone", type=float, default=None,
                    help="generator frequency; reports its bin as the "
                         "known-answer check that the rig was connected")
    ap.add_argument("--skip-seconds", type=float, default=1.0,
                    help="#197 settling transient, tau=171ms; default 1.0s")
    ap.add_argument("--window-frames", type=int, default=240000,
                    help="frames analysed after the skip; 240k = 5s at 48k, "
                         "ample for a 16-slot pattern and tolerable in pure "
                         "Python")
    ap.add_argument("--max-period", type=int, default=64)
    ap.add_argument("--rail-threshold", type=float, default=0.98)
    a = ap.parse_args()

    rate, nch, chans, total, skip = decode(a.wav, a.skip_seconds,
                                           a.window_frames)
    n = len(chans[0])
    print(f"{a.wav}: {total} frames @ {rate} Hz, {nch}ch; "
          f"analysing {n} frames from {skip} (skipped {a.skip_seconds}s)")
    if n < 20000:
        print("  WARNING: short window, periodicity statistics are weak")

    for c in range(nch):
        x = chans[c]
        rms = math.sqrt(sum(v * v for v in x) / len(x))
        med = sorted(v if v >= 0 else -v for v in x)[len(x) // 2]
        zeros = sum(1 for v in x if v == 0)
        print(f"\n--- channel {c} " + "-" * 52)
        print(f"  RMS        {20*math.log10(rms/FS24) if rms else -999:8.2f} dBFS"
              f"   ({rms:.0f} counts)")
        print(f"  median|x|  {med:8d} counts")
        print(f"  zeros      {zeros:8d}  ({100.0*zeros/len(x):.2f}%)")
        if a.tone:
            db = goertzel(x, rate, a.tone)
            print(f"  {a.tone:.0f} Hz bin {db:8.2f} dBFS")

        nrail, chi = rail_chi2(x, a.rail_threshold, a.max_period)
        print(f"  rails      {nrail:8d}  ({100.0*nrail/len(x):.3f}% at "
              f"|x| > {a.rail_threshold} FS)")
        if not chi:
            print("    -> too few rail samples to fold. The 3-in-8 pattern "
                  "put 37.5% of samples here; this is nothing like it.")
        else:
            print("    rail phase clustering, top 5 periods "
                  "(chi2/df; ~1.0 = uniform):")
            for score, p, bins in chi[:5]:
                print(f"      P={p:3d}  {score:10.1f}   {bins}")

        ef = envelope_f(x, a.max_period)
        print("  envelope F, top 5 periods (F~1.0 = no periodic structure):")
        for f, p in ef[:5]:
            note = ""
            if a.tone:
                # The envelope folds |x|, and |sin| has HALF the period of sin,
                # so a tone shows up at rate/tone AND at rate/(2*tone) -- the
                # half-period is usually the STRONGER of the two. Annotating
                # only the full period is how a tone arm gets misread as an
                # artifact at P=24.
                tp = rate / a.tone
                if abs(p - tp) < 1.0:
                    note = "  <- the tone's period, expected"
                elif abs(p - tp / 2.0) < 1.0:
                    note = "  <- the tone's half-period (|x| folds), expected"
            print(f"      P={p:3d}  {f:10.2f}{note}")


if __name__ == "__main__":
    main()
