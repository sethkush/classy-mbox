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
non-zero. That is one telemetry read away -- IEPDCNTY0 and OEPDCNTY0 are not in
any telemetry block, and adding them would settle it.

The fix is two stores at init and costs nothing regardless:

    IEPDCNTY0 = 0;    /* 0xFF6F -- Rev 20 0x0988, Rev 22 0x08A9 */
    OEPDCNTY0 = 0;    /* 0xFFAF -- Rev 20 0x0984, Rev 22 0x08A5 */

## A naming leftover in regs.h

`regs.h` calls 0xFF6B **IEPBCTX0** and 0xFFAB **OEPBCTX0**. On the endpoint grid
(`IEPCNFn = 0xFF68 - n*8`, `+3` = DCNTX, `+7` = DCNTY) those are **IEPDCNTX0**
and **OEPDCNTX0**. This is the same invented-name class that was already
corrected for EP1 and EP2 -- `IEPBCTX1/Y1` and `OEPBCTX2/Y2` were renamed to
`IEPDCNTX1/Y1` and `OEPDCNTX2/Y2` -- and the EP0 pair was missed in that pass.
The SFR-name gate does not catch it because it checks internal consistency, not
agreement with TI's names.

## Also: DMABCNT0 is read by stock and never by mboxfw

    0xFFEB DMABCNT0L   stock: read
    0xFFEC DMABCNT0H   stock: read

These are the read-only playback DMA byte counters, updated every SOF. Stock
reads them; mboxfw never does. Unexamined -- worth knowing what stock uses the
value for, since a per-frame byte count is what an adaptive-rate scheme would
need.
