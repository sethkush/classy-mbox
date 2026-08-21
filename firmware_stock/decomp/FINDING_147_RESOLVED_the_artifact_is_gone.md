# #147 RESOLVED: the 8-frame capture artifact does not exist on 0x0061

2026-08-21, void box 192.168.1.76, both units on build 0x0061 (`bcdDevice
0161`), release tier. Unit A = card0 = serial `RK10874600Q` (capture, under
test). Unit B = card1 = serial `RK1672500M` (generator). One cable: **B line
out 1 -> A line source 1.** No flash, no DFU, no power cycle.

`FINDING_147_the_capture_stream_is_noise.md` ended with a falsifiable
prediction:

> with those fixed, capture RMS drops from -3.5 dBFS to somewhere near the
> ADC's real noise floor. If it does and the 3-in-8 pattern goes with it, #147
> was a symptom of the dead path and is closed.

#166, #167 and #168 shipped. Nobody ran the test. It has now been run.

## The measurement

| | 2026-07-31, on record | 2026-08-21, build 0x0061 |
|---|---|---|
| capture RMS | -3.5 dBFS | **-71.6 dBFS** |
| rails, \|x\| > 0.98 FS | 37.5% (3 in 8) | **0.000%** |
| top rail period | P=16, chi2/df ~ 5000 | no rail samples to fold |
| holds at both rates | yes | yes |

Both quiet arms, 240,000 frames each, starting 1.0 s in to clear the #197
settling transient (tau = 171 ms):

    48000  RMS -71.71 dBFS   rails 0 (0.000%)   top envelope F  8.12 (P=2)
    44100  RMS -71.57 dBFS   rails 0 (0.000%)   top envelope F  1.55 (P=22)

The prediction is confirmed in both of its parts. **#147 is closed.** It was a
symptom of the dead analog path, exactly as that finding argued, and not a
digital framing defect. The 8 was never an audio-path ratio.

## The known-answer arm, at both rates

Per CLAUDE.md, a null from an instrument that was never connected looks exactly
like a null from a refuted hypothesis, so the run carries an arm whose answer
was known in advance. With B generating 1 kHz at 0.5 FS:

    48000  ch0 (cabled)  RMS -54.31 dBFS   1 kHz bin -51.39 dBFS
           ch1 (unfed)   RMS -85.89 dBFS   1 kHz bin -119.4 dBFS
    44100  ch0 (cabled)  RMS -54.48 dBFS   1 kHz bin -51.90 dBFS

**68 dB between the fed and the unfed channel**, against the ~66 dB
`BENCH_WIRING.md` quotes. The tone arrives on A's source 1 and nowhere else, so
the input path was live and correctly routed during the quiet arms too --
identical configuration, only B's output differs, and RMS moves 17 dB between
them. That pairing is the control, and it is why the quiet-arm null means
something.

The `mic` arm was not needed and was not run: A was already on LINE, which the
tone arm proves directly rather than by assuming a button press landed.

## What the two statistics are for

`tools/analyse_147.py` carries two, chosen to fail differently.

**Rail chi-square** folds only samples past 0.98 FS onto each candidate period.
It is specific to this artifact's own signature -- 6 contiguous slots in every
16 pinned to a static level -- and a 0.5 FS tone cannot reach the threshold, so
the signal can never fake the defect.

**Envelope F** is a one-way ANOVA on |x| folded by phase, so it flags any
periodic amplitude structure. It is deliberately fooled by the tone: at 48 kHz
a 1 kHz sine gives F ~ 95,000 at P=24, the HALF period, because |sin| folds at
half the period of sin. Read on the silent arm; on the tone arm it is only
further evidence the tone arrived.

The analyser was verified in **both** directions before it was pointed at
hardware, because an analyser that cannot see the artifact and an artifact that
is gone produce the same output. Fed a synthetic 6-of-16 rail pattern it
reports -3.69 dBFS and 37.5% rails -- against the -3.5 dBFS and 3-in-8 on
record -- and picks P=16 top with the exact histogram. Fed clean audio it
reports zero rails and a Goertzel bin accurate to 0.02 dB.

## One void arm, and how it was caught

The first 44.1 kHz tone arm was VOID. `run_147.sh` named the generator's source
file and the tone arm's capture with the same stem, so they were the same path;
the next arm's `mktone` overwrote the capture with the pristine 0.5 FS source.
The analyser then read the generator instead of the device and reported -9.03
dBFS -- which is exactly a 0.5 FS sine, and about 45 dB hotter than the real
analog round trip.

It was caught because the number was too GOOD. A collision that produced a null
would have passed as a result. The fix separates the names (`147_cap_*` vs
`147_gen_*`) and the reason is commented at the assignment, since the two names
looked obviously distinct until `ARM=tone` made them identical.

Nothing else in the run depended on it: `147_quiet_*` never collided, and the
48 kHz tone arm was analysed before the arm that would have clobbered it. All
four arms were re-run clean afterwards and the numbers reproduced to 0.2 dB.

## What this does NOT claim

**The noise floor is not the ADC's.** -71.6 dBFS on ch0 is well above the
-100 dBFS (median 38-79 counts) recorded on 2026-08-03. The unfed channel sits
lower, at -82 to -86 dBFS, and the difference tracks which channel has a cable
on it, so the likeliest reading is noise injected by B's output stage on an
idle line rather than anything in A. Not measured, not claimed, and not what
#147 asked. If a floor number is ever wanted for its own sake it needs
terminated inputs and its own run.

**P=2 at F=8.12 on the 48 kHz quiet arm is not a finding.** With N=240,000 a
tiny effect gives a large F, the magnitude sits at the noise floor, and it does
not reproduce at 44.1 kHz. Recorded so it is not re-derived as a discovery.

**Playback-side attribution was not tested.** The single cable measures A's
capture with A's playback out of the circuit, which is what #147 needed. Asking
whether a defect follows the capturing or the generating unit needs the same
cable moved to `A out1 -> B src1`, and there is now no defect to attribute.

## Consequences

* **#147 closed.** The highest-value open item in
  `WHAT_REMAINS_UNKNOWN.md` is resolved, and resolved as "already fixed by
  #166/#167/#168", not as new work.
* **#162 and #163 are no longer suspects.** The endpoint buffer size (512 B vs
  stock's 640 B) and the per-stream re-base were the two divergences on the
  table. Neither predicted an 8-frame period, and there is no longer a period
  to predict. They remain divergences; they are not defects.
* **The diagnostic build that would have chased this is not needed.** That was
  a flash and a power cycle per variant, one image per trip.
