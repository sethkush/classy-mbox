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
    # --coverage is documentation and must work anywhere, including the mac
    # this repo is edited on. Every other mode needs pyusb and says so below.
    usb = None

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


# --------------------------------------------------------------------------
# The USB20CV Chapter 9 suite, reconstructed by subject and mapped to where
# each subject is covered here.
#
# HOW THIS WAS BUILT, because it bounds how much the map is worth. USB20CV is
# closed-source and Windows-only and nobody on this project has run it, so this
# is NOT a transcription of its test list -- it is the set of behaviours USB 2.0
# Chapter 9 makes testable over a control pipe, grouped the way USB20CV's
# published suite groups them. Where a group name is a guess at USB20CV's
# wording it is still an accurate name for the SPEC requirement, and the spec
# requirement is the thing being tested. Treat "covered" as "we test the
# behaviour", never as "USB20CV would pass us".
#
# The four UNREACHABLE rows are the honest reason #192 stays open. Three of them
# are unreachable from ANY userspace host stack, not just ours.
# --------------------------------------------------------------------------
COVERAGE = """\
USB20CV Chapter 9 subjects -> ch9_probe coverage

  COVERED (default run)
    Device descriptor              18 bytes, bLength, bMaxPacketSize0, short
                                   read at 8, over-long read clamps to 18
    Configuration descriptor       9-byte header, wTotalLength, full read,
                                   over-long read clamps, wLength 0
    String descriptors             LANGID array, every index the device
                                   descriptor names, undeclared index stalls
    Device Qualifier               must STALL (full-speed-only, bcdUSB 1.10)
    Other Speed Configuration      must STALL, same reason
    Non-retrievable types          INTERFACE / ENDPOINT requested directly
                                   must STALL (§9.4.3)
    Unsupported descriptor types   HID report / BOS / class must STALL
    Get/Set Configuration          reports 1, legal no-op set, bad value stalls
    Get/Set Interface              all 3 interfaces, bad alt and bad iface stall
    Get Status                     device / interface / endpoint, AND the
                                   device bits cross-checked against
                                   bmAttributes
    Set/Clear Feature              unsupported selectors stall (#188);
                                   DEVICE_REMOTE_WAKEUP stalls, matching
                                   bmAttributes
    Unimplemented requests         SET_DESCRIPTOR, SYNCH_FRAME, undefined
                                   bRequest all stall
    Post-stall liveness            device still answers after every stall above

  COVERED (--invasive only, state-changing)
    Halt Endpoint                  SET_FEATURE(ENDPOINT_HALT) on EP 0x83,
                                   GET_STATUS reads back halted, CLEAR_FEATURE,
                                   GET_STATUS reads back clear
    Unconfigured (Address) State   SET_CONFIGURATION(0), GET_CONFIGURATION
                                   returns 0, descriptors still answer, restore

  COVERED ELSEWHERE (offline, no device needed)
    Descriptor field rulebook      tools/verify_descriptors.py walks the built
                                   image: bLength per type, wTotalLength
                                   recomputed, bNumInterfaces, dangling
                                   bSourceID, duplicate unit IDs, endpoint
                                   addresses vs code, UAC1 format fields,
                                   iSerialNumber vs the linked serial string

  WHAT USB20CV REACHES AND THIS CANNOT -- the actual content of #192
    Set Address                    §9.4.6, and the Default/Address state
                                   behaviour around it. USB20CV drives
                                   enumeration itself, so it can re-address the
                                   device, check that out-of-range values stall,
                                   and check the address survives
                                   SET_CONFIGURATION. From Linux userspace the
                                   kernel owns the address map: issuing
                                   SET_ADDRESS moves the device while the host
                                   still believes the old address, stranding it
                                   until a replug.
    Default-state behaviour        everything a device must do BEFORE it is
                                   addressed. By the time libusb can see it,
                                   that phase is over.
    Class-spec rulebook            USB20CV encodes the Audio 1.0 document, not
                                   just Chapter 9. verify_descriptors.py is our
                                   reading of the same rules, which is one
                                   reading by the same authors.

  UNREACHABLE BY USB20CV EITHER -- these need an analyser or a scope
    Malformed packets / timing     a bad CRC, a wrong PID, a SETUP shorter than
                                   8 bytes, a short inter-packet gap. Neither
                                   libusb nor a command-level verifier builds
                                   packets; the host controller does. Needs an
                                   exerciser.
    Data toggle reset              §5.8.5: CLEAR_FEATURE(HALT) and SET_INTERFACE
                                   must reset the toggle to DATA0. Toggle bits
                                   live in host-controller queue heads and are
                                   invisible to both tools. Indirectly visible:
                                   a device that does not reset it drops every
                                   later transfer as a retransmission, which the
                                   --invasive halt cycle would expose as a hang.
    Electrical / signalling        eye diagrams, rise and fall times, EOP width,
                                   droop, inrush. A different USB-IF tool
                                   entirely (USBET/HSET plus a scope and a
                                   fixture) -- USB20CV is command-level only and
                                   never touches this.

  KNOWN DIVERGENCE (open)
    GET_DESCRIPTOR wLength 0       the device STALLs it; legal per §9.3.5 and
                                   USB20CV would flag it. WHO stalls it --
                                   our code or the UBM -- is still unmeasured.
                                   See FINDING_208.
"""


class Probe:
    def __init__(self, dev, invasive=False):
        self.dev = dev
        self.invasive = invasive
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

    # --- wLength shorter and longer than the descriptor, on DEVICE too ----
    #
    # The config case is covered above; USB20CV runs the same pair against the
    # device descriptor, and they fail differently. A SHORT read is the one
    # every host performs for real: Linux asks for 8 bytes of the device
    # descriptor first, to learn bMaxPacketSize0 before it can ask for the
    # rest. A device that returns all 18 to that request overruns EP0 and the
    # host sees a babble error at the very first transaction of enumeration.
    p.expect_ok("GET_DESCRIPTOR(device, wLength 8) returns exactly 8",
                D2H_STD_DEV, GET_DESCRIPTOR, DT_DEVICE << 8, 0, 8,
                lambda b: None if len(b) == 8
                else f"asked 8, got {len(b)} -- a short read must be truncated, "
                     f"not completed")
    p.expect_ok("GET_DESCRIPTOR(device, over-long wLength) returns exactly 18",
                D2H_STD_DEV, GET_DESCRIPTOR, DT_DEVICE << 8, 0, 255,
                lambda b: None if len(b) == 18
                else f"asked 255, got {len(b)} (expected exactly 18)")

    # --- USB20CV: Device Qualifier / Other Speed Configuration ------------
    #
    # These two are the tests most often failed by a device that was written
    # against a high-speed example. USB 2.0 §9.6.2: a device_qualifier
    # describes what changes if the device were operated at its OTHER speed,
    # and a device that operates at ONE speed only "must not return" it -- the
    # request MUST be answered with a Request Error, i.e. a STALL. §9.6.3 says
    # the same for other_speed_configuration.
    #
    # We declare bcdUSB = 1.10 (descriptors.c:24) and are full-speed only, so
    # neither descriptor exists and both must stall. Returning something --
    # even a plausible-looking 10 bytes -- tells the host to try high-speed
    # negotiation against hardware that has no such mode.
    #
    # Answering these WRONGLY is silent on Linux, which never asks a 1.10
    # device for them. That is exactly the class of bug this suite is for.
    for t, nm in ((0x06, "DEVICE_QUALIFIER"), (0x07, "OTHER_SPEED_CONFIG")):
        p.expect_stall(f"GET_DESCRIPTOR({nm}) stalls -- §9.6.2/§9.6.3, "
                       f"full-speed-only device", D2H_STD_DEV, GET_DESCRIPTOR,
                       t << 8, 0, 64)

    # §9.4.3: INTERFACE and ENDPOINT descriptors are only ever returned as
    # part of a configuration descriptor -- "there is no way for the host to
    # retrieve them directly" -- so a request naming one must stall. A device
    # that helpfully answers is inventing a transfer the spec does not define.
    for t, nm in ((0x04, "INTERFACE"), (0x05, "ENDPOINT")):
        p.expect_stall(f"GET_DESCRIPTOR({nm}) stalls -- §9.4.3, not directly "
                       f"retrievable", D2H_STD_DEV, GET_DESCRIPTOR, t << 8, 0, 64)

    # §9.4.3: an unsupported descriptor TYPE must stall.
    for t, nm in ((0x22, "HID report"), (0x0F, "BOS"), (0x21, "class 0x21")):
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
    # §9.4.5 does not merely require two bytes -- it requires two bytes that
    # AGREE WITH THE CONFIG DESCRIPTOR. Bit 0 is Self Powered, bit 1 is Remote
    # Wakeup, and both restate what bmAttributes already declared. USB20CV
    # cross-checks the pair; checking only the length (which is all this did
    # until now) passes a device that reports self-powered while its descriptor
    # says bus-powered, and that contradiction is what a host uses to decide
    # whether it may suspend the port.
    #
    # We declare bmAttributes = 0x80 (descriptors.c:59): bus-powered, no remote
    # wakeup. So the correct answer is exactly 0x0000.
    cfg_attr = cfg[7] if cfg is not None and len(cfg) >= 8 else None

    def _status_matches(b):
        if len(b) != 2:
            return f"returned {len(b)} bytes, §9.4.5 requires 2"
        if cfg_attr is None:
            return None
        want_self = 1 if (cfg_attr & 0x40) else 0
        want_wake = 1 if (cfg_attr & 0x20) else 0
        got_self, got_wake = b[0] & 1, (b[0] >> 1) & 1
        bad = []
        if got_self != want_self:
            bad.append(f"Self Powered = {got_self} but bmAttributes "
                       f"0x{cfg_attr:02X} says {want_self}")
        if got_wake != want_wake:
            bad.append(f"Remote Wakeup = {got_wake} but bmAttributes "
                       f"0x{cfg_attr:02X} says {want_wake}")
        if b[0] & 0xFC or b[1]:
            bad.append(f"reserved bits set in {list(b)}; §9.4.5 requires zero")
        return "; ".join(bad) if bad else None

    p.expect_ok("GET_STATUS(device) agrees with bmAttributes", D2H_STD_DEV,
                GET_STATUS, 0, 0, 2, _status_matches)
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

    # --- UAC1 sampling-frequency attributes (#191) ------------------------
    #
    # NOT Chapter 9, but it belongs with it: the question "which attributes of
    # this control exist" is the same shape, and the answer decided #191.
    #
    # Our rates are DISCRETE -- Type I format, bSamFreqType = 2, list
    # [44100, 48000]. MIN/MAX/RES describe a CONTINUOUS range, so for a
    # discrete list there is no step to report and the valid values already
    # live in the format descriptor. Answering them would imply everything
    # between 44100 and 48000 is selectable, which it is not. A stall is the
    # correct answer, and the device gives it.
    UAC_SAMPLING_FREQ = 0x01
    for ep, nm in ((0x81, "capture EP 0x81"), (0x02, "playback EP 0x02")):
        p.expect_ok(f"UAC GET_CUR sampling freq, {nm}", 0xA2, 0x81,
                    UAC_SAMPLING_FREQ << 8, ep, 3,
                    lambda b: None if len(b) == 3 and (b[0] | (b[1] << 8) |
                                                       (b[2] << 16)) in
                    (44100, 48000)
                    else f"returned {list(b)}, not a declared rate")
        for req, rn in ((0x82, "GET_MIN"), (0x83, "GET_MAX"), (0x84, "GET_RES")):
            p.expect_stall(f"UAC {rn} sampling freq stalls, {nm}", 0xA2, req,
                           UAC_SAMPLING_FREQ << 8, ep, 3)

    # --- USB20CV: Halt Endpoint Test (state-changing, opt-in) -------------
    #
    # §9.4.5 makes the halt feature MANDATORY on every interrupt and bulk
    # endpoint: SET_FEATURE(ENDPOINT_HALT) must halt it, GET_STATUS(endpoint)
    # must then report bit 0 set, and CLEAR_FEATURE must clear it again. This
    # is the one Chapter 9 test we cannot fold into the default run, because
    # a device that implements the SET half and not the CLEAR half leaves the
    # endpoint stuck -- and on this project un-sticking it costs a 2 km round
    # trip. So it runs only under --invasive, and only on EP 0x83, the status
    # interrupt endpoint (#207): nothing streams on it, so a stuck halt loses
    # no audio.
    #
    # #188 made us stall UNSUPPORTED feature selectors. ENDPOINT_HALT on a real
    # interrupt endpoint is not one of those -- it is required to work, and
    # whether we implement it is genuinely unknown until this runs.
    if p.invasive:
        HALT, EP = 0x00, 0x83
        kind, _ = p._xfer(H2D_STD_EP, SET_FEATURE, HALT, EP, None)
        if kind != "ok":
            p.failures.append(
                f"SET_FEATURE(ENDPOINT_HALT, EP 0x{EP:02X}): {kind} -- §9.4.9 "
                f"makes the halt feature mandatory on an interrupt endpoint")
        else:
            p.passes += 1
            st = p.expect_ok(f"GET_STATUS(EP 0x{EP:02X}) reports halted",
                             D2H_STD_EP, GET_STATUS, 0, EP, 2,
                             lambda b: None if len(b) == 2 and (b[0] & 1)
                             else f"returned {list(b)}; after SET_FEATURE the "
                                  f"halt bit must read back set")
            kind, _ = p._xfer(H2D_STD_EP, CLEAR_FEATURE, HALT, EP, None)
            if kind != "ok":
                p.failures.append(
                    f"CLEAR_FEATURE(ENDPOINT_HALT, EP 0x{EP:02X}): {kind} -- "
                    f"the endpoint is now STUCK HALTED until the unit is "
                    f"replugged")
            else:
                p.passes += 1
                p.expect_ok(f"GET_STATUS(EP 0x{EP:02X}) reports cleared",
                            D2H_STD_EP, GET_STATUS, 0, EP, 2,
                            lambda b: None if len(b) == 2 and not (b[0] & 1)
                            else f"returned {list(b)}; halt did not clear")

        # --- USB20CV: Unconfigured (Address) State Test ------------------
        #
        # §9.4.7: SET_CONFIGURATION(0) returns the device to the Address state.
        # It must stay ANSWERABLE there -- GET_CONFIGURATION reports 0, and the
        # standard requests still work -- because that is the state every host
        # passes through on the way to configuring it. A device that goes deaf
        # at config 0 can be enumerated once and never again.
        kind, _ = p._xfer(H2D_STD_DEV, SET_CONFIGURATION, 0, 0, None)
        if kind != "ok":
            p.failures.append(f"SET_CONFIGURATION(0): {kind} -- §9.4.7 requires "
                              f"a device to accept a return to the Address state")
        else:
            p.passes += 1
            p.expect_ok("GET_CONFIGURATION reports 0 when unconfigured",
                        D2H_STD_DEV, GET_CONFIGURATION, 0, 0, 1,
                        lambda b: None if len(b) == 1 and b[0] == 0
                        else f"returned {list(b)}, expected [0]")
            p.expect_ok("GET_DESCRIPTOR(device) still answers when unconfigured",
                        D2H_STD_DEV, GET_DESCRIPTOR, DT_DEVICE << 8, 0, 18,
                        lambda b: None if len(b) == 18 else f"returned {len(b)}")
            kind, _ = p._xfer(H2D_STD_DEV, SET_CONFIGURATION, 1, 0, None)
            if kind != "ok":
                p.failures.append(f"SET_CONFIGURATION(1) to RESTORE: {kind} -- "
                                  f"the device is left unconfigured")
            else:
                p.passes += 1
    else:
        p.notes.append("Halt Endpoint and Unconfigured State tests SKIPPED "
                       "(state-changing) -- rerun with --invasive")

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
    ap.add_argument("--invasive", action="store_true",
                    help="also run the state-changing tests: the Halt Endpoint "
                         "cycle on EP 0x83 and the Unconfigured State test. "
                         "Both restore themselves IF the device implements the "
                         "clearing half; if it does not, the unit needs a "
                         "replug. Off by default for that reason.")
    ap.add_argument("--coverage", action="store_true",
                    help="print the USB20CV-to-here mapping and exit without "
                         "touching a device")
    a = ap.parse_args()

    if a.coverage:
        print(COVERAGE)
        return 0
    if usb is None:
        sys.exit("pyusb not installed. On the void box: ~/mbox-venv/bin/python")

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

    p = Probe(dev, invasive=a.invasive)
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
