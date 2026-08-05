#!/usr/bin/env python3
"""Is there a coherent tone in this capture, and at what frequency?

Written for #46's 96 kHz question, where the whole signature is PITCH: play a
1 kHz tone with the device's codec port at 96 kHz while the host still streams
48 kHz, and the analog output comes back at 2 kHz if the converters follow.

Pure Python on purpose -- the void box's venv has no numpy, and a full FFT in
interpreted Python over 6 s of 48 kHz stereo is slower than the measurement is
worth. Two cheap, independent estimators instead:

  GOERTZEL at the two frequencies we care about. One bin each, O(n), exact.
  The RATIO between them is the answer; the absolute levels are not, because
  a starved buffer at 96 kHz spreads energy and lowers both.

  ZERO-CROSSING RATE, which for a dominant sinusoid is 2f and needs no
  assumption about which frequency to look for. It is the check on the
  Goertzel pair: if the codec came back at some third frequency, or at no
  coherent frequency at all, the ratio could still look plausible while the
  ZCR does not.

Agreement between the two is what makes a reading trustworthy. They fail
differently -- ZCR is fooled by noise crossing zero often, Goertzel is blind to
anything it is not asked about -- so a claim that survives both is worth more
than a stronger claim from either.
"""
import math
import struct
import sys
import wave


def goertzel(samples, sr, freq):
    """Energy at `freq`, normalised by sample count so lengths compare."""
    n = len(samples)
    if n == 0:
        return 0.0
    k = int(0.5 + (n * freq) / sr)
    w = (2.0 * math.pi * k) / n
    coeff = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    for x in samples:
        s0 = x + coeff * s1 - s2
        s2, s1 = s1, s0
    power = s1 * s1 + s2 * s2 - coeff * s1 * s2
    return math.sqrt(max(power, 0.0)) / n


def read_wav_mono(path, channel=0, skip_s=0.5, take_s=3.0):
    """Left channel as floats, with the stream start skipped.

    The skip is not cosmetic. A capture opened before the player produces a
    startup transient that dominates whole-file statistics -- that mistake
    turned a clean 1000.00 Hz playback measurement into a bogus 943.8 Hz
    reading in #151, and the fix was to window past it.
    """
    w = wave.open(path, "rb")
    sr, nch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
    nframes = w.getnframes()
    start = min(int(skip_s * sr), nframes)
    count = min(int(take_s * sr), nframes - start)
    if count <= 0:
        w.close()
        raise SystemExit("%s: only %d frames, nothing left after the skip"
                         % (path, nframes))
    w.setpos(start)
    raw = w.readframes(count)
    w.close()

    out = []
    stride = nch * sw
    for i in range(0, len(raw) - stride + 1, stride):
        b = raw[i + channel * sw: i + channel * sw + sw]
        if sw == 3:
            v = int.from_bytes(b, "little", signed=True) / 8388608.0
        elif sw == 2:
            v = struct.unpack("<h", b)[0] / 32768.0
        elif sw == 4:
            v = struct.unpack("<i", b)[0] / 2147483648.0
        else:
            raise SystemExit("unsupported sample width %d" % sw)
        out.append(v)
    return out, sr


def analyse(path, skip=0.5, take=3.0):
    x, sr = read_wav_mono(path, 0, skip, take)
    n = len(x)
    rms = math.sqrt(sum(v * v for v in x) / n) if n else 0.0
    peak = max((abs(v) for v in x), default=0.0)
    dbfs = 20 * math.log10(rms) if rms > 0 else -999.0

    # DC-block before counting crossings: a small offset makes a clean sine
    # cross zero at the wrong rate, or not at all.
    mean = sum(x) / n
    y = [v - mean for v in x]
    crossings = sum(1 for i in range(1, n)
                    if (y[i - 1] < 0) != (y[i] < 0))
    zcr_hz = (crossings * sr) / (2.0 * (n - 1)) if n > 1 else 0.0

    g1 = goertzel(y, sr, 1000.0)
    g2 = goertzel(y, sr, 2000.0)

    print("  %s" % path)
    print("    window        %.2f s at %d Hz" % (n / float(sr), sr))
    print("    level         %.2f dBFS rms, peak %.4f" % (dbfs, peak))
    print("    zero-crossing %8.1f Hz" % zcr_hz)
    print("    goertzel      1 kHz %.6f   2 kHz %.6f" % (g1, g2))
    if max(g1, g2) > 0:
        ratio = 20 * math.log10((g2 + 1e-12) / (g1 + 1e-12))
        print("    2k vs 1k      %+.1f dB" % ratio)
    if dbfs < -80:
        print("    VERDICT       silent -- nothing to read a pitch from")
    elif g2 > g1 * 2 and abs(zcr_hz - 2000) < abs(zcr_hz - 1000):
        print("    VERDICT       2 kHz dominant, ZCR agrees -> DOUBLED")
    elif g1 > g2 * 2 and abs(zcr_hz - 1000) < abs(zcr_hz - 2000):
        print("    VERDICT       1 kHz dominant, ZCR agrees -> NOT doubled")
    else:
        print("    VERDICT       no clean agreement -- see the numbers above")


if __name__ == "__main__":
    # Optional window, because the interesting arm of #46 is MID-STREAM: the
    # rate can only be changed after the host has opened the stream, since the
    # SET_CUR that opening sends puts the divider back. So one capture holds
    # both arms and they are read out of different windows.
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--"))
    if not args:
        raise SystemExit("usage: tone_peak.py [--skip=S] [--take=S] <capture.wav> [...]")
    skip = float(opts.get("skip", 0.5))
    take = float(opts.get("take", 3.0))
    for p in args:
        analyse(p, skip, take)
