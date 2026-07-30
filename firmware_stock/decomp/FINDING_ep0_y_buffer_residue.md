# EP0's Y buffer counts are cleared by stock and not by mboxfw

Found 2026-07-29 by diffing every XDATA address either stock image touches
against every SFR assignment in mboxfw, in **both** directions. Two addresses
came back as "stock writes it, mboxfw never touches it" in the endpoint region.

## Stock's EP0 buffer setup, Rev 20 0x0970-0x0995

    0970  OEPBBAX0  (0xFFA9) = 0x42     OUT EP0 X buffer base
    0976  IEPBBAX0  (0xFF69) = 0x43     IN  EP0 X buffer base  (A+1)
    097b  OEPDCNTX0 (0xFFAB) = 0        clear OUT X count
    0980  IEPDCNTX0 (0xFF6B) = 0        clear IN  X count
    0984  OEPDCNTY0 (0xFFAF) = 0        clear OUT Y count   <-- mboxfw omits
    0988  IEPDCNTY0 (0xFF6F) = 0        clear IN  Y count   <-- mboxfw omits
    098c  OEPBSIZ0  (0xFFAA) = 1        8-byte buffer
    0991  IEPBSIZ0  (0xFF6A) = 1        8-byte buffer
    0995  OEPCNF0   (0xFFA8) = ...

The base encoding is `(address - 0xF800) >> 3`: 0x42 -> 0xFA10 and 0x43 ->
0xFA18, which agrees with the "base 0xFA10 in Rev 20" note already in
`mboxfw/src/usb.c`. Buffer size 1 -> 8 bytes, matching EP0's 8-byte packets.

## Why the Y counts matter even though Y is not used

Neither firmware ever writes the Y buffer **base** registers (IEPBBAY0 at
0xFF6D, OEPBBAY0 at 0xFFAD) -- a byte scan finds no site in either image. So
the Y buffers are never given an address and are not in use for EP0.

That makes stock's two Y-count writes **defensive clears, not configuration** --
and the thing they defend against is real. **The boot ROM runs DFU over EP0**
before handing control to the application. If it leaves a non-zero count in
IEPDCNTY0 or OEPDCNTY0, the UBM can see a Y buffer that looks armed and ready
when the application has never filled it. Stock zeroes both before enabling the
endpoint. mboxfw does not.

## Why this is a candidate for the measured EP0 loss

The recorded symptom is that **mboxfw drops ~12% of EP0 IN packets past the
second, measured, with a geometric distribution**. A stale armed Y buffer
produces exactly that shape of failure: the transfer works until the UBM
alternates to a buffer the firmware is not managing, and whether it does so
depends on timing, which makes the loss probabilistic rather than every-other-
packet.

This is a candidate, not a diagnosis. What is established:

  * Stock clears both Y counts at init; mboxfw clears neither.
  * The boot ROM uses EP0 immediately before the application starts.
  * Neither firmware configures a Y base, so a non-zero Y count is meaningless
    to the firmware but not necessarily to the hardware.

What is not established: that the boot ROM actually leaves those counts
non-zero.

## Status: fixed, and now measurable — 2026-07-29

Both clears are in, in `usb_ep0_setup()` (split out of `usb_init()` so the
resume path can re-run exactly the same routine, as stock does):

    IEPDCNTY0 = 0;    /* 0xFF6F -- Rev 20 0x0988, Rev 22 0x08A9 */
    OEPDCNTY0 = 0;    /* 0xFFAF -- Rev 20 0x0984, Rev 22 0x08A5 */

The open half is now instrumented. **Telemetry block 7** reports both Y counts
live, plus both X counts:

    byte 0  IEPDCNTY0     byte 2  IEPDCNTX0
    byte 1  OEPDCNTY0     byte 3  OEPDCNTX0

Note what this can and cannot answer now that the clears are in place. It
cannot recover the boot-ROM handoff value -- that is overwritten by our own
clear before any host can ask. What it does answer is the version of the
question that survives the fix: **whether the UBM puts anything back into the Y
counts while the firmware runs.** A non-zero read means the hardware is
alternating into a buffer the firmware does not manage, which would confirm the
mechanism; a persistent zero across a session that still loses packets rules
the mechanism out and sends the ~12% loss back to the interrupt-ordering
explanation already documented in `usb.c`'s VEC_IEP0 case.

To recover the handoff value itself would take a build that samples both
registers into telemetry variables BEFORE clearing them. That is a one-line
change if the block-7 reads come back ambiguous; it is not done now because it
would mean shipping an image that deliberately keeps a suspected defect live.

## A naming leftover in regs.h — fixed

`regs.h` called 0xFF6B **IEPBCTX0** and 0xFFAB **OEPBCTX0**. On the endpoint
grid (`IEPCNFn = 0xFF68 - n*8`, `+3` = DCNTX, `+7` = DCNTY) those are
**IEPDCNTX0** and **OEPDCNTX0**, which is also what TI's `Reg_stc1.h` calls
them (lines 139 and 203). This was the same invented-name class already
corrected for EP1 and EP2 -- `IEPBCTX1/Y1` and `OEPBCTX2/Y2` were renamed to
`IEPDCNTX1/Y1` and `OEPDCNTX2/Y2` -- and the EP0 pair was missed in that pass.
The SFR-name gate did not catch it because it checks internal consistency, not
agreement with TI's names.

Renamed throughout `mboxfw`. `safety_net` and the historical docs still carry
the old spelling; that is deliberate, since those record what was written at
the time, and the addresses were never in doubt.

## Also: DMABCNT0 is read by stock and never by mboxfw

    0xFFEB DMABCNT0L   stock: read
    0xFFEC DMABCNT0H   stock: read

These are the read-only playback DMA byte counters, updated every SOF. Stock
reads them; mboxfw never does. Unexamined -- worth knowing what stock uses the
value for, since a per-frame byte count is what an adaptive-rate scheme would
need.
