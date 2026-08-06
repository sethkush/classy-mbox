#!/usr/bin/env python3
"""Chapter 9 conformance probe — the parts of #192 that do not need Windows.

WHAT THIS IS NOT. USB20CV is USB-IF's own tool, it runs on Windows, and it is
the AUTHORITY: its verdict is what certification rests on. This is not that, and
running it clean does not make the device compliant. #192 exists precisely
because "everything above this line is our own reading of the spec", and a suite
we wrote is one more reading by the same authors.

WHAT IT IS. Most of USB 2.0 §9.4 is mechanical and unambiguous -- a request is
either answered with the right shape or it is not, and an unsupported one either
stalls or wrongly succeeds. Those cases find real bugs, and they can be exercised
from Linux against the live device. This runs them so that whatever USB20CV
eventually says, it is not saying it about defects we could have found ourselves.

What genuinely still needs USB20CV (or an analyser):
  * malformed packets and timing violations -- libusb cannot emit them
  * SET_ADDRESS behaviour; the host stack owns addressing and re-assigning it
    from userspace would strand the device
  * electrical / signalling tests
  * the descriptor-vs-class-spec rulebook USB20CV encodes, which is broader
    than Chapter 9

CLASSIFY BY ERRNO, NEVER BY MESSAGE. A device STALL is EPIPE (32). EIO means the
HOST STACK refused the transfer -- typically because a driver owns the interface
-- and reads as a device divergence unless you look for it. That mistake was
made once already (probe_feature_requests.py's docstring records it), and it
nearly recorded a device bug that did not exist.

    sudo ch9_probe.py --serial RK10874600Q [--keep-driver]

Safe to run against a working unit: it sends only standard requests, never the
DFU trigger, and restores the configuration and driver binding on the way out.
"""
import argparse
import errno
import sys

try:
    import usb.core
    import usb.util
except ImportError:
    sys.exit("pyusb not installed. On the void box: ~/mbox-venv/bin/python")

MBOX_VID = 0x0DBA
AUDIO_PIDS = (0x1000,) + tuple(range(0x2000, 0x2010))

# bmRequestType
H2D_STD_DEV, D2H_STD_DEV = 0x00, 0x80
H2D_STD_IFACE, D2H_STD_IFACE = 0x01, 0x81
H2D_STD_EP, D2H_STD_EP = 0x02, 0x82

GET_STATUS, CLEAR_FEATURE, SET_FEATURE = 0x00, 0x01, 0x03
SET_ADDRESS, GET_DESCRIPTOR, SET_DESCRIPTOR = 0x05, 0x06, 0x07
GET_CONFIGURATION, SET_CONFIGURATION = 0x08, 0x09
GET_INTERFACE, SET_INTERFACE, SYNCH_FRAME = 0x0A, 0x0B, 0x0C

DT_DEVICE, DT_CONFIG, DT_STRING = 0x01, 0x02, 0x03


class Probe:
    def __init__(self, dev):
        self.dev = dev
        self.passes = 0
        self.failures = []
        self.notes = []

    def _xfer(self, bmreq, breq, wval, widx, data_or_len, timeout=2000):
        """-> ('ok', data) | ('stall', None) | ('hosterr', errno) """
        try:
            r = self.dev.ctrl_transfer(bmreq, breq, wval, widx,
                                       data_or_len, timeout)
            return "ok", r
        except usb.core.USBError as e:
            if e.errno == errno.EPIPE:
                return "stall", None
            return "hosterr", e.errno

    def expect_ok(self, name, bmreq, breq, wval, widx, arg, check=None):
        kind, data = self._xfer(bmreq, breq, wval, widx, arg)
        if kind == "hosterr":
            self.failures.append(f"{name}: host stack refused it (errno "
                                 f"{data}) -- not a device result")
            return None
        if kind == "stall":
            self.failures.append(f"{name}: device STALLed a request it must "
                                 f"answer")
            return None
        if check:
            why = check(data)
            if why:
                self.failures.append(f"{name}: {why}")
                return data
        self.passes += 1
        return data

    def expect_stall(self, name, bmreq, breq, wval, widx, arg):
        kind, data = self._xfer(bmreq, breq, wval, widx, arg)
        if kind == "hosterr":
            # ENOENT is the host stack declining to ROUTE a request naming an
            # interface or endpoint the active configuration does not contain.
            # It never reaches the device, so it is inconclusive -- not a pass
            # and not a device failure. Recording it as either would be a
            # verdict about something that did not happen.
            if data == errno.ENOENT:
                self.notes.append(f"{name}: INCONCLUSIVE -- the host stack "
                                  f"would not route it (ENOENT), so the device "
                                  f"never saw it. Needs USB20CV or an analyser.")
                return
            self.failures.append(f"{name}: host stack refused it (errno "
                                 f"{data}) -- the device never saw it, so this "
                                 f"proves nothing either way")
            return
        if kind == "ok":
            self.failures.append(f"{name}: device ACCEPTED a request that does "
                                 f"not exist; USB 2.0 §9.4 requires a STALL")
            return
        self.passes += 1


def run(p):
    d = p.dev

    # --- §9.4.3 GET_DESCRIPTOR, the shapes that must work -----------------
    p.expect_ok("GET_DESCRIPTOR(device)", D2H_STD_DEV, GET_DESCRIPTOR,
                DT_DEVICE << 8, 0, 18,
                lambda b: None if len(b) == 18 and b[0] == 18 and b[1] == 1
                else f"returned {len(b)} bytes / bLength {b[0] if b else '-'}")

    cfg = p.expect_ok("GET_DESCRIPTOR(config, 9)", D2H_STD_DEV, GET_DESCRIPTOR,
                      DT_CONFIG << 8, 0, 9,
                      lambda b: None if len(b) == 9 and b[1] == 2
                      else f"returned {len(b)} bytes, type {b[1] if len(b)>1 else '-'}")
    total = (cfg[2] | (cfg[3] << 8)) if cfg is not None and len(cfg) >= 4 else None
    if total:
        p.notes.append(f"wTotalLength = {total}")
        p.expect_ok("GET_DESCRIPTOR(config, full)", D2H_STD_DEV,
                    GET_DESCRIPTOR, DT_CONFIG << 8, 0, total,
                    lambda b: None if len(b) == total
                    else f"asked {total}, got {len(b)}")
        # A host may ask for MORE than exists. The device must return what it
        # has and no more -- over-running is how a descriptor read starts
        # spilling adjacent ROM, which looks like corruption at the host.
        p.expect_ok("GET_DESCRIPTOR(config, over-long wLength)", D2H_STD_DEV,
                    GET_DESCRIPTOR, DT_CONFIG << 8, 0, total + 64,
                    lambda b: None if len(b) == total
                    else f"asked {total+64}, device returned {len(b)} "
                         f"(expected exactly {total})")
        # wLength 0 is legal and means "no data stage".
        p.expect_ok("GET_DESCRIPTOR(config, wLength 0)", D2H_STD_DEV,
                    GET_DESCRIPTOR, DT_CONFIG << 8, 0, 0,
                    lambda b: None if len(b) == 0 else f"returned {len(b)}")

    # §9.4.3: an unsupported descriptor TYPE must stall.
    for t, nm in ((0x22, "HID report"), (0x0F, "BOS"), (0x07, "reserved 0x07")):
        p.expect_stall(f"GET_DESCRIPTOR(type 0x{t:02X} {nm}) stalls",
                       D2H_STD_DEV, GET_DESCRIPTOR, t << 8, 0, 64)
    # An out-of-range descriptor INDEX must stall too.
    p.expect_stall("GET_DESCRIPTOR(config index 5) stalls", D2H_STD_DEV,
                   GET_DESCRIPTOR, (DT_CONFIG << 8) | 5, 0, 9)

    # --- strings ----------------------------------------------------------
    lang = p.expect_ok("GET_DESCRIPTOR(string 0, LANGID)", D2H_STD_DEV,
                       GET_DESCRIPTOR, DT_STRING << 8, 0, 255,
                       lambda b: None if len(b) >= 4 and b[1] == 3
                       else f"returned {len(b)} bytes")
    langid = (lang[2] | (lang[3] << 8)) if lang is not None and len(lang) >= 4 else 0x0409
    dev_desc = d.ctrl_transfer(D2H_STD_DEV, GET_DESCRIPTOR, DT_DEVICE << 8, 0, 18)
    for idx, what in ((dev_desc[14], "iManufacturer"),
                      (dev_desc[15], "iProduct"),
                      (dev_desc[16], "iSerialNumber")):
        if idx == 0:
            p.notes.append(f"{what} = 0 (no string), not probed")
            continue
        p.expect_ok(f"GET_DESCRIPTOR(string {idx}, {what})", D2H_STD_DEV,
                    GET_DESCRIPTOR, (DT_STRING << 8) | idx, langid, 255,
                    lambda b: None if len(b) >= 2 and b[1] == 3 and b[0] == len(b)
                    else f"bLength {b[0] if b else '-'} vs {len(b)} returned")
    # A string index nothing declares must stall.
    p.expect_stall("GET_DESCRIPTOR(string 200) stalls", D2H_STD_DEV,
                   GET_DESCRIPTOR, (DT_STRING << 8) | 200, langid, 255)

    # --- §9.4.2 / §9.4.7 configuration ------------------------------------
    p.expect_ok("GET_CONFIGURATION", D2H_STD_DEV, GET_CONFIGURATION, 0, 0, 1,
                lambda b: None if len(b) == 1 and b[0] == 1
                else f"returned {list(b)}, expected [1]")
    # SET_CONFIGURATION to the one we already have is a legal no-op.
    p.expect_ok("SET_CONFIGURATION(1)", H2D_STD_DEV, SET_CONFIGURATION,
                1, 0, None)
    # §9.4.7: a configuration value that does not exist must stall.
    p.expect_stall("SET_CONFIGURATION(9) stalls", H2D_STD_DEV,
                   SET_CONFIGURATION, 9, 0, None)

    # --- §9.4.4 / §9.4.10 interfaces --------------------------------------
    for iface in (0, 1, 2):
        p.expect_ok(f"GET_INTERFACE(iface {iface})", D2H_STD_IFACE,
                    GET_INTERFACE, 0, iface, 1,
                    lambda b: None if len(b) == 1 else f"returned {len(b)} bytes")
    for iface, alt in ((1, 0), (2, 0)):
        p.expect_ok(f"SET_INTERFACE(iface {iface}, alt {alt})", H2D_STD_IFACE,
                    SET_INTERFACE, alt, iface, None)
    # An alternate setting that does not exist must stall.
    p.expect_stall("SET_INTERFACE(iface 1, alt 7) stalls", H2D_STD_IFACE,
                   SET_INTERFACE, 7, 1, None)
    # So must a request naming an interface that does not exist.
    p.expect_stall("GET_INTERFACE(iface 9) stalls", D2H_STD_IFACE,
                   GET_INTERFACE, 0, 9, 1)

    # --- §9.4.5 GET_STATUS at all three recipients ------------------------
    p.expect_ok("GET_STATUS(device)", D2H_STD_DEV, GET_STATUS, 0, 0, 2,
                lambda b: None if len(b) == 2 else f"returned {len(b)} bytes, "
                                                   f"§9.4.5 requires 2")
    p.expect_ok("GET_STATUS(interface 0)", D2H_STD_IFACE, GET_STATUS, 0, 0, 2,
                lambda b: None if len(b) == 2 and b[0] == 0 and b[1] == 0
                else f"returned {list(b)}; §9.4.5 reserves both bytes as zero")
    p.expect_ok("GET_STATUS(endpoint 0)", D2H_STD_EP, GET_STATUS, 0, 0, 2,
                lambda b: None if len(b) == 2 else f"returned {len(b)} bytes")

    # --- §9.4.9 / §9.4.1 features (#188) ----------------------------------
    p.expect_stall("SET_FEATURE(DEVICE_REMOTE_WAKEUP) stalls", H2D_STD_DEV,
                   SET_FEATURE, 1, 0, None)
    p.expect_stall("SET_FEATURE(selector 99) stalls", H2D_STD_DEV,
                   SET_FEATURE, 99, 0, None)
    p.expect_stall("CLEAR_FEATURE(device, selector 99) stalls", H2D_STD_DEV,
                   CLEAR_FEATURE, 99, 0, None)

    # --- §9.4.8 / §9.4.11: requests we do not implement must stall --------
    p.expect_stall("SET_DESCRIPTOR stalls", H2D_STD_DEV, SET_DESCRIPTOR,
                   DT_DEVICE << 8, 0, None)
    p.expect_stall("SYNCH_FRAME stalls", D2H_STD_EP, SYNCH_FRAME, 0, 0x81, 2)
    p.expect_stall("undefined bRequest 0x42 stalls", D2H_STD_DEV, 0x42, 0, 0, 2)

    # --- the property every stall above depends on ------------------------
    # A device that stalls correctly and then stops answering is worse than one
    # that never stalled. This is the check the whole suite rests on.
    p.expect_ok("device still answers after every stall", D2H_STD_DEV,
                GET_DESCRIPTOR, DT_DEVICE << 8, 0, 18,
                lambda b: None if len(b) == 18 else f"returned {len(b)} bytes")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--serial")
    ap.add_argument("--addr", metavar="BUS:ADDR")
    ap.add_argument("--keep-driver", action="store_true",
                    help="do not detach snd-usb-audio; interface-recipient "
                         "cases will then read as host-stack refusals")
    a = ap.parse_args()

    found = []
    for pid in AUDIO_PIDS:
        for dev in usb.core.find(find_all=True, idVendor=MBOX_VID, idProduct=pid):
            found.append(dev)
    if a.serial:
        def sn(x):
            try:
                return usb.util.get_string(x, x.iSerialNumber) if x.iSerialNumber else None
            except Exception:
                return None
        found = [x for x in found if sn(x) == a.serial]
    if a.addr:
        b, ad = (int(v) for v in a.addr.split(":"))
        found = [x for x in found if (x.bus, x.address) == (b, ad)]
    if len(found) != 1:
        sys.exit(f"need exactly one target, matched {len(found)}. "
                 f"Use --serial or --addr; a probe run against the wrong unit "
                 f"looks exactly like a valid one.")
    dev = found[0]

    detached = []
    if not a.keep_driver:
        # Interface-recipient requests are refused by the host stack with EIO
        # while a driver owns the interface, and EIO is NOT a device stall.
        for cfg in dev:
            for intf in cfg:
                n = intf.bInterfaceNumber
                try:
                    if dev.is_kernel_driver_active(n):
                        dev.detach_kernel_driver(n)
                        detached.append(n)
                except Exception:
                    pass
    if detached:
        print(f"detached kernel driver from interface(s) {detached}")

    p = Probe(dev)
    try:
        run(p)
    finally:
        try:
            dev.ctrl_transfer(H2D_STD_DEV, SET_CONFIGURATION, 1, 0, None)
        except Exception:
            pass
        # RESTORING THE BINDING IS PART OF THE TEST, not an afterthought.
        #
        # The first run of this probe left unit A unable to capture: EP0
        # telemetry still answered, so the unit looked healthy while its audio
        # device was simply gone. A diagnostic that quietly disables the thing
        # it is diagnosing is worse than no diagnostic.
        #
        # The fix that matters is the ORDER. attach_kernel_driver() and the
        # driver's sysfs bind both fail while LIBUSB still holds the
        # interfaces, and they fail with EBUSY -- which reads as "something
        # else owns it" rather than "you own it". Release the handle first,
        # then rebind, then VERIFY through sysfs rather than through the
        # pyusb object we just disposed of.
        import glob as _g, os as _os
        base = None
        for q in _g.glob("/sys/bus/usb/devices/*"):
            try:
                if (open(_os.path.join(q, "busnum")).read().strip() == str(dev.bus)
                        and open(_os.path.join(q, "devnum")).read().strip()
                        == str(dev.address)):
                    base = _os.path.basename(q)
                    break
            except OSError:
                continue

        usb.util.dispose_resources(dev)      # <- must precede any rebind

        for n in detached:
            try:
                dev.attach_kernel_driver(n)
            except Exception:
                pass
        if base:
            for n in detached:
                if _os.path.exists(f"/sys/bus/usb/devices/{base}:1.{n}/driver"):
                    continue
                try:
                    with open("/sys/bus/usb/drivers/snd-usb-audio/bind", "w") as fh:
                        fh.write(f"{base}:1.{n}")
                except OSError:
                    pass   # binding iface 0 pulls the others back with it

        bound = [n for n in detached if base and
                 _os.path.exists(f"/sys/bus/usb/devices/{base}:1.{n}/driver")]
        if bound:
            print(f"  driver rebound on interface(s) {bound}")
        else:
            print("  DRIVER NOT REBOUND -- audio is down on this unit until it "
                  "is rebound by hand:")
            print(f"      echo {base or '<dev>'}:1.0 | sudo tee "
                  f"/sys/bus/usb/drivers/snd-usb-audio/bind")

    for n in p.notes:
        print(f"  note   {n}")
    print()
    if p.failures:
        print(f"CH9 FAIL: {p.passes} passed, {len(p.failures)} failed")
        for f in p.failures:
            print(f"  - {f}")
        return 1
    print(f"CH9 PASS: all {p.passes} Chapter 9 checks behaved as USB 2.0 §9.4 "
          f"requires")
    print("This is NOT certification -- USB20CV remains the authority (#192).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
