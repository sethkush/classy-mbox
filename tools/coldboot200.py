"""E1 done properly: set the mask BEFORE the power-up's first calibration.

E1 as first run could not reproduce the transient, and the reason is in its own
output. By the time the mask reached 0x00, the shipping arm had already run and
calibrated the part with the analog reference fully settled. Mask 0x00 does not
un-calibrate anything -- it only stops FURTHER calibration -- so the offset
register still held a good value and there was nothing to reveal.

The pre-fix build calibrated exactly ONCE per power-up, at the first stream open
(g_path_enabled is never cleared on close). Whether that calibration was good
depended entirely on when the user first hit record: the analog reference takes
~16 s to settle, and a calibration at 1-2 s latches a VCOM ~17 mV from final.

So the condition to reproduce is not "no calibration". It is "ONE calibration,
taken early". That needs a genuine power cycle, because:

  - the mask defaults to 0x0C at boot, by design
  - a bus reset does not re-run main(), so codec_init() does not re-run and the
    offset register keeps whatever it holds
  - only a cold boot puts RST low again with the reference still charging

This script must therefore start within a couple of seconds of the unit
appearing. It sets mask 0x00 first -- so the ONLY rising edge of the power-up is
the one its own first capture causes -- then captures immediately and keeps
capturing, so the transient's amplitude can be tracked against elapsed time.

Usage:  sudo coldboot200.py <serial>     # start it, THEN power-cycle the unit
"""
import sys
import time
import subprocess
import wave

import usb.core
import numpy as np

sys.stdout.reconfigure(line_buffering=True)

TLM_REQ_DIAG_MODE = 0x17
TLM_REQ_READ = 0x10
PREFIX, SHIPPING = 0x00, 0x0C

serial = sys.argv[1]
mask = int(sys.argv[2], 0) if len(sys.argv) > 2 else PREFIX

def present(sn):
    """True if a unit with this serial is on the bus.

    serial_number access RAISES during an unplug/replug race -- the device
    descriptor is readable but the string descriptors are not yet, and pyusb
    turns that into ValueError("no langid"). The first version of this script had
    no guard and died the moment the unit was pulled, wasting a power cycle.
    """
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
print("gone. waiting for it to come back...")

# Wait for ABSENCE then presence. Matching on presence alone matches the stale
# node from before the unplug -- that mistake has cost replugs before.
dev = None
while dev is None:
    for d in usb.core.find(find_all=True, idVendor=0x0dba):
        try:
            if d.serial_number == serial:
                dev = d
                break
        except Exception:
            pass
    time.sleep(0.05)
t_boot = time.time()
print("back at t=0. setting mask 0x%02X before anything can calibrate..." % mask)

for attempt in range(50):
    try:
        dev.ctrl_transfer(0x40, TLM_REQ_DIAG_MODE, mask, 0, None, 1000)
        got = bytes(dev.ctrl_transfer(0xC0, TLM_REQ_READ, 12, 0, 8, 1000))
        if got[0] == mask:
            print("  mask confirmed 0x%02X at t=%.2f s, RST cycles so far = %d"
                  % (got[0], time.time() - t_boot, got[2]))
            break
    except usb.core.USBError:
        time.sleep(0.1)
else:
    sys.exit("could not set the mask -- ABORT, the run would be meaningless")

# Find the card that belongs to THIS unit, by port path, never by index.
port = None
import glob, os
for d in glob.glob("/sys/bus/usb/devices/*"):
    try:
        if open(os.path.join(d, "serial")).read().strip() == serial:
            port = os.path.basename(d)
    except OSError:
        pass
card = None
for c in glob.glob("/proc/asound/card*/usbbus"):
    pass
for c in glob.glob("/sys/class/sound/card*"):
    real = os.path.realpath(c)
    if port and ("/%s/" % port) in real + "/":
        card = c.rsplit("card", 1)[1]
if card is None:
    for line in open("/proc/asound/cards"):
        if port and port.replace("2-1.", "1.") in line:
            card = line.split("[")[0].strip()
print("  unit is at port %s, card %s" % (port, card))

print("elapsed  leadzeros   openingDC(LSB24)   level(dBFS)   rstcycles")
for i in range(14):
    el = time.time() - t_boot
    subprocess.run(["arecord", "-D", "hw:%s" % card, "-f", "S24_3LE", "-c", "2",
                    "-r", "48000", "-d", "2", "/tmp/cb.wav"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        w = wave.open("/tmp/cb.wav", "rb"); fs = w.getframerate(); n = w.getnframes()
        a = np.frombuffer(w.readframes(n), dtype=np.uint8).reshape(n, 6).astype(np.int32)
        w.close()
    except Exception as e:
        print("  %5.1f s  capture failed: %s" % (el, e)); time.sleep(1); continue
    v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
    x = np.where(v & 0x800000, v - (1 << 24), v).astype(np.float64)
    nz = np.nonzero(x)[0]
    lead = int(nz[0]) if len(nz) else len(x)
    head = x[lead:lead + int(0.05 * fs)]
    cyc = bytes(dev.ctrl_transfer(0xC0, TLM_REQ_READ, 12, 0, 8, 1000))[2]
    print("  %5.1f s   %6d     %+10.1f       %7.1f        %d"
          % (el, lead, head.mean(), 20*np.log10(max(head.std()/8388608.0, 1e-12)), cyc))
    time.sleep(1.0)

dev.ctrl_transfer(0x40, TLM_REQ_DIAG_MODE, SHIPPING, 0, None, 1000)
print("mask restored to shipping")
