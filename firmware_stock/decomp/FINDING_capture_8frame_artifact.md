# Capture returns a fixed 8-frame artifact, not audio

Measured 2026-07-29 on the void box (192.168.1.76), unit `0dba:2000` running
mboxfw as flashed at commit `1fdeec8` (the #147 build). Bench wiring: S/PDIF
out looped to S/PDIF in, analog out 2 looped to source input 2.

> **REFRAMED 2026-07-31 — read `FINDING_147_the_capture_stream_is_noise.md`.**
> The "5 data + 3 rail frames" framing in this document is wrong on the "data"
> half, and it misled every subsequent pass. The broadband RMS reported in the
> table below is **-3.5 dBFS**, ~90 dB hotter than a working ADC on a quiet
> input, identical at both rates and with and without playback. The 3/8 rails
> alone put the RMS floor at -4.26 dBFS, so the energy budget closes with **no
> audio in it at all**: this is 8 frames of noise, 3 of which saturate. The
> right question is not what corrupts 3 frames in 8 but why nothing drives
> CDATI — see #166, #167, #168.

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

## Addendum 3: byte order ruled out; the pattern is sample-clock locked

Two more measurements, both on `loop44100.raw` (the capture taken with the
two-tone WAV playing), so no new hardware run was needed.

### Byte order is not the fault

`hw_init.c` records that `00 00 80` is -8388608 read little-endian but **128**
read big-endian, and `ff ff 7f` is +full-scale LE but **-1** BE. 128 and -1 are
LSB-level dither -- silence. That made a stale BYOR setting the leading
suspect: the pinned frames would be silence with the bytes still reversed.

Re-reading the same capture with each 3-byte sample reversed:

    order  ch    rms      @1000Hz   @1500Hz   @5512Hz
    LE     ch1   -3.5     -87.0     -86.7     -11.5
    LE     ch2   -3.5     -87.0     -86.7     -11.5
    BE     ch1   -7.3     -72.0     -80.2     -20.4
    BE     ch2   -7.3     -76.3     -78.4     -20.4

All dBFS. The BE reading does move things -- @1000 Hz rises 15 dB and the
5512 Hz artifact drops 9 dB -- but -72 dBFS is not a tone. The signal played
was -6 dBFS. A 66 dB shortfall is not a byte-order problem.

**RETRACTED 2026-07-29.** This exclusion is void, for the same reason the
loopback result was void: the measurement was taken with mboxfw's source mux
word at 0x00, an illegal pattern (see MUX_IRAM22_ANNOTATION.md), so there is no
guarantee any signal reached the ADC at all. A byte-order test needs a signal
to reorder. With no signal, "the tone did not appear under either byte order"
says nothing about byte order.

CPTRXCNF3 is therefore a LIVE suspect again. Stock writes 0xAC to it (Rev 20
0x0923, Rev 22 0x0844); mboxfw writes 0xA8, a deliberate divergence from
`7590af1`. Stock works and mboxfw does not, and this is one of only two
audio-path registers where mboxfw disagrees with stock's boot init.

The 5512.5 Hz / 6000 Hz structural finding below stands -- it is a property of
the returned data and needs no input signal. Only the byte-order exclusion is
withdrawn.

### The pattern is locked to the audio sample clock, not to USB framing

If the 8-frame structure were an artefact of USB packet boundaries it could not
hold phase. At 44.1 kHz a full-speed device must alternate 44- and 45-sample
packets to average 44.1 per 1 ms frame, and 44 samples = 264 bytes = 5.5 x 48,
so every packet boundary would shift the 48-byte pattern's phase -- roughly a
hundred slips per second.

Measured across the entire 220500-frame (5.00 s) capture: **4 phase
discontinuities.** The gaps between pattern hits are 12 or 36 bytes everywhere
else.

Four slips in five seconds is not USB framing; it is four sample-domain
discontinuities, at about one per 1.25 s. So the repeating structure is
generated upstream of the USB layer, in the codec / C-port clock domain, and
survives packet reassembly intact.

### The two "channels" look like one serial stream

Adjacent channel pairs run e.g. `53 8b 06 | bf a3 07` (0x068B53 vs 0x07A3BF)
and `99 ae 19 | 5b ee 18` (0x19AE99 vs 0x18EE5B) -- similar magnitude,
correlated, not identical. Two independent ADC channels with only analog out 2
looped into source 2 would not track each other like this. It is more
consistent with time slots 0 and 1 sampling adjacent positions of a single
continuous serial bit stream.

### Where that leaves it

Established, in order of certainty:

  * The capture DMA is armed, its slot map (3 B/slot, slots 0+1) is identical
    to stock, and the endpoint buffer is 512 B (Addendum 2).
  * Full-rate bytes arrive: 263.1 B/ms against 264.6 required, timed.
  * The corrupting structure is periodic at exactly Fs/8, sample-clock locked,
    and present with and without playback.
  * Byte order is not it.

So the fault is in the C-port serial interface configuration or in what the
codec is clocking out -- `CPTCNF1..4` / `CPTRXCNF2..4` in `hw_init.c:58-129`,
or `codec_init()`. Note that `CPTRXCNF4` (the receive bit-clock divider) and
BYOR have each already been wrong once in this project's history, so this
register group has a track record.

### Two ways forward

1. **usbmon**, free and available: capture the raw isoc packet stream to get
   per-packet lengths and contents as the device actually sends them, rather
   than as ALSA reassembles them. Confirms whether any packets are short and
   whether the 4 slips are dropped packets.
2. **A telemetry build that reads back the C-port group**, which costs a
   flash: block 6 exposes none of `CPTCNF1..4`, `CPTRXCNF2..4`, `DMATSH1`,
   `DMATSL1`, or `DMABCNT1L/H`. All had to be read from source, so there is
   currently NO confirmation that the values in `hw_init.c` are the values the
   registers actually hold. `DMABCNT1` updates every SOF and would give the
   live per-frame byte count directly. Reading back the C-port group would
   close the last gap between "the source says" and "the part is".

## Addendum 4: exhaustive stock-vs-mboxfw audio register diff

Now possible mechanically, from `disasm/XDATA_ACCESS_MAP.md` (every stock
register access, site and constant) against every SFR assignment in mboxfw.
Across the whole 0xFFC0-0xFFFF audio/USB range, mboxfw disagrees with stock's
boot init in exactly **two** places:

### 1. CPTRXCNF3 (0xFFD5) -- mboxfw 0xA8, stock 0xAC

    stock   0x0923  write 0xAC          (Rev 22 0x0844)
            0x0FF5  write-computed
    mboxfw  hw_init.c  = 0xA8

Bit 2 is BYOR. This is the deliberate change from `7590af1`, made to match
mboxfw's declared S24_3LE. With the byte-order exclusion above retracted, it is
un-cleared and is the leading suspect for the capture artifact.

### 2. ACGCTL (0xFFE1) -- mboxfw is missing two of stock's four operations

    stock   0x052C  clr-bits 0x3F       <-- mboxfw does not do this
            0x074D  write 0x0D          <-- mboxfw does not do this
            0x07CC  set-bits 0xC0
            0x0824  set-bits 0xC0
            0x0E10  write 0x06
    mboxfw  = 0x06 ; |= 0xC0

mboxfw performs the 0x06 write and the 0xC0 set, but never writes 0x0D and
never clears the low six bits. ACGCTL is the adaptive clock generator control
register -- the register whose misnaming as "DMACTL1" caused the
zero-length-isoc bug -- so a missing write here lands squarely on the audio
clock path.

Both are checkable against the map, and neither needs hardware to establish.

### Registers confirmed identical

CPTRXCNF2/4, CPTSTA, CPTCNF1-4, ACGDCTL, ACG1FRQ0-2, ACG2FRQ0-2, ACG2DCTL,
DMACTL0/1, DMATSH0/1, DMATSL0/1. CPTRXCNF4 deserves a note: stock writes 0x01
at 0x07A0 and 0x03 via the helper at 0x0929, and mboxfw's 0x03 mirrors the boot
init, which is the correct one of the two.

## Addendum 5: the C-port decoded against the TAS1020B datasheet

Every C-port register mboxfw writes, decoded from
`reference/tas1020a/sles025b_tas1020b_datasheet.pdf` sections 6.5.4.x. This
settles two of the three open items and retracts one of my own claims.

### CPTCNF1 = 0x0D (both) -- correct

    NTSL(4:0) = bits 7:3 = 00001 = 2 time slots per frame
    MODE(2:0) = bits 2:0 = 101   = I2S mode 5, 1 OUT and 1 IN at
                                   different frequencies

### CPTCNF2 = 0xE5, CPTRXCNF2 = 0x25 (both) -- correct

    CPTCNF2   TSL0L = 11 (32 CSCLK for slot 0), BPTSL = 100 (24 data bits),
              TSLL = 101 (32 CSCLK per slot)
    CPTRXCNF2 BPTSL = 100 (24 data bits), TSLL = 101 (32 SCLK2 per slot)

24 data bits inside 32-clock slots, two slots per frame. Consistent with
DMATSH = 0x80 (BPTS = 10b = 3 bytes per slot) and DMATSL = 0x03 (slots 0 and 1)
= 6 bytes per stereo frame. Word length and slot geometry are NOT the fault.

### ACGCTL -- RETRACTION, this is not a divergence

Addendum 4 said mboxfw "is missing two of stock's four operations" on ACGCTL and
called it a live suspect. That was wrong: I compared the set of writes without
checking which branch each belongs to.

    bit 7 MCLKO2EN   bit 6 MCLKO1EN   bit 5 reserved
    bits 4:3 MCLKO1 source   bit 2 DIVEN   bits 1:0 MCLKO2 source
      source 00 = acg_clk after /M, x1 = mclki after /I, 10 = acg2_clk after /M

    stock 0x074D  write 0x0D  -> both clocks from MCLKI after /I, DIVEN on.
                                 This is inside the MODE 1 branch (MOV 0x08,#1
                                 immediately follows). mboxfw does not
                                 implement mode 1.
    stock 0x052C  clr-bits 0x3F -> clears the source selects and DIVEN, leaving
                                 the output enables: an idle/stop path.
    stock 0x0E10  write 0x06  -> MCLKO1 = acg_clk//M, MCLKO2 = acg2_clk//M,
                                 DIVEN on
    stock 0x07CC / 0x0824  set-bits 0xC0 -> enable both clock outputs

mboxfw does `= 0x06` then `|= 0xC0`, giving **0xC6**, which is exactly what
stock produces on the mode-2/3 path. Telemetry block 6 read ACGCTL = 0xC6 on the
running device, confirming it. ACGCTL is correct and is not a suspect.

### CPTRXCNF3 -- the one real divergence, and the datasheet sharpens it

    CPTCNF3 / CPTRXCNF3 layout (6.5.4.3, 6.5.4.12 -- identical):
      7 DDLY  6 TRSEN  5 CSCLKP  4 CSYNCP  3 CSYNCL  2 BYOR  1 CSCLKD  0 CSYNCD

    0xAC = DDLY 1, TRSEN 0, CSCLKP 1, CSYNCP 0, CSYNCL 1, BYOR 1
    0xA8 = the same with BYOR 0

    stock   CPTCNF3 = 0xAC (transmit)   CPTRXCNF3 = 0xAC (receive)   SYMMETRIC
    mboxfw  CPTCNF3 = 0xAC (transmit)   CPTRXCNF3 = 0xA8 (receive)   ASYMMETRIC

The datasheet's AC'97 walk-through states the semantics directly: with BYOR not
set, "the byte ordering of the data as received is preserved - both from the USB
bus (OUT transactions) and from the external codec (IN transactions)."

And `reference/mbox1_quirks-table.h.snippet` declares stock as
`SNDRV_PCM_FMTBIT_S24_3BE` for **both** endpoints -- 0x02 (OUT) and 0x81 (IN).

So: stock is symmetric in BYOR and Linux observes it symmetric in endianness,
big-endian both directions. mboxfw broke that symmetry on exactly the one
direction that is broken.

**Recommendation, on this evidence: restore CPTRXCNF3 = 0xAC** and, if
little-endian output is wanted, get it by declaring the format honestly rather
than by flipping BYOR on one path only. The `7590af1` reasoning -- "mboxfw
declares S24_3LE so it wants BYOR=0" -- assumed BYOR=1 produces big-endian. The
datasheet says BYOR=0 *preserves* the received order, which for MSB-first I2S is
big-endian, making BYOR=1 the little-endian setting. One of those two readings is
wrong, and stock plus the quirk table both favour the datasheet's.

### P3MSK ruled out for the buttons

Section 6.5.5.1: P3MSK at 0xFFCA, one mask bit per P3 pin, 1 = masked, **default
0x00 = all unmasked**. A byte scan for `MOV DPTR,#0xFFCA` finds **no site in
either stock image**, so neither firmware ever touches it and stock works with it
at its default. It cannot explain mboxfw's dead buttons.

## Addendum 6: Addendum 4/5's CPTRXCNF3 verdict is RETRACTED — 2026-07-30

Addendum 4 named CPTRXCNF3 "the one real divergence" and Addendum 5 recommended
"restore CPTRXCNF3 = 0xAC". Both compared mboxfw against stock's **boot init**.
Stock does not stay there.

Rev 20 `codec_port_cfg3_commit` @0x0FF4 (Rev 22 @0x0FE2) writes one value to
**both** CPTCNF3 (0xFFDE) and CPTRXCNF3 (0xFFD5) and then raises GLOBCTL CPTEN.
Its two call sites are inside `cmd1_apply_clock_mode`, which runs on every
SET_CONFIGURATION. The 0xAC site (@0x034A) is gated on bit 0x0A = IRAM 0x21.2,
which the Keil init table @0x0F9C zeroes and which **no instruction in either
image ever sets** — verified by scanning both binaries for D2/C2/B2/92 on that
bit and for every direct byte write to IRAM 0x21. It is unreachable. The live
site (@0x0355) passes **0xA8**.

So stock's running codec-port config is 0xA8/0xA8, BYOR clear in both
directions, which is exactly what the datasheet's own I2S Mode 5 example
specifies for this geometry. **mboxfw's CPTRXCNF3 = 0xA8 matches stock's
operating state and is not the cause of the capture artifact.** The divergence
is CPTCNF3 = 0xAC, on the playback path, which has never been measured.

The byte-order exclusion that Addendum 3 retracted therefore stays retracted,
but CPTRXCNF3 is no longer the suspect that replaced it. Full derivation, plus
a second divergence this pass turned up (IEPBSIZ1/OEPBSIZ2 = 512 B where stock
uses 640 B), in `FINDING_147_cport_and_ep_buffer_divergences.md`.
