# #179 — holding the slaved clock across SET_CUR eliminates the slips

**Measured 2026-08-04 on unit B (RK1672500M), builds 0x0021 → 0x0022.**

## The defect

Selecting S/PDIF slaved the clock. Then the next thing every host does —
open a stream — sent `SET_CUR(48000)`, which re-programmed the ACG to the
internal synthesiser. The device was left **routed to the S/PDIF receiver
while generating its own master clock**, with no sample-rate converter in
the path (the CS8427 has none; that is the CS8420).

Reproduced on 0x0021 first, on this bench, rather than trusting the earlier
measurement — the rig had been moved and both units replugged since:

```
cset numid=3 1   → selector=S/PDIF   clock=slaved to S/PDIF (mode 1)
arecord opens    → selector=S/PDIF   clock=internal 48 kHz  (mode 3)   ← defect
arecord closes   → selector=S/PDIF   clock=internal 48 kHz  (mode 3)   ← persists
```

The third line is new information: the un-slaving is not scoped to the
stream. The device stays internally clocked afterwards.

## The fix

While `CODEC25_SEL_SPDIF` is set, a rate request re-runs clock mode 1
instead of mode 2/3. It still goes *through* `streaming_set_rate()`, which
also re-arms both endpoints, raises the 0x23.2/0x23.3 audio-path pair and
republishes the codec word — so the fix passes 0 rather than skipping the
call. The requested rate is kept in `g_internal_rate` (returning to analog
applies it) and reported by `g_sample_rate` (GET_CUR answers the host's own
value instead of contradicting it).

On 0x0022, same bench, same session:

| | 0x0021 | 0x0022 |
|---|---|---|
| select S/PDIF, no stream | mode 1 | mode 1 |
| **mid-stream** | **mode 3** | **mode 1** |
| after close | mode 3 | mode 1 |
| back to analog | — | mode 3, rate restored |

The last row exercises the `g_internal_rate` divergence from stock: stock's
cmd4 reloads `RAM[0x08]`, but mode 1 writes `MOV 0x08,#1`, so stock
re-applies the slaved clock when returning to analog.

## Does it matter? 180 s each, A transmitting 1 kHz over S/PDIF

Detector: for a pure sinusoid `x[n] - 2cos(w)x[n-1] + x[n-2] == 0` exactly.
A dropped or repeated sample breaks the recurrence. Threshold 2% of the
signal's own peak — set from the signal, not tuned.

```
control (internal clock, forced mid-stream)   38 slips   worst 99.1% of peak
fixed   (slaved, class-compliant host path)    1 event   worst 13.1% of peak
```

**Count physical slips, not residual samples.** The raw residual count was
85 for the control arm, but one dropped sample trips the residual across
several consecutive samples; clustering events within 50 ms gives 38. The
raw count over-reports by ~2.2x. This matters for comparing runs: the #177
measurement reported 40 events at a 4.65 s mean gap, and only the clustered
figure here (37 steady-state slips, mean gap 4.81 s, min 0.59, max 9.13) is
the same quantity. Two independent runs agreeing on ~4.7 s is what makes the
crystal-offset reading (~4.6 ppm) more than a single-run artifact.

The first control-arm cluster is 9 residuals at t=5.25 s — the instant the
clock was forced — and is the switch transient, not steady-state drift.

### The one remaining event is not explained

13.1% of peak, once in 180 s. It is an order of magnitude smaller than a
real dropped sample and has the wrong signature for a clock mismatch, which
produces metronome-regular events. Recorded as unexplained rather than
dismissed as noise.

## Ordering trap in the measurement itself

The first A/B run produced **no contrast**, and the reason is a good sign.
The control arm forced mode 3 *before* opening the stream — but on 0x0022,
opening the stream sends `SET_CUR(48000)`, which now **re-slaves**. The fix
corrected the control arm out from under the experiment, and both arms came
out slaved.

The control must therefore force the mismatch *after* `arecord` is up, where
nothing sends a further SET_CUR. This is exactly why `TLM_REQ_SET_CLOCK`
keeps the ability to express the mismatched combination: it is the only way
left to produce the pre-fix state, and without it there would be no control
arm at all. A diagnostic that can only express valid states cannot measure
the value of enforcing validity.

## Inherited from stock, not introduced

Stock has the same tension. `cmd7`/`cmd8` (Rev 20 @ 0x0480/0x049A, Rev 22 @
0x0466/0x0480) call the clock routine with mode 2 or 3 — `ACGCTL = 0x06`,
internal synth — and only *then* branch on 0x25.4 to re-assert
`CLOCKSOURCE = 0x41`. The CS8427 recovers from AES3 while the TAS
synthesises its own master clock: the same split. Stock is playable because
the kernel quirk follows every source change with an explicit rate-0
request. A class-compliant host sends no such thing, which is why mboxfw had
to fix in firmware what stock fixed in the driver.

## Not covered by this measurement

This is the **S/PDIF-input** clock domain only: B's C-port versus A's
transmitter. It says nothing about the **USB isochronous** domain — B's
audio clock versus the host's SOF. Slaving to S/PDIF makes B's clock A's
clock, which is no more related to the PC's frame clock than before. Whether
the capture endpoint's fixed-size packets drift against the host's buffer
over long captures is a separate, unmeasured question.
