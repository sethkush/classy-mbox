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
REQ_IN = 0xC0              # vendor | device-to-host | device
REQ_OUT = 0x40             # vendor | host-to-device | device

BLOCK_SIZE = 8
NUM_BLOCKS = 8

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


DECODERS = {0: block0, 1: block1, 2: block2, 3: block3,
            4: block4, 5: block5, 6: block6, 7: block7}

TITLES = {
    0: "identity and liveness",
    1: "EP0 continuation forensics",
    2: "last SETUP seen",
    3: "VECINT histogram",
    4: "peripheral results + port state",
    5: "isochronous streaming state",
    6: "DMA and C-port live state",
    7: "EP0 buffer counts + suspend tally",
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


def show(index, b, raw=False):
    print("block %d -- %s" % (index, TITLES.get(index, "?")))
    if raw or index not in DECODERS:
        print("  raw: %s" % " ".join("%02x" % x for x in b))
    if all(x == 0xFF for x in b):
        print("  (all 0xFF -- unknown block index sentinel)")
        return
    if index in DECODERS:
        for line in DECODERS[index](b):
            print("  " + line)


# ----------------------------------------------------------------- subcommands

def cmd_all(dev, args):
    for i in range(NUM_BLOCKS):
        show(i, read_block(dev, i), args.raw)
        print()


def cmd_read(dev, args):
    show(args.block, read_block(dev, args.block), args.raw)


def cmd_raw(dev, args):
    b = read_block(dev, args.block)
    print(" ".join("%02x" % x for x in b))


def cmd_watch(dev, args):
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

    args = p.parse_args()
    dev, pid = find_device()
    print("# %04x:%04x audio mode\n" % (MBOX_VID, pid))
    {"all": cmd_all, "read": cmd_read, "raw": cmd_raw,
     "watch": cmd_watch, "reset": cmd_reset}[args.cmd](dev, args)


if __name__ == "__main__":
    main()
