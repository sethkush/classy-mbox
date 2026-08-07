#!/usr/bin/env python3
"""Stepped-sine THD and frequency response over the bench self-loop.

Measures the LOOP -- DAC, analog out, cable, analog in, ADC -- not any one of
them. Nothing on this bench can separate the output stage from the input stage;
that needs an external generator or analyser. Every number here is round trip.

The self-loop (A out2 -> A src2) is used rather than a cross-unit path because
playback and capture then share one crystal. On the crossed cables the two units
differ by ~4.4 ppm (block 11: A -2.46, B -6.83), which smears a long FFT and
would show up as skirts around the fundamental that look like distortion.

Coherent sampling: with an integer tone frequency and an analysis window of
exactly `rate` samples, every tone lands on a bin centre, so a rectangular
window leaks nothing and the harmonic bins are exact. The leakage check below
verifies that assumption per measurement rather than trusting it.
"""
import argparse
import os
import subprocess
import sys
import time
import wave

import numpy as np


def gen_tone(path, hz, rate, secs, amp, channel):
    n = int(rate * secs)
    t = np.arange(n)
    v = amp * np.sin(2 * np.pi * hz * t / rate)
    s = np.zeros((n, 2))
    s[:, channel] = v
    q = np.clip(np.rint(s * 8388607.0), -8388608, 8388607).astype(np.int32)
    b = np.empty((n, 2, 3), dtype=np.uint8)
    b[:, :, 0] = (q & 0xFF).astype(np.uint8)
    b[:, :, 1] = ((q >> 8) & 0xFF).astype(np.uint8)
    b[:, :, 2] = ((q >> 16) & 0xFF).astype(np.uint8)
    w = wave.open(path, "wb")
    w.setnchannels(2); w.setsampwidth(3); w.setframerate(rate)
    w.writeframes(b.tobytes()); w.close()


def read_wav24(path):
    w = wave.open(path, "rb")
    n, ch, rate = w.getnframes(), w.getnchannels(), w.getframerate()
    raw = w.readframes(n); w.close()
    a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
    v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
    v = np.where(v & 0x800000, v - 0x1000000, v).astype(np.float64) / 8388608.0
    return v.reshape(-1, ch), rate


def play_capture(card, tone, cap, play_s, cap_s, rate, settle):
    ap = subprocess.Popen(["aplay", "-D", "hw:%d,0" % card, tone],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(settle)
    subprocess.run(["arecord", "-D", "hw:%d,0" % card, "-f", "S24_3LE",
                    "-c", "2", "-r", str(rate), "-d", str(cap_s), cap],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    try:
        ap.wait(timeout=10)
    except Exception:
        ap.kill()


def analyse(sig, rate, f0, nharm=10):
    """Coherent-window spectrum -> fundamental, THD, THD+N, noise floor."""
    n = rate                                   # 1 s -> 1 Hz bins
    if len(sig) < n:
        return None
    start = (len(sig) - n) // 2                # centre window, avoids edges
    x = sig[start:start + n]
    x = x - np.mean(x)                         # kill DC before anything else
    X = np.fft.rfft(x)
    mag = np.abs(X) / (n / 2.0)

    k0 = int(round(f0))
    if k0 <= 0 or k0 >= len(mag):
        return None
    fund = mag[k0]

    # Leakage check: with coherent sampling the neighbours should be far down.
    nb = max(mag[k0 - 1], mag[k0 + 1]) if 0 < k0 < len(mag) - 1 else 0.0
    leak_db = 20 * np.log10(nb / (fund + 1e-30) + 1e-30)

    hs, hlv = [], []
    for h in range(2, nharm + 1):
        k = k0 * h
        if k >= len(mag) - 1:
            break
        v = mag[k]
        hs.append(v); hlv.append((h, 20 * np.log10(v / (fund + 1e-30) + 1e-30)))
    thd = np.sqrt(np.sum(np.array(hs) ** 2)) / (fund + 1e-30) if hs else float("nan")

    # THD+N: everything in band except DC and the fundamental bin (+/-1 bin for
    # any residual smear), referenced to the fundamental.
    band = np.zeros(len(mag), dtype=bool)
    lo, hi = 20, min(20000, len(mag) - 1)
    band[lo:hi] = True
    resid = band.copy()
    resid[max(0, k0 - 1): k0 + 2] = False
    thdn = np.sqrt(np.sum(mag[resid] ** 2)) / (fund + 1e-30)

    # Noise floor excluding harmonics, as a per-bin average.
    noise = resid.copy()
    for h in range(2, nharm + 1):
        k = k0 * h
        if k < len(noise) - 1:
            noise[max(0, k - 1): k + 2] = False
    nfl = np.sqrt(np.mean(mag[noise] ** 2)) if noise.any() else float("nan")

    d = lambda v: 20 * np.log10(v + 1e-30)
    return dict(fund_db=d(fund), thd=thd, thd_db=d(thd), thdn=thdn,
                thdn_db=d(thdn), noise_db=d(nfl), leak_db=leak_db,
                harmonics=hlv,
                peak_db=d(np.max(np.abs(x))), rms_db=d(np.sqrt(np.mean(x ** 2))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", type=int, default=0)
    ap.add_argument("--ch", type=int, default=1, help="capture channel to analyse")
    ap.add_argument("--pch", type=int, default=1, help="playback channel to drive")
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--mode", choices=["freq", "level"], default="freq")
    ap.add_argument("--amp", type=float, default=0.5)
    ap.add_argument("--f0", type=float, default=1000.0)
    ap.add_argument("--settle", type=float, default=1.2)
    ap.add_argument("--out", default="/tmp/sweep.csv")
    a = ap.parse_args()

    if a.mode == "freq":
        pts = [20, 25, 31, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400,
               500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000,
               6300, 8000, 10000, 12500, 16000, 18000, 20000]
        pts = [p for p in pts if p < a.rate / 2 * 0.96]
        runs = [(float(p), a.amp) for p in pts]
        cols = "hz,amp,fund_db,thd_pct,thd_db,thdn_pct,thdn_db,noise_db,leak_db"
    else:
        amps = [1.0, 0.7079, 0.5012, 0.3162, 0.1778, 0.1, 0.0562, 0.0316,
                0.0178, 0.01, 0.00562, 0.00316, 0.001]
        runs = [(a.f0, x) for x in amps]
        cols = "hz,amp,fund_db,thd_pct,thd_db,thdn_pct,thdn_db,noise_db,leak_db"

    rows = []
    print(cols.replace(",", "  "))
    for hz, amp in runs:
        gen_tone("/tmp/_tone.wav", hz, a.rate, 4.0, amp, a.pch)
        play_capture(a.card, "/tmp/_tone.wav", "/tmp/_cap.wav", 4.0, 2, a.rate,
                     a.settle)
        x, rate = read_wav24("/tmp/_cap.wav")
        r = analyse(x[:, a.ch], rate, hz)
        if r is None:
            print("%8.0f  %.4f  ANALYSIS FAILED" % (hz, amp)); continue
        rows.append((hz, amp, r))
        print("%8.0f  %.4f  %8.2f  %8.4f%%  %8.2f  %8.4f%%  %8.2f  %8.2f  %7.1f"
              % (hz, amp, r["fund_db"], r["thd"] * 100, r["thd_db"],
                 r["thdn"] * 100, r["thdn_db"], r["noise_db"], r["leak_db"]))
        sys.stdout.flush()

    with open(a.out, "w") as f:
        f.write(cols + "\n")
        for hz, amp, r in rows:
            f.write("%g,%g,%.4f,%.6f,%.4f,%.6f,%.4f,%.4f,%.2f\n"
                    % (hz, amp, r["fund_db"], r["thd"] * 100, r["thd_db"],
                       r["thdn"] * 100, r["thdn_db"], r["noise_db"],
                       r["leak_db"]))
    print("wrote", a.out)


if __name__ == "__main__":
    main()
