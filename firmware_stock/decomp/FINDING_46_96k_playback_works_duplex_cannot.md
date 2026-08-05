# 96 kHz: playback fixed by buffer slack; duplex is impossible on full speed

Build 0x002B on both units, 2026-08-05. Playback 696 B / capture 576 B.

## Playback at 96 kHz works now

| arm | DMABCNT0 | resyncs | listener heard | g@1k |
|---|---|---|---|---|
| 0x002A, A plays 96 | (no counter) | (no counter) | **-97.48 dBFS** | 0.000005 |
| 0x002B, A plays 96 | 156 -> 162 B, jittering | **255 (sat)** | **-39.72 dBFS** | 0.001446 |
| 0x002B, A plays 48 (control) | 294 B, steady | **0** | -37.59 dBFS | 0.002292 |

Up ~58 dB from silence, and within 2 dB of the 48 kHz control on the same
signal chain. **The playback buffer was the defect.** 576 B is exactly one
frame at 96 kHz, leaving the DMA nowhere to read that the host was not
concurrently writing; 696 B (116 samples, 1.21 frames) restores the slack
stock had at 48 kHz.

## The SOF watchdog is a SECOND, separate defect

resyncs is 0 at 48 kHz and saturated at 96. The two rates differ in whether
`streaming_sof()` ever evaluates alignment at all:

* At 48 kHz DMABCNT0 is a dead-steady 294 B, so the watchdog takes its
  `unchanged -> nothing to do` early exit (Rev 22 @ 0x0D71) every frame.
* At 96 kHz the C-port drains 576 B/frame against a 696 B buffer, so the fill
  level jitters (156, 162, ...) and the watchdog evaluates every frame.

The DMA moves 3 bytes per time slot (DMATSH/DMATSL: BPTS=3, two slots per
stereo sample), so an SOF snapshot can land mid-sample and read
`content = 3 (mod 6)`. The watchdog cannot distinguish that from a real
misalignment and tears the playback DMA down. A genuine misalignment is
PERSISTENT -- the pointer stays offset -- while a mid-sample snapshot corrects
itself on the very next frame. Rev 22 never had to make that distinction
because it never ran at a rate where the count moved.

CAVEAT ON THE 255: the counter saturates, so it cannot separate "255 fires in
the first second, then quiet" from "every frame forever". Widen it before
drawing a rate from it.

## Duplex 96 kHz cannot work on full-speed USB, at any buffer size

The >24 kHz sweep (1/10/20/26/30 kHz, A playing at 96, B capturing at 96)
returned an IDENTICAL -77 dBFS / peak 0.0078 at every frequency -- a floor, not
a signal, and the same reading as 0x002A's `play 96 -> cap 96` arm. The host
says why:

    usb 2-1.4: cannot submit urb 0, error -28: not enough bandwidth

Both bench units hang off one USB 2.0 hub and both negotiate at 12M, so they
share ONE full-speed transaction translator, and full-speed periodic bandwidth
is budgeted per TT. Two 582 B isochronous endpoints is 1164 B of payload per
1 ms frame; with per-transaction overhead and bit stuffing that exceeds the
~1350 byte-times Linux will allocate, and the second stream is refused.

**The same arithmetic applies to ONE Mbox streaming both directions at 96 kHz**
-- the same 2 x 582 B, the same frame, the same TT.

MEASURED DIRECTLY, 2026-08-05, so this is no longer an inference from the
two-units-on-one-hub case. Unit B alone, playing and capturing at 96 kHz on
build 0x002C:

    usb 2-1.2: cannot submit urb 0, error -28: not enough bandwidth

and the identical test at 48 kHz produces no kernel output at all and runs
clean. One device, one cable, both directions: refused at 96 kHz, fine at 48.

So 96 kHz is inherently SIMPLEX on this hardware. This is a stronger limit than the endpoint-RAM
ceiling recorded in FINDING_46_96k_capture_works_playback_starves.md (1152 +
576 = 1728 > 1288): that one is about where the buffers live, this one would
still bite with unlimited buffer RAM. Indeed the RAM ceiling is MOOT: it
describes a configuration the host refuses to schedule before the buffers
could ever matter. The TAS1020B is full-speed only, so there is no faster
mode to escape into.

Capture at 96 kHz is unaffected and still correct: with A playing 1 kHz at
48 kHz, B captured at 96 kHz reading -27.88 dBFS, identical to B capturing the
same signal at 48 kHz (-27.88).

## What this leaves open

**Whether the analog path resolves above 24 kHz** -- still unmeasured. It needs
a 96 kHz player and a 96 kHz listener simultaneously, which needs them on
different transaction translators, i.e. one unit physically moved to the other
host controller (the void box has a second bus, usb1, currently carrying only a
webcam). No firmware change reaches this.

## 0x002C: the two-sighting rule, measured

Same chain, same tone, A playing to B capturing at 48 kHz.

| build | resyncs | level | g@1k | analyser verdict |
|---|---|---|---|---|
| 0x002A play 96 | (no counter) | -97.48 dBFS | 0.000005 | silent |
| 0x002B play 96 | **255 (sat)** | -39.72 | 0.001446 | no clean agreement |
| **0x002C play 96** | **1** | **-37.65** | **0.002327** | 1 kHz dominant, ZCR agrees |
| 0x002C play 48 (control) | 0 | -37.57 | 0.002294 | 1 kHz dominant, ZCR agrees |

96 kHz playback is now within 0.08 dB and 1.4% of tone energy of the 48 kHz
control, and both pass the agreement check that 0x002B's 96 kHz arm failed.

resyncs reads 1 at t=2 s and the SAME 1 at t=6 s, so the watchdog fired once
during stream ramp-up and never again. That is the seeded shadow working as
designed: sof_bcnt_hi/lo start 0xFF/0xFF so the first frame of a stream is
always evaluated, and a genuinely mid-fill buffer at start can read misaligned
twice running. It is not a residual fault, and it is exactly the reading the
saturating counter could not have distinguished from a thrash.

DMABCNT0 still jitters at 96 kHz (156 -> 162) and is still steady at 48 kHz
(174). That was never the defect -- the defect was ACTING on the jitter.

## 88.2 kHz is also simplex-only AS BUILT, but for a different reason

Measured on 0x002C, unit B alone, counting "not enough bandwidth" in dmesg:

| | duplex | simplex |
|---|---|---|
| 88.2 kHz | denied | OK |
| 96 kHz | denied | OK |

96 kHz is denied because it genuinely needs 576 B/frame per direction. 88.2 kHz
is denied because it SHARES ALT 2 with 96 kHz and the host reserves bandwidth
from wMaxPacketSize, which is 582 for that alt. 88.2 kHz needs only 89 samples
x 6 = 534 B. It is paying 96 kHz's bill for bandwidth it never uses.

Whether a dedicated alt at 534 B would clear the scheduler is NOT established.
The binding constraint is EHCI split-transaction scheduling: a full-speed
transfer behind a transaction translator is carved into ~188 B start-splits,
one per microframe, from the 8 microframes in a frame.

    294 B (48 kHz, alt 1)     2 start-splits    4 of 8 duplex   WORKS
    534 B (88.2, hypothetical) 3 start-splits   6 of 8 duplex   UNTESTED
    582 B (alt 2 today)        4 start-splits   8 of 8 + CS     DENIED

88.2 duplex therefore sits on the boundary. The cheap way to settle it is a
diagnostic image that sets alt 2's wMaxPacketSize to 534 and lists 88200 only
-- one constant and one dropped rate entry, costing negative bytes -- rather
than adding a third alt setting (~92 B of descriptors) on spec.

## ANSWERED: 88.2 kHz cannot be duplex either, and the boundary is 2 splits

Build 0x002D (MBOX_882_DIAG) on unit A, whose out2 -> src2 self-loop makes a
duplex test self-contained -- no second device, so no second stream competing
for the same budget. Alt 2 retargeted at 88.2 kHz alone with wMaxPacketSize
534 B, confirmed parsed by the host (`Altset 2 / Rates: 88200`).

| A duplex, self-loop | denials | level | g@1k | verdict |
|---|---|---|---|---|
| 88.2 kHz @ 534 B | **6553** | -75.92 dBFS | 0.000001 | no coherent tone |
| 48 kHz @ 294 B | **0** | -29.11 dBFS | 0.005449 | 1 kHz dominant |

Dropping the reservation 582 -> 534 changed NOTHING.

**CORRECTION, later the same day.** The first version of this entry concluded
from that null result that the boundary lay "between 2 and 3 start-splits per
direction", and derived a ~62 kHz duplex ceiling from it. That model is WRONG
and the asymmetric measurements falsify it: a 582 B endpoint needs
ceil(582/188) = 4 start-splits, so 96-in/48-out is 4 + 2 = 6 -- the same six as
88.2 duplex at 534 + 534 -- and it schedules fine while 88.2 duplex does not.
Same split count, opposite outcomes.

What fits EVERY measurement is total reserved bytes, reserved from
wMaxPacketSize rather than from the rate actually in use:

    294 + 294 =  588   48 in  / 48 out      WORKS
    582 + 294 =  876   96 in  / 48 out      WORKS
    582 + 294 =  876   88.2 in / 44.1 out   WORKS
    534 + 534 = 1068   88.2 duplex          DENIED
    582 + 582 = 1164   96 duplex            DENIED

So the threshold is between 876 and 1068 B, and the duplex ceiling is a
per-direction reservation of 438-534 B, i.e. somewhere between 73 and 89 kHz.
It cannot be pinned further from the data in hand, and the exact figure does
not matter: 88.2 and 96 are both outside it and 44.1/48 is comfortably inside.

CONSEQUENCE, unchanged: a third alt setting for 88.2 at 534 B would buy
nothing, and the ~92 bytes it would have cost are not worth spending. Alt 2
stays as it is, carrying both doubled rates at 582 B, and both are
duplex-denied. The diagnostic cost NEGATIVE bytes and settled it in one flash --
and note that it settled the SHIPPING question correctly even though the model
it suggested was wrong.

TOOL FIX made in the same session: tone_peak.py reported the silent 88.2
capture as "1 kHz dominant, ZCR agrees" -- a confident verdict on a file with
no signal in it. The g1-vs-g2 ratio test is satisfied by noise (0.000001 vs
0.000000) and the -80 dBFS silence guard did not fire because the floor sat at
-75.92. It now also requires the strongest bin to exceed 5% of rms; the real
loopback reads 15.6% and the silent capture 0.7%.
