# IRAM 0x23 and 0x25 — codec control word and panel/source state

Same method as `MUX_IRAM22_ANNOTATION.md`: byte-scan both images for every
instruction touching the direct address, and every bit op on that byte's bit
addresses, counting only instruction starts as decoded by the recursive-
traversal listing. **Bit address of IRAM byte B bit N = (B - 0x20) * 8 + N**,
so 0x23 -> 0x18..0x1F and 0x25 -> 0x28..0x2F.

Both images: 0x23 has 17 bit sites, 0x25 has 47, with one-to-one
correspondence between revisions.

---

# IRAM 0x23 — the codec control shift word

## Consumer — a SIXTEEN-bit word: 0x23 then 0x25

    0e62  MOV R6,#0x8        ; 8 bits
    0e64  MOV R5,0x23        ; FIRST byte = IRAM 0x23
    0e66  SETB 0x26.0        ; "second byte still to come"
    0e68  MOV A,R6 / JZ 0x0E8B
    0e6b  MOV R0,#1 / MOV R7,0x05 / MOV A,R7 / INC R0
    0e73  RL A               ; rotate left: ACC.0 becomes the old bit 7
    0e76  MOV R5,A
    0e77  JNB ACC.0,0x0E7F
    0e7a    P1 |= 0x01       ;   SDIN = 1   (P1.0)
    0e7f    P1 &= 0xFE       ;   SDIN = 0
    0e82  P1 |= 0x04         ; SCLK high    (P1.2)
    0e85  P1 &= 0xFB         ; SCLK low
    0e88  DEC R6 / loop
    0e8b  JNB 0x26.0,0x0E96  ; both bytes done -> latch
    0e8e  CLR 0x26.0
    0e90  MOV R5,0x25        ; SECOND byte = IRAM 0x25
    0e92  MOV R6,#0x8
    0e94  loop again
    0e96  P1 |= 0x02         ; LATCH high   (P1.1)
    0e99  P1 &= 0xFD         ; LATCH low
    0e9c  RET

`RL A` before testing ACC.0 means the **most significant bit is sent first**.
The routine runs twice, so the codec receives a **16-bit word: 0x23 as the high
byte, 0x25 as the low byte**, latched by one pulse on P1.1 at the end.

**This corrects a claim made earlier in this document.** IRAM 0x25 was written
up as "no shift-out call ... this byte is internal state, not a hardware word".
That is wrong: 0x25 is the low half of the codec control word. It is *both*
internal state and hardware payload, which is why bits of it are pulsed with an
immediate publish either side (0x25.7 at 0x083E-0x0852) — behaviour that makes
no sense for a purely internal variable and complete sense for a codec bit.

A consequence worth stating: **0x25.0-0x25.3, the two per-channel source state
machines, are clocked into the codec.** The "state machine" is not a private
counter that later gets translated; those bits are themselves part of the codec
payload, while 0x22 separately drives the panel LEDs and the analog mux.

This is a second shift chain, separate from the 0x22 panel/mux chain at Rev 20
0x0F0C, and it uses different pins (P1.0/1/2 versus P1.5/6/7). Called from Rev 20 0x037D, 0x0818, 0x0835,
0x0842, 0x084D, 0x0852, 0x096C, 0x0AE9.

Byte-level writes are all computed stores (`MOV 0x23,A`): Rev 20 0x0536,
0x080E, 0x096A / Rev 22 0x0535, 0x088B, 0x09B9.

## Bit sites

    bit 0   SETB   Rev 20 0x07B8   Rev 22 0x0796
    bit 1   SETB   Rev 20 0x07BA   Rev 22 0x0798
    bit 2   CLR    Rev 20 0x072F   Rev 22 0x0716
            SETB   Rev 20 0x07EE   Rev 22 0x07CF
            SETB   Rev 20 0x0831   Rev 22 0x09D8
    bit 3   CLR    Rev 20 0x0731   Rev 22 0x0718
            SETB   Rev 20 0x07F0   Rev 22 0x07D1
            SETB   Rev 20 0x0833   Rev 22 0x09DA
    bit 4   SETB   Rev 20 0x0840   Rev 22 0x09E5
    bit 5   -- no bit operation in either image --
    bit 6   CLR    Rev 20 0x039E (boot), 0x053E, 0x0962, 0x102B
            SETB   Rev 20 0x0941, 0x102E
            JNB    Rev 20 0x0F32, 0x1028
            (Rev 22: 0x03A2, 0x053D, 0x0883, 0x1023 / 0x0862, 0x1026 /
             0x0F20, 0x1020)
    bit 7   -- no bit operation in either image --

Bits 5 and 7 are never touched bitwise in either image. They can still be set
by the computed stores, so "unused" is not established — only "never
bit-addressed".

## Bits 2 and 3 are the #147 pair

Rev 20 0x07EE / 0x07F0 and Rev 22 0x07CF / 0x07D1 are exactly the sites
recorded in the #147 fix, confirmed here by independent scan. They are also
CLEARED as a pair at Rev 20 0x072F / 0x0731 (Rev 22 0x0716 / 0x0718), and set
again as a pair at Rev 20 0x0831 / 0x0833 (Rev 22 0x09D8 / 0x09DA).

Always moved together, three times in each image, never individually. Whatever
they control is a two-bit field or a pair of matched channel switches — a
single mute bit does not need two.

## Bit 6 IS MONO — #144 resolved

Bit 6 is the only bit of 0x23 that is *tested* (`JNB` at Rev 20 0x0F32 and
0x1028). Its behaviour: cleared at boot (0x039E, part of the same
initialisation run that sets up 0x22 and 0x25), and at 0x1028-0x102E it is
read and then written both ways — the classic toggle idiom:

    1028  JNB 0x1e,0x102e
    102b  CLR 0x1e
    102e  SETB 0x1e

### 48V is not a firmware control -- established, not inferred

Seth has stated this from the hardware and it is now confirmed three
independent ways:

  1. **The port pins do not move.** Telemetry block 4 does live P1/P3 reads.
     Five samples with the 48V switch OFF and five with it ON, on hardware
     2026-07-29: P1=0x18, P3=0xFB, byte-identical across both positions.
  2. **No 48V-shaped latch exists.** The census below covers every IRAM bit in
     the image; no bit outside the source state machines and the mono latch
     behaves like a phantom-power control.
  3. **The 48V LED tracks the switch directly**, so the indicator is hardwired
     and the firmware is not in the loop.

48V is a mechanical switch, independent of the firmware. `g_phantom_48v` in
`mboxfw/include/mux.h` and `P3_BTN_48V_MASK` in `regs.h` named a control the
firmware cannot even observe; both are renamed as of 2026-07-29. #144 is
resolved.

### What 0x23.6 actually is

Seth: **mono is off at boot, and the mono button toggles it.** That is exactly
this bit's behaviour, and two independent routes confirm it.

**Route 1 -- census by elimination.** For every IRAM bit address 0x00-0x7F in
Rev 20, collect all SETB / CLR / CPL / JB / JNB / JBC sites and keep bits with
a set, a clear AND a test. Fourteen qualify. Those cleared in the boot
initialisation run are:

    0x23.6  CLR 0x039E          <-- toggle at 0x1028-0x102E
    0x25.5  CLR 0x0395          0x22.6 input
    0x25.0  CLR 0x03A5          channel 1 source state
    0x25.1  CLR 0x03A7          channel 2 source state
    0x25.2  CLR 0x03A9          channel 1 source state
    0x25.3  CLR 0x03AB          channel 2 source state
    0x25.4  CLR 0x03AD          0x22.6 input

All but 0x23.6 are accounted for as source state or 0x22.6 inputs. 0x23.6 is
the only boot-cleared latch left and the only bit in either image with a
read-then-write-both-ways toggle.

**Route 2 -- the button handler reaches it.** Rev 20 0x0EE7 is
`LCALL 0x1028` from the P3.5 button branch (see the button section below).
The mono button calls the 0x23.6 toggle directly.

**IRAM 0x23.6 = MONO.**

It is also clocked out as a 9th bit on the panel shift chain: the mux
shift-out routine tests it at Rev 20 0x0F32 / Rev 22 0x0F20 to choose the
trailing latch sequence.

Follow-on for mboxfw — **done 2026-07-29** (task #144):

  * `mux.h` `g_phantom_48v` -> `g_mono`, with the evidence in the declaration
    comment.
  * `regs.h` `P3_BTN_48V_MASK` -> `P3_BTN_MONO_MASK`, and `buttons.c` names the
    handler it ports (Rev 20 fcn.0x1028 / Rev 22 fcn.0x1020).
  * `mux.c`'s latch-tail comment no longer claims the set branch "asserts 48V";
    it decodes the branch and states that which state the hardware latches on is
    unverified.

---

# IRAM 0x25 — panel/source state bits

**Shifted out as the low byte of the 16-bit codec word** — see the consumer
section above; the load is `MOV R5,0x25` at Rev 20 0x0E90 / Rev 22 0x0E82.
Computed stores at Rev 20 0x0534, 0x080C, 0x0968 / Rev 22 0x0533, 0x0889,
0x09B7. So this byte is internal state AND hardware payload at once.

## Boot state — explicitly cleared

    Rev 20 0x03A5-0x03AD   CLR bits 0,1,2,3,4
    Rev 22 0x03A9-0x03B1   CLR bits 0,1,2,3,4

This is what licenses the source-pattern deduction in
`MUX_IRAM22_ANNOTATION.md`: the source state machines start from (0,0), so the
boot mux value 0x76 is the *first* state, not an arbitrary one.

## Bit map

    bit 0   channel 1 source state, low       (with bit 2)
    bit 2   channel 1 source state, high
    bit 1   channel 2 source state, low       (with bit 3)
    bit 3   channel 2 source state, high
    bit 4   feeds 0x22.6; tested very early at Rev 20 0x0076 / Rev 22 0x0071
    bit 5   feeds 0x22.6
    bit 6   mode/branch flag, tested 4x, set 1x, cleared 1x
    bit 7   toggled in two separate places

The two source state machines are exactly symmetric:

    channel 1   Rev 20 0x0E27   bits 0 and 2   -> 0x22 bits [2:0]
    channel 2   Rev 20 0x0E9D   bits 1 and 3   -> 0x22 bits [5:3]

Rev 20 0x0E9D-0x0EBD mirrors 0x0E27-0x0E4A instruction for instruction with
bits 1/3 substituted for 0/2, and the same three patterns emitted onto bits
[5:3]. Rev 22 at 0x0E8F and 0x0E1B.

## Bits 4 and 5 both drive 0x22.6

    0x22.6 = !(0x25.4) && !(0x25.5)

per Rev 20 0x0E52-0x0E61 (channel 1 path) and 0x0EC5-0x0ED2 (channel 2 path),
Rev 22 0x0E46-0x0E55 and 0x0EB7-0x0EC1. Both channel paths compute the SAME
single bit from the same two inputs, so 0x22.6 is a global, not per-channel.

Bit 4 additionally: cleared at boot and at Rev 20 0x0454, set at 0x0466,
tested at 0x0076, 0x0485, 0x049F. The test at 0x0076 sits in the interrupt/
early-reset region, which makes bit 4 the most interesting unknown here.

Bit 5: cleared Rev 20 0x0395 and 0x041C, set 0x04CA.

## Bit 6 — a mode flag

Tested at Rev 20 0x035D, 0x038F, 0x0416, 0x04C4; cleared 0x037B; set 0x0810.
Note 0x038F is the `JB` immediately before the boot init run at 0x0395-0x03AD,
so bit 6 gates whether the boot initialisation is performed — consistent with
a "already initialised" or "mode already selected" latch.

## Bit 7 — two independent toggle sites

    SETB   Rev 20 0x083E, 0x0850, 0x0C8D    Rev 22 0x09E3, 0x09F3, 0x0C77
    CLR    Rev 20 0x084B, 0x0C4F            Rev 22 0x09EE, 0x0C39

Rev 20 0x084B is the address previously logged as "the bare chip-select pulse
at 0x084B" in `FINDING_open_questions.md`. It is not a chip select: it is
`CLR 0x2F`, clearing IRAM 0x25.7, bracketed by SETB at 0x083E and 0x0850.
That open question is answered as to *what the instruction is*; what the bit
means is still open.

---

## Divergences from mboxfw found here

1. ~~`mboxfw/include/mux.h` documents `g_phantom_48v` as mirroring
   RAM[0x23].6.~~ **RESOLVED 2026-07-29 (#144).** Renamed to `g_mono`
   throughout mboxfw. The bit was always right; only the name was wrong.
2. Nothing in mboxfw corresponds to IRAM 0x25 bits 4, 5, 6 or 7, nor to
   0x23 bits 0, 1 or 4. mboxfw models 0x23.2/0x23.3 (the #147 pair) and
   0x23.6 only.

## Still open

  * What 0x23 bits 0, 1, 2, 3, 4 physically switch. Bits 2/3 always move as a
    pair; bits 0/1 are set once each, adjacently, in the streaming path.
  * Whether 0x23 bits 5 and 7 are used at all (never bit-addressed, but the
    computed stores could set them).
  * IRAM 0x25 bit 4's role, given the early test at Rev 20 0x0076.
  * The meaning of 0x25.7, toggled around Rev 20 0x083E-0x0850.

---

# The button handler — Rev 20 0x0ED5 / Rev 22 0x0F31

    0ed5  CLR A
    0ed6  MOV R6,A
    0ed7  MOV R5,0xb0        ; R5 = P3
    0ed9  MOV A,R5
    0eda  CJNE A,0x20,0x0ee0 ; vs IRAM 0x20 = previous P3; equal -> return 0
    0edd  MOV R7,#0x0
    0edf  RET
    0ee0  JB  0x05,0x0eed    ; prev P3.5 set  -> skip
    0ee3  MOV A,R5
    0ee4  JNB 0xe5,0x0eed    ; cur  P3.5 clear -> skip
    0ee7  LCALL 0x1028       ; MONO toggle (0x23.6)
    0eea  ORL 0x06,#0x1
    0eed  JB  0x03,0x0efa    ; prev P3.3
    0ef0  MOV A,R5
    0ef1  JNB 0xe3,0x0efa    ; cur  P3.3
    0ef4  LCALL 0x0e27       ; channel 1 source
    0ef7  ORL 0x06,#0x1
    0efa  JB  0x04,0x0f07    ; prev P3.4
    0efd  MOV A,R5
    0efe  JNB 0xe4,0x0f07    ; cur  P3.4
    0f01  LCALL 0x0e9d       ; channel 2 source
    0f04  ORL 0x06,#0x1
    0f07  MOV 0x20,R5        ; save P3 as previous
    0f09  MOV R7,0x06
    0f0b  RET

Rev 22's equivalent read is at 0x0F33, `MOV R6,0xb0`.

    P3.3   channel 1 source button
    P3.4   channel 2 source button
    P3.5   mono button
    IRAM 0x20   previous P3 sample

The 0xE3/0xE4/0xE5 bit addresses are ACC.3/ACC.4/ACC.5 -- the current sample,
copied into A. The 0x03/0x04/0x05 bit addresses are IRAM 0x20 bits 3/4/5 --
the previous sample.

**Edge polarity: the action fires when prev = 0 and cur = 1.** The pins idle
high (stock writes `MOV P3,#0xFF` at Rev 20 0x08DC / Rev 22 0x07FD, enabling
the quasi-bidirectional pull-ups) and a press pulls low, so the firing edge is
the **release**, not the press.

~~`mboxfw/src/buttons.c:39` computes `pressed_low = changed & ~now` -- a falling
edge -- so mboxfw acts on press where stock acts on release. Divergence,
recorded, not yet fixed.~~ **FIXED 2026-07-29**, and this note went stale
without being updated. `buttons.c` now computes `released = changed & now` and
carries the stock citation for the two-guard idiom. Verified against the current
source 2026-07-30.

## The panel shift routine and the P1 pin map

Rev 20 0x0F0C, from the same read of 0x22 documented in
`MUX_IRAM22_ANNOTATION.md`:

    0f0c  MOV R6,#0x8        ; 8 bits
    0f0e  MOV R5,0x22
    0f10  ANL 0x90,#0xbf     ; P1.6 = 0
    0f13  (per bit) rotate; JNB ACC.0 ->
    0f22  ORL 0x90,#0x80     ;   P1.7 = 1   (data high)
    0f27  ANL 0x90,#0x7f     ;   P1.7 = 0   (data low)
    0f2a  ORL 0x90,#0x20     ; P1.5 = 1     (clock high)
    0f2d  ANL 0x90,#0xdf     ; P1.5 = 0     (clock low)
    0f30  DJNZ R6 -> 0x0f13
    0f32  JNB 0x1e,0x0f39    ; test 0x23.6 = MONO
    0f35  ORL 0x90,#0xc0     ;   P1.7 and P1.6 high
    0f39  ANL 0x90,#0x7f / ORL 0x90,#0x40 / ANL 0x90,#0xbf

    P1.5 = clock, P1.7 = data, P1.6 = latch

## Open: the buttons do not move P3 under mboxfw

Measured 2026-07-29 on the unit running mboxfw: telemetry block 4 polled at
605 Hz for 100 s (60457 samples) while Seth pressed mono, channel 1 source and
channel 2 source repeatedly. **P3 read 0xFB for every one of the 60457
samples**, and no panel LED changed.

Block 4 reads the port directly, so a press should move the pin regardless of
what the firmware does with it. Sampling was not the limit -- 605 Hz resolves
a 2 ms press.

That leaves two candidates, and the disassembly cannot decide between them:

  1. The buttons are not electrically on P3.3/4/5 on this unit.
  2. The buttons need the panel shift register clocked to be readable. Stock
     refreshes it from the main loop (0x0F0C is called from Rev 20 0x0AE6); if
     one of the shift register's outputs is a button common or enable line,
     an unrefreshed panel leaves the buttons dead. mboxfw's P1 reads 0x18,
     with P1.5/6/7 all low.

Candidate 2 is testable with a diagnostic image that drives the panel and
reports P3. Note also that P3 bits 0, 1, 6 and 7 are untouched by stock's
handler -- the TRS jack presence switches, if the firmware sees them at all,
would be there.

---

# The codec control word is FLAT, not register-addressed

`disasm/NOTES.md` records that the two-byte word "matches the Cirrus CS4272 (or
similar) audio codec control register format". The routine above argues against
that, and the point matters because it changes how the remaining bits can be
identified.

CS4272 is controlled over I2C or SPI with **addressed registers**: a write
carries a register number and then a data byte. A 16-bit word whose bits are
individually meaningful — 0x23.6 mono, 0x23.2/0x23.3 a stereo mute pair,
0x23.4 a one-shot power-up bit, 0x25.0-0x25.3 per-channel source state — is not
an (address, data) pair. It is a **flat control word**: every bit is a function,
latched in parallel by the P1.1 pulse.

Three further observations against an addressed protocol:

  * There is no register-address field. If bits 12-8 were an address, the mute
    pair and the mono bit could not sit in the high byte together and still be
    published by a single write.
  * Every publish sends the *whole* word. An addressed protocol writes one
    register at a time; this routine always shifts both bytes and latches once.
  * The bits are set and cleared independently in IRAM and only then published,
    which is shadow-register behaviour for a flat word, not a register write.

**Consequence for the remaining unknowns.** Bits 0x23.0, 0x23.1 and 0x23.4
cannot be named by looking up a CS4272 register map, because there is no
register map in play. They have to be identified the same way the mute pair was
— from what the firmware does around each write — or from the board itself.
That is a narrower and more honest statement than "it is a datasheet lookup",
which is what this document previously said.

What each is already pinned to, by context:

    0x23.0 / 0x23.1   set together, ONLY in the mode-5 branch, immediately
                      after the receive path is switched to its own /2 clock.
                      The independent-input configuration pair.
    0x23.4            set once in the timed power-up run, after the mute
                      release, bracketed by delays, never cleared.
    0x23.5 / 0x23.7   ALWAYS ZERO. Upgraded from "never individually
                      addressed": all three computed stores to 0x23 are the
                      same idiom, `CLR A / MOV 0x25,A / MOV 0x23,A` (Rev 20
                      0x0536, 0x080E, 0x096A), i.e. the only byte-level writes
                      ZERO the word. With no SETB and no MOV bit,C anywhere for
                      these two bits, they can never be 1.

---

# The codec word is a pure bit-shadow, and 0x0526 is the suspend handler

All three byte-level writes to 0x23 -- and to 0x25 -- are the identical
zeroing idiom:

    Rev 20 0x0533  CLR A / MOV 0x25,A (0x0534) / MOV 0x23,A (0x0536)
    Rev 20 0x080B  CLR A / MOV 0x25,A (0x080C) / MOV 0x23,A (0x080E)
    Rev 20 0x0967  CLR A / MOV 0x25,A (0x0968) / MOV 0x23,A (0x096A)

So the 16-bit codec word is never *computed*. It is zeroed at boot, at codec
re-init, and at suspend, and every other change is an individual bit operation
on an IRAM shadow which is then published. That is what makes the bit-by-bit
annotation in this document a complete account of the word.

0x080B is the "reset the codec word to all-off" helper, called from Rev 20
0x0360, 0x0392, 0x0419 and 0x04C7 — the last of which is the S/PDIF-present
path, immediately before it switches to clock mode 3.

## Rev 20 0x0526 — the USB suspend handler

Reached as work code **0x0E** (`MOV 0x0A,#0x0E` at Rev 20 0x0006, inside the
INT0/USB vector slot, cross-referenced from 0x0CBF), dispatched through the
0x0300 jump table:

    0526  MOV CY,0x21.6 / ORL CY,0x21.2 / JNC 0x0564   ; only if streaming
    052c  ACGCTL &= 0x3F        ; clear MCLKO2EN and MCLKO1EN -- master clocks off
    0533  CLR A
    0534  MOV 0x25,A            ; codec word low  = 0
    0536  MOV 0x23,A            ; codec word high = 0
    0538  LCALL 0x0E62          ; publish 0x0000
    053b  MOV 0x22,#0xFF        ; panel word = blank
    053e  CLR 0x23.6            ; mono off
    0540  LCALL 0x0F0C          ; publish panel
    0543  PCON |= 0x01          ; IDL -- put the MCU into idle mode

Two things follow.

**It independently confirms the mute polarity.** Suspend publishes 0x0000 to the
codec, which must mean everything off. Since 0x23.2/0x23.3 are 0 in that word,
**clear = muted** — the same conclusion the clock-change bracket gave, reached by
a different route.

**Stock uses 0xFF as the panel blank word.** 0xFF has 111 in both 3-bit source
fields, and 111 is not one of the three source patterns (0x05/0x03/0x06). So
non-source patterns are legitimate *off* states for this word, and calling
mboxfw's boot value of 0x00 "illegal" (in `MUX_IRAM22_ANNOTATION.md`) was too
strong. 0x00 remains a divergence — stock boots 0x76 and blanks with 0xFF, never
0x00 — but the word does tolerate patterns outside the source set.

## Resolved: the panel republish is change-triggered in stock too

Listed earlier as an open question. Stock gates it:

    0adf  LCALL 0x0ED5          ; button handler
    0ae2  MOV A,R7
    0ae3  JNB ACC.0,0x0AEC      ; handler returned 0 -> skip both publishes
    0ae6  LCALL 0x0F0C          ; panel word
    0ae9  LCALL 0x0E62          ; codec word

The republish happens only when the handler reports it acted, via the 0x06
accumulator returned in R7. mboxfw's `codec_commit()`-on-change does the same
thing. **Not a divergence.**

---

# What the three remaining bits DO, from usage

An earlier version of this document said these "cannot be determined from the
material we have". That conflated two different questions and I should not have
merged them:

  * *Which physical pin of which part* a bit drives — needs the schematic.
  * *What function the bit performs* — determinable from how the firmware uses
    it, which is exactly how 0x23.2/0x23.3 were pinned as the mute pair.

The second is answerable here. What follows is the second, not the first.

## The polarity of the whole word is fixed

The suspend handler publishes **0x0000** and simultaneously disables the master
clock outputs (`ACGCTL &= 0x3F` at Rev 20 0x052C). An all-zero word is therefore
the all-off state, which makes **every bit in this word active-high**. That
settles polarity for all three unknowns without knowing what they drive.

## The power-up sequence, identical in both images

Rev 20 0x080B–0x0852; Rev 22 0x09B6–0x09F5, which differs only in using R7 for
the delay counters instead of the IRAM 0x2E overlay slot. Bit order is
byte-identical:

    word = 0x0000                    everything off
    SETB 0x25.6 ; delay ; publish    <-- asserted FIRST, then a settling delay
    load ACG1/ACG2 frequency words, ACGDCTL
    mode = 3
    ACGCTL |= 0xC0                   master clock outputs ON
    delay
    SETB 0x23.2 ; SETB 0x23.3 ; publish     unmute
    delay
    SETB 0x25.7 ; SETB 0x23.4 ; publish     <-- asserted LAST
    delay
    CLR  0x25.7 ; publish            <-- single LOW pulse
    SETB 0x25.7 ; publish            <-- restored high

## 0x25.6 — master enable / powered-up

Asserted **first**, out of the all-off word, before any clock is loaded, and
followed by its own settling delay before anything else is published. Held
thereafter; cleared only at Rev 20 0x037B and at suspend. It is additionally
*tested* internally at 0x038F to gate the boot-initialisation run, and at
0x035D, 0x0416 and 0x04C4.

A bit you raise before the clocks, wait on, hold for the whole session, and also
keep as your "already initialised" flag is a **master enable / power-up** line.

## 0x23.4 — final-stage enable, latching

Asserted **last**, after the master clocks are running *and* after the mute has
been released, and **never cleared anywhere in either image** except by the
wholesale zeroing of the word. Whatever it switches on is the last thing in the
chain to come up and is never switched off again while the device is awake.

## 0x25.7 — a one-shot strobe

Held high, pulsed **low once**, then restored — three publishes with a delay
before the pulse. Issued at the very end, after clocks, unmute and 0x23.4.

The ordering rules out a reset: a reset is asserted *before* configuration, not
after it. A trigger issued once, after the clocks are stable and audio is
already unmuted, is a **one-shot strobe** — the firmware asking the hardware to
do something now, rather than setting a state.

(Rev 20 0x084B, the `CLR 0x25.7` in this pulse, is the instruction logged in
`FINDING_open_questions.md` as "the bare chip-select pulse". It is a pulse, but
of a control-word bit, not a chip select.)

## 0x23.0 / 0x23.1 — the independent-rate input path

Set together, and **only** in the mode-5 branch (Rev 20 0x07B8/0x07BA),
immediately after that branch switches the receive path to its own divided clock
(`CPTRXCNF4 = 0x01`, with MEMCFG toggled either side). Never set in modes 1, 2
or 3, and never cleared individually — they are simply re-asserted whenever mode
5 is re-entered after a word zeroing.

Mode 5 is the TAS1020B's "1 OUT and 1 IN at different frequencies" I2S mode, so
these two bits configure whatever the second, independent-rate **input** path
needs. Two bits, always together, for one path.

## The boundary

Functions above are determined from usage. The vendor's *name* for each line,
and which package pin it lands on, are not in either image, are not in the TI
reference (whose application has no such shift register — it uses P1.0/P1.1 as
AC'97 mode pins and P1.6 to mute an amp), and cannot come from a codec register
map because the word is flat. That last step needs the board.
