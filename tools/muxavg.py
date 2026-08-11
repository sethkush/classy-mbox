"""Average many mux steps to resolve the SLOW pole under the fast one.

A single step showed a zero-crossing and an overshoot, which a single high-pass
cannot produce -- so there are at least two cascaded poles. The fast one fits
~58 ms and is identical at both sample rates, so it is analog. The question is
the slow one: the AK5383's digital HPF corner is specified to SCALE WITH fs
(1 Hz at 48 kHz), so a slow pole that stretches by 48000/44100 = 8.84 % at
44.1 kHz is inside the converter, and one that holds constant in ms is not.

Twelve steps averaged, sign-corrected, so the tail is resolvable above the
~90 LSB noise floor.
"""
import sys, time, subprocess, threading, usb.core

MIC, LINE = 0x06, 0x05
serial, card, rate, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]

dev = next((d for d in usb.core.find(find_all=True, idVendor=0x0dba)
            if d.serial_number == serial), None)
if dev is None:
    sys.exit("no device with serial %s" % serial)

def setmux(pat):
    dev.ctrl_transfer(0x40, 0x13, pat | (pat << 3), 0, None, 1000)

setmux(MIC); time.sleep(0.3)

DUR, PERIOD, N = 28, 2.0, 13
rec = subprocess.Popen(["arecord", "-D", "hw:%s" % card, "-f", "S24_3LE", "-c", "2",
                        "-r", str(rate), "-d", str(DUR), out],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
t0 = time.time()
marks = []
for i in range(N):
    at = 1.5 + i * PERIOD
    time.sleep(max(0.0, t0 + at - time.time()))
    pat = LINE if i % 2 == 0 else MIC
    setmux(pat)
    marks.append((time.time() - t0, "LINE" if pat == LINE else "MIC"))
rec.wait()
print("MARKS " + " ".join("%.4f,%s" % m for m in marks))
