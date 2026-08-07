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

## Build 0x0039 got the polarity wrong, and hardware said so

The first attempt raised the pair, held it **HIGH** for ~860 ms, then lowered
it. After the cold boot that flash required, both units still showed the
transient at full size — unit A **−33.4 dBFS** in the first 100 ms with DC
+0.127, unit B −36.8. The hold duration was never the problem. The direction
was: the working sequence holds the gate **LOW** and ends **HIGH**.

Ending high is also what makes it safe. `usb_init()` runs first, so a
SET_INTERFACE can land during the hold; a sequence ending low switches the
capture path off underneath a stream that just opened, which is what 0x0039
risked. Ending high is right either way, and matches stock, which raises the
pair at power-up and holds it.

## The requirement is the EDGE, not a settling time

Measured on units re-armed by a plain power cycle of the 0x0039 image — whose
pulse leaves the transient intact, which makes it a free fixture, no flash
needed.

`tools/mutepulse.c` exists because two `amixer` invocations cannot resolve
below tens of milliseconds: each is a process spawn plus a control transfer.
One process holding the control handle open, with a single `usleep` between two
element writes, resolves to about a millisecond.

| unit | low-hold | first 100 ms |
|---|---|---|
| A | baseline | −38.7 dBFS |
| A | **1 ms** | **−101.1** |
| B | baseline | −37.2 dBFS |
| B | **0 ms (no usleep)** | **−100.9** |

So no settling time is needed at all. The floor actually *proven* is about
**1 ms**, because even a zero-usleep host pulse is still two USB control
transfers apart — two back-to-back `codec_write_word()` calls in firmware are
quicker than anything measured here. One `hw_short_delay()` is kept for that
margin, and it costs less code than the loop it replaced.

**A detection trap, twice.** A muted capture reads −600 dBFS (exact zeros), and
a pass test of `level < -90` counts that as success. It fired twice before the
check became "nonzero samples AND at the floor". The codec word settles it
independently: 0x1CC0 has 0x23.2 set, so the gate was high and the zeros were a
race with an in-flight unmute, not a stuck mute.

## Why a boot pulse is inert: mboxfw has no codec clock at boot

Builds 0x0039 and 0x003A both put the pulse in `main()` after
`cs8427_boot_init()`, and both were measured inert. 0x003A even ended with the
gate provably high (codec word 0x1CC0, 0x23.2 set) and the transient still came
back at full size — unit A −32.6 dBFS, unit B −37.3.

**`ACGCTL` is never written in `hw_init.c`.** mboxfw brings the codec master
clocks up only in `streaming_set_rate()`, when a stream opens. So at boot the
codec is unclocked and a gate transition does nothing.

That is a divergence from stock in its own right: stock's power-up sequence
does `ACGCTL |= 0xC0` — master clock outputs ON — *before* its
`SETB 0x23.2 ; SETB 0x23.3` unmute (Rev 20 0x080B-0x0852, Rev 22
0x09B6-0x09F5). Stock clocks and unmutes at power-up; mboxfw defers both to the
first stream.

**Clocks running is the whole precondition — a capture need never have run.**
Measured on units re-armed by a plain power cycle:

| | first 100 ms |
|---|---|
| unit A, control: capture straight after the power cycle, no pulse | **−36.2 dBFS**, present |
| unit B: PLAYBACK only to raise the clocks, then pulse, then its first ever capture | **−104.6 dBFS**, cleared |

So the pulse belongs at the end of `streaming_set_rate()`, one-shot per
power-up — after that routine's own publish, so it does not sit inside stock's
documented `LCALL 0x0E62` sequence. That is build 0x003B.

## Clocks must have been running a WHILE, not merely be on

Build 0x003B put the pulse at the end of `streaming_set_rate()`, microseconds
after that routine's `ACGCTL` writes. **Also inert** — unit A −32.9 dBFS, unit B
−38.2, transient intact.

The measurement that separated "on" from "on for a while": pulsing unit A with
**no stream running at all**, its clocks merely having been on since some
earlier captures, cleared it from **−33.0 to −101.1 dBFS**. So an active stream
is not required and neither is a capture — elapsed time with the clocks up is.

The shortest clocks-on time measured to work is about 2.5 s (the `aplay` lead-in
of the earlier test). The failures sit at ~0 ms. Nothing narrows the gap, so the
firmware waits **2500 SOFs = 2.5 s**, the measured-good value rather than a
tuned one.

The wait cannot be spun in `streaming_set_rate()`: that routine is reached from
the EP0 handlers, which run in ISR context (`isr_int0` services USB), so a
busy-wait there would stall enumeration. The main loop is the only place that
can afford to wait, and SOF is the right clock to wait on — 1 ms a tick, already
counted, and it only ticks while the bus is live.

Build 0x003C therefore does two things:

- **brings the master clocks up at boot**, which is what stock does and mboxfw
  did not: stock runs `ACGCTL |= 0xC0` before its `SETB 0x23.2 ; SETB 0x23.3`
  unmute, while mboxfw left ACGCTL alone until the first stream. This is a
  divergence worth its own attention beyond #197.
- **pulses from the main loop**, 2500 SOFs after that mark. The wait now elapses
  during boot rather than 2.5 s into whatever the host records first, so no take
  ever contains the pulse.

## Three failed attempts, and what each cost

| build | placement | result |
|---|---|---|
| 0x0039 | boot, hold HIGH ~860 ms, end LOW | inert — wrong polarity *and* no clock |
| 0x003A | boot, hold LOW, end HIGH | inert — right polarity, still no clock |
| 0x003B | end of `streaming_set_rate()` | inert — clocks on, but only just |

Each was reasoned rather than measured. The pattern is the same one this repo
keeps writing down: the hardware was available the whole time, and every
correction came from measuring, not from thinking harder about the previous
failure.

## Both open questions are now closed

The two things this document originally listed as unverified — the minimum
pulse width, and whether a pulse works at boot rather than later — are answered
above and by the 0x003A flash. Nothing about the mechanism is established: what
the gate does inside the codec, and why one edge fixes it for the whole
power-up, is still unknown. Only its behaviour is measured.

## Stock, for the record

Neither Rev 20 nor Rev 22 does this — see `FINDING_196`. Stock raises the pair
once at power-up and never pulses it. So a fix here is an improvement over
stock, and the transient is original Mbox 1 behaviour.
