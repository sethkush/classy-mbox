"""Fire the #197 gate probe (TLM_REQ_GATE_PROBE, 0x16) at one unit.

    gateprobe.py <BUS:ADDR> isr <hold_ms>   -- publish from the EP0 handler
    gateprobe.py <BUS:ADDR> main            -- arm a MAIN-LOOP publish

The point of the pair: the capture gate emits exact digital zeros when low, so
a pulse fired during a live capture is a measurable gap in the recording, and
that gap witnesses what the codec ACCEPTED. Telemetry cannot do this -- block 9
reports mboxfw's mirror of the codec word, never what the shift register
latched. FINDING_197, "2026-08-07".

The `isr` form holds the gate low across two separate control transfers, which
is exactly how the host's own class SET_CUR mute behaves, and both publishes
happen inside isr_int0. The `main` form sets a flag; main() does the publishing
and times the hold itself with hw_short_delay(), ~3 ms. The two differ in
execution context and nothing else -- same function, same bits, same word.

Device recipient, so this works with snd-usb-audio bound. That is required, not
incidental: the capture being measured is running at the time.
"""
import sys, time, usb.core

if len(sys.argv) < 3:
    sys.exit(__doc__)

bus, addr = (int(x) for x in sys.argv[1].split(":"))
mode = sys.argv[2]
hold = float(sys.argv[3]) if len(sys.argv) > 3 else 20.0

dev = usb.core.find(bus=bus, address=addr, idVendor=0x0DBA)
if dev is None:
    sys.exit("no 0dba device at %d:%d" % (bus, addr))

REQ = 0x16


def send(wValue):
    dev.ctrl_transfer(0x40, REQ, wValue, 0, None, timeout=1000)


if mode == "isr":
    # Hold, then release. Two transfers, both serviced in ISR context.
    send(1)
    time.sleep(hold / 1000.0)
    send(0)
    print("isr: gate held low ~%.1f ms across two EP0 transfers" % hold)
elif mode == "main":
    send(2)
    print("main: main-loop pulse armed; firmware times its own ~3 ms hold")
else:
    sys.exit("mode must be 'isr' or 'main'")
