"""Drop a specific mboxfw unit into DFU, selected by SERIAL.

Uses TLM_REQ_ENTER_DFU (0x12), the DEVICE-recipient vendor alias. The Digi class
request does the same job but is INTERFACE-recipient, so the host stack rejects
it with EBUSY once snd-usb-audio binds the audio interfaces -- which is the
normal state of a working unit and therefore the state you actually want to
trigger from. tools/../trigger.py takes the class route and has to detach the
kernel driver first; this does not.

Selects by serial, never by card or address: there are two units on the void box
and a trigger sent to the wrong one costs a 2 km round trip.

WHAT THIS DOES, AND WHY IT IS NOT REVERSIBLE FROM HERE. The handler ACKs, then
main() disconnects, zeroes the EEPROM header CHECKSUM and halts. The unit goes
dark and drops off the bus. On the next power-up the boot ROM finds a bad
checksum, refuses the image, and comes up in DFU instead of running the app.

That next power-up must be a real one -- a physical unplug and ~10-15 s. A USB
bus reset (uhubctl -a cycle) is RSTR_INT and will not do it. So after running
this the unit needs a human at the bench before it will run anything again.

The signature is deliberately NOT touched: zeroing it reaches DFU too, but the
boot ROM then cannot write the EEPROM (errPROG) and the unit cannot be flashed.
Breaking the checksum is the recoverable form.
"""
import sys
import usb.core

TLM_REQ_ENTER_DFU = 0x12

if len(sys.argv) < 2:
    sys.exit("usage: enter_dfu_serial.py <serial> [<serial> ...]")

for serial in sys.argv[1:]:
    dev = None
    for d in usb.core.find(find_all=True, idVendor=0x0DBA):
        try:
            if d.serial_number == serial:
                dev = d
                break
        except Exception:
            pass
    if dev is None:
        print("%-14s NOT FOUND -- skipped" % serial)
        continue

    print("%-14s 0dba:%04x bus %d addr %d -> sending ENTER_DFU"
          % (serial, dev.idProduct, dev.bus, dev.address))
    try:
        dev.ctrl_transfer(0x40, TLM_REQ_ENTER_DFU, 0, 0, None, 2000)
        print("%-14s ACKed; unit will zero its header checksum and halt" % serial)
    except usb.core.USBError as e:
        # The device halting mid-status-stage is a normal outcome here, not a
        # failure: it disconnects as part of servicing the request.
        print("%-14s transfer ended with %s (expected if it halted)" % (serial, e))
