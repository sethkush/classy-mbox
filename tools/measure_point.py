"""#196 -- measure ONE gain-dial position, on demand. Operator sets, this reads.

One invocation = one dial position. Run it again after every move; nothing is
latched between runs, so the runs are independent and can be taken in any order.

## Two rigs, and they measure different things

`BENCH_WIRING.md`:

  * **B src1**, fed by **A line out 1** (crossed cable). A's DAC + B's ADC. B's
    own playback is out of the circuit, so a result here is about B's INPUT.
  * **A src2**, fed by **A line out 2** (self-loop). A's DAC + A's ADC, both in
    the same unit. Every previous loopback had this problem: a number implicates
    both halves and names neither. Useful as a SANITY CHECK -- if it tracks the
    crossed rig, neither unit is an outlier -- but not as a measurement of a
    converter on its own.

The channel matters: out1/src1 is channel 1, out2/src2 is channel 2. The tone is
played on the channel being measured and the OTHER channel is reported as a
crosstalk/noise reference on the same line.

## Clipping

As the dial comes up, the loud source levels clip first. A clipped capture
produces a THD number that is real arithmetic on a meaningless signal, so each
level is checked for sample-domain clipping and flagged. Read the flagged rows
as "the dial is now high enough that this level overloads", not as distortion.

Usage:
    measure_point.py <src-serial> <src-card> <dut-serial> <dut-card> <ch> [label]

  B src1 (crossed):  measure_point.py RK10874600Q 2 RK1672500M 0 1 "9 o'clock"
  A src2 (selfloop): measure_point.py RK10874600Q 2 RK10874600Q 2 2 "9 o'clock"
"""
import subprocess
import sys
import wave

import numpy as np
import usb.core

sys.stdout.reconfigure(line_buffering=True)

FS = 48000
NFFT = 65536
TONE_BIN = 1367                    # exactly 1367 cycles in NFFT -> one bin, so
TONE_HZ = TONE_BIN * FS / NFFT     # leakage cannot masquerade as distortion
SKIP_S = 0.5                       # FINDING_202: ~-85 dBFS opening transient,
                                   # decaying to zero within 400 ms, every open
LEVELS = [-40.0, -20.0, -12.0, -6.0, -1.0]
MUX_LINE = 0x05


def dev_for(serial):
    d = next((x for x in usb.core.find(find_all=True, idVendor=0x0dba)
              if x.serial_number == serial), None)
    if d is None:
        sys.exit("no unit with serial %s on the bus" % serial)
    return d


def set_line(dev, name):
    """Set both channels to LINE and READ IT BACK. Both units boot to MIC while
    these cables feed LINE -- that mismatch voided a whole session 2026-07-29."""
    dev.ctrl_transfer(0x40, 0x13, MUX_LINE | (MUX_LINE << 3), 0, None, 2000)
    b = bytes(dev.ctrl_transfer(0xC0, 0x10, 9, 0, 8, 2000))
    if (b[0] & 0x07, (b[0] >> 3) & 0x07) != (MUX_LINE, MUX_LINE):
        sys.exit("%s mux readback %02x -- wanted line/line. ABORT." % (name, b[0]))


def make_tone(path, dbfs, chan, seconds=3.5):
    n = int(FS * seconds)
    amp = (10.0 ** (dbfs / 20.0)) * (2 ** 23 - 1)
    x = np.round(amp * np.sin(2 * np.pi * TONE_BIN * np.arange(n) / NFFT)).astype(np.int32)
    b = np.zeros((n, 6), dtype=np.uint8)
    v = x.astype(np.int32) & 0xFFFFFF
    o = (chan - 1) * 3
    b[:, o + 0] = v & 0xFF
    b[:, o + 1] = (v >> 8) & 0xFF
    b[:, o + 2] = (v >> 16) & 0xFF
    open(path, "wb").write(b.tobytes())


def play_capture(src_card, dut_card):
    p = subprocess.Popen(["aplay", "-D", "hw:%s" % src_card, "-f", "S24_3LE",
                          "-c", "2", "-r", str(FS), "-t", "raw", "/tmp/mp_tone.raw"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    r = subprocess.run(["arecord", "-D", "hw:%s" % dut_card, "-f", "S24_3LE",
                        "-c", "2", "-r", str(FS), "-d", "3", "/tmp/mp_cap.wav"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    p.terminate(); p.wait()
    if r.returncode != 0:
        return None
    w = wave.open("/tmp/mp_cap.wav", "rb"); n = w.getnframes()
    a = np.frombuffer(w.readframes(n), dtype=np.uint8).reshape(n, 6).astype(np.int32)
    w.close()
    def ch(o):
        v = a[:, o] | (a[:, o+1] << 8) | (a[:, o+2] << 16)
        return np.where(v & 0x800000, v - (1 << 24), v).astype(np.float64)
    return ch(0), ch(3)


def analyse(x):
    x = x[int(SKIP_S * FS):]
    if len(x) < NFFT:
        return None
    clip = float(np.max(np.abs(x))) >= (2 ** 23) * 0.999
    seg = x[:NFFT] / float(2 ** 23)
    sp = np.abs(np.fft.rfft(seg * np.hanning(NFFT))) / (NFFT * 0.5 / 2.0)
    def bp(k, s=2):
        return float(np.sum(sp[max(k - s, 0):min(k + s + 1, len(sp))] ** 2))
    # HANN 3-BIN SPREAD. An exact-bin sine windowed by Hann puts amplitude A in
    # the centre bin and A/2 in each neighbour, so summing power over the bins
    # gives 1.5*A^2, i.e. every absolute level reads 10*log10(1.5) = +1.76 dB
    # high. It did, at every level, by exactly that -- caught by feeding the
    # analyser a tone of known level (the calibration arm below). THD is a
    # RATIO of two bin_pow results so the factor cancels and those numbers were
    # always right; the absolute levels, the gain figures derived from them, and
    # THD+N's denominator were not.
    HANN_3BIN = 1.5
    f_raw = bp(TONE_BIN)
    f = f_raw / HANN_3BIN
    h = sum(bp(TONE_BIN * i) for i in range(2, 11) if TONE_BIN * i < len(sp) - 3) / HANN_3BIN
    resid = max(float(np.sum(sp ** 2)) - f_raw, 1e-30)
    db = lambda v: 10 * np.log10(max(v, 1e-30))
    return {"level": db(f), "thd": db(h / max(f, 1e-30)),
            "thdn": db(resid / max(f, 1e-30)),
            "noise": db(max(resid - h, 1e-30)), "clip": clip}


def main():
    if len(sys.argv) < 6:
        sys.exit(__doc__.strip().splitlines()[-1])
    ss, sc, ds, dc, chan = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
    label = sys.argv[6] if len(sys.argv) > 6 else "(unlabelled)"
    other = 2 if chan == 1 else 1

    src, dut = dev_for(ss), dev_for(ds)
    set_line(src, "source")
    if ds != ss:
        set_line(dut, "DUT")

    rig = "SELF-LOOP (same unit both ends)" if ds == ss else "crossed"
    print("== %s ==  ch%d, %s rig, src %s card %s -> dut %s card %s"
          % (label, chan, rig, ss, sc, ds, dc))
    print("  played    captured    gain      THD    THD+N   noise   ch%d" % other)

    rows = []
    for lv in LEVELS:
        make_tone("/tmp/mp_tone.raw", lv, chan)
        got = play_capture(sc, dc)
        if got is None:
            print("  %+6.1f    CAPTURE FAILED" % lv); continue
        a = analyse(got[chan - 1]); o = analyse(got[other - 1])
        if a is None:
            print("  %+6.1f    capture too short" % lv); continue
        rows.append((lv, a))
        print("  %+6.1f    %+8.2f %+8.2f %+8.1f %+8.1f %+7.1f %+7.1f  %s"
              % (lv, a["level"], a["level"] - lv, a["thd"], a["thdn"],
                 a["noise"], o["level"] if o else float("nan"),
                 "CLIPPED" if a["clip"] else ""))

    clean = [(lv, a) for lv, a in rows if not a["clip"] and a["level"] > -100]
    if clean:
        g = float(np.median([a["level"] - lv for lv, a in clean]))
        best = min(clean, key=lambda r: r[1]["thd"])
        print("\n  GAIN %+0.2f dB   best THD %+0.1f dB (at %+0.0f dBFS in)   "
              "quietest noise %+0.1f dB"
              % (g, best[1]["thd"], best[0],
                 min(a["noise"] for _, a in clean)))
        print("  distance to round-trip unity: %+0.2f dB" % (-g))
    if any(a["clip"] for _, a in rows):
        print("  NOTE: clipped rows are the dial overloading that input level,")
        print("        not distortion -- ignore their THD.")


if __name__ == "__main__":
    main()
