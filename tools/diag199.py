"""#199, final form. Does a clock-frequency CHANGE clear the AK5383's filter state?

Datasheet: "the AK5383 is reset automatically when the synchronization is out of
phase by changing the clock frequencies. Therefore, the reset is only needed for
power-up." So a same-rate reprogramming may trip nothing, and the first run of
this experiment -- which reprogrammed 48000 -> 48000 -- could not have tested the
claim at all.

Arms, each fired while the host streams at `rate`:
  CHG   mux flipped 2.5 s earlier (high-pass holding a correction), then the
        clock is reprogrammed to the OTHER rate and straight back
  SAME  same, but reprogrammed to the SAME rate -- the first run's arm
  CTRL  reprogrammed to the other rate 6 s after the last flip, so nothing is
        being held. This is the artefact of doing it, and it is subtracted.
  LIVE  a plain mux flip, for the amplitude scale

NO SIGN CORRECTION. Each iteration performs the same four flips in the same
order, so the physical direction at each slot is identical across iterations and
the raw mean is already meaningful. The previous version applied an alternating
sign to a non-alternating sequence and averaged its own signal to zero -- the
LIVE arm came out at -56 +/- 769 LSB24, which is how it was caught. If the
reference arm has no signal, nothing else in the run can be believed.
"""
import sys, time, subprocess, usb.core
MIC, LINE = 0x06, 0x05
serial, card, rate, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
OTHER = 1 if rate == 48000 else 2
SAME  = 2 if rate == 48000 else 1
N, PERIOD = 10, 26.0
dev = next((d for d in usb.core.find(find_all=True, idVendor=0x0dba)
            if d.serial_number == serial), None)
if dev is None: sys.exit("no device %s" % serial)
cur = MIC
def setmux(p): dev.ctrl_transfer(0x40, 0x13, p | (p << 3), 0, None, 1000)
def diag(wv):  dev.ctrl_transfer(0x40, 0x14, wv, (0xD1 << 8) | 0xFF, None, 2000)
def flip():
    global cur
    cur = LINE if cur == MIC else MIC
    setmux(cur)
    return cur
setmux(cur); time.sleep(0.5)
rec = subprocess.Popen(["arecord","-D","hw:%s"%card,"-f","S24_3LE","-c","2",
                        "-r",str(rate),"-d",str(2+int(N*PERIOD)),out],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
t0=time.time()
def at(t): time.sleep(max(0.0, t0+t-time.time()))
log=[]
for i in range(N):
    b = 2.0 + i*PERIOD
    at(b);       log.append((b, "flip", flip()))
    at(b+2.5);   diag(OTHER); diag(SAME); log.append((b+2.5, "CHG", cur))
    at(b+6.0);   log.append((b+6.0, "flip", flip()))
    at(b+8.5);   diag(SAME);              log.append((b+8.5, "SAME", cur))
    at(b+12.0);  log.append((b+12.0, "flip", flip()))
    at(b+18.0);  diag(OTHER); diag(SAME); log.append((b+18.0, "CTRL", cur))
    at(b+21.0);  log.append((b+21.0, "LIVE", flip()))
print("SLOTDIR " + " ".join("%s=%s" % (k, "LINE" if v==LINE else "MIC")
                            for _,k,v in log[:7]))
rec.wait(); print("wrote", out)
