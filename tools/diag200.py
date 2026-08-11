"""#200 -- reproduce the #197 transient at runtime and characterise it.

Build 0x004E adds TLM_REQ_DIAG_MODE (0x17): wValue selects which pair bits
streaming_set_rate() clears before reprogramming the clocks, LATCHED, so it
survives the set_rate that arecord's own SET_CUR drives at stream open.

    0x0C  shipping -- calibrate at every open
    0x00  PRE-FIX  -- no edge, no calibration, the transient returns
    0x04  the AK5383's RST only
    0x08  the AK4393's gate only

Why this build exists: every probe before it returned null because on a
calibrated part there is nothing to reveal. The DC has to be PRESENT before a
disturbance can expose it.

Usage:
    diag200.py <serial> <card> e1     reproduce, against a same-session control
    diag200.py <serial> <card> e2     which bit matters
    diag200.py <serial> <card> e3     the #199 re-run, with the offset present
    diag200.py <serial> <card> e5     upstream or downstream of the high-pass
    diag200.py <serial> <card> e6     amplitude vs the close-to-reopen gap
"""
import sys
import time
import subprocess
import wave

import usb.core
import numpy as np

TLM_REQ_DIAG_MODE = 0x17
TLM_REQ_READ = 0x10
TLM_REQ_SET_CLOCK = 0x14
SHIPPING, PREFIX, ADC_ONLY, DAC_ONLY = 0x0C, 0x00, 0x04, 0x08

serial, card, which = sys.argv[1], sys.argv[2], sys.argv[3]
dev = next((d for d in usb.core.find(find_all=True, idVendor=0x0dba)
            if d.serial_number == serial), None)
if dev is None:
    sys.exit("no device with serial %s" % serial)


def set_mask(m):
    dev.ctrl_transfer(0x40, TLM_REQ_DIAG_MODE, m, 0, None, 2000)
    got = bytes(dev.ctrl_transfer(0xC0, TLM_REQ_READ, 12, 0, 8, 2000))
    # Confirm rather than assume. Four measurements were voided in one session
    # by an instrument that was silently doing nothing.
    if got[0] != m:
        sys.exit("mask readback %02x != %02x requested -- ABORT" % (got[0], m))
    return got


def rst_cycles():
    return bytes(dev.ctrl_transfer(0xC0, TLM_REQ_READ, 12, 0, 8, 2000))[2]


def capture(seconds=2.0, rate=48000, path="/tmp/d200cap.wav"):
    subprocess.run(["arecord", "-D", "hw:%s" % card, "-f", "S24_3LE", "-c", "2",
                    "-r", str(rate), "-d", str(int(seconds)), path],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w = wave.open(path, "rb"); fs = w.getframerate(); n = w.getnframes()
    a = np.frombuffer(w.readframes(n), dtype=np.uint8).reshape(n, 6).astype(np.int32)
    w.close()
    v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
    return fs, np.where(v & 0x800000, v - (1 << 24), v).astype(np.float64)


def describe(fs, x, tag):
    nz = np.nonzero(x)[0]
    lead = int(nz[0]) if len(nz) else len(x)
    b = int(fs * 0.010); m = len(x) // b
    dc = x[:m*b].reshape(m, b).mean(axis=1)
    k = lead // b
    open_dc = dc[k:k+3].mean()
    rms0 = x[lead:lead+int(0.1*fs)].std()
    late = x[int(1.2*fs):]
    print("  %-22s lead zeros %5d (%6.2f ms)  opening DC %+9.1f LSB24 "
          "(%.1f dBFS)  settled rms %.1f LSB"
          % (tag, lead, 1000.0*lead/fs, open_dc,
             20*np.log10(max(abs(rms0)/8388608.0, 1e-12)),
             late.std() if len(late) else float("nan")))
    return dc, k


def tau_fit(dc, k):
    """Zero-crossing of the double-pole response IS tau. Returns ms or None."""
    y = dc[k:k+120] - dc[k+120:].mean()
    if abs(y[0]) < 200:
        return None
    s = np.sign(y[0])
    for j in range(1, len(y)):
        if y[j] * s < 0:
            return (j - y[j] / (y[j] - y[j-1])) * 10.0
    return None


if which == "e1":
    # Reproduce, with a same-session control. Interleaved so drift cannot
    # masquerade as an effect, and both directions of the mask change are run.
    print("E1 -- does the transient reproduce? (interleaved, same power-up)")
    for rnd in range(3):
        for mask, tag in ((SHIPPING, "mask 0x0C shipping"), (PREFIX, "mask 0x00 PRE-FIX")):
            set_mask(mask)
            fs, x = capture()
            dc, k = describe(fs, x, "round %d %s" % (rnd, tag))
            t = tau_fit(dc, k)
            if t:
                print("      decay zero-crossing = %.0f ms (tau; expect ~176)" % t)
    set_mask(SHIPPING)

elif which == "e2":
    print("E2 -- which bit matters")
    for mask, tag in ((SHIPPING, "0x0C both"), (PREFIX, "0x00 neither"),
                      (ADC_ONLY, "0x04 ADC RST only"), (DAC_ONLY, "0x08 DAC gate only"),
                      (SHIPPING, "0x0C both again")):
        set_mask(mask)
        fs, x = capture()
        describe(fs, x, tag)
    set_mask(SHIPPING)

elif which == "e3":
    # The experiment #199 could not do: reprogram mid-stream with the offset
    # PRESENT. The control is the same request at mask 0x0C, which must show
    # nothing -- that is #199's result, and reproducing it proves the same rig.
    print("E3 -- reprogram mid-stream, with and without the offset present")
    for mask, tag in ((PREFIX, "mask 0x00 (offset present)"),
                      (SHIPPING, "mask 0x0C (control, expect nothing)")):
        set_mask(mask)
        rec = subprocess.Popen(["arecord", "-D", "hw:%s" % card, "-f", "S24_3LE",
                                "-c", "2", "-r", "48000", "-d", "8", "/tmp/d200e3.wav"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        t0 = time.time()
        for at in (3.0, 5.5):
            time.sleep(max(0, t0 + at - time.time()))
            dev.ctrl_transfer(0x40, TLM_REQ_SET_CLOCK, 2, 0xFF, None, 2000)
        rec.wait()
        w = wave.open("/tmp/d200e3.wav", "rb"); fs = w.getframerate(); n = w.getnframes()
        a = np.frombuffer(w.readframes(n), dtype=np.uint8).reshape(n, 6).astype(np.int32)
        w.close()
        v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
        x = np.where(v & 0x800000, v - (1 << 24), v).astype(np.float64)
        b = int(fs * 0.010); m = n // b
        dc = x[:m*b].reshape(m, b).mean(axis=1)
        for at in (3.0, 5.5):
            k = int(at / 0.010)
            seg = dc[k:k+120] - dc[k+150:k+200].mean()
            print("  %-32s reprogram at %.1fs -> peak %+8.1f LSB24"
                  % (tag, at, seg[np.argmax(np.abs(seg))]))
    set_mask(SHIPPING)

elif which == "e5":
    print("E5 -- does the DC settle to zero (upstream) or stand (downstream)?")
    for mask, tag in ((PREFIX, "mask 0x00"), (SHIPPING, "mask 0x0C")):
        set_mask(mask)
        fs, x = capture(seconds=10)
        nz = np.nonzero(x)[0]; lead = int(nz[0]) if len(nz) else 0
        for lo, hi in ((0.0, 0.5), (1.0, 2.0), (4.0, 6.0), (8.0, 10.0)):
            seg = x[lead + int(lo*fs): lead + int(hi*fs)]
            if len(seg):
                print("  %-10s t=%4.1f-%4.1f s  DC %+9.2f LSB24" % (tag, lo, hi, seg.mean()))
    set_mask(SHIPPING)

elif which == "e6":
    print("E6 -- transient amplitude vs the close-to-reopen gap")
    set_mask(PREFIX)
    for gap in (0.5, 2.0, 8.0, 30.0):
        time.sleep(gap)
        fs, x = capture()
        describe(fs, x, "gap %.1f s" % gap)
    set_mask(SHIPPING)

else:
    sys.exit("unknown experiment %r" % which)

print("mask restored to shipping; rst cycles this power-up = %d" % rst_cycles())
