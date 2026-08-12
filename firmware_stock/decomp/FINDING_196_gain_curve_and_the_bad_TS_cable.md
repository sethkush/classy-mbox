# #196 — the gain curve, unity, and a bad TS cable worth 25 dB of THD

2026-08-11, both units on mboxfw 0x0053, void box 192.168.1.76.
Instrument: `tools/measure_point.py`. Rig per `BENCH_WIRING.md`.

## Unity and the gain curve — unit B, channel 1 (line input)

Round-trip gain, source at unit A, measured at ten dial positions:

| position | gain (dB) |
|---|---|
| minimum | −20.76 |
| 9:00 | −19.57 |
| 10:30 | −16.55 |
| 12:00 | −8.47 |
| 1:00 | −2.31 |
| **1:30** | **−0.07 → UNITY** |
| 3:00 | +6.39 |
| full CW | +30.02 |

**Total range 50.8 dB**, and the taper is far from log-linear:

| span | dB per clock-minute |
|---|---|
| min → 12:00 | 0.041 |
| 12:00 → 1:30 | 0.093 |
| 1:30 → 3:00 | 0.071 |
| 3:00 → full CW | **0.197** |

Nearly half the range is in the last two hours of rotation, so gain is easy to
set finely around unity and nearly impossible near the top.

**"Unity" here is round-trip 0 dB against unit A's output, NOT +4 dBu.** There is
no meter in this rig. Do not read it as a voltage reference.

### Repeatability of a dial position: ±1.3 dB

Set to 1:30, moved away, returned to "1:31ish": −0.07 dB then +1.20 dB. At
0.075 dB per clock-minute that is ~16 minutes of rotation, despite reading as the
same position by eye. A third attempt landed at +0.41 dB.

So the honest specification is **"unity is at 1:30, ±1.3 dB by hand"**. Chasing a
finer setting measures the knob's slop, not the hardware.

### Best operating point

Noise tracks gain, and the clipping ceiling falls as gain rises, so usable
dynamic range peaks in the middle:

| position | noise (dBFS) | max clean in | SNR |
|---|---|---|---|
| minimum | −97.4 | −20.8 | 76.6 |
| 12:00 | −89.9 | −8.5 | 81.4 |
| **1:30** | −81.8 | ~0 | **81.8** |
| 3:00 | −75.7 | 0 | 75.7 |
| full CW | −52.9 | 0 | 52.9 |

**Unity is also the best place to run the input.** Below it you discard headroom
you cannot use; above it you amplify noise. Note these figures include unit A's
DAC noise, so they bound the rig, not the Mbox input alone.

## The outlier is the CABLE, not the output — and the first answer here was wrong

Through all ten dial positions the THD column read **−61.7 dB and never moved**,
across 50 dB of gain, with the five-level profile identical to 0.1 dB every time.
Distortion that does not respond to the gain control is not in the gain stage.
That much was right.

The first conclusion drawn from it was not. Re-patching **A out2 → B src1** gave
−87.2 dB against A out1's −61.7 dB, and that was written up as "unit A's line out
1 is 25 dB worse than its line out 2". **It changed two variables at once.**
`BENCH_WIRING.md` records the cable types and they differ:

| path | cable |
|---|---|
| A out1 → B src1 | **long TS**, unbalanced |
| A out2 → A src2 | **short TRS**, balanced |

Seth caught it. The single-variable test is the same cable on both outputs:

| source into B src1 | cable | best THD |
|---|---|---|
| A out1 | **long TS** | **−61.7 dB** |
| A out1 | short TRS | −86.4 dB |
| A out2 | short TRS | −87.2 dB |

**The two outputs differ by 0.8 dB**, and their gains match to 0.07 dB
(−20.59 vs −20.52). There is nothing wrong with A's line out 1.

**The long unbalanced TS cable costs ~25 dB of THD.** Whether that is the length,
the unbalanced topology, or a nonlinear contact in one of its plugs is not
established — a dirty or partly-seated TS plug is a classic harmonic-distortion
source and would look exactly like this. The cable has not been swapped for a
second long TS, nor inspected.

All four paths, ranked, with cables named:

| path | cable | best THD |
|---|---|---|
| A out2 → A src2 (ch2 self-loop) | short TRS | −94.3 dB |
| A out2 → B src1 | short TRS | −87.2 dB |
| A out1 → B src1 | short TRS | −86.4 dB |
| B out1 → A src1 | long TS | −82.5 dB |
| **A out1 → B src1** | **long TS** | **−61.7 dB** |

Note the other long-TS path (B out1 → A src1) reads −82.5 dB, i.e. NOT degraded
the same way. So it is not "long TS cables are bad" as a class — it is this one
cable, which points at a specific fault in it rather than at the topology.

## Consequences

- **Both of unit A's line outputs are fine.** So is unit B's channel-1 input, at
  ≥ −87 dB. Every THD number in the gain sweep was measuring the long TS cable.
- **The gain, noise, unity and clipping results stand.** None of them depend on
  the source's distortion, and the contamination was a constant.
- **Retire or investigate that TS cable before it contaminates anything else.**
  Use the short TRS for any THD work.
- ch1 and ch2 are different analog front ends (mic preamp vs DI, plan.md §2), so
  the ch2 self-loop's −94.3 dB is not a ceiling for ch1 paths.

## The lesson, since it cost a wrong conclusion

Ten identical readings across 50 dB of gain looked like a solid measurement
rather than a stuck one — the consistency made it MORE convincing, not less. It
only broke when something the number should not have depended on was changed.
And the follow-up test that "confirmed" it moved the cable and the output
together, which is exactly the confound `BENCH_WIRING.md` exists to prevent, in a
table that had already been quoted earlier in the same session.

## Method notes worth keeping

- The analyser initially read **+1.76 dB high at every level** — an exact-bin sine
  under a Hann window spreads power over three bins as A²+2(A/2)², i.e. 1.5×, and
  10·log₁₀(1.5) = 1.76. Caught by feeding it tones of known level. THD is a ratio
  and was never affected; absolute levels, gains, and THD+N's denominator were.
  The calibration arm is now the first thing to run if any number looks odd.
- The tone is an exact integer number of cycles in the analysis window, so
  leakage cannot masquerade as distortion.
- The first 500 ms of every capture is discarded (`FINDING_202`).
- Clipped rows are excluded from the gain median. At full CW only the −40 dBFS
  row survives, and a clipped row's THD collapses to −13 dB — real arithmetic on
  a meaningless signal.
- One capture in ~30 comes back as silence (−123 dBFS). The `level > −100` guard
  drops it rather than averaging it in; a repeat run cleared it.

## Unity is NOT the same dial position for LINE and INST — 18.9 dB apart

The 74HC157 muxes select which front end feeds the shared gain stage, so each
source arrives at its own fixed sensitivity before the knob acts. Measured on
unit B channel 1, same cable, same dial position, same source level, only the mux
changing (`TLM_REQ_SET_MUX`, so no hands needed):

| mode | gain |
|---|---|
| LINE | −20.59, −20.59, −20.60, −20.60 |
| INST | −1.70, −1.69, −1.70, −1.69 |

Interleaved line/inst/line/inst; the two LINE arms agree to 0.01 dB, so the run
is not drift. **INST is 18.9 dB hotter at the same knob position.**

Applying that offset to the LINE curve:

| position | LINE | INST |
|---|---|---|
| min | −20.76 | −1.9 |
| 9:00 | −19.57 | −0.7 |
| 1:30 | 0 (unity) | +18.9 |

**LINE unity is 1:30; INST unity is near 9:00.** The instrument input has ~2 dB
of range below unity and ~50 dB above, so a guitar is set in the bottom eighth of
the rotation — which is also where the taper is flattest (0.041 dB per
clock-minute) and setting is therefore easiest.

**Assumption not yet tested:** the offset was measured at ONE dial position
(minimum). A mux ahead of the shared gain stage implies a constant offset, which
is the natural reading, but it has not been checked at a second position.

### THD does not depend on the input mode, only on level

INST appeared far worse -- −61.6 dB at −20 dBFS in against LINE's −85.0. It is
not. Compared at matched CAPTURED level rather than source level:

| | captured | THD |
|---|---|---|
| LINE @ −1 dBFS in | −21.57 | −62.3 dB |
| INST @ −20 dBFS in | −21.69 | −61.6 dB |

Same level at the converter, same distortion to 0.7 dB. The INST path is not
dirtier; it is 19 dB hotter for a given source, so a given source level drives
the shared stage that much harder.

### MIC is XLR-only — the 1/4" jack does not reach the mic preamp (tested)

Worth testing rather than assuming, and the test is safe: the mux is a 74HC157
SIGNAL selector, so switching it only reroutes what the ADC listens to. It
changes no analog voltage on the jack and does not touch phantom, which is
mechanical and independent.

Interleaved line/mic/line/mic, three source levels each:

| mode | played | captured | gain |
|---|---|---|---|
| LINE | −60 / −40 / −20 | −80.6 / −60.6 / −40.6 | −20.6 |
| **MIC** | −60 / −40 / −20 | **−122.2 / −123.0 / −119.8** | — |

The MIC captures do not track the source at all: they sit at the analysis floor
whatever is played, where a real path would move 40 dB across that range. Both
LINE arms read normally in both interleaves, so the rig was alive and this is a
genuine null rather than a dead cable — a distinction that matters here, because
"input selected with nothing plugged into it" and "mux is broken" produce
identical silence.

It also VALIDATES the MIC mux pattern (0x06): the ADC clearly switched to a
different input. A wrong pattern would have left the line signal audible.

So mic unity needs an XLR source — a padded line feed, or a real mic against a
reference. Expect the offset to far exceed INST's 18.9 dB, since a mic preamp
needs 40--60 dB where a line input needs ~0.
