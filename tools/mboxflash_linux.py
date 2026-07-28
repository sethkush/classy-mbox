#!/usr/bin/env python3
"""Linux flasher for the Digidesign Mbox 1 (TAS1020B boot-ROM DFU).

A pyusb port of the macOS/IOKit `mboxflash` tool. Same wire protocol, same
validation rules, same retry/restart policy — see mboxflash/dfu.m and
mboxflash/main.m for the annotated originals and the reference citations.

Usage:
    sudo ./mboxflash_linux.py probe
    sudo ./mboxflash_linux.py validate <image.bin>
    sudo ./mboxflash_linux.py info     <image.bin>
    sudo ./mboxflash_linux.py flash    <image.bin> [--yes]

Device modes:
    0xFFFF:0xFFFE  bulletproof DFU (boot ROM, SDA short or blank EEPROM)
    0x0DBA:0x1001  app-DFU        (valid header, dataType != APPCODE)
    0x0DBA:0x1000  audio mode     (running app — not flashable)
"""

import argparse
import struct
import sys
import time

# pyusb is only needed for the device-touching commands. `validate` and `info`
# are pure file analysis and must run anywhere — including the dev machine that
# builds the images but never sees the hardware.
try:
    import usb.core
    import usb.util
    HAVE_USB = True
except ImportError:
    HAVE_USB = False


def require_usb():
    if not HAVE_USB:
        sys.exit("pyusb not installed:  pip3 install pyusb  (or apt install python3-usb)")

# ---------------------------------------------------------------- constants

DFU_DEVICES = [(0xFFFF, 0xFFFE, "bulletproof-DFU"),
               (0x0DBA, 0x1001, "app-DFU")]
AUDIO_DEVICE = (0x0DBA, 0x1000)
DFU_INTERFACE = 0          # both DFU PIDs expose the class requests on iface 0

# DFU 1.0 §6.1 request codes
DFU_DETACH, DFU_DNLOAD, DFU_UPLOAD = 0x00, 0x01, 0x02
DFU_GETSTATUS, DFU_CLRSTATUS, DFU_GETSTATE, DFU_ABORT = 0x03, 0x04, 0x05, 0x06

REQ_OUT = 0x21             # class | host-to-device | interface
REQ_IN = 0xA1              # class | device-to-host | interface

STATE_NAMES = ["appIDLE", "appDETACH", "dfuIDLE", "dfuDNLOAD_SYNC",
               "dfuDNBUSY", "dfuDNLOAD_IDLE", "dfuMANIFEST_SYNC",
               "dfuMANIFEST", "dfuMANIFEST_WAIT_RESET", "dfuUPLOAD_IDLE",
               "dfuERROR"]
(appIDLE, appDETACH, dfuIDLE, dfuDNLOAD_SYNC, dfuDNBUSY, dfuDNLOAD_IDLE,
 dfuMANIFEST_SYNC, dfuMANIFEST, dfuMANIFEST_WAIT_RESET, dfuUPLOAD_IDLE,
 dfuERROR) = range(11)

STATUS_NAMES = ["OK", "errTARGET", "errFILE", "errWRITE", "errERASE",
                "errCHECK_ERASED", "errPROG", "errVERIFY", "errADDRESS",
                "errNOTDONE", "errFIRMWARE", "errVENDOR", "errUSBR",
                "errPOR", "errUNKNOWN", "errSTALLEDPKT"]

# bStatus values that DFU 1.1 §6.1.2 treats as transient — safe to recover
# from with CLRSTATUS and a whole-flash restart (see mboxflash/main.m).
TRANSIENT_STATUS = {12, 13, 14, 15}   # errUSBR, errPOR, errUNKNOWN, errSTALLEDPKT

PAGE = 32                  # EEPROM page size = DFU block size
EEPROM_BUDGET = 8192


def state_name(s):
    return STATE_NAMES[s] if s < len(STATE_NAMES) else "unknown(%d)" % s


def status_name(s):
    return "%s(%d)" % (STATUS_NAMES[s], s) if s < len(STATUS_NAMES) else "unknown(%d)" % s


# ------------------------------------------------------------ payload parse
# Record format (mboxflash/payload.h): u32 length BE, u32 address BE,
# u32 type BE, u8 data[length].  type 0 = data, 1 = EOF.

class Record:
    __slots__ = ("file_offset", "length", "address", "type", "data")


def parse_records(blob, start):
    """Parse until a bad record or an EOF record. Mirrors MBoxPayload_Parse."""
    out = []
    off = start
    while True:
        if off + 12 > len(blob):
            break
        length, address, rtype = struct.unpack_from(">III", blob, off)
        if length == 0 or length > 256:
            break
        if rtype > 5:
            break
        if off + 12 + length > len(blob):
            break
        r = Record()
        r.file_offset, r.length, r.address, r.type = off, length, address, rtype
        r.data = blob[off + 12: off + 12 + length]
        out.append(r)
        off += 12 + length
        if rtype == 1:
            break
    return out


def autodetect(blob):
    """Find where the record stream starts. Mirrors MBoxPayload_Autodetect:
    len=32 BE, addr=0, type=0, [wildcard chksum], 0x12 0x12 0x34, 0x0d 0xba."""
    prefix = (b"\x00\x00\x00\x20"      # length 32 BE
              b"\x00\x00\x00\x00"      # address 0
              b"\x00\x00\x00\x00")     # type 0
    tail = b"\x12\x12\x34\x0d\xba"     # headerSize + Mbox sig + Digi VID
    siglen = len(prefix) + 1 + len(tail)
    for i in range(0, max(0, len(blob) - siglen + 1)):
        if blob[i:i + len(prefix)] != prefix:
            continue
        # blob[i+12] is the chksum byte — payload-size dependent, wildcarded.
        if blob[i + len(prefix) + 1: i + len(prefix) + 1 + len(tail)] == tail:
            return i
    return None


def load_image(path):
    with open(path, "rb") as f:
        blob = f.read()
    start = autodetect(blob)
    if start is None:
        sys.exit("FAIL: autodetect signature not found — %s is not a wrapped "
                 "Mbox firmware image" % path)
    recs = parse_records(blob, start)
    if not recs:
        sys.exit("FAIL: parser returned zero records")
    return blob, start, recs


def validate(path, quiet=False):
    """Static validation. Mirrors cmd_validate in mboxflash/main.m."""
    blob, start, recs = load_image(path)
    fails = []
    say = (lambda *a: None) if quiet else print

    if start != 0:
        say("  WARN: autodetect matched at offset 0x%x (expected 0)" % start)
    say("  autodetect signature   OK  (offset %d)" % start)

    expect = 0
    for i, r in enumerate(recs):
        is_last = (i == len(recs) - 1)
        # Every record but the last must be exactly one EEPROM page. The last
        # may be short so the total equals header+payloadSize exactly — padding
        # past payloadSize triggers errFILE and bricks the flash (2026-07-22).
        if r.length != PAGE and not is_last:
            fails.append("record %d has length %d (expected 32)" % (i, r.length))
        if not (1 <= r.length <= PAGE):
            fails.append("record %d has length %d (must be 1..32)" % (i, r.length))
        if r.type != 0:
            fails.append("record %d has type %d (expected 0)" % (i, r.type))
        if r.address != expect:
            fails.append("record %d addr=0x%04x (expected 0x%04x)" % (i, r.address, expect))
        expect += r.length
    if not fails:
        say("  %d records contiguous   OK  (0x0000..0x%04X)" % (len(recs), expect - 1))

    h = recs[0].data
    if len(h) < 18:
        fails.append("record 0 is %d bytes — cannot contain the 18-byte header" % len(h))
        h = h + bytes(18 - len(h))

    chk = sum(h[1:18]) & 0xFF
    if h[0] != chk:
        fails.append("header chksum=0x%02X, computed=0x%02X" % (h[0], chk))
    else:
        say("  header chksum          OK  (0x%02X)" % h[0])
    if h[1] != 18:
        fails.append("headerSize=%d (expected 18)" % h[1])
    if h[2] != 0x12 or h[3] != 0x34:
        fails.append("sig bytes = 0x%02X 0x%02X (expected 0x12 0x34)" % (h[2], h[3]))

    vid = (h[4] << 8) | h[5]
    pid = (h[6] << 8) | h[7]
    if vid != 0x0DBA:
        fails.append("VID=0x%04X (expected 0x0DBA)" % vid)
    if pid not in (0x1000, 0x1001):
        fails.append("PID=0x%04X (expected 0x1000 or 0x1001)" % pid)
    else:
        say("  VID:PID                OK  (0x%04X:0x%04X — %s)"
            % (vid, pid, "flasher/DFU" if pid == 0x1001 else "audio-mode"))

    # Header field order per tools/wrap_hex.py:
    #   0 chksum, 1 headerSize, 2-3 sig, 4-5 VID, 6-7 PID, 8 productVersion,
    #   9 FirmwareVersion, 10 usbAttribute, 11 maxPower(mA/2), 12 attribute,
    #   13 wPageSize, 14 dataType, 15 rPageSize, 16-17 payloadSize BE.
    max_power = h[11]
    page_size = h[13]
    data_type = h[14]
    payload_size = (h[16] << 8) | h[17]
    if page_size != PAGE:
        fails.append("wPageSize=%d (expected 32)" % page_size)
    if data_type not in (0x01, 0x03):
        fails.append("dataType=0x%02X (expected 0x01 APPCODE or 0x03 APPCODE_UPDATING)"
                     % data_type)
    total = sum(r.length for r in recs)
    if 18 + payload_size > total:
        fails.append("header says payload=%d B but image has only %d B after the header"
                     % (payload_size, total - 18))
    elif 18 + payload_size > EEPROM_BUDGET:
        fails.append("payload+header = %d B exceeds the 8 KB EEPROM budget" % (18 + payload_size))
    else:
        say("  payload size           OK  (%d B code, %d B total image)" % (payload_size, total))
    say("  dataType               0x%02X (%s), wPageSize %d, maxPower %d mA" % (
        data_type, {0x01: "APPCODE", 0x03: "APPCODE_UPDATING"}.get(data_type, "unknown"),
        page_size, max_power * 2))

    if fails:
        for f in fails:
            print("FAIL: " + f, file=sys.stderr)
        print("\nFAIL: %d validation issue(s) in %s" % (len(fails), path), file=sys.stderr)
        return None
    say("\nPASS: %s is a valid Mbox 1 firmware image" % path)
    return recs


# ------------------------------------------------------------- device layer

def find_device(require_dfu=True):
    require_usb()
    for vid, pid, label in DFU_DEVICES:
        dev = usb.core.find(idVendor=vid, idProduct=pid)
        if dev is not None:
            return dev, vid, pid, label
    if not require_dfu:
        dev = usb.core.find(idVendor=AUDIO_DEVICE[0], idProduct=AUDIO_DEVICE[1])
        if dev is not None:
            return dev, AUDIO_DEVICE[0], AUDIO_DEVICE[1], "audio-mode"
    return None, None, None, None


def open_dfu():
    dev, vid, pid, label = find_device()
    if dev is None:
        audio = usb.core.find(idVendor=AUDIO_DEVICE[0], idProduct=AUDIO_DEVICE[1])
        if audio is not None:
            sys.exit("Found 0dba:1000 (audio mode) — the app is running, not DFU.\n"
                     "Hold the channel-1 source button while plugging in to reach DFU,\n"
                     "or short SDA as a last resort.")
        sys.exit("No Mbox found. Expected 0xFFFF:0xFFFE (bulletproof) or 0x0DBA:0x1001 (app-DFU).")
    print("device: %04x:%04x (%s)" % (vid, pid, label))
    # The kernel binds nothing to bDeviceClass 0xFE in practice, but a stale
    # usbfs claim or a generic driver would make ctrl_transfer fail with EBUSY.
    try:
        if dev.is_kernel_driver_active(DFU_INTERFACE):
            print("  detaching kernel driver from interface %d" % DFU_INTERFACE)
            dev.detach_kernel_driver(DFU_INTERFACE)
    except (usb.core.USBError, NotImplementedError):
        pass
    return dev


def dfu_download(dev, block, data, timeout=5000):
    return dev.ctrl_transfer(REQ_OUT, DFU_DNLOAD, block, DFU_INTERFACE,
                             data if data else None, timeout)


def dfu_get_status(dev, timeout=5000):
    raw = dev.ctrl_transfer(REQ_IN, DFU_GETSTATUS, 0, DFU_INTERFACE, 6, timeout)
    if len(raw) < 6:
        raise usb.core.USBError("short GET_STATUS: %d bytes" % len(raw))
    poll_ms = raw[1] | (raw[2] << 8) | (raw[3] << 16)
    return raw[0], poll_ms, raw[4], raw[5]      # bStatus, poll, bState, iString


def dfu_get_status_retry(dev, tries=3):
    """GET_STATUS is spec-idempotent (§6.1.3) so retrying is safe."""
    last = None
    for attempt in range(tries):
        try:
            return dfu_get_status(dev)
        except usb.core.USBError as e:
            last = e
            time.sleep(0.05)
    raise last


def dfu_abort(dev):
    dev.ctrl_transfer(REQ_OUT, DFU_ABORT, 0, DFU_INTERFACE, None, 5000)


def dfu_clear_status(dev):
    dev.ctrl_transfer(REQ_OUT, DFU_CLRSTATUS, 0, DFU_INTERFACE, None, 5000)


# ------------------------------------------------------------------ commands

def cmd_probe(_args):
    dev, vid, pid, label = find_device(require_dfu=False)
    if dev is None:
        print("no Mbox on the bus (looked for ffff:fffe, 0dba:1001, 0dba:1000)")
        return 1
    print("found %04x:%04x — %s" % (vid, pid, label))
    print("  bcdDevice   0x%04x" % dev.bcdDevice)
    print("  bDeviceClass 0x%02x" % dev.bDeviceClass)
    for attr, name in (("manufacturer", "iManufacturer"), ("product", "iProduct")):
        try:
            print("  %-12s %s" % (name, getattr(dev, attr)))
        except (usb.core.USBError, ValueError):
            print("  %-12s <unreadable>" % name)
    if label == "audio-mode":
        print("\naudio mode — not flashable. Reach DFU first.")
        return 1
    try:
        st, poll, state, _ = dfu_get_status_retry(open_dfu())
        print("  DFU state   %s   status %s   bwPollTimeout %d ms"
              % (state_name(state), status_name(st), poll))
    except usb.core.USBError as e:
        print("  GET_STATUS failed: %s" % e)
        return 1
    return 0


def cmd_validate(args):
    return 0 if validate(args.image) else 1


def cmd_info(args):
    _blob, start, recs = load_image(args.image)
    total = sum(r.length for r in recs)
    print("%s: %d records from offset 0x%x, %d bytes" % (args.image, len(recs), start, total))
    ff = sum(1 for r in recs if all(b == 0xFF for b in r.data))
    for i, r in enumerate(recs):
        if i < 8 or i >= len(recs) - 4:
            print("  [%3d] @0x%05x  addr=0x%04x  len=%3d  type=%d  %s"
                  % (i, r.file_offset, r.address, r.length, r.type, r.data[:8].hex()))
        elif i == 8:
            print("  ...")
    print("\n%d records: %d real, %d FF-fill. Addressable 0x0000..0x%04x"
          % (len(recs), len(recs) - ff, ff, total - 1))
    return 0


def cmd_flash(args):
    recs = validate(args.image, quiet=True)
    if recs is None:
        return 1
    total = sum(r.length for r in recs)
    dev = open_dfu()

    print("=== ABOUT TO WRITE EEPROM ===")
    print("image:   %s — %d records, %d bytes" % (args.image, len(recs), total))
    if not args.yes:
        if input("proceed? [type 'yes' to confirm]: ").strip() != "yes":
            print("aborted.")
            return 1

    st, _poll, state, _ = dfu_get_status_retry(dev)
    # Self-heal a non-idle entry state rather than forcing a replug. DFU 1.0
    # §6.1.4: ABORT returns the machine to dfuIDLE from any non-error state.
    if state not in (dfuIDLE, dfuERROR):
        print("device is in %s — sending DFU_ABORT" % state_name(state))
        dfu_abort(dev)
        st, _poll, state, _ = dfu_get_status_retry(dev)
    if state == dfuERROR:
        print("device is in dfuERROR (%s) — sending DFU_CLRSTATUS" % status_name(st))
        dfu_clear_status(dev)
        st, _poll, state, _ = dfu_get_status_retry(dev)
    if state != dfuIDLE:
        print("device is in %s, need dfuIDLE — aborting" % state_name(state), file=sys.stderr)
        return 1

    # Whole-flash restart wrapper. On a transient dfuERROR, CLRSTATUS and
    # restart the block loop from 0: DFU_DNLOAD with wValue=0 from dfuIDLE
    # resets loadStatus=DFU_LOAD_NOT (UsbDfu.c:500) and re-inits bufferAddr /
    # dataRemain from scratch, so a restart walks the same path as a first
    # flash. Bounded at 2 restarts — beyond that it is likely not transient.
    flash_complete = False
    for restart in range(3):
        if restart:
            print("=== FLASH RESTART %d/2 ===" % restart)
        need_restart = False
        for i, r in enumerate(recs):
            sys.stdout.write("  block %3d/%d  size=%2d  " % (i, len(recs) - 1, r.length))
            sys.stdout.flush()
            # Per-block transport retry: a failed DNLOAD was never accepted by
            # the boot ROM, so re-sending the same block index is idempotent.
            block_ok = False
            for attempt in range(3):
                try:
                    dfu_download(dev, i, r.data)
                    block_ok = True
                    break
                except usb.core.USBError as e:
                    sys.stdout.write("transport retry %d/3 (%s) " % (attempt + 1, e.errno))
                    sys.stdout.flush()
                    time.sleep(0.1)
            if not block_ok:
                print("FAILED")
                return 1

            block_errored = False
            for _poll_i in range(100):
                st, poll_ms, state, _ = dfu_get_status_retry(dev)
                if state == dfuDNLOAD_IDLE:
                    break
                if state == dfuERROR:
                    if st in TRANSIENT_STATUS and restart < 2:
                        print("transient dfuERROR (%s) — CLRSTATUS + restart" % status_name(st))
                        dfu_clear_status(dev)
                        for _ in range(20):
                            st, _p, state, _ = dfu_get_status_retry(dev)
                            if state == dfuIDLE:
                                break
                            time.sleep(0.05)
                        if state != dfuIDLE:
                            print("CLRSTATUS did not reach dfuIDLE (state=%s)"
                                  % state_name(state), file=sys.stderr)
                            return 1
                        block_errored = need_restart = True
                        break
                    print("terminal dfuERROR: %s" % status_name(st), file=sys.stderr)
                    return 1
                time.sleep((poll_ms or 5) / 1000.0)
            if block_errored:
                break
            print("OK")
        if not need_restart:
            flash_complete = True
            break
    if not flash_complete:
        print("flash did not complete after 2 restarts", file=sys.stderr)
        return 1

    sys.stdout.write("  zero-length end marker... ")
    sys.stdout.flush()
    try:
        dfu_download(dev, len(recs), b"")
    except usb.core.USBError as e:
        print("FAILED (%s)" % e)
        return 1
    print("OK")

    # Manifest phase, DFU 1.0 §7.1.7. The TAS1020A boot ROM reports a bogus
    # bwPollTimeout of 0x200000 (35 min) during dfuMANIFEST for TARGET_EEPROM;
    # the real metadata writes already committed in dfuDnloadData when
    # dataRemain hit 0 (UsbDfu.c:1004-1014), so cap the sleep at 200 ms and
    # re-poll rather than hanging for half an hour.
    state = None
    for _ in range(100):
        st, poll_ms, state, _ = dfu_get_status_retry(dev)
        if state in (dfuMANIFEST_WAIT_RESET, dfuIDLE):
            break
        if state == dfuERROR:
            print("dfuERROR during manifest: %s" % status_name(st), file=sys.stderr)
            return 1
        time.sleep(min(poll_ms or 200, 200) / 1000.0)
    print("manifest complete. Final state: %s" % state_name(state))

    # The boot ROM's dfuSetup loop only exits when RSTR_INT fires
    # (UsbDfu.c:697-704), so without a bus reset it sits in DFU forever and
    # the freshly-flashed app never runs. Force the reset.
    print("issuing USB bus reset to trigger app switch...")
    try:
        dev.reset()
    except usb.core.USBError as e:
        print("  (reset returned %s — non-fatal; a physical replug works too)" % e)

    print("\n=== FLASH COMPLETE ===")
    print("Wait ~2 s, then run `probe` to see the current VID/PID.")
    print("Flashing this device often takes two attempts: a run that reports every")
    print("block OK and reaches dfuMANIFEST can still leave the old mode after the")
    print("replug. Retry the flash before theorising about a new failure.")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("probe", help="report the Mbox's current mode and DFU state")
    p = sub.add_parser("validate", help="static image validation, no device needed")
    p.add_argument("image")
    p = sub.add_parser("info", help="dump the record stream of an image")
    p.add_argument("image")
    p = sub.add_parser("flash", help="write an image to the EEPROM over DFU")
    p.add_argument("image")
    p.add_argument("--yes", action="store_true", help="skip the confirmation prompt")
    args = ap.parse_args()
    return {"probe": cmd_probe, "validate": cmd_validate,
            "info": cmd_info, "flash": cmd_flash}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
