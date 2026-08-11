# #197 RESOLVED — the full mechanism, measured end to end

2026-08-10, on build 0x004E with `g_diag_clr_mask`. Supersedes three earlier
accounts, each of which was retired by experiment rather than by argument.

## The mechanism

1. **The boot path calibrates three times, within milliseconds of power-up.**
   Block 12 reads `rst_cycles = 3` before any capture is opened. The analog
   reference needs ~16 s to settle (10 uF on each of VREFL/VREFR), so all three
   latch an offset constant taken against a VCOM that is nowhere near final.

2. **The error grows as the reference charges.** Holding the mask at 0x00 from
   the instant of enumeration -- so those three are the only calibrations of the
   power-up -- the resulting DC climbs and asymptotes:

   | elapsed | opening DC (LSB24) | level |
   |---|---|---|
   | 3.1 s | +709242 | −43.2 dBFS |
   | 6.1 s | +933978 | −40.6 |
   | 12.2 s | +1011582 | −39.8 |
   | 24.4 s | **+1024190** | −39.7 |

   +1024190 LSB24 is **0.122 of full scale**. The asymptote at ~20 s matches the
   measured reference settling time.

3. **Between streams the converter is not clocked, so its high-pass cannot
   converge.** The AK5383 is a slave: LRCK and SCLK come from the C-port, which
   idles when no stream is open. MCLKO keeps running throughout -- which is why
   block 11 never saw this, and why "the clock never stops" was true and
   irrelevant. **MCLKO is not the converter's sample clock.**

   **Directly confirmed 2026-08-11 by #202**, which is worth recording because
   the evidence originally given here did NOT establish it. The support offered
   was that opening DC is independent of the close-to-reopen gap (0/1/20 s), and
   that does not discriminate: a step injected into a *running* 1 Hz high-pass
   decays with the same tau = 171 ms as a filter converging from scratch, so the
   observation fits both stories equally. The register also pointed the other
   way -- `CPTCNF4 = 0x03` has `CPTBLK` clear, and TI says CSYNC and CSCLK
   free-run in that state. The direct test: raise RST with no stream open, wait
   5 s, capture. The capture still showed 8769 leading zeros, i.e. the
   calibration had not run despite 5 s at the raised level, and completed only
   once audio started. `FINDING_202_the_cport_does_not_free_run.md`.

4. **So every stream open starts the filter from scratch**, the latched error
   appears at full amplitude, and the filter removes it over ~2 tau.

## The decisive measurement

One 6 s capture, mask 0x00, reference fully settled:

    0 ms +1057326   60 ms +743902   120 ms +523387   180 ms +368236
  240 ms  +259082  300 ms +182290   420 ms  +90253   600 ms  +31435
  900 ms    +5416  1.2 s    +934    1.8 s      +49   3.0 s       +6

**tau = 170.6 ms** over the first 60 ms and **170.7 ms** over the full 600 ms,
on both channels. `FINDING_197` measured the historical transient at
**tau = 171 ms**. It is the same phenomenon, reproduced to within 0.3 %.

And the state does not survive the stream closing:

| | opening DC | ending DC |
|---|---|---|
| baseline | +950150 | +3.3 |
| after 1 s closed | +944047 | −0.7 |
| after 20 s closed | +945550 | +4.9 |

Identical opening amplitude whether the device sat idle for one second or
twenty, and driven to zero within each capture. The filter converges only while
a stream is open, and loses that convergence when it closes. That is what makes
the transient reappear every single time.

## Why the fix works

Clearing RST forces a fresh calibration at every stream open, with the reference
long settled, so the constant is right and there is nothing for the filter to
converge away from. The 183 ms of zeros is that calibration (tRTV = 8960/fs).
Stock does exactly this, at exactly this point in the sequence.

It is worth being precise about what changed: **not** never-calibrated to
calibrated. Before 0x004A the part was calibrated three times at boot and never
again. After it, the part is calibrated at every stream open. The bug was never
a missing calibration -- it was a calibration taken at the worst possible moment
and then never revisited.

## Why every earlier probe returned null

- **#199** fired the reprogramming diagnostic on a *calibrated* part. The offset
  was ~0, so a disturbance had nothing to reveal. The null was about the
  instrument, not the hypothesis.
- **The "does reprogramming clear the filter state" test** used a mux-injected
  correction *mid-stream*, where LRCK never stops. The state legitimately
  survives that. Reprogramming really is innocent; what loses the state is the
  stream *closing*, which no mid-stream probe can produce.
- **The MCLKO measurements** were the wrong clock, and their conclusion --
  "the clock never stops" -- is true of MCLKO and false of LRCK.

Each null was correct about what it measured and wrong about what it was taken
to mean. The common cause is that the phenomenon under investigation had been
FIXED, and nothing was left to observe until 0x004E made it reproducible again.

## Also settled on the way

**ZCAL = "L".** The calibration references VCOM, not the analog input pins.
Calibrating with LINE selected and then switching to MIC leaves no standing
offset (−101 head, −9 tail LSB24). This is why a bad calibration can only come
from a cold boot: the reference is only wrong while VREF is charging, and there
is no way to make it wrong on demand.

**What the bracket costs.** The pair is low only for the clock reprogramming,
~1.5 ms. The 183 ms is tRTV, internal to the AK5383. Playback, which has no such
recovery, loses a single ~2-3 ms glitch. Monitoring while starting a recording
costs a click, not a hole.

## Open

Whether the three boot calibrations are worth suppressing. They are harmless on
the shipping build -- every stream open recalibrates -- but they are also pure
waste, and a build that skipped them would spend less time in a state where a
capture opened at that exact moment would be wrong. Low value; recorded for
completeness rather than proposed.
