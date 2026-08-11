# The 171 ms decay is the AK5383's own digital high-pass, and HPFE is HIGH

2026-08-10. Closes the one loose end left by `FINDING_197`: the transient's
decay had no confirmed source after the datasheet forced a retraction.

## What was left open, and why

`FINDING_197` originally explained the transient's tau = 171 ms as the AK5383's
1 Hz digital high-pass, on the strength of 1 Hz implying tau = 159 ms. Reading
the datasheet showed calibration and the HPF are **independent blocks** -- the
calibration reference is VCOM or the AIN pins per ZCAL, no filter involved -- so
the tau match was **not** evidence for the calibration mechanism, and it was
retracted as such. That retraction was correct: it was a non-sequitur.

But the retraction was then over-extended into a claim that **HPFE is tied
low**, on the grounds that post-calibration DC measured 5-24 LSB24 against a
+/-1 LSB24 spec for HPF=ON. **That claim was wrong** and is retracted here. A
1 Hz high-pass attenuates sub-1-Hz content barely at all, so a bench source with
slow drift leaves exactly that kind of residual; the +/-1 LSB figure is the
converter's own offset with a clean input. The measurement never bore on the pin.

## The experiment

`tools/muxavg.py`. `TLM_REQ_SET_MUX` (0x13) switches the 74HC157 source muxes and
**never touches 0x23.2**, so it injects a DC step at the analog input with no
calibration cycle to confound it. 112 steps per rate, alternating LINE/MIC,
4 s apart so each fully settles, sign-corrected and averaged, 2 ms bins.

The discriminator is the one that identified RST: the AK5383's HPF corner is
specified as *"1Hz at fs=48kHz and also scales with sampling rate(fs)"*, so a
pole inside the part stretches by 48000/44100 = **1.0884** at 44.1 kHz, while
anything analog outside it holds constant in milliseconds.

## Result: it scales with fs

| fs | zero-crossing | overshoot peak | overshoot |
|---|---|---|---|
| 48000 | **176.44 ms** | 377.64 ms | 16.26 % |
| 44100 | **191.53 ms** | 400.24 ms | 17.36 % |

Zero-crossing ratio **1.0855** against the sample-rate ratio **1.0884** -- a
0.3 % match. The response is clocked at fs. **It is inside the converter, and
HPFE is HIGH.**

The zero-crossing is the landmark to trust: the curve passes through it steeply,
where the overshoot peak is a flat maximum and fits ~2.6 % low with much worse
conditioning. Tail-slope fits were abandoned outright -- channel-to-channel
scatter of 294 vs 309 ms and 186 vs 235 ms shows noise dominating out there.

## Shape: two poles, not one

A single high-pass decays monotonically to zero. This one **crosses zero at
176 ms and overshoots to 16.3 %**, peaking near 2x the crossing time. That is a
repeated-pole response, `(a + bt)e^{-t/tau}`, for which the zero crossing is
exactly `tau`. Two-exponential fits confirm it by degenerating: they converge on
near-equal time constants with large cancelling amplitudes, which is what a
least-squares fit does when the truth is a double pole.

So tau ~ 176 ms per pole at 48 kHz, a corner of 0.90 Hz against the datasheet's
nominal 1 Hz. Good agreement.

**Open:** why two poles when the block diagram draws one HPF per channel. Both
scale with fs, so both are digital and both are in the part -- either the HPF is
second-order, or something else fs-clocked follows it. Not resolved.

Note the trap avoided: `Table 4. Frequency Response of HPF` (-1.56 dB at 1 Hz)
is **not** the converter's filter. It belongs to Figure 8, the evaluation
board's input buffer, which the text gives as a 1st-order HPF at fc = 0.66 Hz.
Reading Table 4 as the AK5383's own response gives a corner of 0.657 Hz and a
tau of 242 ms, which is wrong by 35 %.

## So the original attribution was right after all

tau = 176 ms measured here against tau = 171 ms measured in `FINDING_197` by an
entirely different route. Same network. **The transient's decay is the AK5383's
digital high-pass.**

What stays retracted is the *inference* -- that the tau match supported the
calibration mechanism. It never did. What is now established, by measurement
rather than by assertion, is the attribution itself.

## The mechanism, and what remains unproven

`tools/clkstep.py` fires `streaming_set_rate()` mid-stream on unit A
(`RK10874600Q`, 0x004A) via `TLM_REQ_SET_CLOCK`:

| event | zero run | what follows |
|---|---|---|
| stream start | 8837 frames = 184.10 ms | flat |
| SET_CLOCK at t=2 s | 9033 frames = 188.19 ms | **flat**, DC within +/-58 LSB24 |
| SET_CLOCK at t=5 s | 9032 frames = 188.17 ms | **flat**, DC within +/-25 LSB24 |

Two things fall out.

**tRTV = 8960 is corroborated from a second geometry.** The mid-stream runs are
*longer* than the stream-start run because there is no URB head loss: the run
there spans the clock reprogramming plus the full 8960. 9033 - 8960 = 73 frames
= 1.5 ms of reprogramming, and 8960 - 8837 = 123 frames = 2.6 ms of head loss at
stream start, both plausible and mutually consistent.

**Reprogramming the clocks produces no transient once the part is calibrated.**
That is the prediction of the proposed mechanism: reprogramming disturbs the
converter's digital-filter state, so the high-pass re-converges from scratch and
reveals whatever DC sits under it for ~2 tau. With calibration the underlying
offset is ~0 and there is nothing to reveal; without it, the un-calibrated
offset appears and decays with exactly this tau, re-armed on every stream open.

That accounts for every observation in `FINDING_197` -- the amplitude tracking
the offset, the shape, the re-arming, and why the fix removes it. **It is not
proved.** Proving it needs a build with the `CLR` removed again, and a flash,
and the fix is not in doubt, so it has not been done. Recorded as the best
available account and labelled as such.


## Digging further: the converter is settling during the tRTV window

`tools/calwindow_sweep.py`. If the high-pass state is cleared when RST goes low
-- the datasheet says the digital section is powered down -- then it must
re-converge somewhere, and the only place it can hide is the 187 ms of zeros
before SDATA goes valid. That is testable: inject a mux step at a known delay
INTO the window, and see how much of it survives to the output.

48 kHz, 4 repeats per delay, both channels, all steps MIC->LINE. The comparator
is the reverse step made with audio live, in the same capture.

| switch at | residual |peak| | suppression | HPF-alone prediction |
|---|---|---|---|
| +10 ms | 999 LSB24 | 4.3x | 18 (242x) |
| +50 ms | 1218 | 3.5x | 424 (10.1x) |
| +90 ms | 1550 | 2.8x | 1092 (3.9x) |
| +130 ms | 1874 | 2.3x | 2074 (2.1x) |
| +170 ms | 2384 | 1.8x | 3486 (1.2x) |
| **+230 ms** | **6293** | 0.7x | 4302 (1.0x) |
| **+400 ms** | **6223** | 0.7x | 4302 (1.0x) |

The last two rows land after SDATA is valid and are the control: they come back
full size, as they must.

**The trend is real and monotonic.** A step introduced early in the window
survives four times smaller than the same step introduced late, so the
converter's digital path is running and settling while its output is still
muted. The 187 ms is 1.06 tau, and it is a settling window as well as a
calibration cycle.

**But the early delays leave a floor the high-pass alone does not explain.** At
+10 ms a clean model -- state cleared at RST release, double pole, tau = 176 ms,
1.01 tau of convergence available -- predicts 18 LSB24 and 999 was measured.
Something keeps ~23 % of the step alive no matter how early it is introduced.

Two candidates, and this bench cannot separate them:

- the mux step is not a step. Switching source changes the DC the analog path
  must charge to, and that path has its own settling, so the input is still
  moving when SDATA goes valid.
- the model of what is cleared at RST-low is wrong.

So the direction is established and the magnitude is not. **ZCAL is not
determined by any of this** -- the reasoning that briefly suggested it
(suppression larger than the high-pass could explain, therefore the calibration
must be cancelling input DC) rested on the retracted number below.

## RETRACTED: the "45x suppression" figure

An earlier run of this experiment reported a mux step made during the window
emerging **45x** smaller than one made live, and inferred from the excess over
the high-pass prediction that the calibration must also be cancelling input DC,
hence ZCAL = "H". **All of that is wrong.**

The driver chose the target source from the loop index rather than from the
current source, so after the first iteration the "test" switch was usually a
no-op -- setting MIC when the mux was already MIC. The average was one real step
diluted by eleven flat traces. The true figure at that delay is ~3.5x.

It was caught by the control arm: the +230 ms case lands after SDATA is valid
and MUST look like an ordinary live step. It did not, and there was no reading
of the physics under which it could. **The lesson is the cheap one: the sweep
only became trustworthy once it included a delay whose answer was known in
advance.** The first version had no such arm, and its headline number was off by
an order of magnitude in the flattering direction.

## What is still not proved, and what it would take

That reprogramming the clocks is what disturbs the converter and re-arms the
transient. Everything measured is consistent with it and nothing tests it,
because neither available build offers the needed arm: 0x004A always brackets
the reprogramming with RST, and 0x004B (unit B) gates the reprogramming and the
bracket together, so a same-rate request does nothing at all.

It needs an image that reprograms the clocks WITHOUT touching RST -- cleanest as
a diagnostic vendor request rather than by re-introducing the regression. That
is a flash, and a flash is a physical trip. The fix is not in doubt, so this is
recorded as the open question rather than pursued.

## #199: the proposed mechanism is REFUTED (2026-08-10)

Both units flashed to 0x004D, which adds `TLM_REQ_SET_CLOCK` with
`wIndexH == 0xD1`: reprogram the clocks with the AK5383's RST left HIGH. That is
the arm neither shipping build could supply.

The claim under test: reprogramming disturbs the converter's digital-filter
state, so its high-pass re-converges from scratch and reveals whatever DC sits
under it. If true, a high-pass that is actively holding a correction must LOSE it
when the clocks are reprogrammed.

`tools/diag199.py`. Flip the source, wait 2.5 s so the high-pass is holding a
correction, then fire the diagnostic. Controls in the same capture: the same
diagnostic fired 6 s after the last flip, with nothing held (the artefact of
doing it at all), and a plain mux flip for the amplitude scale.

| arm | peak | vs control |
|---|---|---|
| LIVE (plain mux step) | **+3327 ± 175 LSB24** | reference |
| CHG (rate CHANGED, correction held) | +7 ± 66 | **−56 ± 88, 0.6σ, 1.7 % of LIVE** |
| SAME (rate unchanged, correction held) | +44 ± 47 | **−20 ± 82, 0.2σ, 0.6 % of LIVE** |
| CTRL (rate changed, nothing held) | +63 ± 70 | — |

**Nothing. The held correction survives.** Reprogramming the clocks does not
clear or meaningfully disturb the high-pass state, at either the same or a
different frequency, with an upper bound around 5 % of the held correction. If
the mechanism were right the CHG arm would read something approaching +3327.

### Why the rate had to be changed, and why the first run was void

The first run reprogrammed 48000 -> 48000 and returned null. That null meant
nothing, because of this:

> As the AK5383 includes the phase detect circuit for LRCK, the AK5383 is reset
> automatically when the synchronization is out of phase by **changing the clock
> frequencies**. Therefore, the reset is only needed for power-up.

A same-rate reprogramming lands on the same frequency, so the phase detector need
never trip. The arm had to be re-run with the frequency genuinely changing, and
both arms are reported above.

### Two of my own errors, and the arm that caught them

The first corrected attempt applied an **alternating sign to a non-alternating
sequence**. Four flips per iteration returns the mux to its starting source, so
the physical direction at each slot is identical every time; the alternating sign
averaged the signal to zero. Its LIVE arm came out at −56 ± 769 LSB24 -- no
reference signal at all -- which is how it was caught. The same run also had a
"control" with a mux flip only 2.5 s before it, so it was not a control.

Both are the same failure as the retracted 45x figure earlier in this document:
an arm whose answer is known in advance is what makes the rest legible. **The
rule this bench keeps re-learning: if the reference arm shows no signal, nothing
else in the run can be believed, and the run is void rather than interesting.**

The final version applies no sign correction at all -- the raw mean is already
meaningful -- which removes the entire class of error rather than getting it
right once.

### What this does and does not settle

Settled: reprogramming the clocks **mid-stream** does not disturb the converter's
high-pass state. The #197 account in this document and in `streaming.c` is wrong
about the mechanism, and is corrected accordingly.

NOT settled, and an honest limit on the refutation: the transient appeared at
stream **start**, where the ACG additionally comes up from idle -- MCLKO stops
while the generator is idle, which is why `streaming_set_rate()` has to re-prime
`acg_prev`. The diagnostic fires mid-stream with the generator already running,
so it does not reproduce a clock that has been stopped and restarted. That
remains the live candidate.

**The fix is untouched by any of this.** It is proved by measurement -- transient
gone, floors at −99 to −104 dBFS, DC at zero -- and by stock doing the same
thing. What is wrong is the story about WHY, which is worth more corrected than
plausible.

## Two more candidates eliminated, and a premise found false (2026-08-10)

### The clock never stops, so nothing at stream start can be a clock stop

The refutation above left one candidate: at stream start the ACG comes up from
idle, and `streaming_set_rate()`'s own comment said "MCLKO stops while the
generator is idle". That would be a stopped-and-restarted clock with RST high,
exactly the condition the datasheet warns about.

Two measurements, both free, both using block 11 -- which counts MCLKO against
the USB frame clock and needs no stream open:

| condition | window total | per frame |
|---|---|---|
| streaming at 48 kHz | 12582882 | 12287.9707 |
| clock mode 1, no S/PDIF present | 12582822 .. 12582873 | ~12287.90 |
| **NO STREAM OPEN AT ALL** | 12582881 .. 12582883 | **12287.97** |

**MCLKO does not stop between streams, and mode 1 does not stop it either.** The
mode-1 figures differ from the streaming ones by 4.8 ppm, which is nothing.

So the comment in `streaming.c` was false and is corrected. And the candidate
dies at the premise rather than by experiment: there is no clock stop at stream
start to disturb the converter, because the clock never stops.

This also disposes of an inference `streaming.c` had flagged as unverified --
"if it is wrong, mode 1 leaves the codec with no clock at all". It does not.
Mode 1 keeps MCLKO running at essentially 48 kHz x 256. (Something else does
change: the noise floor drops 7.4 dB, 96.4 -> 41.4 LSB RMS, while the stream
keeps flowing with unchanged repeat rates. Unexplained, and not pursued.)

A clock-stop experiment WAS run before these measurements and returned null
(STOP minus CTRL +15 +/- 65 LSB24, 0.4 % of a 3904 LSB24 reference). That null is
void, not evidence: the instrument never stopped the clock. It is recorded only
so nobody re-derives it as a result.

### The ADC was calibrated ONCE per power-up, not never

`g_path_enabled` is written in exactly two places: set in `streaming_set_rate()`,
cleared only in `codec_init()` at boot. **Nothing clears it on stream close.** So
before 0x004A the pair was raised at the FIRST stream open and never lowered
again.

The consequence, and it corrects the fix's own commit message ("the ADC was
never offset-calibrated"):

- boot: `codec_init()` publishes 0x0000, so RST goes LOW
- first stream open: the `SETB` raises it -> **one** offset calibration
- every later stream open: already high, no edge, no calibration

So the converter was calibrated exactly once per power-up, at whatever moment the
user first hit record. What 0x004A changed is not never-to-once but
**once-to-every-open**. That is also precisely the 0x004B behaviour, which is why
0x004B reproduced the pre-fix failure mode on a cold boot.

### What that leaves, stated as a constraint rather than a story

The transient's DC **decayed to zero**, not to some standing value. The
high-pass removed it completely. So the DC was UPSTREAM of the filter -- at the
input or in the modulator -- and was not a wrong constant in the calibration
register, because that register is subtracted downstream and its error cannot be
filtered away. (0x004B's cold-boot offset was permanent, which is what a
downstream constant looks like, and is a different failure.)

So the thing being sought is an INPUT-side DC step that recurs at every stream
open. Eliminated as its cause: clock reprogramming at the same rate, at a changed
rate, and any clock stop. Not yet eliminated: whatever else stream start does --
the capture DMA arming, the endpoint configuration, the alt-setting switch, and
the digital activity that begins with them.

**Nothing here touches the fix**, which is proved by measurement and matches
stock. What keeps being wrong is the explanation, and three versions of it have
now been retired by experiment rather than by argument.
