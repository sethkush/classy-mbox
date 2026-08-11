# #201 — the 183 ms of silence is reclaimed, and the threshold is measured

Build **0x004F**, verified on a genuine cold boot on unit B (`RK1672500M`)
2026-08-11. Both units carry the image.

## What #198 left behind, and why it was not free

`FINDING_197_RESOLVED_the_full_mechanism.md` closed the transient by calibrating
the AK5383 at **every** stream open. That is correct and it is what stock does,
but it costs the AK5383's `tRCF` — 8704 LRCK edges, plus `tRTV` — at the top of
**every capture**, which arrives at the host as ~183 ms of exact digital zero.

For a recording device that is the wrong trade. The first 183 ms of a take is
not a place to put a gate; a musician counting in loses the count-in.

## The change

Calibrate at every open **until the analog reference has settled**, then latch
and never calibrate again:

```c
if (!g_cal_done) {
    g_codec_state_23 &= (unsigned char)~CODEC23_MUTE_PAIR_ALL;
    codec_write_word();   /* Rev 20 @ 0x0733, Rev 22 @ 0x071a */
    if (g_ref_settled) { g_cal_done = 1; }
}
```

with the settle detector in the main loop, compared on the high byte only
(a 16-bit compare cost 9 bytes more, and 256-SOF granularity is irrelevant
against a 30 s threshold):

```c
if (!g_ref_settled && (unsigned char)(tlm.sof_count >> 8) >= 0x75u) {
    g_ref_settled = 1;
}
```

`0x75 << 8` = 30000 SOFs = **30 s**.

The ordering matters and is the whole design: the calibration happens
**before** the settle test, so the open that first sees a settled reference is
itself calibrated *and* latches. There is never an open that skips calibration
before a good one has been taken.

## The cold-boot verification

Warm tests cannot see any of this — by the time anything is warm the reference is
settled and the very first open latches. **0x004B passed every warm test and
failed only on a real power-up**, which is why this was verified the hard way.

`tools/coldboot201.py` waits for the unit to disappear, then to return, then
captures continuously with every open timestamped against boot.

```
  t_open   leadzeros      ms   head DC   50-200ms     tail    floor
    0.00        8799   183.3   +9338.9   +32201.3  +58376.5   -63.8
    2.05        8800   183.3   +7640.4   +21302.2  +24625.3   -71.0
    4.10        8815   183.6   +3239.9    +9009.1  +10473.1   -78.5
    6.14        8800   183.3   +1207.2    +3754.2   +4525.8   -85.9
    8.18        8812   183.6    +663.3    +1704.2   +1981.0   -93.8
   10.22        8819   183.7    +136.0     +668.0    +877.9   -99.3
   12.26        8788   183.1      -5.4     +273.4    +398.2  -101.5
   14.30        8799   183.3     -53.0      +95.6    +197.1  -103.4
   ...        (16 calibrating opens in all)
   30.63        8804   183.4    -170.6      -87.5      +6.5  -103.7
   32.67           0     0.0     -81.8      -56.5      +6.5  -103.7
   34.72           0     0.0      -8.8       -0.9      +9.3  -103.8
   ...        (every later open, 0 lead zeros, through t=73 s)
```

Sixteen consecutive calibrating opens, then a clean permanent transition at the
threshold. The two failure modes this separates:

- **~8790 forever** → `g_ref_settled` never fires, threshold wrong
- **0 on the first open** → calibration skipped, the #197 transient is back

Neither occurred. Cold boot proved independently by **bus resets 7 → 3** — a
counter going *down*, the project's standing evidence for a real power-up.

## The reference settling, measured directly

The `head DC` column is the thing the whole #197 investigation had only inferred.
It is the AK5383's offset error as VREFL/VREFR (10 µF each) charge:

| t | head DC (LSB24) | floor |
|---|---|---|
| 0.0 s | +9,339 | −63.8 dBFS |
| 4.1 s | +3,240 | −78.5 dBFS |
| 8.2 s | +663 | −93.8 dBFS |
| 12.3 s | −5 | −101.5 dBFS |
| 16.3 s | +116 | −103.6 dBFS |

At the noise floor by ~14 s and flat after. **The 30 s threshold therefore
carries about 2× margin over the settling it waits for** — it was chosen on
judgment and is now measured.

Note this is the *recalibrated* error at each open, not the standing offset of a
part calibrated once at t=0; the latter is the +1,024,190 LSB24 (0.122 FS)
recorded in `FINDING_197_RESOLVED`.

## What it costs

Not free, and the trade should be stated as a trade.

| | 0x004E | 0x004F (#201) |
|---|---|---|
| every capture | 183 ms of digital black, then −103.7 dBFS | audio immediately |
| opening DC | ~0 (just calibrated) | up to ~560 LSB24, decaying over ~350 ms |

Worst measured opening peak was **622 LSB24 ≈ −82.6 dBFS**, decaying through the
high-pass (τ ≈ 171 ms) to a true zero by ~400 ms. Inaudible, but it is not
nothing: a null test or a THD measurement taken from the first 100 ms of a take
will see it. Everything past ~400 ms is identical to 0x004E.

## Why one calibration is enough — the drift test

The obvious objection is that a single calibration might go stale. Measured over
**41 minutes, 21 captures, calibration disabled** (mask `0x00`, so no open
recalibrated — every capture confirmed `lead 0`):

| | mean | sd | range | trend |
|---|---|---|---|---|
| ch L | −61.6 | 211.0 | −339 … +272 | +3.33 LSB24/min (0.9σ) |
| ch R | −158.0 | 231.7 | −424 … +141 | −1.93 LSB24/min (0.5σ) |

Neither trend is significant, and they point in **opposite directions** — which
is what noise looks like, not drift. The floor held at −103.7 dBFS throughout.
No periodic refresh is needed.

## Notes for anyone changing this

- The threshold is **not observable over the wire.** `sof_count` is not decoded
  by any telemetry block, so the audio is the only instrument for it. Re-verify
  by cold boot with `tools/coldboot201.py`, never by a warm test.
- `TLM_REQ_DIAG_MODE` (0x17) no longer carries #199's `wIndexH` modifier; it now
  means *recalibrate at the next stream open* (`g_cal_done = 0`), which makes the
  behaviour re-armable on the bench without a power cycle.
- Code size 5949 (unit A) / 5947 (unit B) of **6016**. 37/37 gates.
