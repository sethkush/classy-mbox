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
-- the same 2 x 582 B, the same frame, the same TT. So 96 kHz is inherently
SIMPLEX on this hardware. This is a stronger limit than the endpoint-RAM
ceiling recorded in FINDING_46_96k_capture_works_playback_starves.md (1152 +
576 = 1728 > 1288): that one is about where the buffers live, this one would
still bite with unlimited buffer RAM.

Capture at 96 kHz is unaffected and still correct: with A playing 1 kHz at
48 kHz, B captured at 96 kHz reading -27.88 dBFS, identical to B capturing the
same signal at 48 kHz (-27.88).

## What this leaves open

**Whether the analog path resolves above 24 kHz** -- still unmeasured. It needs
a 96 kHz player and a 96 kHz listener simultaneously, which needs them on
different transaction translators, i.e. one unit physically moved to the other
host controller (the void box has a second bus, usb1, currently carrying only a
webcam). No firmware change reaches this.
