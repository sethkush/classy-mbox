# #216 — the LF-noise regression does not reproduce, and the numbers that started it were never measurable

2026-08-16. Closing an investigation by failing to reproduce it, and recording
why the original figures could not have been compared against anything.

## What was chased

An LF-noise regression: roughly **-71 dBFS below 200 Hz against -97 up high**,
called 26-31 dB above baseline. Two variables had changed together — firmware
0x0053 to 0x0054, and the units moving behind the Genesys hub — and the session
went looking for which.

On 2026-08-15 an ad-hoc script found a further wrinkle: a ~27 dB spread
**between channels**, with the two hot channels being exactly the two fed by
unit A's outputs. That looked like a clean single-variable result and was
written up as a strong inference that A's output stage was the source.

## It does not reproduce

`tools/noisefloor.py`, a committed instrument with a stated normalisation and a
built-in floor reference:

```
channel                       20-200Hz    200Hz-2k      2k-20k
ANALYSIS FLOOR (24-bit LSB)     -196.9      -197.1      -197.1

RK1672500M ch1                  -144.1      -144.7      -151.2
RK1672500M ch2                  -145.9      -147.3      -152.7
RK10874600Q ch1                 -148.2      -152.5      -153.9
RK10874600Q ch2                 -146.8      -151.5      -153.8
```

Every channel is 45-50 dB above the quantisation floor, so every row is
measuring hardware rather than arithmetic. The **spread between channels is
under 8 dB in the worst band and under 3 dB up high**. The two channels that
read 22-32 dB hot yesterday now sit with the others.

The LF-over-HF tilt is real and is present on *every* channel at 5-8 dB. That is
a property of the analog path, not a per-channel defect, and it is almost
certainly what "sub-200 Hz only" was describing.

## Why the original numbers could not have settled anything

Neither the -71/-97 figures nor yesterday's per-channel spread came from a
committed tool. Nothing recorded how either was normalised, and for a "dBFS"
noise figure the choices are not cosmetic:

* band **sum** versus per-bin **mean** — a sum grows with bandwidth, so a
  20-200 Hz band and a 2-20 kHz band are not comparable under it at all
* window power correction applied or not
* one-sided versus two-sided spectrum

Each is worth 10 dB or more. The effect being chased was 26. Yesterday's script
was itself edited from `sum` to `mean` *mid-session*, which is why its two sets
of numbers disagree by ~28 dB and neither can be compared to the day before.

**A measurement whose normalisation is not written down is not a baseline.**
That is the whole content of this finding, and it is the reason
`tools/noisefloor.py` now exists and prints its floor reference above the data.

## What is NOT concluded

Not "the noise was never there". A transient cause — a mis-timed ADC offset
calibration, which #197 and #201 both established can put a decaying error on a
capture — would produce exactly this: dramatic on one session, absent on the
next, with no code change in between. The units have been cold-booted several
times since.

What is concluded is narrower and firmer: **there is no reproducible
channel-to-channel noise difference today**, the inference that unit A's output
stage was the source is **withdrawn** as unsupported, and any future claim on
this subject has to come through the committed tool or it cannot be compared to
this table.
