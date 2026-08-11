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


---

# CLOSING STATE, 2026-08-07: 29 dB won, mechanism NOT understood

Eight builds (0x0039 - 0x0040). The transient is much reduced and the root
cause is still open. This section is the map, so nobody repeats the search.

## What ships

| | before (0x0038) | now (0x0040) |
|---|---|---|
| first 100 ms of a capture | **-32.9 dBFS** | **-62 dBFS** |
| DC step | +0.134 | +0.0046 |

(The −62 dBFS figure is a first-100 ms level measured after streams had already
run in that boot. A cold first capture on the same build reads about −52 dBFS —
see the 2026-08-07 section. Both are the same transient; the level depends on
stream history, which was not controlled when −62 was taken.)

Two changes earned it: the codec master clocks now come up **at boot**
(`streaming_set_rate(48000)` in `main()`), which is what stock does and mboxfw
did not, and a capture-gate pulse fires once per power-up from the main loop,
8000 SOFs after that.

## The wall

A pulse driven **from the host** reaches **-101 dBFS**. The identical pulse
driven **from firmware** stops at **-62 dBFS**. Same unit, same boot, same
codec word before and after (0x1CC0).

Everything below was tested and ELIMINATED. Do not re-test these.

| hypothesis | how it was killed |
|---|---|
| wrong polarity | 0x003A held LOW and ended HIGH; still inert at the time |
| codec unclocked at boot | 0x003C brought clocks up at boot; improved 9 dB, did not fix |
| pulse never fired | `TLM_PHASE_ADC_PULSE` proves it fires |
| hold too short | a **3 ms** host pulse on a freshly armed unit clears fully — the same hold the firmware uses |
| needs repetition | one host pulse is enough |
| wrong bits | dropping the whole pair (0x003E) changed nothing vs capture-only (0x003D) |
| wrong code path | 0x003F sets `g_host_mute` and calls `codec_apply_mute()` — byte for byte what SET_CUR does |
| delay elided by SDCC | generated asm keeps both `lcall _hw_short_delay` |
| ISR preemption of the bit-bang | 0x0040 wraps each publish in `EA = 0` / `EA = 1`; no change |
| timing (early vs late) | a host pulse at **7.5 s**, first of the boot, clears fully (-98.1 -> -101.7) |

## Why the instruments could not settle it

Telemetry block 9 reports `g_codec_state_23` — mboxfw's **mirror** of the word,
not what the shift register latched. Every build reported the correct codec word
while the chip may have received something else. No host-visible signal
distinguishes "wrote the word" from "the codec accepted the word".

The obvious probe — a scope on P1, data P1.0, clock P1.2, latch P1.1 —
**is not available on this bench**. Seth has a multimeter and nothing else.
The section below replaces that recommendation with one that can actually be
carried out.

---

# 2026-08-07: the residual is the SAME transient, 33 dB down — not a half-clear

The framing above ("the pulse only half-works") is wrong, and one free
measurement showed it. 2 s idle capture on each unit running 0x0040, sliced at
50 ms, no flash and no pulse:

| t (ms) | unit A, dBFS | unit B, dBFS | A, DC | B, DC |
|---|---|---|---|---|
| 0 | −48.3 | −50.7 | +0.00384 | +0.00289 |
| 50 | −50.8 | −53.3 | +0.00286 | +0.00216 |
| 100 | −53.4 | −55.8 | +0.00214 | +0.00161 |
| 200 | −58.5 | −60.9 | +0.00119 | +0.00090 |
| 400 | −68.7 | −71.0 | +0.00037 | +0.00028 |
| 800 | −88.3 | −91.6 | +0.00004 | +0.00003 |

**−2.5 dB per 50 ms bin = −50.6 dB/s = τ = 171 ms**, on both units, on both
channels. That is the identical time constant `FINDING_196` measured by window
stepping and `FINDING_147` measured by median-|x| slices.

So the transient is not partly suppressed. It is **fully present, fully
re-armed on every stream start, and its initial amplitude is 33 dB smaller**
(DC +0.0029 against a 0x0038 baseline of +0.134). What 0x0040 bought is a
smaller step into the same DC-blocking high-pass, not a shorter or absent one.

That kills the premise that made a scope the next step. The question was
"did the codec receive the word", because a digital gate either transitions or
it does not, and −62 dBFS sat implausibly between the two. It no longer sits
between anything: there is a clean exponential with the right τ, exactly what a
smaller DC step produces. **A dropped or corrupted bit-bang does not produce a
correctly-shaped decay at 1/33 amplitude.**

## What the host pulse still does that the firmware pulse does not

Re-run on 0x0040, `mutepulse <card> 3`, then capture:

| | first 400 ms |
|---|---|
| unit B after host pulse | −103.8 −102.0 −102.9 −103.8 … **flat, no decay** |
| unit A after host pulse | −100.9 −97.3 −98.5 −99.0 … **flat, no decay** |

Flat, not decaying — so the host pulse removes the DC step itself rather than
shrinking it, and the effect survives into the next capture. The two-tier state
is real and reproducible on the current build. Two details recorded and not
chased: the first ~150 ms of a post-pulse capture is exact digital zeros, and on
the *second* capture exactly one channel per unit (B left, A right) re-arms a
small decaying residual while the other stays flat. The per-channel asymmetry is
new; the cross-wiring in `BENCH_WIRING.md` is a confound and it has not been
controlled for.

## The measurement to make next, with the instruments that exist

**The device contains a 48 kHz ADC. That is the logic analyser.** The capture
gate produces *exact digital zeros* when low (#189, and again in the zeros
above), so a gate pulse during a live capture is directly visible in the
recording with 20 µs resolution. The recorded gap is a witness of what the
codec **accepted**, not of what the firmware mirror says it wrote — which is
precisely the blind spot telemetry block 9 has.

One build, one capture, three arms compared inside a single stream on a single
boot:

1. **host** — an ALSA `PCM Capture Switch` pulse (`tools/mutepulse.c`), the
   known-good path.
2. **firmware, ISR context** — a vendor request (DEVICE recipient) that pulses
   the gate inside the EP0 handler, which is where the host path already runs.
3. **firmware, main loop** — the same vendor request arms a flag; the pulse
   fires from the main loop, which is where #197's pulse runs.

Each commanded hold is a known number of milliseconds. Measure the width of
each zeros gap in the recording. Outcomes:

- all three gaps present and correctly sized → the bit-bang delivers fine from
  every context, the delivery hypothesis is dead, and the remaining difference
  is *when* the pulse happens relative to the ADC's own power-up, not *whether*
  it happens.
- main-loop gap missing or short → main-loop publishes really are unreliable,
  which immediately implicates `buttons_poll` and every other main-loop codec
  and mux publish, and is a far larger finding than the transient.

This needs no instrument the bench lacks. It costs one flash and one power
cycle per unit.

## RESULT: main-loop publishes land perfectly. The delivery hypothesis is dead.

Build 0x0041, both units, the experiment above. Three arms in one 6 s capture:
an ALSA `PCM Capture Switch` pulse at 1 s, an ISR-context pulse at 2.5 s, a
main-loop pulse at 4.5 s. Every arm produced a run of exact digital zeros.

| arm | unit A | unit B |
|---|---|---|
| host, ALSA, 20 ms commanded | 207.67 ms | 207.62 ms |
| firmware, ISR context, 40 ms commanded | 227.69 ms | 227.96 ms |
| **firmware, main loop** | **266.17 ms** | **266.17 ms** |

The main-loop arm is repeatable to ±0.2 ms over three runs (266.38, 266.15,
266.17) and identical across two units. **A main-loop codec publish reaches the
chip exactly as reliably as an ISR-context one**, for both the mute and the
unmute. Seven builds of suspicion, closed by one measurement.

That also retires the suspicion recorded at the end of the previous section:
`buttons_poll` and the other main-loop codec and mux publishes are not exposed.

## Releasing the capture gate costs a fixed 188.0 ms of digital zeros

Sweeping the commanded hold on unit A, ISR arm:

| hold | zeros width | width − hold |
|---|---|---|
| 5 ms | 192.79 ms | 187.8 |
| 10 ms | 198.06 ms | 188.1 |
| 20 ms | 207.90 ms | 187.9 |
| 40 ms | 228.02 ms | 188.0 |
| 80 ms | 267.94 ms | 187.9 |
| 160 ms | 348.12 ms | 188.1 |

**188.0 ms ± 0.2 over a 32× range of holds.** So the gate is not a simple mute:
releasing it puts the codec through a fixed recovery during which it emits
exact zeros, and only then does audio resume. 188 ms against the τ = 171 ms of
the start-up transient is close enough to suggest the same physical settling —
the codec zeroing its own output while the ADC re-settles, rather than passing
the DC step through. That would explain *why* a gate pulse clears the transient
at all, which nothing before this had a mechanism for. It is a suggestion, not
a measurement: nothing here proves the two are the same process.

## hw_short_delay() is 78 ms, not the ~3 ms the comments claimed

The main-loop arm holds for `hw_short_delay()`, and 266.17 − 188.0 = **78.2 ms**.
Every comment in `codec.c` and every "~3 ms" in this document was wrong by a
factor of 25. Nothing was decided on that number — the measured floor for the
hold is about 1 ms and any of these values clears it — but it was stated as
fact in four places and is now measured.

## So the pulse was never broken. Its PLACEMENT was.

A firmware main-loop pulse fired while a capture was running clears the
transient completely, exactly as a host pulse does. Plain captures taken
straight afterwards, no pulse of any kind:

| | first 250 ms |
|---|---|
| unit A | −99.0 −98.9 −98.5 −98.8 −99.4 −99.1, **flat** |
| unit B | −102.6 −102.6 −102.9 −103.1 −103.7 −103.7, **flat** |

Flat, not decaying, on the same firmware whose *boot* pulse leaves a
τ = 171 ms transient re-armed on every stream start. Same code, same context,
same unit, same power-up. **The variable is when the pulse fires relative to
the ADC being enabled, and nothing else.**

That also settles what the 33 dB in 0x0040 came from: clocks-at-boot (0x003C),
not the pulse. The boot pulse contributes nothing, and eight builds of tuning
its context and timing were tuning something that was never the problem.

One thing does NOT fit this model and is recorded rather than smoothed over.
The earlier section "Clocks running is the whole precondition" reports a host
pulse clearing the transient with no capture stream running at all, and unit B
cleared by a playback-only stream followed by a pulse. If an open capture were
strictly required, neither should have worked. So the precondition is something
weaker — plausibly that the host has bound and set an alt setting at least once
— and it has not been isolated.

## What to build next

Move the pulse from boot to the first stream start, still one-shot per
power-up. The cost is now known rather than feared: **188 ms of digital zeros
at the top of the first capture only**, against the 1.34 s that the original
mute-through-the-transient design would have cost on every take.

Note this is the placement build 0x003B already tried and measured inert. The
reason to expect a different answer is that 0x003B predated clocks-at-boot: its
pulse ran microseconds after `streaming_set_rate()` first raised ACGCTL, and
the codec needs the clocks up for a while, not merely on. From 0x003C the
clocks are up from boot, so a stream-start pulse now lands on a codec that has
been clocked for seconds. That is a real difference, and it is still a
prediction — 0x003B is a reason for caution, not a reason not to try.

## What the multimeter can and cannot do here

Honestly: very little. It cannot see a 16-bit bit-bang. Its two legitimate uses
are (a) confirming the idle DC levels on P1.0/P1.1/P1.2 against Vcc, which would
catch a shorted or hard-loaded line, and (b) reading the *average* voltage on
those pins under a diagnostic build that republishes the codec word in a tight
loop, where the DMM reads duty cycle × Vcc and a gross difference between a
main-loop and an ISR-driven loop would show up as a different average. Both are
weak tests. Neither is a substitute for the ADC experiment above.

## Suspicion worth recording, unproven

If a main-loop publish really is less reliable than an ISR-context one, that is
not confined to #197: `buttons_poll`, source changes and every other main-loop
codec/mux publish share the exposure. `EA` masking did not change the transient
result, but it was only ever measured against the transient, which is a coarse
instrument for this question.

---

# 2026-08-07, later: #198 works, and the bug was a saturating counter

Build 0x0047, unit A. The pulse moved from boot to the first capture bring-up,
and it clears the transient.

| capture | zero runs | first 100 ms |
|---|---|---|
| 1st of the power-up | one, at t = 0.248 s, **266.69 ms wide** | — |
| 2nd | none | **−96.6 / −87.6 dBFS**, DC −0.00001 |
| 3rd | none | −86.3 / −95.2 |
| 4th | none | −92.6 / −96.1 |

Against a baseline of **−20.4 dBFS with DC +0.094** on the same unit two builds
earlier, that is about 76 dB. The gap lands at the 250 ms dwell and measures
266.69 ms against the 78.2 + 188.0 = 266.2 ms predicted from the hold and the
gate-release recovery — the two independent measurements agree to half a
millisecond.

## The three failures were one defect, and it was not in the pulse

`TLM_INC16` is `if (c < 0xFFFF) c++`. Saturating is correct for the forensic
counters it was written for, where "stopped climbing" is the signal. It is
wrong for a time base, and `tlm.sof_count` is a time base. Read on unit B
during a live capture, build 0x0046:

```
mark_set=1 pulsed=0 mark=65535 sof=65535 elapsed=0    (x5, 0.6 s apart)
```

Pinned at 65535 after ~65.5 s of uptime, so every mark equalled 65535 and every
elapsed-since-mark computed 0. **No SOF-based wait could elapse after the first
minute of uptime.** The mark logic had been correct throughout.

This is also why the builds looked incoherent rather than broken. 0x0040's
8000-SOF boot wait and 0x0043's mark at bind both happened inside the first
65 seconds, so they fired. 0x0044 and 0x0045 were tested minutes after boot, so
they never could. Same code, opposite results, decided by uptime alone.

## What actually went wrong in the process

Three builds were spent theorising about alt settings and execution context
against a system whose clock had stopped, with no way to see that it had. The
state that settled it in one read — mark flag, one-shot flag, SOF counter — was
reported by no block, and `tlm.sof_count` had been unreadable since block 5 was
retired while block 11's byte 7 was *documented* as carrying it and never did.

The rule this repo already had was enough: measure, do not reason. The
amendment it needs is that **a timer nothing can read is not an instrument, and
a wait on it is not a measurement.** Block 11 now reports the pulse state, at
the cost of the ACG read-out, whose questions #186 closed.

## Cost, as shipped

One capture per power-up opens with 266 ms of exact digital zeros starting
250 ms in: 250 ms of transient, then the gap, then clean for the rest of the
power-up. Most of the gap is `hw_short_delay()`'s 78 ms against a proven ~1 ms
requirement, so it could be cut to about 190 ms; the dwell is what keeps
`snd-usb-audio`'s bind-time alt 1 from consuming the one-shot, and it is a
margin rather than a measured minimum.

## A clean FIRST capture is not available. Boot self-capture, measured.

Build 0x0048 asked the obvious follow-up: if what a working pulse needs is an
ADC that is actually converting, the firmware can arrange that itself. It armed
the capture path at boot with no host involved (`streaming_capture_enable(1)`),
let it run 300 ms, pulsed the gate, disarmed, and set the one-shot so the
stream-start pulse could never fire.

It boots and enumerates cleanly, and the self-capture completes —
`TLM_PHASE_ADC_PULSE` is set before any host activity. **It does not work.**
Unit B, first capture of that power-up:

| t (ms) | dBFS | DC |
|---|---|---|
| 0 | −22.2 / −22.7 | +0.0769 |
| 100 | −27.2 / −27.8 | +0.0428 |
| 300 | −37.4 / −38.0 | +0.0133 |
| 500 | −47.6 / −48.1 | +0.0041 |

Full transient, τ = 171 ms, and no zero run (correct — the pulse happened at
boot). So arming the ADC without a host does not reproduce whatever a real
stream start does, and **the transient is armed by the host's own alt 0 → alt 1
and can only be cleared after it.** A clean first capture is not reachable this
way. Reverted in 0x0049.

The first version of that build also hung: it busy-waited on `sof_count` before
entering the main loop, and `sim_smoke` plus three EP0 gates failed instantly.
With no SOF the spin never ends and `main()` never reaches its loop — a device
powered without an active host would never enumerate. The gates caught it
before it reached hardware, which is the second time this week a wait on SOF
has been the defect.

## What is left to cut, if the first take matters

The first capture costs 250 ms of transient then 266 ms of zeros. Both parts
are reducible, and neither is a measured minimum:

- the 250 ms dwell exists only to outlast `snd-usb-audio`'s bind-time alt 1,
  whose duration has never been measured. Measure it with usbmon and the dwell
  can be set just above it.
- the 266 ms gap is 78 ms of `hw_short_delay()` plus the fixed 188 ms recovery.
  The hold's proven requirement is about 1 ms, so a dedicated short delay takes
  the gap to ~190 ms.

Better than either: key the pulse off the first isochronous IN interrupt rather
than off SET_INTERFACE. That is an event only a real capture produces, so the
dwell disappears entirely and the pulse fires at the true stream start — the
first take would open with the gap and nothing else.

---

# RESOLVED 2026-08-08: mboxfw had dropped stock's CLR. Root cause, not a trick.

Build 0x004A, unit A. Two lines restored; the entire pulse mechanism deleted.

| capture | zeros at top | after that |
|---|---|---|
| 1st | 183.35 ms | −104.8 dBFS, flat, DC 0 |
| 2nd | 183.67 ms | −103.9, flat, DC 0 |
| 3rd | 183.15 ms | −102.5, flat, DC 0 |

Against −20.4 dBFS with DC +0.094 two builds earlier. **The transient is gone
entirely** — not reduced, not hidden: there is no decay left to measure.

## What was wrong

Stock brackets its clock reprogramming:

```
CLR 0x23.2 / CLR 0x23.3 ; PUBLISH ; reprogram ; SETB both ; PUBLISH
```

Rev 20 fcn.0x0728 @ 0x072F/0x0731 then 0x0733, release @ 0x07EE/0x07F0 then
0x07F2. Rev 22 fcn.0x070F @ 0x0716/0x0718 then 0x071A, release @ 0x07CF/0x07D1
then 0x07D3.

mboxfw ported only the release. The comment above that code has always
described the full bracket; no CLR was ever emitted, so the SETB wrote bits
already set and neither edge occurred.

0x23.2 is the AK5383's RST. AKM: *"When this pin returns to High, an offset
calibration cycle starts. An offset calibration cycle should always be
initiated upon powering up the device."* Without a falling edge that cycle
never ran, so the ADC has been operating **un-calibrated** for the life of the
project, and its DC offset settled out through the digital high-pass at the top
of every capture.

Both measured constants are the part's own, which is what makes this a
mechanism rather than a story:

| measured | datasheet |
|---|---|
| decay τ = 171 ms | HPF −3 dB at 1.0 Hz → τ = 159 ms |
| 188.0 ± 0.2 ms of zeros from a manual pulse | tRTV = 8960/fs = **186.7 ms** |
| 183.4 ms of zeros now, every stream start | the same tRTV, seen from inside the stream |

## The transient was an mboxfw regression

`FINDING_196` concluded *"the capture start-up thump is original Mbox 1
behaviour, not an mboxfw regression"* — explicitly flagged as an RE inference
that had never been checked against hardware. Stock's own code contradicts it.
That sentence licensed two days of treating the transient as normal.

## Eight builds to rediscover a write we already had written down

`FINDING_197` recorded on day one that `streaming_set_rate()` does an OR onto
an already-set bit, and read it as *why the transient persists* rather than as
*a missing stock write*. `FINDING_bringup_waveform.md` had "stock establishes a
running clock during bring-up, mboxfw waits for the host to ask" under a
heading reading **Not yet assessed**. The answer was in the repo before the
first build.

Every intermediate theory — execution context, clock dwell, time since boot,
data flow, ISR preemption — was a property of the codec inferred from the
outside, and each one was wrong. What settled it was reading the decompiled
stock function and a part number off the board.

## Remaining cost, and the one open choice

Every stream start recalibrates, because the host's `SET_CUR(48000)` re-runs
`streaming_set_rate()`, so every take opens with ~183 ms of digital silence.
**That is what stock does** — Rev 20 0x03BA calls the same 0x0728 on every
stream open — so it is now stock behaviour, and it predicts stock has the same
silence.

It could be made conditional: the bracket exists to protect the ADC across a
clock disturbance, so when the requested rate equals the running one and no
clock register would change, neither edge is needed. That would confine the
183 ms to boot and to genuine rate changes. It is a deliberate divergence from
stock and belongs in `tools/rev20_diff_justifications.md` if taken.

## 0x004B: the calibration made conditional, with a regression suite

Stock recalibrates on every stream open. The bracket exists to hold the ADC in
reset across a **clock disturbance**, and when the host asks for the rate
already running -- which `snd-usb-audio` does at every stream open -- no clock
register changes value. So 0x004B guards the reprogramming on what was actually
PROGRAMMED (`clock_programmed`, 0 until the first call) rather than on what was
requested.

Deliberate divergence from stock. No row was added to
`tools/rev20_diff_justifications.md`: that table is keyed by SFR address and
write pattern, and this changes *when* writes happen rather than which, so a
row there would be a wrong row -- which CLAUDE.md rates worse than none.

**Measured on unit B, build 0x004B:**

| test | result |
|---|---|
| three consecutive 48 kHz captures | **no zero run, no transient**, flat −103.6 / −104.8 dBFS from sample 0 |
| rate change to 44.1 kHz | one 183.56 ms calibration; ACG reads **11289.4** MCLK/frame against a nominal 11289.6 |
| 44.1 kHz repeated | **no** calibration |
| back to 48 kHz | one 183.33 ms calibration; ACG reads **12287.9** against 12288.0 |
| clock mode 1 (S/PDIF slave) then back to mode 3 | both apply and read back correctly |
| tone from unit A into unit B over the cross-wired loop | **−29.8 dBFS steady** across 2 s, other channel at the floor |
| preflight | 37/37 |

The ACG figures are the load-bearing ones: they prove the clock really is
reprogrammed on a genuine rate change and not merely skipped everywhere.

So the shipped behaviour is now: calibration once at boot, and again only on a
real rate or clock-mode change. Every take opens clean, with no silence and no
transient.

## The analog reference takes ~16 s to settle, which is why boot-only cannot work

Measured on unit B, build 0x004B used as the instrument: its calibration runs
ONLY on a clock-mode change, so alternating the capture rate forces exactly one
calibration per rung, and a bad calibration leaves a STANDING offset -- so every
rung is independently readable rather than depending on earlier ones failing.

One cold boot:

| calibrated at | DC left standing | level |
|---|---|---|
| t = 1 s | +0.00304 | −50.3 dBFS |
| t = 2 s | +0.00104 | −59.6 |
| t = 4 s | +0.00029 | −70.8 |
| t = 8 s | +0.00003 | −89.4 |
| t = 16 s | +0.00000 | **−100.5** |
| t = 32 s | −0.00002 | −93.9 |

Roughly 10 dB better per doubling of elapsed time, reaching the noise floor
somewhere between 8 and 16 seconds. That is the VREFL/VREFR node charging --
each sits on a 10 uF electrolytic per the datasheet's own application circuit.

**So a boot-only calibration is not viable.** It would have to wait ~16 s, and
a user can plug the unit in and hit record in five. A capture opened before the
delay elapsed would carry a −70 dBFS DC offset for the whole power-up with no
way to clear it short of a power cycle.

This is also, retrospectively, why stock recalibrates on every stream open: it
never has to know this number, and it is right whenever the user records.

## Candidate refinement, untested

Calibrate at every stream CLOSE rather than every open, plus once at the first
open of a power-up. A take would then open already calibrated -- by the close of
the previous one -- so only the very first take of a power-up pays the 183 ms.

Not attempted. It needs its own cold-boot proof, and the lesson of 0x004B is
that this class of change passes every warm test and fails the only one that
counts.

## 0x23.2 IS the AK5383's RST, proved on the wire (2026-08-09)

Everything above rests on one inference: that 0x23.2 drives the AK5383's RST
pin. The plan was to prove it with a multimeter -- buzz the 4094 output against
pin 10. That was the wrong instrument. A continuity beep proves a net exists; it
does not prove the chip on the far end treats the edge as RST.

The wire proves the stronger claim, because the datasheet's intervals are
specified in SAMPLE CLOCKS, not in time:

    tRTV  (RST rising -> valid SDATA)  = 8960 / fs
    tRCF  (calibration cycle)          = 8704 / fs

mboxfw advertises 44100 as well as 48000 (`mboxfw/src/descriptors.c:291`), and
every capture on the 0x004A fix opens with a run of exact digital zeros. So
count that run in FRAMES at both rates. A counter clocked at fs inside the part
gives the same frame count and a different wall time; anything analog, or
anything clocked off MCLK independently of fs, gives the reverse.

Unit A (`RK10874600Q`, port 2-1.4, build 0x004A), three takes at each rate,
`tools/leadzero.py`:

| fs | lead zeros (frames) | = ms |
|---|---|---|
| 48000 | 8800, 8800, 8787 | 183.33, 183.33, 183.06 |
| 44100 | 8813, 8807, 8806 | 199.84, 199.71, 199.68 |

Constant in frames to 0.3 % across the pair. The wall times differ by 9.0 %,
against an fs ratio of 8.84 % -- the interval tracks the sample clock and not
the clock on the bus. **The silence is generated by a counter clocked at fs
inside a chip on the far end of 0x23.2.**

### Which constant, and why it can only be tRTV

The measured run can only ever be SHORTER than the true interval: the host does
not begin capturing at the instant of the RST edge, so some frames of the
silence are lost off the front. Nothing emits zeros after the ADC goes valid, so
the run cannot run long. That makes the largest observation a hard lower bound
on the constant -- and 8813 frames were seen at 44.1 kHz, which **excludes tRCF
= 8704 outright**.

Fitting the head loss to the remaining candidate:

| assumed constant | implied head loss @48k | @44.1k |
|---|---|---|
| 8704 | −1.91 ms | −2.37 ms | (impossible: negative) |
| **8960** | **+3.42 ms** | **+3.43 ms** |

Two independent rates agreeing on 3.4 ms of URB-submission latency to within
0.01 ms, with no free parameter beyond that single constant. The interval is
tRTV = 8960/fs, the AK5383's specified RST-rising-to-valid-SDATA time.

So 0x23.2 is RST on the AK5383, established without touching the board. The
whole account in this document -- the 183 ms being the calibration the part
performs on the rising edge, and the transient being the un-calibrated DC that
ran for the life of the project because mboxfw only ever ported stock's SETB --
is now measurement rather than inference.

Correction carried in from `FINDING_the_parts_are_identified.md`: that document
called the pin **PDN**. The AK5383 has no PDN pin. It is **RST, pin 10**.

### Bonus: 44.1 kHz was never validated with the fix until now

The floors above -- −103.6, −103.1, −101.1 dBFS at 44.1 kHz and −98.8, −99.0,
−99.3 at 48 kHz, DC at zero to five decimal places on all six -- are the first
confirmation that 0x004A is clean at the second rate. Every capture behind the
0x004A result to date was at 48 kHz.

## The datasheet, read at last (2026-08-09)

`reference/AK5383_datasheet_M0049-E-03.txt` (AKM M0049-E-03, 2000/4), text of
the Digi-Key PDF. Everything below is quoted, not inferred.

### The latched-offset model is CONFIRMED, verbatim

> **Offset Calibration** -- When RST pin goes to "L", the digital section is
> powered-down. Upon returning "H", an offset calibration cycle is started. An
> offset calibration cycle should always be initiated after power-up.
>
> During the offset calibration cycle, the digital section of the part measures
> and **stores the values of calibration input of each channel in registers. The
> calibration input value is subtracted from all future outputs.** The
> calibration input may be obtained from either the analog input pins (AIN+/-)
> or the VCOM pins depending on the state of the ZCAL pin.

Latched into a register, subtracted statically thereafter. That is why a bad
calibration is *permanent for the power-up* rather than self-healing, which is
the entire justification for recalibrating at every stream open, and the reason
0x004B's cold-boot offset could never decay away.

### The timing constants are exactly the ones the wire proof used

> RST rising to CAL falling (Note 11)  tRCF  8704  1/fs
> RST rising to SDATA Valid (Note 11)  tRTV  8960  1/fs
>
> Note 11. The number of the LRCK rising edges after RST brought high at
> DFS="L". ... When DFS="H", tRCF=17408 and tRTV=17920.

Two things fall out. The units are LRCK edges -- sample clocks -- which is what
made the rate sweep decisive. And **DFS = "L"** on this board: had the part been
strapped for double speed we would have measured ~17920 frames, not ~8800.

### My mechanism was wrong in one part, and I am retracting a piece of evidence

I had it that the part "uses the high-pass filter to derive an offset value
during calibration, latches it, and subtracts it statically". The latching half
is right; the derivation half is not. They are **two independent blocks**:

- the calibration reference is VCOM or the AIN pins, selected by ZCAL. The HPF
  is not involved.
- the HPF is a separate, continuously-running 1 Hz digital filter gated by HPFE.

So the near-match between our measured tau = 171 ms and a 1 Hz corner's 159 ms
was **not** evidence for the calibration mechanism, and I should not have used
it as such. That much stands.

It does **not** follow that the HPF is uninvolved in the decay, which is where
this went next and got it wrong. Measured 2026-08-10: the decay is the HPF, and
tau = 176 ms by direct measurement against the 171 ms here.
`FINDING_the_171ms_decay_is_the_ADC_high_pass.md`.

### ~~HPFE is LOW on this board, and it does not matter~~ — WRONG, retracted 2026-08-10

The spec separates the two cases sharply:

> Offset Error, after calibration, HPF=OFF   ±200 typ, ±1000 max  LSB24
> Offset Error, after calibration, HPF=ON    ±1                   LSB24

Measured post-calibration DC on the six 0x004A captures above, in LSB24:

| fs | DC left | DC right |
|---|---|---|
| 48000 | +14.79, −6.71, −6.91 | +4.80, +5.79, +7.34 |
| 44100 | +1.53, +3.07, −23.72 | −7.97, +18.32, −6.29 |

The noise floor is sd ~90 LSB over ~87000 frames, so the standard error on each
mean is ~0.3 LSB: these are resolved, and they are consistently 5-24 LSB rather
than the ±1 the HPF=ON case would give. **HPFE is tied low.** The values are
comfortably inside the HPF=OFF ±1000 max and better than its ±200 typ, which is
what ZCAL="L" (calibrating against VCOM rather than the input pins) should give.

~~This closes the HPFE question as not worth chasing.~~

**RETRACTED 2026-08-10. HPFE is HIGH; the filter is already enabled.** The
reasoning above is a non-sequitur: a 1 Hz high-pass barely attenuates sub-1-Hz
content, so a bench source with slow drift leaves exactly this residual, and the
±1 LSB24 figure describes the converter's own offset with a clean input. The
measurement never bore on the pin.

What settles it is the sample-rate lever again. A DC step injected with
`TLM_REQ_SET_MUX` -- which never touches 0x23.2, so no calibration confounds it
-- recovers with a zero-crossing at 176.44 ms at 48 kHz and 191.53 ms at
44.1 kHz. Ratio 1.0855 against the fs ratio 1.0884, a 0.3 % match, so the pole is
clocked at fs and is inside the converter. 112 steps per rate.
`FINDING_the_171ms_decay_is_the_ADC_high_pass.md`.

### And it explains the scale of the 0x004B failure

0x004B's cold boot left −43 dBFS, which is 0.007 FS = ~58700 LSB24 -- three
orders of magnitude outside the ±1000 max. So that was not a calibration that
landed badly within spec. Against a 2.45 Vpp full scale it is ~17 mV, inside the
part's

> Offset Calibration Range (HPF=OFF)  ±50 mV

so the cycle completed and faithfully stored what it was shown: a VCOM node still
17 mV from its final value, 1-2 s into a ~16 s charge. The part did its job. The
firmware asked it the question too early.
