#!/usr/bin/env python3
"""
mboxtlm -- read mboxfw's telemetry blocks over EP0 and decode them.

mboxflash_linux.py talks to the boot ROM's DFU interface. This talks to the
RUNNING application, which is a different conversation entirely: the device is
in audio mode, snd-usb-audio owns the audio interfaces, and the questions are
about what the firmware is doing right now.

Two properties of the device side make this tool trivial and both are
deliberate (see mboxfw/include/telemetry.h):

  * Every read is EXACTLY 8 bytes -- one EP0 packet. The multi-packet
    continuation path is itself under investigation, so telemetry must never
    depend on it.
  * The vendor requests are DEVICE recipient, not interface. An
    interface-recipient request is rejected by the host stack with EBUSY once
    snd-usb-audio has claimed the interfaces -- that is exactly how the
    enter-DFU request silently never arrived on 2026-07-27. Device recipient
    means these keep working with a driver bound, which is the whole point:
    the interesting state only exists WHILE arecord is streaming.

Usage:
    mboxtlm.py all                  read and decode every block
    mboxtlm.py read 6               read one block
    mboxtlm.py watch 6 [-n N] [-i S]  re-read a block N times, S apart
    mboxtlm.py reset                clear the per-experiment counters
    mboxtlm.py raw 6                hex only, no interpretation
    mboxtlm.py setmux line line     select the input source on both channels

`setmux` exists because the bench loopbacks are wired to the LINE inputs while
the firmware boots to MIC on both channels, and that mismatch silently voided a
whole measurement session on 2026-07-29. Block 9 reports the selected source, so
a capture can state its own input routing instead of it being read off the front
panel afterwards. See BENCH_WIRING.md.

Add --raw to any decode to also print the underlying bytes.

This tool NEVER sends TLM_REQ_ENTER_DFU. Dropping the unit into DFU is a
power-cycle-and-a-2-km-round-trip commitment; it belongs in the flasher where
that is the explicit intent, not in a diagnostic reader that gets pointed at a
working device.
"""
import argparse
import sys
import time

try:
    import usb.core
    import usb.util
    HAVE_USB = True
except ImportError:
    HAVE_USB = False

# ------------------------------------------------------------------ constants

MBOX_VID = 0x0DBA
# Audio-mode PIDs. 0x1000 is stock/default; mboxfw can be built with
# MBOX_PID overridden (`make MBOX_PID=0x2000`) to tell two bench units apart,
# and anything in 0x2000..0x200F is such an alias. Kept in step with
# AUDIO_PID_ALIASES in mboxflash_linux.py.
AUDIO_PIDS = (0x1000,) + tuple(range(0x2000, 0x2010))

TLM_REQ_READ = 0x10        # bmRequestType 0xC0, wValue = block index
TLM_REQ_RESET = 0x11       # bmRequestType 0x40
TLM_REQ_SET_MUX = 0x13     # bmRequestType 0x40, wValue = mux, wIndex = mono
REQ_IN = 0xC0              # vendor | device-to-host | device
REQ_OUT = 0x40             # vendor | host-to-device | device

BLOCK_SIZE = 8
NUM_BLOCKS = 11

# The build in which each block FIRST EXISTED, read off the TLM_NUM_BLOCKS
# bumps in telemetry.h's history. A device running an older build answers a
# later block with the out-of-range sentinel, which for block 10 is
# byte-identical to a real reading -- so this table, checked against the build
# id in block 0, is the only thing that tells a measurement from a sentinel.
# Verified against the header history by sim_telemetry_roundtrip.py.
BLOCK_FIRST_BUILD = {
    0: 0x0000, 1: 0x0000, 2: 0x0000, 3: 0x0000, 4: 0x0000, 5: 0x0000,
    6: 0x0006,   # NUM_BLOCKS 6 -> 7
    7: 0x000C,   # NUM_BLOCKS 7 -> 8
    8: 0x0010,   # NUM_BLOCKS 8 -> 9
    9: 0x0013,   # NUM_BLOCKS 9 -> 10
    10: 0x0015,  # NUM_BLOCKS 10 -> 11
}

# The three one-cold source patterns, from the stock cycle handlers (Rev 20
# fcn.0x0E27 / fcn.0x0E9D, Rev 22 fcn.0x0E1B / fcn.0x0E8F). The firmware
# rejects anything else, so the same names are the tool's vocabulary.
SOURCES = {"mic": 0x06, "line": 0x05, "inst": 0x03}
SOURCE_NAMES = {v: k for k, v in SOURCES.items()}

PHASE_BITS = [(0x01, "USB_INIT"), (0x02, "HW_INIT"), (0x04, "ATTACH"),
              (0x08, "CS8427"), (0x10, "CODEC"), (0x20, "MAIN_LOOP")]

# Stage is a high-water mark, so it names the furthest point reached, not the
# current one. main.c 1-9, usb.c 14-20.
STAGES = {
    0: "nothing recorded",
    1: "main() entered",
    2: "usb_init() returned",
    3: "hw_init() returned",
    4: "check_boot_dfu_button() returned",
    5: "EA=1 (interrupts on)",
    6: "usb_attach() returned",
    7: "cs8427_boot_init() returned",
    8: "codec_init() returned",
    9: "main loop reached",
    14: "GET_DESCRIPTOR(DEVICE) served",
    15: "SET_ADDRESS staged (write deferred to status stage)",
    16: "deferred address applied",
    17: "GET_DESCRIPTOR(CONFIGURATION) served",
    18: "an EP0 chunk was pushed",
    19: "an EP0 transfer drained completely",
    20: "enumeration essentially complete",
}

ALT_BITS = [(0x01, "playback-on"), (0x02, "capture-on")]


def u16(b, o):
    """Blocks store 16-bit counters little-endian (telemetry.c put16)."""
    return b[o] | (b[o + 1] << 8)


def bits(v, table):
    on = [name for mask, name in table if v & mask]
    return "|".join(on) if on else "none"


# ------------------------------------------------------------------- decoders
#
# Each decoder returns a list of "label: value" lines. Register bit meanings
# are cited to the source that establishes them -- an uncited bit meaning in a
# diagnostic is how a wrong reading becomes a wrong conclusion.

def dec_dmactl(v, what):
    """DMACTLn, datasheet 6.5.2.3: 7=DMAEN 6=HSKEN 5:4=rsvd 3=EPDIR 2:0=EPNUM."""
    return ("%s=0x%02X  DMAEN=%d HSKEN=%d EPDIR=%s EPNUM=%d"
            % (what, v, (v >> 7) & 1, (v >> 6) & 1,
               "IN" if (v >> 3) & 1 else "OUT", v & 7))


def dec_iepcnf(v):
    """IEPCNFn -- value only. The bit NAMES are deliberately not printed.

    An earlier version of this function printed
    "UBME/TOGLE/STALL/USBIE/ISO/BPS" from a bit map assembled here out of a
    partial comment in regs.h. Run against hardware it decoded the stock
    value 0xC5 as ISO=0 -- i.e. the audio endpoint not marked isochronous --
    which contradicts regs.h's own reading of 0xC5 as "ISO, BPS field = 5".
    One of the two is wrong and this tool is not the place to guess: a
    confident-looking bit decode is exactly how a wrong reading becomes a
    wrong conclusion, which is the failure this whole telemetry path exists
    to avoid.

    What IS established: stock Rev 20 writes 0xC5, mboxfw writes 0xC5, and
    0x00 means the endpoint config has been torn down. Compare against those
    until the datasheet bit map is transcribed and cited.
    """
    known = {0xC5: "  (= stock Rev 20 value)", 0x00: "  (torn down)"}
    return "IEPCNF1=0x%02X%s" % (v, known.get(v, "  (non-stock)"))


def block0(b):
    return [
        "build id:        0x%04X" % u16(b, 0),
        "stage:           %d  (%s)" % (b[2], STAGES.get(b[2], "unknown")),
        "phases:          0x%02X  %s" % (b[3], bits(b[3], PHASE_BITS)),
        "loop_count:      %d" % u16(b, 4),
        "bus resets:      %d" % u16(b, 6),
    ]


def block1(b):
    setups, iep0, chunks, drains = (u16(b, 0), u16(b, 2), u16(b, 4), u16(b, 6))
    out = ["setup_count:     %d" % setups,
           "iep0_count:      %d" % iep0,
           "chunks pushed:   %d" % chunks,
           "transfers drained: %d" % drains]
    # For an N-packet reply the device takes N IEP0 interrupts and pushes N
    # chunks. chunks short of iep0 means the device stopped being asked.
    if iep0 and chunks < iep0:
        out.append("  NOTE: chunks < iep0_count -- pushes are being missed")
    return out


def block2(b):
    return [
        "last bmRequestType: 0x%02X" % b[0],
        "last bRequest:      0x%02X%s" % (b[1],
                                          "  (0xEE = no SETUP yet)" if b[1] == 0xEE else ""),
        "wValue:             0x%04X" % u16(b, 2),
        "wIndex:             0x%04X" % u16(b, 4),
        "wLength:            %d" % u16(b, 6),
    ]


def block3(b):
    out = ["VECINT histogram (saturating at 255):",
           "  setup=%d iep0=%d oep0=%d rstr=%d none=%d other=%d"
           % (b[0], b[1], b[2], b[3], b[4], b[5]),
           "  susr=%d resr=%d   (bus suspend / resume)" % (b[6], b[7])]
    if b[4]:
        out.append("  NOTE: nonzero 'none' -- ISR firing with no vector set")
    if b[5]:
        out.append("  NOTE: nonzero 'other' -- an unhandled vector is arriving")
    return out


def block4(b):
    def res(v):
        return "not run" if v == 0xFF else "0x%02X" % v
    return [
        "eeprom_ok:       %s" % res(b[0]),
        "cs8427_status:   %s" % res(b[1]),
        "codec_status:    %s" % res(b[2]),
        "stalls:          %d" % b[3],
        "P1 live=0x%02X boot=0x%02X   P3 live=0x%02X boot=0x%02X"
        % (b[4], b[6], b[5], b[7]),
    ]


def block5(b):
    iface, alt = b[7] >> 4, b[7] & 0x0F
    out = [
        "sof_count:       %d" % u16(b, 0),
        "vec_iep1 (capture EP int):  %d" % b[2],
        "vec_oep2 (playback EP int): %d" % b[3],
        dec_iepcnf(b[4]),
        "OEPCNF2=0x%02X" % b[5],
        "alt_seen:        0x%02X  %s" % (b[6], bits(b[6], ALT_BITS)),
        "last SET_INTERFACE: iface %d alt %d%s"
        % (iface, alt, "  (0xFF/0xFF = none seen)" if b[7] == 0xFF else ""),
    ]
    if u16(b, 0) == 0:
        out.append("  NOTE: sof_count 0 -- no frame clock reaching us")
    return out


def block6(b):
    out = [
        dec_dmactl(b[0], "DMACTL1 (capture, EP1 IN) "),
        dec_dmactl(b[1], "DMACTL0 (playback, EP2 OUT)"),
        "CPTSTA=0x%02X   (C-port status; may be clear-on-read)" % b[2],
        "ACGCTL=0x%02X   (adaptive clock generator control)" % b[3],
        dec_iepcnf(b[4]),
        "IEPDCNTX1=0x%02X (capture EP byte count -- 0 = buffer armed/empty)" % b[5],
        "IEPBSIZ1=0x%02X  (capture EP buffer = %d bytes; regs.h EP_BSIZE "
        "encodes size>>3)" % (b[6], b[6] * 8),
        "OEPDCNTX2=0x%02X (playback EP byte count)" % b[7],
    ]
    # The question this block was added for: is the capture DMA armed at all?
    if not (b[0] & 0x80):
        out.append("  NOTE: capture DMAEN CLEAR -- the DMA is not running")
    if not (b[1] & 0x80):
        out.append("  NOTE: playback DMAEN CLEAR -- the DMA is not running")
    return out


def block7(b):
    """EP0 buffer counts + suspend/resume tally.

    The Y counts are the point of this block. Both stock images clear them at
    init and mboxfw did not; build 0x000C clears both. Since the clear happens
    before any host can ask, a read here cannot recover the boot-ROM handoff
    value -- what it answers is whether the UBM puts anything BACK into Y while
    we run, which is the version of the question that survives the fix.
    See firmware_stock/decomp/FINDING_ep0_y_buffer_residue.md.
    """
    out = [
        "IEPDCNTY0=0x%02X (EP0 IN  Y count -- 0 expected)" % b[0],
        "OEPDCNTY0=0x%02X (EP0 OUT Y count -- 0 expected)" % b[1],
        "IEPDCNTX0=0x%02X (EP0 IN  X count; bit 7 = NAK)" % b[2],
        "OEPDCNTX0=0x%02X (EP0 OUT X count)" % b[3],
        "suspends:  %3d   (completed suspend+resume cycles)" % b[4],
        "pb_resyncs:%3d   (playback frame-alignment resyncs -- Rev 22's SOF"
        " watchdog)" % b[5],
        "PCON=0x%02X     (bit 0 = IDL; reads 0 once awake)" % b[7],
    ]
    if b[0] or b[1]:
        out.append("  NOTE: a Y count is NON-ZERO -- the UBM is using a buffer"
                   " the firmware does not manage. This is the EP0-loss"
                   " mechanism, confirmed.")
    if b[5]:
        out.append("  NOTE: the playback watchdog HAS fired -- the DMA buffer"
                   " held a partial sample frame and the path was restarted."
                   " Rev 20 has no such check; this is the Rev 22 behaviour.")
    return out


def block8(b):
    """Boot-ROM handoff snapshot, sampled as the FIRST action in main().

    This is what closes WHAT_REMAINS_UNKNOWN.md §3a. usb_ep0_setup() clears both
    EP0 Y counts, so block 7's live read can never recover what the boot ROM
    handed over -- only a sample taken before we write anything can, and byte 3
    in particular is destroyed by main()'s own `USBCTL = 0` two lines later.

    0xFF in every byte means main() never reached the sample, not a real reading.
    """
    if b[0] == 0xFF and b[1] == 0xFF and b[2] == 0xFF and b[3] == 0xFF:
        return ["NOT SAMPLED (all 0xFF sentinel) -- main() never got this far,"
                " or this build predates block 8"]
    out = [
        "IEPDCNTY0=0x%02X  (EP0 IN  Y count AT HANDOFF)" % b[0],
        "OEPDCNTY0=0x%02X  (EP0 OUT Y count AT HANDOFF)" % b[1],
        "GLOBCTL  =0x%02X  (boot-ROM value; TI RomBoot.c says 0x04)" % b[2],
        "USBCTL   =0x%02X  (boot-ROM value, before main() zeroes it)" % b[3],
    ]
    if b[0] or b[1]:
        out.append("  §3a ANSWERED YES: the boot ROM DOES leave an EP0 Y count"
                   " non-zero. That is the residue both stock images clear at"
                   " init and mboxfw did not until build 0x000C -- the candidate"
                   " mechanism for the ~12% geometric EP0 IN loss.")
    else:
        out.append("  §3a ANSWERED NO: both Y counts were already zero at"
                   " handoff, so ROM residue is NOT the EP0-loss mechanism."
                   " Read block 7 for whether the UBM writes Y during a session.")
    if b[2] != 0x04:
        out.append("  NOTE: GLOBCTL at handoff is 0x%02X, NOT the 0x04 that"
                   " hw_init's `GLOBCTL |= 0x02` assumes. That RMW was chosen"
                   " over stock's outright `= 0x06` precisely because the ROM"
                   " was believed to leave 0x04 -- recheck hw_init.c." % b[2])
    if b[3]:
        out.append("  NOTE: USBCTL was NON-ZERO at handoff (0x%02X). main()"
                   " zeroes it defensively for exactly this case; on the audited"
                   " paths (cold boot, and UsbDfu.c:699 after a DFU manifest)"
                   " it should be 0." % b[3])
    return out


def block9(b):
    """Panel state -- which source is actually selected, right now.

    Read this alongside every audio measurement. On 2026-07-29 a full capture
    session was voided because the mux sat at mic on both channels while the
    loopback fed a line input, and that was only discovered afterwards, from
    the front-panel LEDs. A measurement that cannot state its own input routing
    cannot be trusted, and before this block there was no way to ask.
    """
    mux = b[0]
    ch1, ch2 = mux & 0x07, (mux >> 3) & 0x07
    n1 = SOURCE_NAMES.get(ch1, "ILLEGAL")
    n2 = SOURCE_NAMES.get(ch2, "ILLEGAL")
    out = [
        "mux word  =0x%02X   ch1=%s(0x%X)  ch2=%s(0x%X)" % (mux, n1, ch1, n2, ch2),
        "mono      =%d" % b[1],
        "codec word=0x%02X%02X  (RAM[0x23]:RAM[0x25], the 16-bit chain)"
        % (b[2], b[3]),
        "P3 live   =0x%02X   btn ch1(P3.3)=%d ch2(P3.4)=%d mono(P3.5)=%d"
        "  (active low: 0 = held)"
        % (b[4], (b[4] >> 3) & 1, (b[4] >> 4) & 1, (b[4] >> 5) & 1),
        "host mux sets accepted=%d  rejected=%d" % (b[5], b[6]),
    ]
    if "ILLEGAL" in (n1, n2):
        out.append("  ILLEGAL PATTERN: not one of mic/line/inst. No source is"
                   " selected, so any audio measurement taken now is void --"
                   " this is the exact state that invalidated 2026-07-29.")
    elif ch1 != SOURCES["line"] or ch2 != SOURCES["line"]:
        out.append("  NOTE: the bench loopbacks are wired to the LINE inputs"
                   " (BENCH_WIRING.md). A channel not on `line` is not carrying"
                   " the test signal. Fix with: mboxtlm.py setmux line line")
    if b[6]:
        out.append("  %d host mux request(s) were REJECTED as illegal patterns"
                   " and left the mux unchanged." % b[6])
    return out


def block10(b):
    """#165 — the CS8427 readback probe, transposed here rather than on the 8051.

    b[i] is P3 sampled after read clock i, MSB of the reply first. The firmware
    does not know which pin CDOUT is on -- nothing establishes that it is wired
    at all -- so it reports the whole port and lets the pin identify itself.

    Bit p across the eight samples is the byte pin P3.p produced. We wrote
    CLOCKSOURCE = 0x40 (RUN set), so a pin reading 0x40 IS CDOUT, and the part
    is on SPI and holding our configuration.
    """
    out = ["raw P3 samples: " + " ".join("%02x" % x for x in b)]
    if len(set(b)) == 1:
        out.append("every sample identical (0x%02x) -- NO PIN ANSWERED." % b[0])
        out.append("  CDOUT is not wired to P3, or the part is not driving it.")
        out.append("  This is a real answer, not a failed guess: see")
        out.append("  FINDING_ep0_request_harness.md and cs8427.c.")
        return out
    hits = []
    for pin in range(8):
        val = 0
        for s in b:
            val = ((val << 1) | ((s >> pin) & 1)) & 0xFF
        note = ""
        if val == 0x40:
            note = "  <== CLOCKSOURCE = 0x40, RUN set: THIS IS CDOUT"
            hits.append(pin)
        elif val in (0x00, 0xFF):
            note = "  (stuck %s)" % ("low" if val == 0 else "high")
        out.append("  P3.%d -> 0x%02x%s" % (pin, val, note))
    if hits:
        out.append("CS8427 answered on P3.%s. It is on SPI and configured."
                   % ",".join(str(h) for h in hits))
    else:
        out.append("no pin spelled 0x40. The part did not return CLOCKSOURCE;")
        out.append("  compare against what cs8427_boot_init() wrote before")
        out.append("  concluding the mode is wrong.")
    return out


DECODERS = {0: block0, 1: block1, 2: block2, 3: block3,
            4: block4, 5: block5, 6: block6, 7: block7,
            8: block8, 9: block9, 10: block10}

TITLES = {
    0: "identity and liveness",
    1: "EP0 continuation forensics",
    2: "last SETUP seen",
    3: "VECINT histogram",
    4: "peripheral results + port state",
    5: "isochronous streaming state",
    6: "DMA and C-port live state",
    7: "EP0 buffer counts + suspend tally",
    8: "boot-ROM handoff snapshot",
    9: "panel state (selected source)",
    10: "CS8427 readback probe (#165) -- which pin answered",
}


# ------------------------------------------------------------------- transport

def find_device():
    if not HAVE_USB:
        sys.exit("pyusb not installed. On the void box: ~/mbox-venv/bin/python")
    for pid in AUDIO_PIDS:
        dev = usb.core.find(idVendor=MBOX_VID, idProduct=pid)
        if dev is not None:
            return dev, pid
    sys.exit("no audio-mode Mbox found (looked for %04x:%s)"
             % (MBOX_VID, "/".join("%04x" % p for p in AUDIO_PIDS)))


def read_block(dev, index, timeout=2000):
    """One 8-byte EP0 IN. Short reads are an error, not something to pad."""
    data = dev.ctrl_transfer(REQ_IN, TLM_REQ_READ, index, 0,
                             BLOCK_SIZE, timeout)
    if len(data) != BLOCK_SIZE:
        raise IOError("block %d returned %d bytes, expected %d"
                      % (index, len(data), BLOCK_SIZE))
    return bytes(data)


def show(index, b, raw=False, device_build=None):
    """Print one block, decoded -- but only if the DEVICE serves it.

    The firmware answers an out-of-range block index with eight 0xFF bytes,
    and block 10's legitimate "no pin answered" reading is also eight 0xFF
    bytes. Those are indistinguishable on the wire, so the disambiguation has
    to come from somewhere else: which build is running.

    Two wrong versions of this preceded the current one, and both produced a
    confident false reading rather than an error:

      1. Originally, ANY all-0xFF reply printed the sentinel. That made block
         10's decoder unreachable for its no-pin-answered result.
      2. The fix for (1) keyed on `index >= NUM_BLOCKS` -- the HOST's block
         count. Run against build 0x0011 on 2026-08-03 that decoded the
         sentinel for blocks 9 and 10 as data, and reported "NO PIN
         ANSWERED. CDOUT is not wired to P3" from a build with no CS8427
         probe compiled into it. The host's block count says nothing about
         the device's.

    The device's build id (block 0) against BLOCK_FIRST_BUILD is the only
    thing that actually answers the question, so that is what this uses.
    Callers pass device_build; without it, an all-0xFF reply is reported as
    ambiguous rather than guessed at.
    """
    print("block %d -- %s" % (index, TITLES.get(index, "?")))
    if raw or index not in DECODERS:
        print("  raw: %s" % " ".join("%02x" % x for x in b))

    need = BLOCK_FIRST_BUILD.get(index)
    if device_build is not None and need is not None and device_build < need:
        print("  NOT SERVED BY THIS BUILD -- block %d arrived in build 0x%04X "
              "and the device is running 0x%04X." % (index, need, device_build))
        print("  The eight bytes above are the out-of-range sentinel, not a "
              "measurement. Flash a newer build to read this block.")
        return

    if all(x == 0xFF for x in b):
        if device_build is None:
            print("  all 0xFF -- AMBIGUOUS. This is either the out-of-range "
                  "sentinel or a genuine all-ones reading; block 0's build id "
                  "is what separates them, and it was not read.")
            return
        if need is None:
            print("  (all 0xFF -- unknown block index sentinel)")
            return
        # Served by this build, so all-0xFF is data. Fall through and decode.

    if index in DECODERS:
        for line in DECODERS[index](b):
            print("  " + line)


# ----------------------------------------------------------------- subcommands

def device_build(dev):
    """The build id the DEVICE reports, from block 0. One 8-byte EP0 read.

    Every path that decodes a block needs this: without it an all-0xFF reply
    cannot be told from the out-of-range sentinel. Block 0 has existed in
    every build, so this read is always meaningful.
    """
    return u16(read_block(dev, 0), 0)


def cmd_all(dev, args):
    build = device_build(dev)
    served = max(i for i, v in BLOCK_FIRST_BUILD.items() if v <= build)
    if served < NUM_BLOCKS - 1:
        print("# device build 0x%04X serves blocks 0..%d; this tool knows "
              "0..%d. The rest are sentinels, not measurements."
              % (build, served, NUM_BLOCKS - 1))
        print()
    for i in range(NUM_BLOCKS):
        show(i, read_block(dev, i), args.raw, device_build=build)
        print()


def cmd_read(dev, args):
    show(args.block, read_block(dev, args.block), args.raw,
         device_build=device_build(dev))


def cmd_raw(dev, args):
    b = read_block(dev, args.block)
    print(" ".join("%02x" % x for x in b))


def cmd_watch(dev, args):
    build = device_build(dev)
    need = BLOCK_FIRST_BUILD.get(args.block)
    if need is not None and build < need:
        sys.exit("block %d arrived in build 0x%04X and the device runs 0x%04X. "
                 "Watching it would poll the out-of-range sentinel."
                 % (args.block, need, build))
    for i in range(args.count):
        b = read_block(dev, args.block)
        print("[%2d] %s" % (i, " ".join("%02x" % x for x in b)))
        for line in DECODERS.get(args.block, lambda _b: [])(b):
            print("     " + line)
        if i + 1 < args.count:
            time.sleep(args.interval)


def cmd_reset(dev, _args):
    dev.ctrl_transfer(REQ_OUT, TLM_REQ_RESET, 0, 0, None, 2000)
    print("per-experiment counters cleared "
          "(stage/phases/loop_count/peripheral results are kept by design)")


def cmd_ep0test(dev, args):
    """Cross-check the device's own chunk counter against host-visible success.

    For an N-packet reply the device pushes N chunks per successful transfer;
    a shortfall means it stopped being asked, which is a lost interrupt rather
    than a host-side timeout. Host-visible success alone cannot separate those
    two, which is the whole reason block 1 exists.

    Ported from the retired tools/mbox_telemetry.py, which read only the first
    5 of the 11 blocks and so silently reported nothing from block 5 on.
    """
    print("%-28s %-6s %-9s %s"
          % ("transfer", "pkts", "host ok", "device chunks"))
    for label, wv, wlen, pk in [("DEVICE  wLen=8", 0x0100, 8, 1),
                                ("DEVICE  wLen=18", 0x0100, 18, 3),
                                ("CONFIG  wLen=64", 0x0200, 64, 8)]:
        dev.ctrl_transfer(REQ_OUT, TLM_REQ_RESET, 0, 0, None, 2000)
        ok = 0
        for _ in range(args.trials):
            try:
                dev.ctrl_transfer(0x80, 0x06, wv, 0, wlen, 400)
                ok += 1
            except Exception:
                pass
        chunks = u16(read_block(dev, 1), 4)
        expect = ok * pk
        flag = "" if chunks >= expect else "   <-- SHORTFALL, packets lost"
        print("%-28s %-6d %2d/%-6d %d (expect >= %d)%s"
              % (label, pk, ok, args.trials, chunks, expect, flag))


def cmd_setmux(dev, args):
    """Select the input source on both channels without touching the panel.

    Safe to point at a running device: it reaches only states the front-panel
    buttons already reach, by the same publish path, and the firmware rejects
    any pattern that is not one of the three legal ones. Unlike enter-DFU this
    costs nothing to get wrong -- send it again with different arguments.
    """
    mono = {"on": 1, "off": 0, "keep": 0xFF}[args.mono]
    wvalue = (SOURCES[args.ch2] << 3) | SOURCES[args.ch1]
    dev.ctrl_transfer(REQ_OUT, TLM_REQ_SET_MUX, wvalue, mono, None, 2000)
    print("requested ch1=%s ch2=%s mono=%s (wValue=0x%04X wIndex=0x%02X)"
          % (args.ch1, args.ch2, args.mono, wvalue, mono))
    # Read it back rather than report success from the absence of an exception.
    # A stall raises, but a request that is accepted and then does not take
    # would otherwise look identical to one that worked.
    print()
    show(9, read_block(dev, 9), args.raw, device_build=device_build(dev))


def main():
    # --raw is accepted on BOTH sides of the subcommand. It was top-level
    # only at first, so the natural `read 6 --raw` died with "unrecognized
    # arguments" -- mid-experiment, against a device that does not stay on
    # the bus indefinitely. A diagnostic tool that is fussy about argument
    # order costs a whole run to find out.
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--raw", action="store_true", help="also print raw bytes")

    p = argparse.ArgumentParser(description=__doc__.split("\n")[1],
                                parents=[common],
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("all", parents=[common], help="read and decode every block")
    for name, helptext in (("read", "read one block"),
                           ("raw", "read one block, hex only")):
        sp = sub.add_parser(name, parents=[common], help=helptext)
        sp.add_argument("block", type=int)
    sp = sub.add_parser("watch", parents=[common], help="re-read a block repeatedly")
    sp.add_argument("block", type=int)
    sp.add_argument("-n", "--count", type=int, default=10)
    sp.add_argument("-i", "--interval", type=float, default=0.5)
    sub.add_parser("reset", parents=[common], help="clear the per-experiment counters")
    sp = sub.add_parser("ep0test", parents=[common],
                        help="measure EP0 continuation loss against block 1")
    sp.add_argument("-n", "--trials", type=int, default=40)
    sp = sub.add_parser("setmux", parents=[common],
                        help="select the input source on both channels")
    sp.add_argument("ch1", choices=sorted(SOURCES), help="channel 1 source")
    sp.add_argument("ch2", choices=sorted(SOURCES), help="channel 2 source")
    sp.add_argument("--mono", choices=("on", "off", "keep"), default="keep")

    args = p.parse_args()
    dev, pid = find_device()
    print("# %04x:%04x audio mode\n" % (MBOX_VID, pid))
    {"all": cmd_all, "read": cmd_read, "raw": cmd_raw,
     "watch": cmd_watch, "reset": cmd_reset,
     "ep0test": cmd_ep0test,
     "setmux": cmd_setmux}[args.cmd](dev, args)


if __name__ == "__main__":
    main()
