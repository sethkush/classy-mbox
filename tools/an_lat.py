#!/usr/bin/env python3
"""Locate the burst in an rtlat capture and turn two frame indices into a latency.

Because rtlat linked the two substreams, capture frame 0 and playback frame 0
are the same instant, so the answer is a subtraction. Cross-correlation against
the known transmitted burst is used rather than a peak/threshold search: the
bench loop attenuates by ~23 dB at minimum gain, and a threshold on a signal
that quiet is a threshold on noise.
"""
import argparse
import numpy as np
from scipy.signal import correlate


def read24(path, ch=2):
    b = np.fromfile(path, dtype=np.uint8)
    b = b[: (len(b) // (3 * ch)) * 3 * ch].reshape(-1, 3).astype(np.int32)
    v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
    v = np.where(v & 0x800000, v - 0x1000000, v).astype(np.float64) / 8388608.0
    return v.reshape(-1, ch)


ap = argparse.ArgumentParser()
ap.add_argument("raw")
ap.add_argument("--rate", type=int, default=48000)
ap.add_argument("--burst-at", type=int, required=True)
ap.add_argument("--burst-len", type=int, default=512)
ap.add_argument("--burst-hz", type=float, default=2000.0)
ap.add_argument("--ch", type=int, default=1)
a = ap.parse_args()

x = read24(a.raw)
sig = x[:, a.ch]

n = np.arange(a.burst_len)
w = 0.5 - 0.5 * np.cos(2 * np.pi * n / (a.burst_len - 1))
ref = w * np.sin(2 * np.pi * a.burst_hz * n / a.rate)

c = correlate(sig, ref, mode="valid")
k = int(np.argmax(np.abs(c)))

# Parabolic interpolation around the correlation peak -> sub-sample estimate.
if 0 < k < len(c) - 1:
    y0, y1, y2 = np.abs(c[k - 1]), np.abs(c[k]), np.abs(c[k + 1])
    denom = (y0 - 2 * y1 + y2)
    frac = 0.5 * (y0 - y2) / denom if denom != 0 else 0.0
else:
    frac = 0.0

arrival = k + frac
lat_frames = arrival - a.burst_at
lat_ms = lat_frames * 1000.0 / a.rate

# Confidence: how far the true peak stands above the rest of the correlogram.
mask = np.ones(len(c), dtype=bool)
lo, hi = max(0, k - a.burst_len), min(len(c), k + a.burst_len)
mask[lo:hi] = False
bg = np.abs(c[mask])
ratio = np.abs(c[k]) / (bg.max() + 1e-30)

seg = sig[int(round(arrival)): int(round(arrival)) + a.burst_len]
pk = np.max(np.abs(seg)) if len(seg) else 0.0

print("frames captured   %d" % len(sig))
print("burst sent at     %d" % a.burst_at)
print("burst arrived at  %.2f" % arrival)
print("ROUND TRIP        %.2f frames = %.3f ms" % (lat_frames, lat_ms))
print("peak/next-peak    %.1fx  (>3 is unambiguous)" % ratio)
print("burst peak level  %.2f dBFS" % (20 * np.log10(pk + 1e-30)))
