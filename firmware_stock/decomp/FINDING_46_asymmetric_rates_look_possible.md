# Asymmetric rates (96 in / 48 out) look possible, and the blocker is our own code

Measured 2026-08-05 on unit B, build 0x002C, capture opened at 96 kHz and
playback at 48 kHz simultaneously.

## Three of the four gates are already passed

    bandwidth denials     0          (582 B + 294 B = 3 + 2 = 5 microframes)
    IEPDCNTX1             0xE0       NAK + 96 samples/frame   capture at 96 kHz
    OEPDCNTX2             0xB0       NAK + 48 samples/frame   playback at 48 kHz
    stalls                1
    clock                 mode 7, both dividers /2

**The USB engine is already doing it.** Those two DCNT registers are hardware
counters, and they show 96 samples in and 48 samples out in the same frame.

This also settles the scheduling question that killed 88.2 duplex. 5 microframes
fits, where 6 does not:

    2 + 2 = 4   48 kHz duplex          WORKS
    3 + 2 = 5   96 in / 48 out         WORKS (this measurement)
    3 + 3 = 6   88.2 duplex @ 534 B    DENIED
    4 + 4 = 8   96 duplex @ 582 B      DENIED

## The blocker is firmware, in two places

1. **usb.c stalls it.** The SET_CUR cross-check refuses a rate whose class
   disagrees with an ACTIVE stream's alt -- that is the `stalls: 1` above,
   working exactly as written. Its comment justifies itself with "the device
   has ONE clock for both directions", which is true of how streaming_set_rate()
   PROGRAMS the part, not of the silicon.

2. **Both dividers move together.** Clock mode 7 writes CPTCNF4 AND CPTRXCNF4 to
   /2, so the C-port frames at 96 kHz in both directions while USB feeds
   playback only 48 samples/frame -- which would starve playback. Per Figure 2-1
   these are separate registers for separate directions: CSCLK = MCLKO/B from
   CPTCNF4[2:0] (playback) and CSCLK2 = MCLKO2/B2 from CPTRXCNF4[2:0] (capture).
   Stock's mode 5 writes CPTRXCNF4 ALONE (Rev 20 0x07A0, Rev 22 0x077E) -- a
   per-direction divider is precisely what that branch was for, and it is the
   only stock code that ever sets /2.

The fix is to drive each divider from its own direction's rate and to relax the
cross-check to the pairings the hardware can actually hold.

## Which pairings are legal

Each rate family shares an ACG frequency word -- 88.2 is 44.1 doubled and 96 is
48 doubled, via the divider, with the word unchanged (see
streaming_set_rate()). So:

    96 kHz in / 48 kHz out       legal, both from the 48 kHz word
    88.2 kHz in / 44.1 kHz out   legal, both from the 44.1 kHz word
    96 kHz in / 44.1 kHz out     NOT legal, different words
    (and the mirrored cases, capture slow / playback fast)

## What is NOT established

**Whether the CODEC tolerates different rates per direction.** It has no
register interface -- its whole control surface is the 16-bit word in codec.c,
with no rate or mode field -- so it derives its rate from the clocks it is
given. Whether its ADC and DAC take independent clock domains or share one
master is a board-wiring question this measurement cannot reach. If they share,
asymmetric stops at the codec whatever the TAS1020B can do.

Testing it needs the firmware change first, then a self-loop measurement on
unit A (out2 -> src2), because a starved or wrong-pitch playback is exactly
what the CURRENT firmware would produce anyway and proves nothing.

## Cost

Splitting the divider programming and relaxing the cross-check is realistically
40-80 bytes against the 1 byte free at 0x002C. This needs deliberate reclaim,
not another encoding trick. Candidates named in the 0x002C notes: widening
tlm_playback_resyncs out of saturation is a COST not a saving; the honest
sources are retiring a settled telemetry block or the mute-pair experiment
machinery once #171's per-direction question is closed.
