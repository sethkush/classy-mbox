"""Does the reproduced cold-boot offset DECAY within a capture, or stand?

This is the question that decides whether the phenomenon reproduced by
coldboot200.py is the #197 transient or the 0x004B failure mode.

  DECAYS with tau ~176 ms  -> it IS the transient. The DC is upstream of the
                              high-pass, which removes it, and the whole #197
                              account finally closes.
  STANDS                   -> it is the 0x004B mode: a wrong constant subtracted
                              downstream of the filter, which no filter can
                              remove. The transient remains unexplained.

Sets mask 0x00 within a fraction of a second of the unit appearing, so the only
calibrations of the power-up are the three the boot path performs with the
reference still charging. Then takes ONE long capture and prints the full DC
trajectory, and DOES NOT restore the mask -- the previous run destroyed the state
by restoring it, and a follow-up capture recalibrated on settled analog.
"""
import sys, time, subprocess, wave, glob, os
import usb.core
import numpy as np
sys.stdout.reconfigure(line_buffering=True)

SERIAL = "RK1672500M"

def present():
    for d in usb.core.find(find_all=True, idVendor=0x0dba):
        try:
            if d.serial_number == SERIAL:
                return True
        except Exception:
            pass
    return False

print("waiting for %s to disappear (unplug now)..." % SERIAL)
while present():
    time.sleep(0.2)
print("gone; waiting for it to return...")
dev = None
while dev is None:
    for d in usb.core.find(find_all=True, idVendor=0x0dba):
        try:
            if d.serial_number == SERIAL:
                dev = d; break
        except Exception:
            pass
    time.sleep(0.05)
t0 = time.time()
for _ in range(60):
    try:
        dev.ctrl_transfer(0x40, 0x17, 0x00, 0, None, 1000)
        b = bytes(dev.ctrl_transfer(0xC0, 0x10, 12, 0, 8, 1000))
        if b[0] == 0x00:
            print("mask 0x00 confirmed at t=%.2f s, rst_cycles=%d" % (time.time()-t0, b[2]))
            break
    except usb.core.USBError:
        time.sleep(0.1)
else:
    sys.exit("could not set mask -- ABORT")

port = None
for d in glob.glob("/sys/bus/usb/devices/*"):
    try:
        if open(os.path.join(d, "serial")).read().strip() == SERIAL:
            port = os.path.basename(d)
    except OSError:
        pass
card = None
for c in glob.glob("/sys/class/sound/card*"):
    if port and ("/%s/" % port) in os.path.realpath(c) + "/":
        card = c.rsplit("card", 1)[1]
print("port %s -> card %s" % (port, card))

# Let the reference finish charging so the latched error is at its largest,
# then take ONE long capture.
time.sleep(25.0)
print("t=%.1f s: capturing 6 s" % (time.time()-t0))
subprocess.run(["arecord", "-D", "hw:%s" % card, "-f", "S24_3LE", "-c", "2",
                "-r", "48000", "-d", "6", "/tmp/cb2.wav"],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
w = wave.open("/tmp/cb2.wav", "rb"); fs = w.getframerate(); n = w.getnframes()
a = np.frombuffer(w.readframes(n), dtype=np.uint8).reshape(n, 6).astype(np.int32); w.close()
for name, o in (("L", 0), ("R", 3)):
    v = a[:, o] | (a[:, o+1] << 8) | (a[:, o+2] << 16)
    x = np.where(v & 0x800000, v - (1 << 24), v).astype(np.float64)
    b_ = int(fs * 0.010); m = n // b_
    dc = x[:m*b_].reshape(m, b_).mean(axis=1)
    print("ch%s DC (LSB24):" % name)
    print("   " + " ".join("%dms:%+.0f" % (i*10, dc[i]) for i in range(0, 60, 6)))
    print("   " + " ".join("%dms:%+.0f" % (i*10, dc[i]) for i in range(60, 300, 30)))
    print("   " + " ".join("%.1fs:%+.0f" % (i*0.01, dc[i]) for i in range(300, m, 60)))
    print("   ch%s  first 50 ms %+.0f   last 1 s %+.0f   VERDICT: %s"
          % (name, dc[:5].mean(), dc[-100:].mean(),
             "STANDS" if abs(dc[-100:].mean()) > 0.3*abs(dc[:5].mean()) else "DECAYS"))
print("mask LEFT at 0x00 deliberately -- do not capture again before reading this")
