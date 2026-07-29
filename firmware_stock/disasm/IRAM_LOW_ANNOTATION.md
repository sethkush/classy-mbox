# IRAM 0x00–0x1E — register bank 0 and the Keil DATA segment

Evidence for every claim here is in `LEDGER_WORKLIST.txt`, which lists every
instruction in either image that touches each address. Nothing below is
inferred from mboxfw source; mboxfw's names have been wrong before.

## The memory layout, which explains the whole range at once

Keil C51 places register bank 0 at IRAM 0x00–0x07, reserves any other bank the
program selects, and fills the gaps with its `DATA` segment.

An earlier draft of this document claimed neither image switches banks. That
was wrong, and a byte scan for `MOV PSW,#imm` (opcode `75 D0`) found the
counter-example:

    Rev 20 0x0DB4  PUSH PSW
    Rev 20 0x0DB6  MOV PSW,#0x10      ; RS1=1 RS0=0 -> register bank 2
    Rev 22 0x0DE9  MOV PSW,#0x10

That is an interrupt prologue: save PSW, switch to a private bank so the
handler need not preserve the interrupted code's registers. **Bank 2 is live**,
at 0x10–0x17, and bank 1 and bank 3 are not selected anywhere.

So the layout, which accounts for the shape of the whole range:

    0x00–0x07   register bank 0, R0–R7
    0x08–0x0F   Keil DATA segment (globals)
    0x10–0x17   register bank 2 — reserved for the ISR at 0x0DB4 / 0x0DE9
    0x18–0x1E   Keil DATA segment continues
    0x20–0x26   bit-addressable globals (see the other IRAM documents)
    0x27–0x33   Keil overlaid locals (see IRAM_OVERLAY_ANNOTATION.md)

The split DATA region — 0x08–0x0F, then a hole, then 0x18 onward — is a direct
consequence of bank 2 being reserved in the middle of it, and is the reason the
used addresses look scattered.

## 0x01, 0x03, 0x05, 0x06, 0x07 — register bank 0 slots

These are R1, R3, R5, R6 and R7 reached by **direct address** rather than by
register name. That is not a separate variable: on 8051 the register bank is
also plain IRAM, and Keil emits a direct access whenever it wants a register
slot as a memory operand.

    Rev 20 0x0C87  MOV R3,0x01      ; R3 = R1
    Rev 20 0x0C59  MOV R7,0x03      ; R7 = R3
    Rev 20 0x0BEE  MOV R6,0x05      ; R6 = R5
    Rev 20 0x0C47  MOV R1,0x05
    Rev 20 0x0E6D  MOV R7,0x05
    Rev 20 0x0F15  MOV R7,0x05

R7/R5/R3/R1 is Keil's register-parameter convention — first argument in R7,
second in R5, and so on — which is what makes these particular slots the ones
that get addressed directly. `0x07` appears in Rev 22 only, same reason.

**0x06 has a specific role worth recording.** In the button handler it is a
"something changed" accumulator, returned to the caller in R7:

    Rev 20 0x0EEA  ORL 0x06,#0x1    ; mono fired
    Rev 20 0x0EF7  ORL 0x06,#0x1    ; ch1 source fired
    Rev 20 0x0F04  ORL 0x06,#0x1    ; ch2 source fired
    Rev 20 0x0F09  MOV R7,0x06      ; return it
    Rev 20 0x0D03  ORL 0x06,#0x1    ; same idiom, unrelated caller

So the handler returns nonzero iff it acted, which is how the caller knows to
republish the panel. See `IRAM23_IRAM25_ANNOTATION.md` for the handler.

## 0x08 — enumerated mode selector

Written only with small immediates, read in the audio dispatch region:

    Rev 20 0x0753  MOV 0x08,#0x1     0x0785  MOV 0x08,#0x2
    Rev 20 0x0791  MOV 0x08,#0x3     0x07BF  MOV 0x08,#0x5
    Rev 20 0x0821  MOV 0x08,#0x3     0x093B  MOV 0x08,#0x3
    reads: Rev 20 0x0098, 0x00BA, 0x00E2 (interrupt region), 0x045E

Values 1, 2, 3 and 5 with no 4, matching the clock/interface mode numbering
already documented in `rev20_audio_dispatch.md` and `rev20_dynamic_reconfig.md`
(mode 2 = the 48 kHz family word, mode 3 and mode 5 = the S/PDIF and I²S
variants). This is the live mode number.

## 0x09, 0x0B — loop counters

Both are decremented in place, which is what fixes them as counters rather
than state:

    Rev 20 0x0BB0  DJNZ 0x09,0x0BBF
    Rev 20 0x0BBA  MOV 0x09,#0xFF
    Rev 20 0x0BBD  DEC 0x0B

0x09 and 0x0B are also written from A at 0x003B/0x003D, 0x0183/0x0186,
0x01A7/0x01AC, 0x01E1/0x01E4 and 0x09FF/0x0A01 — always as an adjacent pair,
and compared as a pair at 0x0D77 (`CJNE A,0x0B`) and 0x0D9D
(`CJNE A,0x09`). They are a 16-bit quantity as well as a countdown: written
together, counted down together in the 0x0BAC–0x0BDC loop.

## 0x0C — one write, never read

    Rev 20 0x0A03  MOV 0x0C,#0xFE

That is the complete set of accesses to 0x0C in the image. There is no read,
in either image. A dead store is a complete account, not a gap: the value
cannot affect behaviour.

## 0x0D — enumerated state, values 1/2/5

    Rev 20 0x0063  MOV 0x0D,#0x2    0x006B  MOV 0x0D,#0x1
    Rev 20 0x024D  MOV 0x0D,#0x5
    reads: 0x0D28, 0x0D45, 0x0FD8;  written back from A at 0x0FE4

A second small enumeration, distinct from 0x08 and read in a different region.
The 0x0FD8/0x0FE0/0x0FE4 run reads 0x0D, reads 0x0E, and writes 0x0D back.

## 0x0E — save/restore partner of 0x0D

    Rev 20 0x0254  MOV 0x0E,A
    Rev 20 0x0FE0  MOV A,0x0E

Two accesses, one store and one load, consumed by the 0x0FD8 run above.

## 0x16 — register bank 2's R6, not a variable

    Rev 20 0x0DD1  MOV R2,0x16

This was first written up here as a variable "read once, never written, so it
holds whatever the boot ROM left". That was wrong. 0x16 is inside bank 2
(0x10–0x17) and 0x0DD1 lies inside the very ISR that switched to bank 2 at
0x0DB6, so this instruction reads **R6 of the bank it is currently running
in** — `MOV R2,R6` expressed as a direct access. Bank 2's R6 is set by the
handler's own register-parameter traffic.

The correction came from the `MOV PSW,#imm` scan above. It is a good example of
why a location cannot be annotated from its own access sites alone: 0x16 in
isolation looks exactly like an uninitialised global.

## 0x18 — bit-accumulator in the block loop

    Rev 20 0x0B8D  MOV 0x18,A       0x0BBF  INC 0x18
    Rev 20 0x0BC1  MOV A,0x18       0x0BCD  ORL A,0x18
    Rev 20 0x0BDC  MOV A,0x18

Lives entirely inside the 0x0B8D–0x0BDC loop alongside the 0x09/0x0B counters
and the 0x19/0x1A pointer. `ORL A,0x18` accumulates bits rather than sums.

## 0x19 : 0x1A — a 16-bit table pointer, big-endian

The pair is loaded straight into the data pointer, high byte first:

    Rev 20 0x01A1  MOV DPH,0x19     Rev 20 0x019E  MOV DPL,0x1A
    Rev 20 0x0B71  MOV DPH,0x19     Rev 20 0x0B6E  MOV DPL,0x1A

so 0x19 is the high byte and 0x1A the low byte — Keil's big-endian pointer
convention, the same one the EP0 pointer uses. Initialised to fixed addresses
and then walked:

    0x017A/0x017D  -> 0x0596        0x0198/0x019B  -> 0x0670
    0x01C1         -> 0x06A6        0x01CE         -> 0x06AA
    0x01DB         -> 0x06C8
    Rev 20 0x0BAE  INC 0x19         Rev 20 0x0BA8  INC 0x1A

Those five constants are all in the low CODE region and are the table bases
this pointer is set to before each walk.

## 0x1B : 0x1C (Rev 20) — the EP0 working pointer

Same big-endian pair convention:

    Rev 20 0x00CF, 0x00F7, 0x0B1A, 0x0B38   MOV DPH,0x1B
    Rev 20 0x0B11, 0x0B3E                   MOV 0x1B,#0xFA
    INC 0x1B at 0x00A8, 0x00B3, 0x00CB, 0x00DB, 0x00F3, 0x0103, 0x023D

The high byte is loaded with **0xFA**, putting the pointer in the 0xFAxx USB
endpoint buffer region, and it is incremented across the interrupt-region code
at 0x00A8–0x0103 that services EP0.

**Rev 22 moves this pair to 0x1D : 0x1E.** That relocation was already known
and is why the two images disagree on which low addresses they touch — Rev 22
additionally uses 0x07, Rev 20 additionally uses 0x33.

## Coverage note

Every address in 0x01–0x1E that either image touches is accounted for above,
with every access site enumerated. Where the complete account is "one write
and no reads" (0x0C) or "one read" (0x16), that is stated as such rather than
dressed up.
