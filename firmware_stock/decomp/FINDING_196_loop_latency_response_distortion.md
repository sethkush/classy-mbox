# The loop measures 3.59 ms, ±0.16 dB and 0.0016% — after the first answer was wrong

2026-08-06, build 0x0038, unit A (`RK10874600Q`), self-loop `out2 -> src2`,
source LINE, **both gain dials at minimum** (Seth, and unchanged for the whole
history of this bench).

Data: `tools/bench_data/loop_*_0038_unitA_mingain.csv`. Tools: `tools/rtlat.c`,
`tools/sweep.py`, `tools/an_lat.py`, `tools/build_report.py`.

## What is being measured

The whole loop — DAC, analog out, TRS cable, line in, ADC. **Nothing here
separates the output stage from the input stage**, and this bench cannot: that
needs an external generator or analyser. Every number is round trip.

The self-loop was used rather than the crossed inter-unit cables because
playback and capture then share one crystal. The two units differ by ~4.4 ppm
(block 11: A −2.46, B −6.83); over a 1 s window that smears a spectrum into
skirts which read as distortion. **Only unit A has a self-loop**, so this
measurement cannot currently be repeated on B.

## The correction, first, because the first answer was published

The first run of this measurement reported **THD rising to 0.21% at 20 Hz** and
explained it as a 1/f skirt centred on DC that was a property of the device. The
skirt was real in the data. The explanation was wrong, and so was every
sub-200 Hz number.

**It was the capture stream's start-up transient, and the analysis window sat
inside it.** Stepping a 1 s window through a 10 s idle capture:

| window | LF 1–15 Hz | total RMS |
|---|---|---|
| 0–1 s | **−25.5 dBFS** | −28.2 |
| 1–2 s | −76.4 | −79.1 |
| 2–3 s | −119.9 | −101.7 |
| 3–9 s | ≈ −118 | −101.7 |

−50.9 dB in the first second is a first-order decay with **τ = 171 ms**, which
is *independently the same time constant* `FINDING_147` measured by a completely
different method (median |x| over 16 slices, τ ≈ 8,200 frames). #147 already
names the mechanism: a DC step settling through the codec's DC-blocking
high-pass when the ADC is enabled, re-armed on every stream start because alt 0
tears the input path down and alt 1 re-enables it. It also notes that real
interfaces mute their first few hundred milliseconds for exactly this reason.

So this was not a new phenomenon. It was a known one, not applied.

Moving the analysis window from 0.5–1.5 s to 4.0–5.0 s:

| | first run | corrected |
|---|---|---|
| THD @ 20 Hz | 0.208% | **0.0032%** |
| THD @ 1 kHz | 0.0041% | **0.0016%** |
| noise floor | −107 dBFS/bin | **−142 dBFS/bin** |
| THD+N @ −26 dBFS | 1.27% | **0.023%** |

The frequency response was never affected — it reads the fundamental bin, which
towers over the transient.

**Rule for this bench: no spectral measurement may analyse the first 3 seconds
of a capture.** `sweep.py` now captures 6 s and analyses from 4 s, and says why
in a docstring.

## Round-trip latency: 3.59 ms, and it is hardware

`snd_pcm_link()` is what makes it exact: the offset between playback start and
capture start is unknown and is the same size as the thing being measured, but
linked substreams start on one trigger, so capture frame 0 *is* playback frame 0
and the latency is a subtraction of two frame indices.

| period | buffer | round trip, frames |
|---|---|---|
| 64 | 512 | 172.19 |
| 128 | 1024 | 172.21 |
| 256 | 2048 | 172.25 |
| 512 | 4096 | 172.44 |
| 1024 | 8192 | 172.30 |

**172.2–172.5 frames = 3.59 ms, flat across a 16× range of buffer sizes.** That
flatness is the evidence it is converter and USB transport rather than software
buffering. One run at period=512 first read **156.27 frames**, exactly 16 low,
and did not reproduce across four repeats — recorded, unexplained, not averaged.

`alsabat --roundtriplatency` cannot do this here: it starts at a 48-frame
buffer, underruns immediately on a 1 ms full-speed device, and then reports
"too much background noise".

Two traps, both commented in `rtlat.c`:

- `snd_pcm_start` returned **-EBADFD** because the prefill had already started
  the group — ALSA clamps a "never" start threshold to the buffer boundary.
  Harmless *only because the pair is linked*: one trigger still started both.
- Playback running dry stops the linked group and overruns capture (it did, at
  frame 95760 of 96000). Playback is now padded, and an xrun is **fatal** rather
  than recovered, because `snd_pcm_prepare` resets the shared time origin.

## Frequency response: ±0.16 dB, 20 Hz – 20 kHz

−26.24 dB at 20 Hz to −25.92 dB at 20 kHz, drifting gently *upward*. No LF
roll-off and no anti-alias droop, which is mildly surprising at 48 kHz — a
reconstruction filter and an input anti-alias filter both sit near 22 kHz and
would normally dip. A slight analog HF lift appears to offset them.

## Distortion: flat at 0.0012–0.0032%

Across the whole 20 Hz – 10 kHz range, with no structure worth naming.
0.0016% at 1 kHz, 0.0012% at 25 Hz, 0.0032% at 20 Hz as the only mild outlier.
Above 12.5 kHz no harmonic fits below Nyquist and THD is undefined.

Against level at 1 kHz, THD sits at 0.0022–0.0043% from −30 to −23 dBFS capture.
Full-scale playback drives it to **0.0349%** — a fifteenfold rise in the last
3 dB, the analog path running out of headroom. That was the one figure the
transient error did not touch.

Noise floor **−142 dBFS/bin**; integrated across 20 kHz that is ≈ −99 dBFS,
which agrees with the −101.7 dBFS idle RMS measured directly. Best THD+N is
**0.0168% (−75.5 dB)** at −23.2 dBFS.

## Minimum gain costs 20 dB of converter range

Full-scale playback reaches the ADC at only **−20.2 dBFS**. The top 20 dB of the
converter is unreachable at this dial position. Not a fault — it is what the
dial is for — but it means every absolute level in `BENCH_WIRING.md` sits 20 dB
below where the converter would like to be. The loop is perfectly linear over
the full 60 dB swept (every 3 dB in gives 3 dB out).

## What this does not cover

One dial position, one unit, one direction — see `#196`. And the start-up
transient is a candidate defect in its own right: #147 observed that real
interfaces mute their first few hundred ms, and mboxfw does not. See `#197`.
