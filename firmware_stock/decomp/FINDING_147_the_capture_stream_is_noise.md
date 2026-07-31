# #147 is misnamed: it is not "5 data + 3 rail frames", it is 8 frames of noise

2026-07-31, working the 3-in-8 duty as a numbers problem. No new hardware run —
this is arithmetic on measurements already in
`FINDING_capture_8frame_artifact.md` that nobody had interrogated.
`tools/analyse_capture_energy.py` reproduces it.

## The number nobody looked at

The four capture runs reported, alongside the tone bins:

    rate    stream     ch1 rms   ch2 rms   @1000Hz   @1500Hz
    44100   baseline   -3.6      -3.6      -91.8     -95.0
    44100   loopback   -3.5      -3.5      -94.7     -88.6
    48000   baseline   -3.5      -3.5      -92.3     -95.9
    48000   loopback   -3.5      -3.5      -93.8     -87.4

Every previous pass read the two right-hand columns — the tone bins at the
numerical floor — and concluded "no audio". Nobody read the **RMS**.

**-3.5 dBFS.** A working ADC on a quiet input reads -80 to -95 dBFS. This is
about **90 dB hotter than silence**, identical at both sample rates, and
identical whether or not anything was playing.

Broadband energy at half scale with every narrow bin at the floor is the
signature of **white noise**, not of a signal.

## The energy budget closes without any audio in it

Model the stream as 3/8 of samples pinned to ±full scale (the measured rail
pattern) and the remaining 5/8 as uniform noise of half-amplitude *a*:

    E[x²] = (5/8)·(a²/3) + (3/8)·1

    a = 1.00   ->  -2.34 dBFS
    a = 0.75   ->  -3.08 dBFS
    a = 0.50   ->  -3.69 dBFS
    a = 0.25   ->  -4.11 dBFS
    rails only ->  -4.26 dBFS      <- floor with the other 5/8 at digital silence

Two things fall out.

**The rails alone put the floor at -4.26 dBFS.** The measurement is -3.5 dBFS.
So there is only ~0.8 dB of headroom between "everything except the rails is
silence" and what was measured — and closing it takes the other 5/8 sitting at
roughly half scale. **There is no room in the energy budget for this stream to
be mostly silence with an artifact on top.**

**The quoted "surviving audio" is not audio.** The values recorded as
"continuous and plausible" — 2772782, -2706029, 2121805, 177505, 519251 — are
0.33, -0.32, 0.25, 0.02, 0.06 of full scale. Those are large. A capture of a
quiet or disconnected input does not produce ±0.3 FS excursions.

## What that changes

The task title, and every framing since, has been "**5 data + 3 rail frames**".
That is wrong. It is **8 frames of noise, 3 of which saturate**. The 5/8 were
never carrying audio, so "what corrupts 3 frames in 8" was the wrong question —
it presupposes the other 5 were right.

Three already-measured facts corroborate this and had been read as separate
puzzles:

  * Capture output does not depend on whether anything is playing. Not "the
    loopback failed" — there is no input path at all.
  * "De-interleaving to drop the 3 pinned frames per 8 does not reveal a tone
    either — the 5-of-8 stream's top bin is the same 5500 Hz at -52 dB." A flat
    spectrum with the artifact as its only feature is what noise looks like.
  * "Zero samples: none." A silent ADC produces many near-zero samples. Noise at
    half scale produces almost none.

And the bit-level result from `FINDING_147_cport_and_ep_buffer_divergences.md`
Part 2 — that the rail words are a line held at a **static logic level** for the
whole 24-bit window with a one-clock framing offset — is exactly what an
**undriven** CDATI pin looks like when the sampled level happens to hold. The
noise between the rails is the same pin when it does not.

## Why the 8 has resisted eight eliminations

Every mechanism killed so far was digital: codec frame length, master/slave,
stale buffer bytes, secondary communication, a slipping sample window, byte
order, slot geometry, the DMA slot mask, the ACG divider chain. They were all
eliminated because none of them produces 5-good-of-8 — and if there is no
"good", none of them was ever the right shape of answer.

Note honestly what this does **not** dissolve. The rail positions are rigidly
periodic — 6 contiguous slots in every 16, with 4 phase slips in 220,500 frames
— and rigid periodicity is a digital property. Pure crosstalk from LRCK2 or
SCLK2 would repeat every 1 or 2 slots, not every 16. So an integer 8 does exist
somewhere. What this finding establishes is that it is **not an audio-path
ratio**, because there is no audio in the path, and that it will not be found by
diffing register values that are already byte-identical to stock.

Checked and rejected as the source of the 8: the ACG fractional accumulator.
Its period is 2^24/gcd(word, 2^24) — 524,288 cycles for the 44.1 kHz word
(0x6A4B20) and 2^24 for the 48 kHz word (0x61A80F, odd). Different at the two
rates, where the observed period is 8 at both.

## What to do instead

Stop hunting the 8 until something actually drives CDATI. There are now three
concrete, unshipped reasons nothing does:

  * **#166** — mboxfw never sets IRAM 0x23.4, so the external-chip RESET is held
    asserted for the life of the firmware.
  * **#167** — `cs8427_boot_init()` runs before the codec word carrying that
    reset and the chip select has ever been published.
  * **#168** — mboxfw never writes GLOBCTL, so CPTEN, the codec port enable, is
    never set.

Any one of those is sufficient to produce a dead input path. All three ship in
one image with #165's readback.

**The falsifiable prediction:** with those fixed, capture RMS drops from -3.5
dBFS to somewhere near the ADC's real noise floor. If it does and the 3-in-8
pattern goes with it, #147 was a symptom of the dead path and is closed. If real
audio appears and the 3-in-8 survives on top of it, then it is a genuine digital
artifact and worth hunting with an actual integer in hand — and the measurement
will be worth far more than it is now, because it will be made on a path that
carries signal.
