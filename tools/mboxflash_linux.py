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
    0xFFFF:0xFFFE  boot-ROM DFU   (see below — target is NOT implied by the PID)
    0x0DBA:0x1001  app-DFU        (valid header, dataType != APPCODE)
    0x0DBA:0x1000  audio mode     (running app — not flashable)

0xFFFF:0xFFFE DOES NOT MEAN "RAM LOADER", and this tool used to say it did.
The substantive correction was made 2026-07-28 (RomBoot.c:60-66); only these
labels were left stale, and they are misleading enough to reverse a decision.

The boot ROM presents this one descriptor set for BOTH DFU targets. Which is
active comes from `dataType`, i.e. from HOW the EEPROM failed — never the PID:

    dataType EEPROM_UNEXIST (0xFE) / EEPROM_DEVICE_TYPE (0x02)
        -> DFU_TARGET_RAM.    Only a genuinely unreadable EEPROM (a real SDA
           short holding I2C down, a blank part). Downloads go to volatile RAM
           and a power cycle discards them.

    anything else — including a readable header whose CHECKSUM was zeroed,
    which is what eeprom_invalidate_signature() does
        -> DFU_TARGET_EEPROM. Downloads are PROGRAMMED and survive a power
           cycle, including a freshly written valid header.

Re-confirmed end to end 2026-08-03: trigger zeroed the checksum, device came up
0xFFFF:0xFFFE, flashed 166/166 to dfuMANIFEST, started via a bus reset, and a
real power cycle then brought build 0x001B back FROM EEPROM with counters reset
(bus resets 7 -> 3, setup_count -> 24). See
firmware_stock/decomp/FINDING_170_audio_works.md.

So the mode strings below name the PID, not the target. Do not conclude from
them that a flash will or will not persist.
"""

import argparse
import struct
import shutil
import subprocess
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

# These strings name the PID, NOT the DFU target. 0xFFFF:0xFFFE is presented
# for both DFU_TARGET_RAM and DFU_TARGET_EEPROM -- see the module docstring.
DFU_DEVICES = [(0xFFFF, 0xFFFE, "boot-ROM DFU (target unknown from PID)"),
               (0x0DBA, 0x1001, "app-DFU")]
AUDIO_DEVICE = (0x0DBA, 0x1000)
# mboxfw can be built with MBOX_PID overridden (`make MBOX_PID=0x2000`) so that
# two units on one bench are tellable apart. Those units are still audio-mode
# Mboxes -- not flashable as they stand, but they must be FINDABLE, or probe
# reports "no Mbox on the bus" and there is nothing to send the DFU trigger to.
# Anything in 0x2000..0x200F is treated as an audio-mode alias of 0x1000.
AUDIO_PID_ALIASES = tuple(range(0x2000, 0x2010))
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

# Address of the unit to operate on, as (bus, address), or None for "the
# only one on the bus". Set from --addr.
#
# Two Mboxes now share a host, and in DFU BOTH enumerate as ffff:fffe with
# no serial number and nothing else to tell them apart. usb.core.find()
# returns the first match, so flashing twice in a row would write the same
# unit twice and silently leave the other one dark. Ambiguity is a hard
# error here rather than a warning: a wrong guess costs a 2 km round trip.
TARGET_ADDR = None


def _matches(dev):
    return TARGET_ADDR is None or (dev.bus, dev.address) == TARGET_ADDR


def _find_all(vid, pid):
    return [d for d in usb.core.find(find_all=True, idVendor=vid, idProduct=pid)
            if _matches(d)]


def find_device(require_dfu=True):
    require_usb()
    candidates = [(vid, pid, label) for vid, pid, label in DFU_DEVICES]
    if not require_dfu:
        candidates.append((AUDIO_DEVICE[0], AUDIO_DEVICE[1], "audio-mode"))
    for vid, pid, label in candidates:
        devs = _find_all(vid, pid)
        if len(devs) > 1:
            listing = "\n".join("    bus %d addr %d  (--addr %d:%d)"
                                % (d.bus, d.address, d.bus, d.address)
                                for d in devs)
            sys.exit("AMBIGUOUS: %d devices at %04x:%04x. In DFU both units\n"
                     "look identical, so pick one explicitly:\n%s"
                     % (len(devs), vid, pid, listing))
        if devs:
            return devs[0], vid, pid, label
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
    require_usb()
    seen = [(v, p, l, d) for v, p, l in
            DFU_DEVICES + [(AUDIO_DEVICE[0], AUDIO_DEVICE[1], "audio-mode")]
            for d in usb.core.find(find_all=True, idVendor=v, idProduct=p)]
    if len(seen) > 1:
        print("%d Mbox units on the bus:" % len(seen))
        for v, p, l, d in seen:
            print("  %04x:%04x  %-16s bus %d addr %d   --addr %d:%d"
                  % (v, p, l, d.bus, d.address, d.bus, d.address))
        print()
    dev, vid, pid, label = find_device(require_dfu=False)
    if dev is None:
        alias = None
        for pid in AUDIO_PID_ALIASES:
            if usb.core.find(idVendor=0x0DBA, idProduct=pid) is not None:
                alias = pid
                break
        if alias is not None:
            print("audio mode at 0dba:%04x -- a custom MBOX_PID build, not "
                  "flashable as it stands." % alias)
            print("Send the class-request DFU trigger, then power-cycle the "
                  "unit; it returns as ffff:fffe and can be flashed.")
        else:
            print("no Mbox on the bus (looked for ffff:fffe, 0dba:1001, "
                  "0dba:1000, 0dba:2000-200f)")
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



def _hub_port_for(dev):
    """
    Map a pyusb device to its (hub-location, port) for uhubctl, via sysfs.
    Returns (None, None) if it cannot be determined.
    """
    try:
        import glob, os
        for p in glob.glob("/sys/bus/usb/devices/*"):
            try:
                v = open(os.path.join(p, "idVendor")).read().strip()
                d = open(os.path.join(p, "idProduct")).read().strip()
            except OSError:
                continue
            if int(v, 16) != dev.idVendor or int(d, 16) != dev.idProduct:
                continue
            name = os.path.basename(p)          # e.g. "2-1.2"
            if "-" not in name or "." not in name:
                continue
            hub, port = name.rsplit(".", 1)     # ("2-1", "2")
            return hub, int(port)
    except Exception:
        pass
    return None, None

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
    # Reaching the manifest phase is MANDATORY, and is the only proof the
    # boot ROM considers the download complete. UsbDfu.c:520-537: the
    # zero-length terminator from dfuDNLOAD_IDLE goes to dfuMANIFEST_SYNC
    # *only* if loadStatus == DFU_LOAD_COMPLETED; otherwise it goes to
    # dfuERROR/errNOTDONE. loadStatus only becomes COMPLETED when
    # dataRemain hits 0 (UsbDfu.c:1013). So "no manifest" means the boot
    # ROM received fewer payload bytes than the header promised.
    #
    # This matters enormously in RAM mode, because loadStatus is also what
    # the bus reset dispatches on (UsbDfu.c:698-722):
    #   DFU_LOAD_COMPLETED -> ROM_APP_RUNNING, the RAM image is launched
    #   default            -> dfuERROR + errPOR, image is NOT launched
    # Resetting without a manifest therefore throws the download away. That
    # is exactly what happened on 2026-07-27: 58/58 blocks "OK", no
    # manifest, bus reset, errPOR, RAM image never ran.
    state = None
    reached_manifest = False
    for _ in range(100):
        st, poll_ms, state, _ = dfu_get_status_retry(dev)
        if state in (dfuMANIFEST_SYNC, dfuMANIFEST, dfuMANIFEST_WAIT_RESET, dfuIDLE):
            reached_manifest = True
            break
        if state == dfuERROR:
            print("dfuERROR after terminator: %s" % status_name(st), file=sys.stderr)
            if st == 9:   # errNOTDONE
                print("errNOTDONE => the boot ROM got fewer payload bytes than the\n"
                      "header's payloadSize. The image was NOT accepted. Not resetting.",
                      file=sys.stderr)
            return 1
        if state == dfuDNLOAD_IDLE:
            print("still dfuDNLOAD_IDLE after the zero-length terminator — the boot ROM\n"
                  "did not accept the download as complete. Not resetting (a reset here\n"
                  "would discard it and report errPOR).", file=sys.stderr)
            return 1
        time.sleep(min(poll_ms or 200, 200) / 1000.0)
    if not reached_manifest:
        print("never reached the manifest phase — not resetting.", file=sys.stderr)
        return 1
    print("manifest reached. Final state: %s" % state_name(state))

    # The boot ROM's dfuSetup loop only exits when RSTR_INT fires
    # (UsbDfu.c:697-704), so without a bus reset it sits in DFU forever and
    # the freshly-flashed app never runs. Force the reset.
    # MEASURED 2026-07-29, and the old advice here was actively wrong.
    #
    # dev.reset() fails with [Errno 2] Entity not found on the void box, and
    # worse, it knocks the device OFF THE BUS -- after which no bus reset can be
    # delivered at all and the download is stranded. This used to be reported as
    # "non-fatal; a physical replug works too". A physical replug is a POWER
    # CYCLE, which restarts the boot ROM and DISCARDS the RAM image, so the
    # device comes back in bulletproof-DFU and the flash appears to have failed.
    # That advice cost three replug cycles and produced the false conclusion
    # that a clean 186/186 + dfuMANIFEST flash "did not take".
    #
    # What actually works: leave the device attached and deliver a BUS RESET
    # (RSTR_INT), which is what the boot ROM's dfuSetup loop waits for
    # (UsbDfu.c:697-704). `uhubctl -a cycle` does exactly that on this hardware
    # -- it does NOT switch VBUS (see the void-box notes), so the 8051 keeps
    # running and only the bus is reset. Verified: known-good image, app running
    # 3 s later.
    print("triggering app switch with a BUS RESET (not a power cycle)...")
    switched = False
    try:
        dev.reset()
        switched = True
        print("  dev.reset() OK")
    except usb.core.USBError as e:
        print("  dev.reset() failed: %s" % e)
        hub, port = _hub_port_for(dev)
        if hub and shutil.which("uhubctl"):
            cmd = ["uhubctl", "-l", hub, "-p", str(port), "-a", "cycle", "-d", "2"]
            print("  delivering RSTR_INT via: %s" % " ".join(cmd))
            try:
                subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
                switched = True
            except Exception as e2:
                print("  uhubctl failed: %s" % e2)
        if not switched:
            print("\n  DO NOT POWER-CYCLE / REPLUG: that discards the RAM image.")
            print("  Deliver a bus reset instead, e.g.:")
            print("      sudo uhubctl -l <hub> -p <port> -a cycle")

    print("\n=== FLASH COMPLETE ===")
    print("Wait ~2 s, then run `probe` to see the current VID/PID.")
    if not switched:
        print("The app switch was NOT triggered — the image is downloaded but the")
        print("boot ROM is still in dfuSetup waiting for a bus reset.")
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
    for _p in sub.choices.values():
        _p.add_argument("--addr", metavar="BUS:ADDR", default=None,
                        help="target one unit when several are on the bus, "
                             "e.g. --addr 2:8 (see lsusb)")
    args = ap.parse_args()
    if getattr(args, "addr", None):
        global TARGET_ADDR
        _b, _a = args.addr.split(":")
        TARGET_ADDR = (int(_b), int(_a))
    return {"probe": cmd_probe, "validate": cmd_validate,
            "info": cmd_info, "flash": cmd_flash}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
