#!/usr/bin/env python3
"""Per-channel signal level of a capture, in dBFS. Bench measurement primitive.

Reports RMS and peak for each channel separately, because every question this
bench asks is about ONE channel while the other is the control -- the cross-link
feeds src1 only, so ch2's level is what says "the reading is a signal path and
not a crosstalk floor". BENCH_WIRING.md quotes ~66 dB between a fed and an
unfed channel; a result that does not reproduce that separation is not a result.

RMS, not peak, is the number to compare. Peak is one sample and a single burst
of interface noise sets it; RMS over the window is what the 71 dB in #171 was.
Both are printed so a disagreement between them is visible rather than averaged
away.

Skips the head of the file by default: `arecord` opening the stream is not the
steady state, and #186 measured the first 64 ms of a stream carrying a feedback
value from before the rate settled.

    ch_level.py [--skip=S] [--take=S] capture.wav
"""
import math
import struct
import sys
import wave


def read_channels(path, skip, take):
    """-> (rate, [[samples ch0], [samples ch1], ...]) as floats in -1..1."""
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
        nch = w.getnchannels()
        width = w.getsampwidth()
        w.setpos(min(int(skip * rate), w.getnframes()))
        want = w.getnframes() - w.tell()
        if take > 0:
            want = min(want, int(take * rate))
        raw = w.readframes(want)

    chans = [[] for _ in range(nch)]
    if width == 3:
        # S24_3LE -- the format the device actually streams. Sign-extend by
        # hand; struct has no 24-bit code, and getting this wrong reads as a
        # plausible level rather than as an error.
        full = 1 << 23
        step = 3 * nch
        for off in range(0, len(raw) - step + 1, step):
            for c in range(nch):
                b = raw[off + 3 * c: off + 3 * c + 3]
                v = b[0] | (b[1] << 8) | (b[2] << 16)
                if v & full:
                    v -= 1 << 24
                chans[c].append(v / float(full))
    elif width == 2:
        full = 1 << 15
        for i, v in enumerate(struct.unpack("<%dh" % (len(raw) // 2), raw)):
            chans[i % nch].append(v / float(full))
    else:
        raise SystemExit("unsupported sample width %d" % width)
    return rate, chans


def dbfs(x):
    # A digitally silent channel is the EXPECTED result of half these
    # measurements, so it needs a name rather than a math-domain error.
    return float("-inf") if x <= 0 else 20.0 * math.log10(x)


def fmt(x):
    return "  -inf" if x == float("-inf") else "%6.2f" % x


def analyse(path, skip, take):
    rate, chans = read_channels(path, skip, take)
    n = len(chans[0]) if chans else 0
    print("%s  %d ch, %d Hz, %.2f s analysed" % (path, len(chans), rate,
                                                 n / float(rate) if rate else 0))
    for c, s in enumerate(chans):
        if not s:
            print("  ch%d  EMPTY" % c)
            continue
        rms = math.sqrt(sum(v * v for v in s) / len(s))
        peak = max(abs(v) for v in s)
        nz = sum(1 for v in s if v != 0.0)
        print("  ch%d  rms %s dBFS   peak %s dBFS   non-zero %d/%d"
              % (c, fmt(dbfs(rms)), fmt(dbfs(peak)), nz, len(s)))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--"))
    if not args:
        raise SystemExit("usage: ch_level.py [--skip=S] [--take=S] <capture.wav>")
    for p in args:
        analyse(p, float(opts.get("skip", 0.5)), float(opts.get("take", 0.0)))
