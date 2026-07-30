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
