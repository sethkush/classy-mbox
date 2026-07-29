# Capture returns a fixed 8-frame artifact, not audio

Measured 2026-07-29 on the void box (192.168.1.76), unit `0dba:2000` running
mboxfw as flashed at commit `1fdeec8` (the #147 build). Bench wiring: S/PDIF
out looped to S/PDIF in, analog out 2 looped to source input 2.

## What was run

Two-tone stereo WAV (left 1000 Hz, right 1500 Hz, -6 dBFS, S24_3LE) played to
`hw:1,0` while capturing `hw:1,0`, at 44100 and again at 48000. Distinct
per-channel frequencies so the captured channel would identify which output
fed it. A silence baseline (capture with nothing playing) was taken at each
rate first.

## What came back

    rate    stream     ch1 rms   ch2 rms   @1000Hz   @1500Hz
    44100   baseline   -3.6      -3.6      -91.8     -95.0
    44100   loopback   -3.5      -3.5      -94.7     -88.6
    48000   baseline   -3.5      -3.5      -92.3     -95.9
    48000   loopback   -3.5      -3.5      -93.8     -87.4

All dBFS. Neither tone is present at either rate: every Goertzel bin at 1000
and 1500 Hz sits at the -87..-96 dB numerical floor, and the loopback columns
are indistinguishable from the baseline columns. Capture output does not
depend on whether anything is playing.

## The artifact

The capture byte stream has a rigid 48-byte period = 8 frames of 6 bytes:

    frames 0..4   varying data
    frame  5      00 00 80 00 00 80   (both channels -full-scale)
    frame  6      ff ff 7f ff ff 7f   (both channels +full-scale)
    frame  7      00 00 80 00 00 80   (both channels -full-scale)

Offsets of the pinned frames repeat at gaps of exactly 12 and 36 bytes, with
no exception in 300 kB. 12499 hits in 50000 frames -- 3 of every 8, to the
frame.

A DFT scan of the loopback capture, 100..6000 Hz, finds its largest component
at 5500 Hz, which is the scan's nearest bin to 44100/8 = 5512.5 Hz. At 48 kHz
the pinned frames land on the same 8-frame period, i.e. 6000 Hz. **The only
spectral content in the capture is the framing artifact itself**, and it is
locked to the sample clock, not to a fixed frequency.

De-interleaving to drop the 3 pinned frames per 8 does not reveal a tone
either -- the 5-of-8 stream's top bin is the same 5500 Hz at -52 dB.

Byte throughput is correct: 5 s of capture at 44100 yielded exactly 1323000
bytes = 44100 x 6 x 5. The device delivers the right *number* of bytes; the
content is wrong. `dmesg` is clean -- no xruns, no USB errors, no resets
across either run.

## What this does and does not establish

It does **not** resolve #147. The loopback test was meant to name IRAM
0x23.2/0x23.3 by showing audio passing at 44.1 on the flashed unit where the
old build was silent. That test cannot run: capture carries no audio at
*either* rate, so 44.1 and 48 kHz are indistinguishable and there is nothing
for the mute hypothesis to move. The #147 change remains correct on its own
terms -- it makes mboxfw match stock, which was never contingent on this
measurement -- but the mute reading of those bits stays *inference from
timing*, exactly as recorded in `FINDING_open_questions.md` Sec 1.6.

It does establish that the capture path is broken in a new and specific way.
Previously capture delivered zero-length isoc packets (see the "Isoc returns
zero-length" note). It now delivers full-rate byte counts with structured
non-audio content. That is a different failure, and the 8-frame period is a
strong lead: whatever fills the capture buffer is writing 5 frames and
leaving 3 at rail values, every time.

Playback was not independently verified. The only return path to the host is
capture, so a broken capture path hides the state of the output path -- the
tone may or may not have reached the outputs.

## Next step

Read telemetry block 6 (DMA and C-port live state, `mboxfw/src/telemetry.c`)
from the running unit while capture is streaming. `tools/mboxflash_linux.py`
has no telemetry reader; one has to be written. That block was added for
exactly this question and reads the DMA and C-port state that would show
whether the capture DMA is filling only 5 of every 8 frames.

## Addendum: the unit dropped off the bus unprompted

Timeline from `dmesg` (void box uptime 43200 s at 02:30):

    40009.9   device 32 enumerates, 0dba:2000  (post-flash, audio mode)
    ...       the loopback runs above, all clean, no errors logged
    40830.2   usb 2-1.2: USB disconnect, device number 32

That is roughly 8 minutes AFTER the last capture finished, with nothing being
sent to the device in the interval -- the analysis in between was pure local
Python over already-captured files. No error precedes the disconnect and no
re-enumeration follows it.

`uhubctl` reports port 2 of hub 2-1 as `0100 power`: port power is applied,
but the connect bit is clear. The device is powered and is not asserting its
D+ pull-up. That is consistent with the firmware having cleared USBCTL CONN,
or having hung in a state where the pull-up is off -- not with a cable or a
host-side fault.

Unprompted and delayed, this is a separate defect from the capture artifact,
and it is the more serious of the two: it makes the unit unreachable, and
telemetry is the only instrument that can see inside a running image.

`tools/mboxtlm.py` was written to read the blocks and has NOT been run against
hardware -- by the time it existed there was nothing on the bus to answer it.
It is unvalidated code.
