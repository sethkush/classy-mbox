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

RETRACTED. This was written up as a second firmware defect -- "the device is
powered and is not asserting its D+ pull-up" -- on the strength of the trace
alone. Seth then reported that both units were unplugged at the bench. A clean
disconnect with no preceding error and no re-enumeration is exactly what
pulling a cable looks like, and that explanation needs no firmware fault at
all.

The hardware account outranks the inference from the trace. There is no
evidence here of a self-detach defect, and the earlier paragraph claiming one
should not be cited. What the log actually supports is: the device left the
bus at 40830.2 and the reason is not recorded on the host side.

`tools/mboxtlm.py` was written to read the blocks and has NOT been run against
hardware -- by the time it existed there was nothing on the bus to answer it.
It is unvalidated code.

## Addendum 2: telemetry read live, 2026-07-29

`tools/mboxtlm.py` now exists and has been run against the unit. Block 6 read
three times while `arecord` was streaming at 44100, and once after it stopped:

    streaming:  89 02 70 c6 c5 ad 40 00
    streaming:  89 02 70 c6 c5 ac 40 00
    streaming:  89 02 70 c6 c5 ac 40 00
    stopped:    09 02 70 c6 00 2c 40 00

Decoded:

  * **DMACTL1 = 0x89 while streaming, 0x09 stopped.** Bit 7 is DMAEN
    (datasheet 6.5.2.3). The capture DMA IS armed during streaming and
    disarms on stop. The DMA-enable fix from `ad7ff3b` works, and the
    zero-length-isoc era is genuinely over.
  * **IEPBSIZ1 = 0x40.** This is the ENCODED size: `regs.h` EP_BSIZE is
    `size >> 3`, so 0x40 means a 512-byte buffer, against the 264.6 bytes a
    44.1 kHz frame needs. Ample. Worth stating because 0x40 read as a raw
    byte count looks like a fatal undersize buffer, and it is not one.
  * **DMATSH1 = 0x80, DMATSL1 = 0x03** (from `hw_init.c:171-172`, both cited
    to Rev 20 fcn.0x08CB): 3 bytes per time slot on slots 0 and 1 = 6 bytes
    per stereo 24-bit sample. Identical to stock.
  * **vec_iep1 = 0 throughout.** The capture endpoint interrupt never fires,
    which is consistent with the design -- the path is DMA-driven and the
    IEP1 vector is deliberately unhandled.
  * **CPTSTA = 0x70 and ACGCTL = 0xC6, constant** across streaming and idle.
  * **IEPCNF1 = 0xC5 streaming, 0x00 stopped**, while OEPCNF2 stays 0xC5 in
    both. The capture endpoint config is torn down on stop and the playback
    one is not. Asymmetric; unexplained; not obviously harmful.

**What this narrows.** The DMA is armed, its time-slot configuration is
byte-identical to stock, the endpoint buffer is large enough, and full-rate
bytes arrive. So the 8-frame artifact is NOT a DMA arming or sizing fault.
Whatever is wrong is upstream of the DMA -- in the C-port serial interface
configuration or in what the codec is actually clocking out. The C-port
registers to suspect are the CPTCNF/CPTRXCNF group in `hw_init.c:58-129`,
particularly the BYOR byte-order bit and the CPTRXCNF4 divider, both of which
have already been wrong once each in this project's history.

**Instrumentation gap for the next flash.** Block 6 does not expose DMATSH1,
DMATSL1, DMABCNT1L/H, or the CPTCNF group, so all four had to be read from
source rather than from the running part. DMABCNT1 in particular is updated
every SOF and would show the live per-frame byte count directly. Also:
`TLM_BUILD_ID` is still 0x000B, not bumped for the #147 flash, so block 0
currently cannot distinguish this build from its predecessor. Both are cheap
to fix and both should be fixed before the next image goes out.

**Caveat on bit decoding.** `mboxtlm.py` no longer prints IEPCNF bit names.
The bit map used at first was assembled in the tool from a partial comment and
decoded the stock value 0xC5 as "ISO=0" -- not marked isochronous -- which
contradicts `regs.h`'s own reading of 0xC5 as "ISO, BPS field = 5". One of the
two is wrong. Until the datasheet bit map is transcribed and cited, IEPCNF
values are compared against known-good constants instead of decoded.
