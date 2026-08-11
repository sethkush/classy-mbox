"""#201 cold-boot verification: does g_ref_settled actually delay the latch?

0x004F calibrates at EVERY stream open until the analog reference is declared
settled (sof_count high byte >= 0x75, i.e. 30 s), then latches g_cal_done and
never calibrates again. Warm tests cannot see this: by the time anything is
warm the reference is settled and the very first open latches. Only a genuine
power-up puts the firmware on the other side of that threshold.

0x004B passed every warm test and failed only on a real power-up. That is the
whole reason this script exists.

Expected signature, and it is unambiguous:

    t <  30 s   every capture shows ~8790 lead zeros  (calibrating, not latching)
    t ~= 30 s   one more capture with ~8790            (the latching one)
    t >  30 s   every capture shows 0                  (g_cal_done set)

Failure modes it separates:
    all captures ~8790 forever  -> g_ref_settled never fires; threshold wrong
    first capture 0             -> calibration skipped entirely; transient back
    zeros before t=30 s         -> latched too early; the #197 bug is back

Every capture is also checked for the transient itself, so a "clean" verdict
is measured rather than assumed.

Usage:  coldboot201.py <serial> <card>    # start it, THEN unplug and replug
"""
import sys
import os
import glob
import time
import subprocess
import wave

import usb.core
import numpy as np

sys.stdout.reconfigure(line_buffering=True)

serial = sys.argv[1]
card = sys.argv[2]
RUN_FOR = float(sys.argv[3]) if len(sys.argv) > 3 else 75.0


def present(sn):
    """serial_number RAISES during the replug race -- pyusb turns the
    not-yet-readable string descriptor into ValueError('no langid'). An
    unguarded version of this cost a power cycle once already."""
    for d in usb.core.find(find_all=True, idVendor=0x0dba):
        try:
            if d.serial_number == sn:
                return True
        except Exception:
            pass
    return False


print("waiting for %s to DISAPPEAR (unplug it now)..." % serial)
while present(serial):
    time.sleep(0.2)
print("gone. leave it out ~15 s, then plug it back in.")

# Absence THEN presence. Matching on presence alone matches the stale node from
# before the unplug -- that mistake has cost replugs before.
while not present(serial):
    time.sleep(0.05)
t_boot = time.time()
print("back at t=0.00 s")

# Wait for the ALSA card to exist, by port path -- never by index, because the
# index is not stable across a replug.
port = None
for _ in range(200):
    for d in glob.glob("/sys/bus/usb/devices/*"):
        try:
            if open(os.path.join(d, "serial")).read().strip() == serial:
                port = os.path.basename(d)
        except OSError:
            pass
    if port:
        break
    time.sleep(0.1)
found = None
for _ in range(200):
    for c in glob.glob("/sys/class/sound/card*"):
        if port and ("/%s/" % port) in os.path.realpath(c) + "/":
            found = c.rsplit("card", 1)[1]
    if found:
        break
    time.sleep(0.1)
if found and found != card:
    print("  card renumbered to %s (was told %s) -- using %s" % (found, card, found))
    card = found
print("  port %s, card %s, first capture at t=%.2f s" % (port, card, time.time() - t_boot))

print()
print("  t_open   dur   leadzeros    ms   head DC   50-200ms   tail    floor")
i = 0
while time.time() - t_boot < RUN_FOR:
    t_open = time.time() - t_boot
    p = "/tmp/cb201_%02d.wav" % i
    i += 1
    r = subprocess.run(["arecord", "-D", "hw:%s" % card, "-f", "S24_3LE", "-c", "2",
                        "-r", "48000", "-d", "2", p],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if r.returncode != 0:
        print("  %6.2f   capture failed: %s" % (t_open, r.stderr.decode()[:60]))
        time.sleep(0.5)
        continue
    w = wave.open(p, "rb"); fs = w.getframerate(); n = w.getnframes()
    a = np.frombuffer(w.readframes(n), dtype=np.uint8).reshape(n, 6).astype(np.int32)
    w.close()
    v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
    x = np.where(v & 0x800000, v - (1 << 24), v).astype(np.float64)
    nz = np.nonzero(x)[0]
    lead = int(nz[0]) if len(nz) else n
    y = x[lead:]
    seg = lambda lo, hi: y[int(lo * fs):int(hi * fs)]
    tail = seg(1.0, 1.8)
    print("  %6.2f  %5.1f   %7d %7.1f  %+8.1f  %+9.1f  %+6.1f  %7.1f"
          % (t_open, time.time() - t_boot - t_open, lead, 1000.0 * lead / fs,
             seg(0, .05).mean() if seg(0, .05).size else float("nan"),
             seg(.05, .2).mean() if seg(.05, .2).size else float("nan"),
             tail.mean() if tail.size else float("nan"),
             20 * np.log10(max(tail.std() / 8388608.0, 1e-12)) if tail.size else float("nan")))

print()
print("VERDICT: lead zeros must be ~8790 for every open before t=30 s and 0 for")
print("every open after it. Anything else is a #201 regression -- see the header.")
