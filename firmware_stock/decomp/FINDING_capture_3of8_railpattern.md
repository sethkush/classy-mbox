# The capture artifact measured: a fixed 18-byte pattern, exactly 3/8 of samples, rate-independent

Measured on hardware 2026-07-29 on the void box (192.168.1.76), against the
image actually flashed -- telemetry block 0 reports **build 0x000B**, which
predates every fix made this session. Read-only: `arecord` plus telemetry, no
EEPROM write, no flash.

## Two prior notes are now obsolete

**Capture is NOT returning zero-length packets.** The project memory note
"our capture EP delivers 0 bytes/frame; stock Rev 18 delivers 288" does not hold
for this build. `arecord -f S24_3LE -c 2 -r 48000 -d 4` produced 1,152,044 bytes
-- exactly 4 s x 48000 x 2 ch x 3 B plus a 44-byte header -- with no `-EIO`, and
918,681 of the 1,152,000 payload bytes are non-zero.

**The capture DMA does run.** Sampled MID-STREAM rather than after teardown,
which is what earlier reads got wrong:

    DMACTL1 = 0x89   DMAEN=1, EPDIR=IN, EPNUM=1     (capture DMA running)
    IEPCNF1 = 0xC5   (the same value stock Rev 20 writes)
    last SET_INTERFACE: iface 2 alt 1

After `arecord` exits, both read back torn down (`DMACTL1=0x09`, `IEPCNF1=0x00`),
which is correct behaviour and is what made this look dead before.

`vec_iep1` stays 0 throughout. That is not a fault: the DMA shuttles the data
without per-transaction MCU involvement, which is the design.

## The artifact, exactly

Every 16 samples (48 bytes), samples at positions 8..13 are not audio. They are
a **fixed 18-byte constant**, byte-identical in all 24,000 occurrences:

    00 00 80  00 00 80  ff ff 7f  ff ff 7f  00 00 80  00 00 80
      -FS       -FS       +FS       +FS       -FS       -FS

In stereo-frame terms: of every 8 stereo frames, 3 are corrupt and 5 carry
audio -- which is the "5 data + 3 rail frames" shape recorded for #147, now
pinned down.

Facts worth keeping:

  * **Exactly 37.50% of samples, at BOTH 48 kHz and 44.1 kHz.** The ratio is
    3/8 to the digit at both rates. Only the phase differs (positions 8..13
    mod 16 at 48 kHz; spread across 4..15 at 44.1 kHz, i.e. not 16-aligned).
  * **Both channels equally**: 72,000 rail samples each, phase-locked.
  * **Six repeats per USB frame** at 48 kHz (96 samples/frame / 16), identical
    in every one of the 4000 frames captured.
  * The surviving audio (positions 0-7, 14-15) is continuous and plausible,
    not clipped: e.g. 2772782, -2706029, 2121805, 177505, 519251, ...
  * Zero samples: none. So this is not dropout-to-silence.

## What that rules out

**Not a USB or DMA boundary effect.** A fault at a packet or DMA-buffer edge
would show one disturbance per 96 samples. This shows six, evenly spaced,
inside every frame. The corruption is in the serial stream before the DMA.

**Not a sample-rate or clock-frequency error.** A wrong clock divider would
change the valid/invalid ratio with rate. 3/8 holds identically at 48 kHz and
44.1 kHz, so whatever is wrong is a fixed structural ratio in the codec-port
framing, not a frequency. This is worth stating because `CPTRXCNF4`'s DIVB2
divider was the last thing found wrong in this area (0x01 vs 0x03, which DID
scale with rate -- it doubled the frame rate). This is a different defect.

**Not random.** The invalid bytes are one constant, never garbage, never
varying. Something deterministic supplies them -- a C-port receive register read
when no new word has arrived, or the codec driving a fixed pattern in slots we
are clocking but it is not filling.

## Where that points, and what has NOT been established

The remaining suspects are the codec-port receive framing registers --
`CPTCNF1..4`, `CPTSTA`, `CPTRXCNF2..4` -- and specifically a slot or word-length
mismatch rather than a clock rate. 5 valid : 3 invalid out of 8 is rate-
independent, so the port is being clocked for more slots per frame than the
codec fills, in a fixed 8:5 proportion.

**No root cause is claimed here.** What is established is the shape, the
determinism, the rate-independence, and the two layers it is NOT in.

One strong reason to re-measure before theorising further: **the flashed build
0x000B has every CS8427 inter-register settling delay deleted by the compiler**
(`FINDING_delay_calls_elided.md` -- `inter_reg_delay` lost all nine call sites).
The CS8427 register sequence in this image therefore ran with zero gap between
writes, so the external chip's configuration is itself suspect. Build 0x0010
restores those delays. Re-running this measurement on 0x0010 is the next step
and may change the picture before any register-level theory is worth building.

## Method note

Sampling telemetry MID-STREAM rather than after `arecord` returns is what made
the DMA state visible. Any future isoc measurement should do the same; a read
after teardown reports the torn-down state and reads as "the DMA never ran".
