# The request-code dispatch table, and the small const tables

Both were found by `tools/byte_census.py`, which partitions the image into
instructions / named data / 0xFF padding and refuses to let anything else pass.
Neither had been decoded before 2026-07-29.

## Rev 20 0x011F (37 bytes) — a Keil `?C?CASE` switch table

The mechanism is visible in the three instructions before it:

    0118  MOV DPTR,#0xFF29     ; SETPACK byte = the request code
    011b  MOVX A,@DPTR
    011c  LCALL 0x0F70         ; the ?C?CASE helper
    011f  <table follows inline>

`0x0F70` reads the table from its own return address — the Keil `?C?CASE`
convention — which is why the table sits immediately after the `LCALL` rather
than in a data segment.

Eleven entries, each **(16-bit big-endian handler address, 1-byte case
value)**, then a terminator and a default:

    0x011F  handler 0x022F  code 0x00
    0x0122  handler 0x0144  code 0x01
    0x0125  handler 0x029C  code 0x03
    0x0128  handler 0x024D  code 0x05
    0x012B  handler 0x0173  code 0x06
    0x012E  handler 0x0299  code 0x07
    0x0131  handler 0x015D  code 0x08
    0x0134  handler 0x025B  code 0x09
    0x0137  handler 0x01F1  code 0x0A
    0x013A  handler 0x029F  code 0x0B
    0x013D  handler 0x02E7  code 0x0C
    0x0140  00 00           terminator
    0x0142  02 EA           default handler 0x02EA

11 x 3 + 2 + 2 = 37 bytes, exactly the run length. The default target 0x02EA is
`LCALL 0x1009 / RET`, the error path.

The field order and endianness are not a guess: Ghidra independently records
cross-references from 0x0125 to 0x029C, from 0x012E to 0x0299 and from 0x013D
to 0x02E7, and only this decode reproduces all three. Reading the entries as
3-byte `LJMP` instructions instead yields targets like 0x9C03, outside the
8174-byte image.

### CORRECTION: these are USB bRequest codes, not byte-0x0A work codes

An earlier version of this document said the case values "are the values written
to IRAM byte 0x0A" and that 0x0B/0x0C were the P3.1 insert/remove events. **Both
claims were wrong.** I saw that byte 0x0A also takes the values 0x0B and 0x0C and
treated the coincidence as identity, without checking what the dispatcher keys
on.

The key is at 0x0118: `MOV DPTR,#0xFF29` — and 0xFF29 is **SETPACK_BREQ**, the
USB setup packet's bRequest byte. So the case values are USB **standard request
codes**, and they match the standard set exactly:

    0x00 GET_STATUS         -> 0x022F      0x07 SET_DESCRIPTOR   -> 0x0299
    0x01 CLEAR_FEATURE      -> 0x0144      0x08 GET_CONFIGURATION-> 0x015D
    0x03 SET_FEATURE        -> 0x029C      0x09 SET_CONFIGURATION-> 0x025B
    0x05 SET_ADDRESS        -> 0x024D      0x0A GET_INTERFACE    -> 0x01F1
    0x06 GET_DESCRIPTOR     -> 0x0173      0x0B SET_INTERFACE    -> 0x029F
                                           0x0C SYNCH_FRAME      -> 0x02E7

0x02 and 0x04 are absent because they are **reserved in the USB specification** —
which is the confirmation that this is the standard-request table and not an
arbitrary numbering. The default handler 0x02EA covers unrecognised requests.

That also explains something that should have tipped me off immediately: handler
0x029F begins `MOV DPTR,#0xFF2C` (SETPACK_WIDX_L), which is exactly what
SET_INTERFACE needs and makes no sense for a panel event.

### The byte-0x0A work-code dispatcher is a SEPARATE table at 0x0300

    02ee  MOV A,0x0A
    02f0  DEC A
    02f1  CJNE A,#0x0E,0x02F4     ; range-check 1..14
    02f4  JC 0x02F9
    02f9  MOV DPTR,#0x0300
    02fc  MOV R0,A / ADD A,R0 / ADD A,R0   ; A = index * 3
    02ff  JMP @A+DPTR

A plain jump table of 3-byte LJMPs, index = code - 1:

    code 0x01 -> 0x032A   0x06 -> 0x0478   0x0B -> 0x04C4
    code 0x02 -> 0x0386   0x07 -> 0x0480   0x0C -> 0x0511
    code 0x03 -> 0x03FD   0x08 -> 0x049A   0x0D -> 0x0518
    code 0x04 -> 0x0454   0x09 -> 0x04B4   0x0E -> 0x0526
    code 0x05 -> 0x0466   0x0A -> 0x04BC   0x0F -> 0x1001 (LCALL, not LJMP)

So the P3.1 events raised at 0x0AF6 (code 0x0B) and 0x0B07 (code 0x0C) dispatch
to **0x04C4 and 0x0511**, not to 0x029F and 0x02E7.

## Consequence for the annotation ledger

These eleven handler addresses, and the boot-ROM delegate at 0x2F00, are
reached by `JMP @A+DPTR` with the address coming from *data*. The ledger's call
denominator was built from `LCALL`/`LJMP` targets in decoded instructions, so
none of them were ever counted. "Call targets 71/71" meant 100% of
instruction-reachable targets — a narrower claim than it appeared. The
denominator now includes table-driven targets.

## Rev 20 0x0A48 / Rev 22 0x0969 (8 bytes) — bit-mask table

    01 02 04 08 10 20 40 80

Powers of two, consumed by the `ORL A,@R0` at Rev 20 0x0A42 inside the
`?C_INITSEG` interpreter: the table converts a bit index into a mask so the
initialiser can set individual bits.

## Rev 22 block addresses

The same five const blocks appear in Rev 22 at shifted addresses:

    0x057D  402  USB descriptor block
    0x0969    8  bit-mask table
    0x0C7D   74  VECINT dispatch table
    0x0FBA   36  ?C_INITSEG   (Rev 20: 40 bytes)

Ghidra mis-decodes three bytes inside Rev 22's descriptor block as
instructions, which is why the census sees it as four runs rather than one.
That is a listing artefact, not a difference between the images: the span
0x057D–0x070E is 402 bytes, the same length as Rev 20's block at 0x0596.

## Rev 22 uses a different idiom for the same dispatch

Rev 22 does NOT have a `?C?CASE` table. A signature search for the same case
values (00,01,03,05,06,07,08,09,0A,0B,0C at stride 3) finds a match in Rev 20
only. Rev 22 instead computes an index and jumps through a table of `LJMP`
instructions:

    011c  ADD A,R0
    011d  JMP @A+DPTR
    011e  LJMP 0x022F
    0121  LJMP 0x0145
    0124  LJMP 0x02EF
    ...

Same handlers, different generated dispatch. Because these are real
instructions, Ghidra decodes them and the instruction scan already covered
their targets — which is why Rev 22 showed no unaccounted run here and needed
no table entry.

**A caution recorded because it nearly became an error.** Assuming symmetry and
decoding Rev 22 at 0x011E with Rev 20's `(address, case)` layout produced
handlers 0x0201/0x0202 repeated and case values 0x2F, 0x45, 0xEF, 0x9B — the
tell was that the case values were not the known set. Note also that reading
Rev 20's table one byte earlier, as 3-byte `LJMP` entries, yields the *same*
eleven handler addresses, because that just re-reads the same big-endian
address fields at a different offset. The two encodings are only
distinguishable by the third byte and by the surrounding code, so the
`LCALL 0x0F70` above is what settles it.

---

# ~~What P3.1 actually is: S/PDIF / external clock presence~~

> **CORRECTED 2026-08-04 — P3.1 is TXD; both handlers are unreachable.**
> The decode of the two handlers below is correct and still the reference for
> what they contain. The *conclusion this section draws about P3.1* is not.
>
> P3.1 is the serial port transmit pin (TI `Utils.SRC:67`, `TXD BIT 0B0H.1`);
> neither image configures `SCON`/`SBUF`, and the pin rests high. Measured with
> two units on crossed S/PDIF: pulling one unit's S/PDIF IN leaves P3.1 at 1,
> control unit unchanged. Code 0x0B needs P3.1 == 0, and only 0x0B arms the
> latch 0x0C needs, so **neither work code can ever be posted on this board.**
>
> Consequence for #145, stated below as "this resolves the mechanism": the
> mechanism is real in the bytes and dead in the hardware. There is nothing to
> port. The host-driven path in `decomp/FINDING_host_control_protocol.md` is the
> only route to slaving. See `decomp/FINDING_p31_is_txd.md`.

Decoding the two handlers the 0x0300 table points at settles the question left
open in `IRAM_BITS_ANNOTATION.md`.

**Code 0x0B — P3.1 asserted — handler 0x04C4:**

    04c4  JB   0x25.5,0x04CA      ; already in this state? skip the init
    04c7  LCALL 0x080B
    04ca  SETB 0x25.5
    04cc  MOV  R7,#3
    04ce  LCALL 0x0728            ; audio_clock_mode_apply(MODE 3)
    04d1  0x2C = 0x04 ; 0x2D = 0x41
    04d7  MOV R5,0x2D / MOV R7,0x2C
    04db  LCALL 0x0C45            ; CS8427 write: CLOCKSOURCE = 0x41 (RUN set)

**Code 0x0C — P3.1 released — handler 0x0511:**

    0511  MOV  R7,#1
    0513  LCALL 0x0728            ; audio_clock_mode_apply(MODE 1)
    0516  SJMP 0x0564

P3.1 asserted switches the part to **clock mode 3 and starts the CS8427 with
CLOCKSOURCE = 0x41**; P3.1 released reverts to **mode 1**. Mode 1 sources both
master clocks from MCLKI (see the ACGCTL decode in
`FINDING_capture_8frame_artifact.md`), i.e. the internal reference.

**So P3.1 is the S/PDIF / external-clock presence input, not a TRS jack
detect.** Signal present -> slave the audio clock to the incoming S/PDIF stream
via the CS8427; signal gone -> fall back to the internal clock. The two
"insert/remove" edges I described are lock-acquired and lock-lost.

This also resolves the mechanism behind task #145 (S/PDIF clock slaving, Rev 20
modes 3 and 5): the trigger is P3.1, the switch is `audio_clock_mode_apply(3)`
plus CS8427 CLOCKSOURCE = 0x41, and the revert is
`audio_clock_mode_apply(1)`.

And it gives 0x25.5 a meaning: it is the **"clock is slaved to S/PDIF" latch**,
set at 0x04CA and cleared at Rev 20 0x0395 (boot) and 0x041C. Since
`0x22.6 = !(0x25.4) && !(0x25.5)`, the panel bit 0x22.6 goes low whenever either
clock-source latch is set — consistent with a front-panel indicator that is lit
only on the internal clock, though which LED is not established here.
