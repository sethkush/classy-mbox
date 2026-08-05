# IRAM bit-addressable locations — bytes 0x20, 0x22–0x26

Companion to `IRAM_LOW_ANNOTATION.md` (layout), `MUX_IRAM22_ANNOTATION.md` and
`IRAM23_IRAM25_ANNOTATION.md`. Every access site is in `LEDGER_WORKLIST.txt`.

Reminder, because this trips every pass: the bit address of IRAM byte B bit N is
`(B - 0x20) * 8 + N`. Bit ops print the bit address, byte ops print the byte
address, and they look identical in a listing.

## Byte 0x20 bits — the previous-P3 sample

Byte 0x20 holds the last P3 read (see the button handler in
`IRAM23_IRAM25_ANNOTATION.md`). Its bits are tested individually as the "was it
already pressed" half of the edge detection:

    0x20.3   Rev 20 0x0EED  JB 0x03  -> ch1 source button, previous state
    0x20.4   Rev 20 0x0EFA  JB 0x04  -> ch2 source button, previous state
    0x20.5   Rev 20 0x0EE0  JB 0x05  -> mono button, previous state

**0x20.1 is a fourth input, and it is not one of the three panel buttons:**

    Rev 20 0x0AEC  JB  0x01,0x0AFC
    Rev 20 0x0AFC  JNB 0x01,0x0B0D

That is P3.1, tested twice in the function at 0x0A96 — the same function that
owns the overlaid locals 0x27–0x2B. ~~**P3.1 is the S/PDIF / external-clock
presence input.**~~ Asserted, it raises work code 0x0B, whose handler at 0x04C4
switches to clock mode 3 and starts the CS8427 with CLOCKSOURCE = 0x41;
released, it raises code 0x0C, whose handler at 0x0511 reverts to mode 1
(internal clock). Full derivation in `DISPATCH_TABLE_011F.md`.

> **CORRECTED 2026-08-04 — P3.1 is TXD, and neither handler is reachable.**
> P3.1 is the 8051 serial port transmit pin: TI's own source for this part gives
> `TXD BIT 0B0H.1` (`reference/tas1020a/ti_uac_reference/ROM/Utils.SRC:67`), and
> P3 is at 0xB0. Neither stock image ever touches `SCON` or `SBUF`, so the UART
> is unconfigured and the pin rests high.
>
> Measured 2026-08-04 on two units with crossed S/PDIF: pulling the coax from
> one unit's S/PDIF IN does not move P3.1 (`P3 = 0xC2` seated, pulled and
> re-seated, with the other unit as a control). Since code 0x0B requires
> P3.1 == 0 and only code 0x0B arms the latch that code 0x0C needs, **a
> permanently-high P3.1 makes both handlers dead code in stock.**
>
> The description of the two handlers below is still an accurate read of their
> bytes. It is what they *would* do. Nothing posts either code on this board.
> See `decomp/FINDING_p31_is_txd.md`.

An earlier draft here guessed "the first place to look for the TRS jack-presence
switches". That guess is withdrawn — the handlers name the function outright, and
nothing in either image reads a TRS jack.

## 0x22.7 — panel shift word, top bit

    Rev 20 0x03A0  CLR  0x17     (boot init, then published via LCALL 0x0F0C)
    Rev 20 0x03E6  SETB 0x17     (then published immediately at 0x03E8)

Only two sites, both followed by a panel republish, so this bit is shifted out
to the panel hardware and nothing in the firmware ever reads it back. Cleared
at boot, set at one point. Rev 22 at 0x03A4 / 0x03EA.

## Byte 0x23 bits 0–4 — codec control word

Byte 0x23 is the codec control shift word (consumer Rev 20 0x0E62). These bits
are written and never read, so the firmware's use of them is fully described by
where they are set and cleared; what they switch is inside the codec.

    0x23.0   SETB Rev 20 0x07B8   Rev 22 0x0796    set once, adjacent to 0x23.1
    0x23.1   SETB Rev 20 0x07BA   Rev 22 0x0798    set once, adjacent to 0x23.0
    0x23.4   SETB Rev 20 0x0840   Rev 22 0x09E5    set once

    0x23.2   CLR  Rev 20 0x072F   SETB 0x07EE, 0x0831
    0x23.3   CLR  Rev 20 0x0731   SETB 0x07F0, 0x0833
             (Rev 22: CLR 0x0716/0x0718, SETB 0x07CF/0x07D1, 0x09D8/0x09DA)

0x23.2 and 0x23.3 are the **#147 pair**. They move together at all three sites
in each image and never individually, so they are a two-bit field or a matched
pair of channel switches — a single mute would not need two.

### 0x23.2 / 0x23.3 ARE the stereo mute pair — established from structure

An earlier version of this section said their meaning was "NOT established" and
could not be, because the bits are write-only. That was wrong, and wrong in a
specific way worth naming: write-only rules out reading the value back, and
nothing else. It does not rule out inferring the function from *what the
firmware does around the write*. That evidence was in the image the whole time.

`0x0728` is the clock-mode apply function (mode number arrives in R7). Its shape:

    072f  CLR  0x23.2 ; CLR 0x23.3
    0733  LCALL 0x0E62          <-- publish to the codec IMMEDIATELY
    0736  ACGDCTL / ACG2DCTL = 0x10      (idle the clock generators)
    073c  dispatch on mode 1 / 2 / 3 / 5
          ... reprogram ACG1FRQ2/1/0, ACG2FRQ2/1/0, CPTRXCNF4, MEMCFG ...
    07c5  CS8427 CLOCKSOURCE write via the 0x31:0x32 pair
    07cc  ACGCTL |= 0xC0                 (re-enable the clock generators)
    07d3  IEPDCNTX1 = IEPDCNTY1 = OEPDCNTX2 = OEPDCNTY2 = 0   (flush EP buffers)
    07e4  IEPCNF1 = 0xC5 ; OEPCNF2 = 0xC5                     (enable endpoints)
    07ee  SETB 0x23.2 ; SETB 0x23.3
    07f2  LCALL 0x0E62          <-- publish to the codec IMMEDIATELY

Clear the pair, publish, tear the clocks down, reprogram, bring the clocks back,
flush and re-enable the endpoints, set the pair, publish. That is the canonical
mute-across-a-clock-change sequence, and nothing else in a codec control word
has that usage pattern. Four things make it tight:

  * The bits are cleared immediately *before* the clocks are idled and set
    immediately *after* they are restored — the exact window in which a PLL
    relock would otherwise be audible.
  * Each change is followed instantly by its own publish. You only pay for an
    immediate shift-out when the effect has to take place now.
  * No other bit of the codec word is touched inside that bracket.
  * The same pair, in the same order, appears in the timed power-up sequence at
    0x0831/0x0833 followed by a publish at 0x0835.

Two bits moved always together, never individually, across all three sites in
each image = a **stereo (left/right) mute pair**. Polarity: **clear = muted,
set = unmuted**.

### This vindicates #147, and upgrades it from timing inference to structure

mboxfw cleared the pair on the 44.1 kHz path and never set it again, so the
device came up **permanently muted at 44.1 kHz**. Stock always re-sets the pair
in the common tail. The #147 fix — setting them unconditionally in that tail —
is exactly what stock does, and the "44.1 kHz mute" reading was correct.

It no longer rests on a timing argument or on any hardware measurement. It rests
on the bracket above, which is in both images.

### 0x23.0 / 0x23.1 — mode-5-only codec configuration pair

Set once each, adjacently, and only inside the **mode 5** branch, then published:

    0799  MEMCFG (0xFFB1) &= 0xFE
    07a0  CPTRXCNF4 = 0x01              (receive bit clock divided by 2)
    07a6  MEMCFG |= 0x01
    07b2  ACG2DCTL = 0x10
    07b8  SETB 0x23.0 ; SETB 0x23.1
    07bc  LCALL 0x0E62

Mode 5 is the "one out and one in at different frequencies" I2S mode. The pair
is set only after the receive path has been switched to its own divided clock,
so these two bits configure the codec for that independent-input mode. They are
never cleared, in either image. Which two codec bits they are is not settled
here; that they are the mode-5 input-path pair is.

### 0x23.4 — one-time power-up bit

    083e  SETB 0x25.7
    0840  SETB 0x23.4
    0842  LCALL 0x0E62
    0845  delay

Set exactly once, inside the timed power-up sequence, *after* the mute pair is
released at 0x0831/0x0835, bracketed by DJNZ delays, and never cleared in
either image. A one-shot power-up configuration bit, sequenced after unmute.

### The codec is identifiable, so the remaining bit names are obtainable

`disasm/NOTES.md` records that the two-byte control word matches the **Cirrus
CS4272 (or close relative)** format, bit-banged on P1.0/P1.1/P1.2. So naming
0x23.0/0x23.1/0x23.4 precisely is a datasheet lookup, not an impossibility —
which is the correction to the claim that their meaning "is not in the image".

## Byte 0x25 bits 4–7

Established in `IRAM23_IRAM25_ANNOTATION.md`; claimed here for completeness.

    0x25.4   input to 0x22.6; also tested at Rev 20 0x0076, 0x0485, 0x049F
    0x25.5   input to 0x22.6            0x22.6 = !(0x25.4) && !(0x25.5)
    0x25.6   gates the boot init run — Rev 20 0x038F JB immediately precedes
             the 0x0395–0x03AD initialisation
    0x25.7   two toggle sites, Rev 20 SETB 0x083E/0x0850/0x0C8D,
             CLR 0x084B/0x0C4F; 0x084B is the instruction formerly mislabelled
             "the bare chip-select pulse"

## 0x26.0 — shift-loop flag in the codec shift routine

    Rev 20 0x0E66  SETB 0x30     (inside the 0x0E62 codec shift-out routine)
    Rev 20 0x0E8B  JZ / JNB test
    Rev 20 0x0E8E  CLR  0x30

Set on entry to the shift routine, tested and cleared as it finishes: a
loop/first-pass flag local to that routine.

## Byte 0x21 — the SET_INTERFACE alt-setting flags

Byte 0x21 is never accessed as a byte, only bitwise: a pure flag word. What
identifies it is the pair of XDATA sources feeding it, both already named in
`regs.h`:

    0xFF2A = SETPACK_WVAL_L    (SETUP wValue low  = alt setting)
    0xFF2C = SETPACK_WIDX_L    (SETUP wIndex low  = interface number)

Rev 20 0x0267–0x0293 decodes wValue with the interface implied, and
0x029F–0x02E1 decodes the (wIndex, wValue) pair:

    0x0267  wValue == 0  -> CLR 0x21.2, CLR 0x21.6, CLR 0x21.0, CLR 0x21.1
    0x0279  wValue == 1  -> CLR 0x21.2, SETB 0x21.6, CLR 0x21.0, CLR 0x21.1
    0x0288  wValue == 2  -> CLR 0x21.2, SETB 0x21.6, CLR 0x21.0, CLR 0x21.1
    0x02BA  wIndex == 1  -> A = wValue + 0xFF; MOV 0x21.0,CY   (i.e. alt >= 1)
    0x02CE  wIndex == 2  -> A = wValue + 0xFF; MOV 0x21.1,CY   (i.e. alt >= 1)

So:

    0x21.0   interface 1 is on a non-zero alt setting
    0x21.1   interface 2 is on a non-zero alt setting
    0x21.6   an alt setting was selected (set for wValue 1 or 2, clear for 0)

Both are reset as a group at Rev 20 0x09F7–0x09FD and 0x0F63–0x0F69, the bus
reset / detach paths.

### 0x21.2 is never set anywhere in either image

Its sites are `CLR` at 0x026D, 0x027C, 0x028B, 0x09F7, 0x0F63; `ORL CY,0x0A`
at 0x0528; and eight `JB`/`JNB` tests. **There is no `SETB 0x0A` and no
`MOV 0x0A,CY` in either image.** IRAM is zero at reset and every write to this
bit clears it, so it is always false and every `JB 0x21.2` branch is dead code.

One of those dead branches matters. At Rev 20 0x0347:

    0347  JNB 0x0A,0x0352          ; always taken
    034a  MOV DPTR,#0xFFDE / A=#0xAC / LCALL 0x0FF4   ; DEAD
    0352  JNB 0x0E,0x035D
    0355  MOV DPTR,#0xFFDE / A=#0xA8 / LCALL 0x0FF4   ; reached

0xFFDE is CPTCNF3 and bit 2 is BYOR. The 0xAC (BYOR set) write on this path is
unreachable; only the 0xA8 (BYOR clear) write can execute here. That is a fact
about this code path only — `hw_init` writes 0xAC to CPTCNF3 at 0x090B by a
different route — but it means any claim that "stock always sets BYOR" cannot
cite this region.

## 0x21.3 : 0x21.4 — EP0 control-transfer state, written as a pair

Three small setter routines write the pair together with byte 0x0D:

    0x005B  MOV 0x0A,#0x0D ; CLR  0x21.3 ; CLR 0x21.4 ; RET
    0x0063  MOV 0x0D,#0x02 ; SETB 0x21.3 ; CLR 0x21.4 ; RET
    0x006B  MOV 0x0D,#0x01 ; SETB 0x21.3 ; CLR 0x21.4 ; RET

and the consumers test them in order:

    0x0D25  JNB 0x21.3,0x0D67     then dispatch on byte 0x0D
    0x0FC4  JNB 0x21.3,0x0FCA     else LJMP 0x0B77
    0x0FCA  JNB 0x21.4,0x0FD8     then CLR 0x21.4 and touch OEPCNF0 (0xFFA8)
    0x1019  CLR 0x21.3 ; 0x101B CLR 0x21.4   after OEPCNF0 |= 0x08

0x21.3 is "a control transfer is in progress" and 0x21.4 its second-phase
flag; they are set as a pair, tested in sequence, and cleared together on
completion. Byte 0x0D carries which transfer it is (see
`IRAM_LOW_ANNOTATION.md`).

## 0x21.5 — one-shot

    CLR  Rev 20 0x0039 (ISR, right after LCALL 0x0B2B), 0x0D8F
    SETB Rev 20 0x0DA9
    test Rev 20 0x0BE1  JNB 0x0D,0x0BE9

Set at one site, cleared at two, tested once: a one-shot consumed by the
0x0BE1 branch.

## 0x24.0 — the timer-0 tick flag that drives the main loop

This is the one that explains how the panel is serviced at all. Rev 20
0x0AD3 is the main loop:

    0ad3  JB 0x24.0,0x0ADF     ; tick pending? -> service the panel
    0ad6  MOV A,0x0A           ; else: pending request code?
    0ad8  JZ 0x0AD3            ; nothing to do, spin
    0ada  LCALL 0x02EE         ; dispatch it
    0add  SJMP 0x0AD3
    0adf  LCALL 0x0ED5         ; the BUTTON HANDLER
    0ae2  MOV A,R7 / JNB ACC.0 ; did it act? (the 0x06 accumulator)
    0ae6  LCALL 0x0F0C         ;   yes -> republish panel word 0x22
    0ae9  LCALL 0x0E62         ;        -> republish codec word 0x23
    0aec  JB 0x20.1,0x0AFC     ; P3.1 handling, below
    ...
    0b0d  CLR 0x24.0           ; consume the tick
    0b0f  SJMP 0x0AD3

The flag is **set in the timer-0 interrupt**: 0x101E is `CLR IE.7` reached from
the timer-0 vector at 0x000B, and 0x1020 is `SETB 0x24.0`. Timer 0 is loaded
with `TH0 = 0xCE` at 0x08DF.

So the button chain is: timer 0 ticks -> ISR sets 0x24.0 -> main loop calls the
button handler -> if it acted, both shift words are republished. **The panel is
only ever read and rewritten on a timer tick**, which is the mechanism any
firmware has to reproduce for the buttons to work at all.

## 0x20.1 — P3.1, read as an insert/remove input; it is TXD and never moves

> **CORRECTED 2026-08-04.** The reading below — "the shape of a jack-presence /
> plug-detect line" — is withdrawn. P3.1 is TXD (TI `Utils.SRC:67`), the UART is
> never configured in either image, and a cable-pull test on real crossed S/PDIF
> showed the pin does not move. The *shape* argument was sound and is kept; what
> it lacked was any check that the pin was an input at all.
> See `decomp/FINDING_p31_is_txd.md`.

Continuing the same loop body:

    0aec  JB  0x20.1,0x0AFC
    0aef  MOV A,0x27 / JNZ 0x0AFC        ; local 0x27 already set?
    0af3  MOV 0x27,#1 ; MOV 0x0A,#0x0B ; LCALL 0x02EE
    0afc  JNB 0x20.1,0x0B0D
    0aff  MOV A,0x27 / CJNE A,#1,0x0B0D
    0b04  CLR A ; MOV 0x27,A ; MOV 0x0A,#0x0C ; LCALL 0x02EE

Two complementary edges on P3.1, latched through local 0x27 so each fires once,
raising **two distinct request codes**: 0x0B on one transition and 0x0C on the
other. That is level-detect on a sustained input with separate
"became-present" and "became-absent" events — the shape of a **jack-presence /
plug-detect** line, not a pushbutton. The three panel buttons are P3.3/4/5 and
act on a single edge each.

Which connector it belongs to is not determined by the firmware, but P3.1 is
the input to instrument if the TRS jack-detect question is to be settled.

## 0x24.2 — cleared once, never set, never tested

    Rev 20 0x0AAB  CLR 0x22

The only access in either image. Vestigial; cannot affect behaviour.

