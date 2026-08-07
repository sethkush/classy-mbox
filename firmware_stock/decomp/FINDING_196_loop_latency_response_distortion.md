# The loop measures 3.59 ms, ±0.16 dB and 0.0041% — and the LF distortion figure is an artifact

2026-08-06, build 0x0038, unit A (`RK10874600Q`), self-loop `out2 -> src2`,
source LINE, **both gain dials at minimum** (Seth, and unchanged for the whole
history of this bench).

Report page with the curves is built by `tools/build_report` inputs
`tools/bench_data/loop_*_0038_unitA_mingain.csv`.

## What is being measured

The whole loop — DAC, analog out, TRS cable, line in, ADC. **Nothing here
separates the output stage from the input stage**, and this bench cannot: that
needs an external generator or analyser. Every number is round trip.

The self-loop was used rather than the crossed inter-unit cables because
playback and capture then share one crystal. The two units differ by ~4.4 ppm
(block 11: A −2.46, B −6.83); over a 1 s analysis window that smears a spectrum
into skirts which read as distortion. **Only unit A has a self-loop**, so this
measurement cannot currently be repeated on B.

## Round-trip latency: 3.59 ms, and it is hardware

`tools/rtlat.c`. The measurement problem is that the offset between playback
start and capture start is unknown and is the same size as the thing being
measured. `snd_pcm_link()` removes it: both substreams are on one card and start
on a single trigger, so capture frame 0 *is* playback frame 0, and the latency
is a subtraction of two frame indices rather than an inference.

| period | buffer | round trip, frames | peak/next |
|---|---|---|---|
| 64 | 512 | 172.19 | 6177× |
| 128 | 1024 | 172.21 | 6573× |
| 256 | 2048 | 172.25 | 6164× |
| 512 | 4096 | 172.44 | 5693× |
| 1024 | 8192 | 172.30 | 6203× |

**172.2–172.5 frames = 3.59 ms, flat across a 16× range of buffer sizes.** That
flatness is the evidence that this is converter and USB transport rather than
software buffering — the ALSA buffer sets how far ahead you must write, not the
frame-to-frame relationship.

Recorded and not averaged away: one run at period=512 first read **156.27
frames**, exactly 16 low, and did not reproduce across four repeats. Unexplained.

`alsabat --roundtriplatency` cannot do this measurement here: it starts at a
48-frame buffer, which underruns immediately on a 1 ms full-speed device, and
its detector then reports "too much background noise".

Two traps found while building the tool, both now commented in place:

- `snd_pcm_start` returned **-EBADFD** because the prefill had already started
  the group — ALSA clamps a "never" start threshold to the buffer boundary.
  Harmless *only because the pair is linked*: one trigger still started both.
- Playback running dry while capture continues stops the linked group and
  overruns capture (it did, at frame 95760 of 96000). Playback is now padded.
  An xrun is **fatal** in the tool rather than recovered, because
  `snd_pcm_prepare` resets the shared time origin and every index after it
  measures from a different zero.

## Frequency response: ±0.16 dB, 20 Hz – 20 kHz

−26.23 dB at 20 Hz to −25.92 dB at 20 kHz, drifting gently *upward*. No LF
roll-off and no anti-alias droop at the top, which is mildly surprising at
48 kHz — reconstruction and anti-alias filters both sit near 22 kHz and would
normally dip. A slight analog HF lift appears to offset them.

## Distortion: 0.0041% at 1 kHz, best 0.0016% at 8 kHz

Above ~300 Hz THD falls steadily with frequency. Lowest at −26 dBFS capture;
driving to full-scale playback raises it to 0.0346%, a sevenfold rise in the
last 6 dB — the analog path running out of headroom, not anything digital.

Noise floor is **−107 dBFS/bin**, constant across the entire 60 dB level sweep.

## The low-frequency THD rise is NOT distortion

The THD column climbs to **0.21% at 20 Hz**, scaling almost exactly as 1/f. It
would be easy to write that up as LF distortion from coupling capacitors. It is
not, and three checks say so:

1. **A smooth 1/f skirt centred on DC exists with no signal playing at all**,
   at −79 dBFS RMS (1 Hz −79.4, 2 Hz −83.5, 3 Hz −86.6 …).
2. **For a 20 Hz tone the "harmonic" bins land on that skirt**, to within
   0.3 dB — extrapolating the skirt from 14 Hz predicts 40 Hz at −83.3
   (measured −83.34), 60 Hz at −86.9 (measured −86.65), 80 Hz at −89.4
   (measured −89.37). Those bins are the skirt, not harmonics.
3. It is **not** mains hum — there is no 50 or 60 Hz peak anywhere — and it is
   **not** lost coherence, because with a 1 kHz tone the bins either side of the
   fundamental sit at −130 to −140 dBFS (−104 dBc) and the LF bins are
   *identical to the silence capture*.

So true LF distortion is below the skirt and this method cannot reach it. The
sub-200 Hz figures are an **upper bound**, and the report shades them.

The skirt itself rises ~25 dB when a 20 Hz tone is present but not when a 1 kHz
tone is — that is unexplained and left open rather than guessed at.

## Minimum gain costs 20 dB of converter range

Full-scale playback reaches the ADC at only **−20.2 dBFS**. The top 20 dB of the
converter is unreachable at this dial position — over three bits discarded
before anything digital happens. Not a fault; it is what the dial is for. But it
means every absolute level recorded in `BENCH_WIRING.md` sits 20 dB below where
the converter would like to be, and the loop is perfectly linear over the whole
60 dB swept (every 3 dB in gives 3 dB out).

## What this does not cover

One dial position, one unit, one direction. See `#197`.
