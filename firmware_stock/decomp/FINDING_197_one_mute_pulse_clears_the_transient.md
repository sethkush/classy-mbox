# One pulse of the capture gate clears the start-up transient, permanently

2026-08-06, build 0x0038, both units. Follows `FINDING_196`, and corrects a
wrong explanation I reached on the way.

## The result

**Driving the capture gate 0x23.2 low and back high, once, removes the ADC
start-up transient for good.** Unit B, 100 ms slices of the first 800 ms of a
capture:

| | slices, dBFS |
|---|---|
| baseline | −38.9 −43.9 −49.0 −54.1 −59.2 −64.3 −69.4 −74.5 |
| baseline again | −38.9 −44.0 −49.1 −54.2 −59.3 −64.3 −69.5 −74.5 |
| **after one mute/unmute** | **−101.1 −102.9 −104.2 −104.7 −104.8 −105.1 −105.0 −104.8** |
| again | −102.8 −104.3 −104.7 −104.9 −104.8 −104.8 −104.8 −104.6 |
| again | −105.0 −105.1 −104.9 −105.0 −105.0 −105.0 −105.0 −104.9 |

Gone, and it stays gone across repeated stream starts, a rate change to 44.1 kHz
and back, 60 s idle, and a **USB bus reset**. Nothing short of a power cycle has
been found that brings it back.

## Why stream start does not do the same thing

`streaming_set_rate()` runs on every stream open and does `g_codec_state_23 |=
CODEC23_MUTE_PAIR` — an OR onto a bit that is **already set**, so the publish
writes an unchanged value. The bit never moves.

`codec_apply_mute()` on a host mute genuinely drives it **1 → 0 → 1**, with a
`codec_write_word()` each way. That transition is what clears it, not the write.

Consistent with the earlier observation in `FINDING_196` that
`codec_apply_mute()` produces no transient of its own: the mute gate is clean in
both directions, so the pulse costs nothing audible.

## The wrong explanation I published first, and how it was caught

After the mute experiment on unit A, A's transient was gone. I attributed that
to cumulative streaming — A had streamed for over an hour, B for about thirty
seconds — and the timeline fitted.

**Unit B refuted it.** Both units were power-cycled together at the same replug,
3.5 hours earlier, and B still showed the transient at full size with the same
τ = 171 ms on both channels. So it was never time-since-power-up, and a 26-point
burn-in run was started on the wrong hypothesis before the one-minute mute test
settled it.

The lesson is the ordinary one: the state change coincided with the mute cycle,
and I reached for the slow explanation instead of testing the coincidence.
The two units existing at different exposure levels is what made it falsifiable.

## What this means for the fix

`#197` was "mute capture for the first ~500 ms of every stream", which would
have needed **1.34 s** to fully hide and would have cost silence at the top of
every take. That is now unnecessary.

The fix is a **single pulse at init** — one `&= ~CODEC23_MUTE_CAPTURE`, publish,
short delay, one `|=`, publish. Once per power-up, not per stream. No silence in
any take, and it should be 15–30 bytes rather than a counter, a time base and a
per-stream state machine.

Note the pulse worked with **no stream running** (mute/unmute happened between
captures, not during one), so init is a plausible place for it.

## Two things not established

- **Minimum pulse width.** The test held the gate low for 4 s. It may work in
  milliseconds. Untested, because characterising it needs a unit that still has
  the transient, and both are now cleared.
- **Whether a pulse at `codec_init()` time works**, with the codec freshly up,
  rather than later with everything running.

Both need a power-cycled unit. That is not a blocker in practice: shipping the
fix needs a flash, a flash needs a replug, and the replug re-arms the transient
— so the verification comes free with the attempt. Verify by capturing
immediately after the replug and checking the first 100 ms sits at the −101 to
−105 dBFS floor rather than −39.

## Stock, for the record

Neither Rev 20 nor Rev 22 does this — see `FINDING_196`. Stock raises the pair
once at power-up and never pulses it. So a fix here is an improvement over
stock, and the transient is original Mbox 1 behaviour.
