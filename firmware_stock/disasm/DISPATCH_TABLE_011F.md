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

The request codes are the values written to **IRAM byte 0x0A**, the pending-work
code the main loop dispatches through 0x02EE (see `IRAM_LOW_ANNOTATION.md`).
Two of them were pinned by today's other work:

  * **0x0B** and **0x0C** are the P3.1 insert and remove events, raised at
    Rev 20 0x0AF6 and 0x0B07 (see `IRAM_BITS_ANNOTATION.md`). Their handlers
    are 0x029F and 0x02E7.
  * **0x0D** is written by the setter at 0x005B — and note it has **no entry in
    this table**, so codes above 0x0C are handled elsewhere or fall through.

Codes 0x02 and 0x04 are absent from the table entirely.

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
