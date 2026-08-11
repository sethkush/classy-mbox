"""#199 -- does reprogramming the clocks disturb the AK5383's digital filter?

The mechanism proposed for #197 is that reprogramming the clocks disturbs the
converter's filter state, so its high-pass re-converges from scratch and reveals
whatever DC sits under it for ~2 tau. Every measurement so far is consistent with
it and none tests it, because neither shipping build offers the arm: 0x004A
always brackets the reprogramming with RST, and 0x004B gates reprogramming and
bracket together, so a same-rate request does nothing at all.

Build 0x004D adds the arm. TLM_REQ_SET_CLOCK with wIndexH == 0xD1 reprograms the
clocks with RST left HIGH -- no calibration, no tRTV mute.

The part must be given a DC to reveal. On a calibrated converter the underlying
offset is ~0, so a disturbance has nothing to show and the result is flat either
way, which proves nothing. So: switch the source to change the steady analog DC,
wait for the high-pass to cancel it, and only THEN fire the diagnostic. The
filter is at that point actively holding a ~6000 LSB24 correction.

  ARMED    mux switched 2.5 s earlier, correction being held, then diag fires
             DC reappears, decaying with tau ~ 176 ms  -> reprogramming DOES
                                                          disturb the filter.
                                                          MECHANISM PROVED.
             flat                                      -> it does not. The
                                                          transient came from
                                                          somewhere else, and
                                                          the account in
                                                          FINDING_197 is wrong.

  CONTROL  same diagnostic, no mux switch beforehand. Must be flat under either
           reading -- there is no DC to reveal. If this one shows a step, the
           diagnostic itself is producing the artefact and neither ARMED result
           can be believed.

  LIVE     an ordinary mux step with no clock event, for the amplitude scale.

Both arms run inside one capture so gain and drift cannot differ between them.

Usage:  sudo python3 diag199.py <serial> <card> <rate> <out.wav>
"""
import sys
import time
import subprocess
import usb.core

MIC, LINE = 0x06, 0x05
NO_RST = 0xD1

serial, card, rate, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]

dev = next((d for d in usb.core.find(find_all=True, idVendor=0x0dba)
            if d.serial_number == serial), None)
if dev is None:
    sys.exit("no device with serial %s -- select by serial, never by card "
             "number, which swaps across bus resets" % serial)


def setmux(pat):
    dev.ctrl_transfer(0x40, 0x13, pat | (pat << 3), 0, None, 1000)


def diag_reprogram():
    """Reprogram the clocks with RST left high. wIndexL 0xFF = leave the
    Selector alone; wIndexH 0xD1 = suppress the RST bracket."""
    dev.ctrl_transfer(0x40, 0x14, 2 if rate == 48000 else 1,
                      (NO_RST << 8) | 0xFF, None, 2000)


cur = MIC
setmux(cur)
time.sleep(0.5)

rec = subprocess.Popen(["arecord", "-D", "hw:%s" % card, "-f", "S24_3LE",
                        "-c", "2", "-r", str(rate), "-d", "26", out],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
t0 = time.time()


def at(t):
    time.sleep(max(0.0, t0 + t - time.time()))


def flip():
    global cur
    cur = LINE if cur == MIC else MIC
    setmux(cur)
    return cur


print("t=1.5   mux flip           (ARMED: high-pass begins cancelling)")
at(1.5);  flip()
print("t=4.0   DIAG reprogram     <-- ARMED, correction is being held")
at(4.0);  diag_reprogram()
print("t=8.0   mux flip back      (settle)")
at(8.0);  flip()
print("t=12.0  DIAG reprogram     <-- CONTROL, nothing to reveal")
at(12.0); diag_reprogram()
print("t=16.0  mux flip           <-- LIVE step, amplitude scale")
at(16.0); flip()
print("t=20.0  mux flip back")
at(20.0); flip()

rec.wait()
print("wrote", out)
