# Rev 22 has a playback frame-alignment watchdog. Rev 20 has none.

Found 2026-07-29 while working `WHAT_REMAINS_UNKNOWN.md` §3b, which listed
`DMABCNT0L/H` as "read by stock, never by mboxfw, untraced". Tracing it produced
something larger than a register meaning.

## Correction to the premise

`WHAT_REMAINS_UNKNOWN.md` said "stock reads them", which implied both images.
A byte scan for `90 ff eb` / `90 ff ec` says otherwise:

    rev20  DMABCNT0L: none        DMABCNT0H: none
    rev22  DMABCNT0L: 0x0D5D      DMABCNT0H: 0x0D58

**Only Rev 22 reads these registers.** That makes this a Rev 20 -> Rev 22
*addition*, not a stock-wide behaviour mboxfw was missing.

## Rev 22's SOF handler

Rev 20's VECINT table entry for SOF (source 0x14) is at `0x0C93 + 20*2 = 0x0CBB`
and points to `0x1034`, a bare `RET`. Rev 22's table is at `0x0C7D`, so the same
entry is at `0x0CA5` -- which is exactly the XREF Ghidra records for `0x0D58`.
Rev 22 has a real handler where Rev 20 has a no-op:

    0d58  MOV DPTR,#0xFFEC ; MOVX A,@DPTR ; MOV R6,A    ; DMABCNT0H
    0d5d  MOV DPTR,#0xFFEB ; MOVX A,@DPTR               ; DMABCNT0L
    0d61  MOV R4,#0 ; ADD A,#0 ; MOV R7,A               ; widen to R4:R6:R7
    0d66  MOV A,R4 ; ADDC A,R6 ; MOV R6,A
    0d69  MOV A,R7 ; XRL A,0x1C ; JNZ 0x0D71
    0d6e  MOV A,R6 ; XRL A,0x1B
    0d71  JZ 0x0D9D                       ; unchanged since last SOF -> return
    0d73  MOV 0x1B,R6 ; MOV 0x1C,R7       ; save the new count
    0d77  MOV R5,#0x6 ; LCALL 0x0B7F      ; divide by 6, remainder -> R5
    0d7c  MOV A,R5 ; ORL A,R4 ; JZ 0x0D9D ; remainder 0 -> aligned -> return
    0d80  DMACTL0  (0xFFE8) &= 0x7F       ; ---- stop the playback DMA
    0d87  OEPDCNTX2 (0xFF9B) = 0
    0d8c  OEPDCNTY2 (0xFF9F) = 0
    0d90  OEPCNF2   (0xFF98) = 0xC5       ; re-enable the endpoint
    0d96  DMACTL0  (0xFFE8) |= 0x80       ; ---- restart the DMA
    0d9d  RET

`0x0B7F` is a divide-with-remainder helper: 8-bit `DIV AB` fast path when the
dividend fits in R7, shift-subtract long division otherwise. Either way R5 holds
the remainder on return (`CLR A / XCH A,R6 / MOV R5,A` at 0x0BAA).

## What the register is, from the datasheet

Not inferred. TAS1020B datasheet §6.5.2.4 and §6.5.2.5, DMABCNT0L/H:

> "This register shows the buffer content (bytes) for an ISO OUT endpoint. This
> register is updated every SOF and is stable for the following USB frame, during
> which the MCU can read it **to implement USB audio synchronization**."

and §2.2.7:

> "For isochronous OUT transactions, the count in the register represents the
> number of bytes being transferred from the OUT endpoint buffer to the C-port
> during the current USB frame. A new count is derived at each USB SOF event, and
> is the value of the write pointer address setting minus the read pointer
> address setting at the time of the USB SOF event."

So it is the playback circular buffer's **fill level**, and TI names USB audio
synchronisation as its purpose. 6 is one stereo 24-bit sample frame (2 ch x 3 B),
which is also the `BPS` field value 5 that both firmwares write into `OEPCNF2`
(0xC5).

## What it therefore does

Rev 22 checks, once per USB frame, whether the playback buffer holds a whole
number of sample frames. If it does not, the DMA would from then on emit bytes
offset within the frame -- samples split across frame boundaries and channels
swapped -- so Rev 22 tears the playback path down and restarts it, which resets
the circular buffer's read/write pointers to a known-aligned state.

The change-detection against `RAM[0x1B]:RAM[0x1C]` is an optimisation: if the
fill level has not moved, the DMA is not consuming, and there is nothing to
realign.

## Why the two IRAM bytes matter as corroboration

`IRAM_OVERLAY_ANNOTATION.md` records that Rev 22 moved its EP0 pointer from
`0x1B:0x1C` to `0x1D:0x1E`, and lists that as one of the low-IRAM differences
between the images without a reason. This is the reason: Rev 22 needed a
two-byte shadow for the saved count and took 0x1B:0x1C for it. An independent
structural fact lands exactly where this reading predicts.

## Why this is the most consequential item in the inventory

The project's founding hardware fact is that the unit on the bench runs Rev 20,
and that **Rev 20 is buggy and needs a v22 flash before playback works**. This is
a playback-only fix, present in Rev 22, absent from Rev 20. It is the strongest
candidate yet identified for what that bug actually is.

mboxfw had Rev 20's behaviour: `streaming_sof()` was an empty function. Its
comment justified that with "Rev 20's Timer 0 ISR at 0x101E is a 9-byte
set-a-flag stub, so no per-SOF work is needed" -- a category error, since Timer 0
and SOF are different interrupts, and the timer stub said nothing about SOF.

## Ported

`mboxfw/src/streaming.c` `streaming_sof()`, with the divisor derived from
`AUDIO_NUM_CHANNELS * AUDIO_SUBFRAME_BYTES` rather than hardcoded 6, and every
store carrying its Rev 22 address.

One deliberate divergence, stated in the code: the port is gated on
`playback_running`. Rev 22 runs the handler unconditionally, so a stale
misaligned count while playback is stopped would make it write `OEPCNF2 = 0xC5`
and set `DMAEN` -- enabling playback the host never asked for. Rev 22 gets away
with it because the count settles to zero and stops changing; that is not a
reason to copy it.

Telemetry block 7 byte 5 counts resyncs (`tlm_playback_resyncs`), so the
watchdog firing is observable rather than silent.

## Not yet verified on hardware

Everything above is static. What a flash would show: whether the watchdog ever
fires in normal playback (block 7 byte 5), and whether playback works at all on
mboxfw now that it has Rev 22's behaviour instead of Rev 20's. Neither has been
tested -- mboxfw playback has never been confirmed working, so there is no
before/after baseline yet.

## Gate gap this exposed, also closed

`tools/check_citation_targets.py` validated **only Rev 20** citations; its regex
matched the literal string "Rev 20". The five new citations here are Rev 22 and
were unverified by any gate -- the precise condition that tool exists to
eliminate. Made rev-aware; it now checks 55 Rev 20 and 7 Rev 22 citations,
including two pre-existing Rev 22 citations that had been silently skipped.
Mutation-tested against a far-wrong address and against an address inside a data
table, and it correctly reports Rev 22 access sites rather than Rev 20's.

---

## VERIFIED ON HARDWARE 2026-08-05 — build 0x0023, unit B

First confirmation that **mboxfw playback works at all.** B played, A captured
over the analog cross-link (`B out1 -> A src1`, BENCH_WIRING.md).

### 1. Does playback work?

Yes, and cleanly. Steady window t=2..17 s, 720000 frames:

```
peak 382267   rms 269882   -29.9 dBFS
crest 1.4164                        (pure sine = 1.4142)
zero crossings -> 1000.00 Hz        (exactly)
THD 0.03%                           (2nd harmonic -71.3 dB, 3rd -80.5 dB)
glitch events, 2% linear-prediction residual: 0 in 15 s
envelope: 269885 +/- 5 rms per 0.5 s block for the whole run
```

-29.9 dBFS matches the ~-28 dBFS this analog path measured independently in
BENCH_WIRING.md, so the level is the cable, not the firmware.

**A measurement error worth recording.** The first pass reported crest 4.407
and 943.8 Hz and looked like badly distorted audio. Both numbers were
artifacts: the capture was started 1 s before the player, and a startup
transient at t=0 (peak 1187758, ~4x the tone) dominated whole-file statistics
and suppressed the zero-crossing rate. Whole-file aggregates over a window
that includes transients describe the transient, not the signal. The envelope
profile is what exposed it -- the "quiet" blocks began at t=0.2 s, before
playback had even started, which is impossible for a playback defect.

### 2. Does the watchdog fire?

**No. `pb_resyncs` stayed 0** at baseline, at t=3/8/13 s mid-stream, and after
teardown.

That number is only meaningful with proof the code ran, because "never needed"
and "never ran" produce the same 0. Two independent observables, both sampled
mid-stream:

```
sof_count:  303 (idle) -> 2921 -> 7856 -> 12787      ~1000/s: the SOF ISR runs
mux word:   0xED (idle) -> 0x6D (playing)            bit 7 = playback_running
```

`mux word` bit 7 is driven by `panel_update_streaming()` from exactly the
`playback_running` flag that `streaming_sof()` early-returns on. ISR running +
gate open => the watchdog body executes on every SOF. So the count is a real
"the buffer was always frame-aligned", not silence from dead code.

Nothing upstream misaligns the playback buffer under mboxfw. The watchdog is
insurance that has not yet been called on.

### 3. The deliberate divergence (gated on `playback_running`)

Nothing is lost. Rev 22 runs the check unconditionally; mboxfw skips it while
playback is stopped. `sof_count` climbs while idle (303 before any stream), so
the ISR is live in exactly the window where the two firmwares differ -- and in
that window DMAEN is clear and the host has no buffer, so the only thing Rev
22's version can do with a stale count is write `OEPCNF2 = 0xC5` and set
DMAEN, enabling playback nobody asked for. The gate exists to prevent that,
and the idle reading confirms the gate is the only thing standing between us
and it.

### Not established

That the watchdog *works* -- only that it is reachable, correctly gated, and
never triggered in 17 s of aligned playback. Forcing a genuine misalignment
would need a deliberately mis-sized host transfer; nothing here exercises the
resync path itself. Rev 20's lack of a SOF handler remains the leading
candidate for the "needs a v22 flash before playback works" bug, but mboxfw
playback working on the first hardware test means this port has no measured
before/after to attribute it to.
