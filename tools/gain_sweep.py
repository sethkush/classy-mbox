"""#196 -- THD, noise and gain at marked positions of the input gain dial.

The gain knob is ANALOG and reaches no register (plan.md §2), so this is the one
measurement that cannot be driven from the desk. Everything else here IS driven
from the desk, so the person at the unit only ever turns one knob.

## The path

`BENCH_WIRING.md`: A line out 1 -> B line source 1. So **A is the signal source
and B is the unit under test**, and B's playback path is entirely out of the
circuit -- which is the point of the crossed rig.

    A ch1 plays a tone ──TS──► B src1 ──► B ch1 captured

B ch2 is captured too and carries no signal, so it reads out as a free
crosstalk/noise reference on every line.

## "Unity" here means round-trip 0 dB, not a voltage

Without a meter -- and the meter is explicitly not happening -- there is no way
to reference dBu. What this finds is the dial position where **what B captures
equals what A played**, i.e. A's DAC and B's ADC cancel. That is the useful
number for this bench and it is NOT the same as a +4 dBu unity mark. Said plainly
so nobody later reads it as one.

## Two things that will silently ruin the run

1. **A's input-playback mix knob feeds A's line outputs.** If it moves, the
   source level moves and every reading after it is against a different
   reference. Set it fully to playback and do not touch A at all.
2. **Both units boot their source mux to MIC while these cables feed LINE.**
   That voided a whole session on 2026-07-29. This script sets the mux over the
   wire and re-reads it back, rather than assuming.

## Known-answer arm

The FIRST dial position is measured again at the END. If the two do not agree
within a couple of dB, something moved -- the mix knob, a cable, the dial
detent -- and the run is void rather than interesting. That check has caught
more than it has cost on this project.

Usage:
    gain_sweep.py <src-serial> <src-card> <dut-serial> <dut-card> [labels...]

e.g. gain_sweep.py RK10874600Q 2 RK1672500M 0 min 9oclock 12oclock 3oclock max
"""
import os
import subprocess
import sys
import wave

import numpy as np
import usb.core

sys.stdout.reconfigure(line_buffering=True)

FS = 48000
NFFT = 65536              # analysis window
TONE_BIN = 1367           # 1367 * 48000/65536 = 1001.2 Hz -- an EXACT integer
                          # number of cycles in NFFT, so the fundamental lands in
                          # one bin and leakage does not masquerade as distortion
TONE_HZ = TONE_BIN * FS / NFFT
SKIP_S = 0.5              # discard the ADC's opening transient -- FINDING_202:
                          # ~-85 dBFS decaying to zero within 400 ms, at EVERY
                          # capture open. Measuring into it reads the settling
                          # instead of the converter.
LEVELS_DBFS = [-40.0, -20.0, -12.0, -6.0, -1.0]

TLM_REQ_SET_MUX = 0x13
MUX_LINE = 0x05           # per CLAUDE.md: 0x06 mic, 0x05 line, 0x03 inst


def dev_for(serial):
    d = next((x for x in usb.core.find(find_all=True, idVendor=0x0dba)
              if x.serial_number == serial), None)
    if d is None:
        sys.exit("no unit with serial %s on the bus" % serial)
    return d


def set_line_inputs(dev, name):
    """Both channels to LINE, then READ IT BACK. Never assume a stimulus fired."""
    dev.ctrl_transfer(0x40, TLM_REQ_SET_MUX,
                      MUX_LINE | (MUX_LINE << 3), 0, None, 2000)
    b = bytes(dev.ctrl_transfer(0xC0, 0x10, 9, 0, 8, 2000))
    got1, got2 = b[0] & 0x07, (b[0] >> 3) & 0x07
    if (got1, got2) != (MUX_LINE, MUX_LINE):
        sys.exit("%s mux readback %o/%o, wanted line/line -- ABORT, a capture "
                 "on the wrong input is worse than none" % (name, got1, got2))
    print("  %s mux confirmed line/line" % name)


def make_tone(path, dbfs, seconds=4.0):
    """S24_3LE stereo, tone on ch1 only so ch2 reads crosstalk."""
    n = int(FS * seconds)
    t = np.arange(n)
    amp = (10.0 ** (dbfs / 20.0)) * (2 ** 23 - 1)
    x = np.round(amp * np.sin(2 * np.pi * TONE_BIN * t / NFFT)).astype(np.int32)
    frames = np.zeros((n, 2), dtype=np.int32)
    frames[:, 0] = x
    b = np.zeros((n, 6), dtype=np.uint8)
    for ch in (0, 1):
        v = frames[:, ch].astype(np.int32) & 0xFFFFFF
        b[:, ch*3 + 0] = v & 0xFF
        b[:, ch*3 + 1] = (v >> 8) & 0xFF
        b[:, ch*3 + 2] = (v >> 16) & 0xFF
    with open(path, "wb") as f:
        f.write(b.tobytes())


def play_and_capture(src_card, dut_card, tone_path, out_path, seconds=3.0):
    p = subprocess.Popen(["aplay", "-D", "hw:%s" % src_card, "-f", "S24_3LE",
                          "-c", "2", "-r", str(FS), "-t", "raw", tone_path],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    r = subprocess.run(["arecord", "-D", "hw:%s" % dut_card, "-f", "S24_3LE",
                        "-c", "2", "-r", str(FS), "-d", str(int(seconds + 1)),
                        out_path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    p.terminate(); p.wait()
    if r.returncode != 0:
        return None
    w = wave.open(out_path, "rb"); n = w.getnframes()
    a = np.frombuffer(w.readframes(n), dtype=np.uint8).reshape(n, 6).astype(np.int32)
    w.close()
    def ch(o):
        v = a[:, o] | (a[:, o+1] << 8) | (a[:, o+2] << 16)
        return np.where(v & 0x800000, v - (1 << 24), v).astype(np.float64)
    return ch(0), ch(3)


def analyse(x):
    """Fundamental level, THD, THD+N and noise floor, all in dB."""
    x = x[int(SKIP_S * FS):]
    if len(x) < NFFT:
        return None
    seg = x[:NFFT] / float(2 ** 23)
    win = np.hanning(NFFT)
    # Coherent gain of a Hann window is 0.5; correct so dBFS is the real level.
    sp = np.abs(np.fft.rfft(seg * win)) / (NFFT * 0.5 / 2.0)
    def bin_pow(k, spread=2):
        lo, hi = max(k - spread, 0), min(k + spread + 1, len(sp))
        return float(np.sum(sp[lo:hi] ** 2))
    f_pow = bin_pow(TONE_BIN)
    h_pow = sum(bin_pow(TONE_BIN * h) for h in range(2, 11)
                if TONE_BIN * h < len(sp) - 3)
    total = float(np.sum(sp ** 2))
    # Everything that is not the fundamental: distortion AND noise.
    resid = max(total - f_pow, 1e-30)
    db = lambda v: 10 * np.log10(max(v, 1e-30))
    noise_only = max(resid - h_pow, 1e-30)
    return {
        "level": db(f_pow),
        "thd": db(h_pow / max(f_pow, 1e-30)),
        "thdn": db(resid / max(f_pow, 1e-30)),
        "noise": db(noise_only),
    }


def measure_position(src_card, dut_card, label):
    print("\n  %-12s  %8s %9s %9s %9s %10s"
          % ("played", "captured", "delta", "THD", "THD+N", "ch2 xtalk"))
    rows = []
    for lv in LEVELS_DBFS:
        make_tone("/tmp/gs_tone.raw", lv)
        got = play_and_capture(src_card, dut_card, "/tmp/gs_tone.raw", "/tmp/gs_cap.wav")
        if got is None:
            print("  %+8.1f dBFS  CAPTURE FAILED" % lv)
            continue
        c1, c2 = got
        a1, a2 = analyse(c1), analyse(c2)
        if a1 is None:
            print("  %+8.1f dBFS  capture too short" % lv)
            continue
        rows.append((lv, a1))
        print("  %+8.1f dBFS  %+8.2f %+9.2f %+9.1f %+9.1f %+10.1f"
              % (lv, a1["level"], a1["level"] - lv, a1["thd"], a1["thdn"],
                 a2["level"] if a2 else float("nan")))
    return label, rows


def continuous(src_card, dut_card, seconds, level=-20.0):
    """Turn the knob; the script watches. No keyboard between positions.

    A stepped sweep needs someone at a terminal between every dial position,
    which is a poor fit for a knob that is 1 km from the keyboard. This plays one
    steady tone and reports gain and THD every 250 ms, so the operator just turns
    the dial slowly and PAUSES at each marked position. The pauses show up as
    plateaus in the gain column and are what gets read off afterwards.

    -20 dBFS is the source level deliberately: it measured the best THD (-61.7 dB)
    of the five levels tried, so distortion here is the path's and not the tone's,
    and it leaves ~19 dB of headroom before the ADC clips as the gain comes up.
    """
    make_tone("/tmp/gs_tone.raw", level, seconds + 2.0)
    print("\nPlaying %.0f dBFS at %.2f Hz for %ds." % (level, TONE_HZ, seconds))
    print("Turn the DUT channel-1 gain dial SLOWLY from minimum to maximum,")
    print("pausing ~5 s at each marked position. Do not touch unit A.\n")
    print("   t(s)   gain(dB)      THD    THD+N   clip?")
    p = subprocess.Popen(["aplay", "-D", "hw:%s" % src_card, "-f", "S24_3LE",
                          "-c", "2", "-r", str(FS), "-t", "raw", "/tmp/gs_tone.raw"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    r = subprocess.run(["arecord", "-D", "hw:%s" % dut_card, "-f", "S24_3LE",
                        "-c", "2", "-r", str(FS), "-d", str(int(seconds)),
                        "/tmp/gs_cont.wav"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    p.terminate(); p.wait()
    if r.returncode != 0:
        sys.exit("capture failed: %s" % r.stderr.decode()[:200])
    w = wave.open("/tmp/gs_cont.wav", "rb"); n = w.getnframes()
    a = np.frombuffer(w.readframes(n), dtype=np.uint8).reshape(n, 6).astype(np.int32)
    w.close()
    v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
    x = np.where(v & 0x800000, v - (1 << 24), v).astype(np.float64)
    x = x[int(SKIP_S * FS):]

    # 250 ms hop, but a 65536-point window for the analysis, so windows overlap.
    hop = int(0.25 * FS)
    track = []
    for k in range(0, len(x) - NFFT, hop):
        a1 = analyse_seg(x[k:k + NFFT])
        clipped = np.any(np.abs(x[k:k + hop]) > (2 ** 23) * 0.999)
        t = SKIP_S + k / float(FS)
        track.append((t, a1["level"] - level, a1["thd"], a1["thdn"], clipped))
        print("  %6.2f %+10.2f %+8.1f %+8.1f     %s"
              % (t, a1["level"] - level, a1["thd"], a1["thdn"], "CLIP" if clipped else ""))

    print("\n=== PLATEAUS (a pause on a marked position) ===")
    print("  gain(dB)   THD     THD+N    held for")
    i = 0
    while i < len(track):
        j = i
        while j + 1 < len(track) and abs(track[j + 1][1] - track[i][1]) < 0.35:
            j += 1
        held = (track[j][0] - track[i][0]) + 0.25
        if held >= 1.0:
            seg = track[i:j + 1]
            print("  %+8.2f %+7.1f %+8.1f     %.1f s"
                  % (float(np.median([s[1] for s in seg])),
                     float(np.median([s[2] for s in seg])),
                     float(np.median([s[3] for s in seg])), held))
        i = j + 1

    g = [s[1] for s in track]
    print("\n  gain range swept: %+0.2f dB to %+0.2f dB" % (min(g), max(g)))
    if min(g) <= 0.0 <= max(g):
        k = int(np.argmin([abs(s[1]) for s in track]))
        print("  UNITY (round-trip 0 dB) is INSIDE the dial's range, at t=%.2f s"
              % track[k][0])
        print("  -- pause there on a second pass and read the mark off the panel.")
    else:
        print("  UNITY IS NOT REACHABLE on this dial: 0 dB lies outside %+0.2f..%+0.2f."
              % (min(g), max(g)))
    return track


def analyse_seg(seg):
    """analyse() without the SKIP -- the caller has already trimmed."""
    seg = seg / float(2 ** 23)
    win = np.hanning(NFFT)
    sp = np.abs(np.fft.rfft(seg * win)) / (NFFT * 0.5 / 2.0)
    def bin_pow(k, spread=2):
        lo, hi = max(k - spread, 0), min(k + spread + 1, len(sp))
        return float(np.sum(sp[lo:hi] ** 2))
    f_pow = bin_pow(TONE_BIN)
    h_pow = sum(bin_pow(TONE_BIN * h) for h in range(2, 11) if TONE_BIN * h < len(sp) - 3)
    resid = max(float(np.sum(sp ** 2)) - f_pow, 1e-30)
    db = lambda v: 10 * np.log10(max(v, 1e-30))
    return {"level": db(f_pow), "thd": db(h_pow / max(f_pow, 1e-30)),
            "thdn": db(resid / max(f_pow, 1e-30))}


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__.strip().splitlines()[-1])
    src_serial, src_card, dut_serial, dut_card = sys.argv[1:5]
    rest = sys.argv[5:]
    cont = "--continuous" in rest
    if cont:
        rest.remove("--continuous")
    labels = rest or ["as-found"]

    src, dut = dev_for(src_serial), dev_for(dut_serial)
    print("source  = %s (card %s)   DUT = %s (card %s)"
          % (src_serial, src_card, dut_serial, dut_card))
    print("tone    = %.2f Hz, exactly %d cycles in the %d-point window"
          % (TONE_HZ, TONE_BIN, NFFT))
    print("skipping the first %.0f ms of every capture (FINDING_202)" % (SKIP_S * 1000))
    set_line_inputs(src, "source")
    set_line_inputs(dut, "DUT")
    print("\nDO NOT TOUCH UNIT A AT ALL -- its mix knob sets the source level,")
    print("and if it moves every later reading is against a different reference.")

    if cont:
        continuous(src_card, dut_card, seconds=90)
        return

    results = []
    order = list(labels) + ([labels[0]] if len(labels) > 1 else [])
    for i, label in enumerate(order):
        again = " (REPEAT of the first, drift check)" if i == len(order) - 1 and len(order) > len(labels) else ""
        input("\n>>> set the DUT channel-1 GAIN dial to '%s'%s, then press Enter: "
              % (label, again))
        results.append(measure_position(src_card, dut_card, label))

    print("\n\n=== SUMMARY ===")
    print("  %-12s %10s %10s %10s" % ("dial", "gain(dB)", "best THD", "best THD+N"))
    for label, rows in results:
        if not rows:
            continue
        # Gain from the quietest level that is still well clear of the floor.
        g = [a["level"] - lv for lv, a in rows if a["level"] > -100]
        print("  %-12s %+10.2f %10.1f %10.1f"
              % (label, float(np.median(g)) if g else float("nan"),
                 min(a["thd"] for _, a in rows), min(a["thdn"] for _, a in rows)))

    if len(order) > len(labels):
        (l0, r0), (l1, r1) = results[0], results[-1]
        if r0 and r1:
            g0 = float(np.median([a["level"] - lv for lv, a in r0]))
            g1 = float(np.median([a["level"] - lv for lv, a in r1]))
            print("\n  DRIFT CHECK '%s': %+0.2f dB then %+0.2f dB -- %s"
                  % (l0, g0, g1,
                     "consistent, the run stands" if abs(g0 - g1) < 2.0
                     else "MOVED. Something was touched; treat the run as VOID."))

    print("\n  UNITY = the dial position whose gain is closest to 0.00 dB.")
    print("  This is round-trip unity against unit A's output, NOT +4 dBu.")


if __name__ == "__main__":
    main()
