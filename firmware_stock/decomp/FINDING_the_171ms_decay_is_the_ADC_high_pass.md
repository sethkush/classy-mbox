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
