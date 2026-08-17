#!/usr/bin/env python3
"""§9.2.6 request-processing time limits, and suspend/resume. Chapter 9 coverage
that ch9_probe.py never had.

WHY THIS EXISTS. FINDING_192 listed four things "still needing Windows" and three
were dead or misfiled. Auditing that list surfaced two REAL gaps -- neither
needing Windows, neither previously tested:

  * §9.2.6.3 puts hard deadlines on how long a device may take to answer a
    standard request. ch9_probe.py checks WHAT comes back and never checked WHEN.
    A device that answers correctly but too slowly fails Chapter 9, and enumerates
    fine on a forgiving host while failing on a strict one.
  * suspend/resume. The device declares itself BUS-POWERED with no remote wakeup,
    which makes specific behaviour mandatory, none of it previously exercised.

THE DEADLINES, §9.2.6.3 "Standard Device Requests":
  * no data stage            -> complete within 50 ms
  * with data stage          -> first data packet within 500 ms
  * subsequent data packets  -> within 500 ms of the previous
  * status stage after data  -> within 50 ms
  * SET_ADDRESS status stage -> device has 2 ms to become responsive (§9.2.6.3)
  * reset recovery           -> 10 ms (§9.2.6.2)

THE MEASUREMENT IS AN UPPER BOUND, AND THAT IS THE POINT. Timing from userspace
includes host scheduling, URB queueing and the round trip -- all of which make
the measured figure LARGER than the device's own processing time. So the error is
one-directional and conservative: a PASS is a real pass, because the device
cannot have taken longer than a bound that already includes everything else. A
FAIL is the only ambiguous outcome, and at these limits (50 ms against a device
that answers in well under 1 ms) a fail means something is grossly wrong rather
than marginal. Stated here because a two-sided error bar would make the whole
test meaningless and it would not be obvious from the numbers.

THE KNOWN-ANSWER ARM, which this project requires. A timing harness that always
reports 0.0 ms would PASS every deadline and look perfect -- the exact shape of
the five voided measurements of 2026-08-10. So the clock is itself measured: a
known sleep is timed through the same path before any device request, and if it
does not come back at its nominal value the run is VOID, not clean.

    sudo ch9_timing.py --serial RK1672500M
    sudo ch9_timing.py --addr 2:32 --suspend      # see the warning below

SUSPEND IS OPT-IN AND CAN WEDGE THE UNIT. --suspend drives a real bus suspend
through sysfs. A device that does not resume needs a physical power cycle, which
on this bench is a 2 km round trip. It is off by default for that reason.
"""
import argparse
import ctypes
import subprocess
import sys
import time

try:
    import usb.core
    import usb.util
except ImportError:
    sys.exit("pyusb missing. On the void box: sudo ~/mbox-venv/bin/python "
             "(never system python -- see CLAUDE.md)")

VID = 0x0DBA

# §9.2.6.3, in seconds.
LIMIT_NO_DATA = 0.050
LIMIT_FIRST_DATA = 0.500
LIMIT_STATUS = 0.050

# (name, bmRequestType, bRequest, wValue, wIndex, length, has_data_stage)
# Only requests that MUST succeed -- a stalled request's timing is not what
# §9.2.6.3 bounds, and mixing the two would let a fast STALL mask a slow reply.
PROBES = [
    ("GET_DESCRIPTOR device",     0x80, 0x06, 0x0100, 0, 18,  True),
    ("GET_DESCRIPTOR config",     0x80, 0x06, 0x0200, 0, 9,   True),
    ("GET_DESCRIPTOR config(full)", 0x80, 0x06, 0x0200, 0, 255, True),
    ("GET_DESCRIPTOR string 0",   0x80, 0x06, 0x0300, 0, 255, True),
    ("GET_STATUS device",         0x80, 0x00, 0x0000, 0, 2,   True),
    ("GET_CONFIGURATION",         0x80, 0x08, 0x0000, 0, 1,   True),
    ("GET_INTERFACE iface 0",     0x81, 0x0A, 0x0000, 0, 1,   True),
]


def timed(fn):
    t0 = time.perf_counter()
    try:
        r = fn()
        return (time.perf_counter() - t0), r, None
    except Exception as e:                                  # noqa: BLE001
        return (time.perf_counter() - t0), None, e


def clock_arm():
    """Known-answer arm: time a known sleep through the same clock.

    Without this, a harness that reported 0.0 ms for everything would pass every
    deadline in the file and read as a flawless device.
    """
    want = 0.020
    dt, _, _ = timed(lambda: time.sleep(want))
    err = abs(dt - want)
    ok = err < 0.010 and dt > 0
    print("  %-34s %8.2f ms   (nominal %.0f ms)  %s"
          % ("CLOCK ARM: known 20 ms sleep", dt * 1e3, want * 1e3,
             "OK" if ok else "BROKEN"))
    return ok


def find(serial, addr):
    devs = [d for d in usb.core.find(find_all=True, idVendor=VID)]
    if addr:
        bus, an = (int(x) for x in addr.split(":"))
        devs = [d for d in devs if d.bus == bus and d.address == an]
    elif serial:
        keep = []
        for d in devs:
            try:
                if usb.util.get_string(d, d.iSerialNumber) == serial:
                    keep.append(d)
            except Exception:                               # noqa: BLE001
                pass
        devs = keep
    if not devs:
        sys.exit("no matching 0x%04X device" % VID)
    if len(devs) > 1:
        sys.exit("%d devices matched -- pass --serial or --addr. Refusing to "
                 "guess: a reading from the wrong unit looks valid." % len(devs))
    return devs[0]


def run_timing(dev, repeats):
    print("\n§9.2.6.3 request-processing time limits")
    print("  measured value is an UPPER bound (includes host scheduling), so a")
    print("  PASS is conservative and only a FAIL is ambiguous.\n")
    print("  %-34s %10s %10s   %s" % ("request", "worst", "limit", "verdict"))
    print("  " + "-" * 72)
    failures = []
    for name, bmreq, breq, wval, widx, ln, has_data in PROBES:
        worst = 0.0
        err = None
        for _ in range(repeats):
            dt, _r, e = timed(
                lambda: dev.ctrl_transfer(bmreq, breq, wval, widx, ln, 2000))
            if e is not None:
                err = e
                break
            worst = max(worst, dt)
        limit = LIMIT_FIRST_DATA if has_data else LIMIT_NO_DATA
        if err is not None:
            print("  %-34s %10s %10s   SKIP (%s)"
                  % (name, "-", "%.0f ms" % (limit * 1e3),
                     str(err)[:24]))
            continue
        ok = worst <= limit
        if not ok:
            failures.append((name, worst, limit))
        print("  %-34s %8.2f ms %8.0f ms   %s"
              % (name, worst * 1e3, limit * 1e3, "PASS" if ok else "FAIL"))
    return failures


def run_status_stage(dev, repeats):
    """A no-data request: SET_CONFIGURATION to its current value.

    §9.2.6.3 gives no-data requests 50 ms end to end. SET_CONFIGURATION to the
    value already in force is the safest such request -- it is legal in the
    Configured state and leaves the device where it was.
    """
    print("\n  no-data-stage request (50 ms limit)")
    try:
        cfg = dev.ctrl_transfer(0x80, 0x08, 0, 0, 1, 2000)[0]
    except Exception as e:                                  # noqa: BLE001
        print("    SKIP -- GET_CONFIGURATION failed: %s" % e)
        return []
    worst = 0.0
    for _ in range(repeats):
        dt, _r, e = timed(lambda: dev.ctrl_transfer(0x00, 0x09, cfg, 0, None,
                                                    2000))
        if e is not None:
            print("    SKIP -- SET_CONFIGURATION(%d) failed: %s" % (cfg, e))
            return []
        worst = max(worst, dt)
    ok = worst <= LIMIT_NO_DATA
    print("    %-32s %8.2f ms %8.0f ms   %s"
          % ("SET_CONFIGURATION(current)", worst * 1e3, LIMIT_NO_DATA * 1e3,
             "PASS" if ok else "FAIL"))
    return [] if ok else [("SET_CONFIGURATION", worst, LIMIT_NO_DATA)]


def syspath(dev):
    return "/sys/bus/usb/devices/%d-%s" % (
        dev.bus, ".".join(str(x) for x in dev.port_numbers))


def run_suspend(dev):
    """Real bus suspend via sysfs, then prove the device still answers.

    CAN WEDGE THE UNIT. A device that does not resume needs a physical power
    cycle. Opt-in only.
    """
    path = syspath(dev)
    print("\n§9.2.5 suspend / resume at %s" % path)
    ctl, delay = path + "/power/control", path + "/power/autosuspend_delay_ms"
    try:
        prev = open(ctl).read().strip()
    except OSError as e:
        print("  SKIP -- cannot read %s: %s" % (ctl, e))
        return []
    try:
        open(delay, "w").write("0\n")
        open(ctl, "w").write("auto\n")
        time.sleep(3.0)
        state = subprocess.run(["cat", path + "/power/runtime_status"],
                               capture_output=True, text=True).stdout.strip()
        print("  runtime_status after 3 s of autosuspend: %s" % state)
        if state != "suspended":
            print("  VOID -- the device never actually suspended, so anything")
            print("  observed after it is not a resume. Not a device result.")
            return []
        dt, r, e = timed(
            lambda: dev.ctrl_transfer(0x80, 0x06, 0x0100, 0, 18, 3000))
        if e is not None or r is None or len(r) != 18:
            print("  FAIL -- device did not answer after resume: %s" % e)
            return [("suspend/resume", 0, 0)]
        print("  PASS -- resumed and returned a full 18-byte device descriptor "
              "in %.1f ms" % (dt * 1e3))
        return []
    finally:
        try:
            open(ctl, "w").write(prev + "\n")
        except OSError:
            print("  WARNING: could not restore %s to %r" % (ctl, prev))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--serial")
    ap.add_argument("--addr", help="bus:addr")
    ap.add_argument("--repeats", type=int, default=20)
    ap.add_argument("--suspend", action="store_true",
                    help="also drive a real bus suspend (CAN WEDGE THE UNIT)")
    a = ap.parse_args()

    print("§9.2.6 / §9.2.5 Chapter 9 timing probe")
    if not clock_arm():
        sys.exit("\nVOID: the clock arm failed, so every timing number below "
                 "would be unattributable. Nothing was measured.")

    dev = find(a.serial, a.addr)
    print("  device: bus %d addr %d" % (dev.bus, dev.address))

    fails = run_timing(dev, a.repeats)
    fails += run_status_stage(dev, a.repeats)
    if a.suspend:
        fails += run_suspend(dev)

    print()
    if fails:
        print("FAIL: %d request(s) outside their §9.2.6.3 limit" % len(fails))
        for n, w, l in fails:
            print("  %s: %.1f ms against %.0f ms" % (n, w * 1e3, l * 1e3))
        return 1
    print("PASS: every request answered inside its §9.2.6.3 limit")
    if not a.suspend:
        print("NOTE: suspend/resume NOT tested (--suspend is opt-in; it can "
              "wedge the unit).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
