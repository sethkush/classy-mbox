"""Fire streaming_set_rate() mid-capture and watch what follows the calibration.

Tests the proposed mechanism for the #197 transient: that reprogramming the
clocks disturbs the AK5383's digital-filter state, so its 1 Hz high-pass
re-converges from scratch and reveals whatever DC sits under it for ~2 tau. On a
build that calibrates (0x004A) the underlying offset is ~0, so the prediction is
tRTV zeros and then FLAT -- no decay.
"""
import sys, time, subprocess, usb.core
serial, card, rate, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
dev = next((d for d in usb.core.find(find_all=True, idVendor=0x0dba)
            if d.serial_number == serial), None)
if dev is None: sys.exit("no device %s" % serial)
wv = 2 if rate == 48000 else 1
rec = subprocess.Popen(["arecord","-D","hw:%s"%card,"-f","S24_3LE","-c","2",
                        "-r",str(rate),"-d","8",out],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
t0=time.time()
for at in (2.0, 5.0):
    time.sleep(max(0.0, t0+at-time.time()))
    dev.ctrl_transfer(0x40, 0x14, wv, 0xFF, None, 2000)
    print("  t=%.3f  SET_CLOCK -> %d" % (time.time()-t0, rate))
rec.wait(); print("wrote", out)
