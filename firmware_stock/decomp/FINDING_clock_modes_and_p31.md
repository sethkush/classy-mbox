# Clock modes 1/2/3/5, and why P3.1's meaning is NOT what we recorded

Work on `WHAT_REMAINS_UNKNOWN.md` §2 and task #145 (S/PDIF clock slaving). The
modes are now determined from the datasheet. The P3.1 signal that selects between
them is not, and the reading previously carried in project notes produces an
incoherent machine.

## ACGCTL decoded — datasheet §6.5.3.11

    bit 7 MCLKO2EN   6 MCLKO1EN   5 reserved
    bit 4:3 MCLKO1S1:S0          2 DIVEN         1:0 MCLKO2S1:S0

    S1 S0 = 0 0  ->  acg_clk  (internal frequency synthesizer 1, after /M)
    S1 S0 = x 1  ->  mclki    (EXTERNAL clock input, after /I)
    S1 S0 = 1 0  ->  acg2_clk (internal frequency synthesizer 2, after /M)

Every mode ends at the shared tail `0x07C5`, which writes the CS8427
(register, value) pair from `RAM[0x31]:RAM[0x32]` and then `ACGCTL |= 0xC0`
(enable both MCLKO outputs). So the effective values are:

    mode 1   ACGCTL = 0x0D -> 0xCD    MCLKO1 = mclki,    MCLKO2 = mclki
    mode 2   ACGCTL = 0x06 -> 0xC6    MCLKO1 = acg_clk,  MCLKO2 = acg2_clk
    mode 3   ACGCTL = 0x06 -> 0xC6    same as mode 2, different frequency word

**Mode 1 sources both codec clocks from the external clock input. Modes 2 and 3
source them from the two internal synthesizers.** Mode 1 is therefore the
slave-to-external-clock path, and it programs no frequency word at all -- there
is nothing to program, because the clock is not being synthesized.

Per-mode detail:

    mode 1  @0x074D  ACGCTL = 0x0D ; RAM[0x08] = 1 ; 0x31:0x32 = 04:41
                     (CS8427 CLOCKSOURCE = 0x41, RUN SET -- start the receiver)
    mode 2  @0x075F  44.1 kHz frequency word (0x6A/0x4B/0x20)
    mode 3  @0x078E  LCALL 0x0DEC (48 kHz word 0x61/0xA8/0x0F) ; RAM[0x08] = 3 ;
                     LCALL 0x0E20 -> 0x31:0x32 = 04:40 (RUN CLEAR -- stop it)
    mode 4           not implemented; dispatch falls through to the common tail
    mode 5  @0x0799  the "1 OUT and 1 IN at different frequencies" branch;
                     writes CPTRXCNF4 = 0x01 (DIVB2 = /2)

`RAM[0x08]` is the persisted current mode: 3 from hw_init (0x093B), 1 from the
mode-1 branch (0x0753), read back by work code 0x04 (`MOV R7,0x08` at 0x045E) to
restore the internal clock at whatever mode was last in use.

## The two P3.1 handlers

Main loop, Rev 20 0x0AEC-0x0B0A, using the P3 snapshot in `RAM[0x20]` bit 1 with
`RAM[0x27]` as an edge latch:

    0aec  JB  0x20.1,0x0AFC   ; P3.1 SET -> skip the code-0x0B block
    0af3  RAM[0x27] = 1 ; RAM[0x0A] = 0x0B    ; so 0x0B runs when P3.1 == 0
    0afc  JNB 0x20.1,0x0B0D   ; P3.1 CLEAR -> skip the code-0x0C block
    0b05  RAM[0x27] = 0 ; RAM[0x0A] = 0x0C    ; so 0x0C runs when P3.1 == 1

Handler for code 0x0B (`0x04C4`), reached when **P3.1 == 0**:

    04c4  JB 0x25.6,0x04CA       ; skip CS8427 init if already initialised
    04c7  LCALL 0x080B           ; CS8427 boot init
    04ca  SETB 0x25.5            ; the "clock slaved to S/PDIF" latch
    04cc  R7 = 3 ; LCALL 0x0728  ; clock mode 3  <-- INTERNAL 48 kHz
    04d1  0x31... no: 0x2C:0x2D = 04:41 ; LCALL 0x0C45
                                 ; CS8427 CLOCKSOURCE = 0x41, RUN SET
    04de  R5 = 0xFF ; R7 = 0x1F ; LCALL 0x0CDD   ; read CS8427 reg 0x1F
    04e7  XRL 0x2C,#0xFF         ; complement it
    04ec  R7 = 0x1F ; LCALL 0x0BEE               ; write the complement back
    04f1  R5 = 0xFF ; LCALL 0x0CDD               ; read it again
    04f8  CJNE A,0x2C,0x04FF     ; readback == what we wrote?
    04fd  CLR 0x22.6             ; only if it matched -> assert external-clock line
    04ff  LCALL 0x0F0C           ; publish the panel word
    0502  0x2C:0x2D = 12:00 ; LCALL 0x0C45       ; CS8427 reg 0x12 = 0

The 0x04DE-0x04F8 run is a **write-readback presence probe**: complement a
register, write it, read it back, compare. ~~The external-clock panel line is
asserted only if the CS8427 answered.~~

> **CORRECTED 2026-07-30 — this probe targets the EEPROM, not the CS8427.**
> `0x0CDD` and `0x0BEE` both write slave address **0xA0** to `I2C_SADDR`
> (0xFFC3) and drive the TAS1020B's hardware I²C peripheral at 0xFFC0-0xFFC3.
> 0xA0 is the 24Cxx EEPROM; the CS8427 is 0x20 on the bit-banged SPI bus (see
> `FINDING_cs8427_is_spi_not_i2c.md`). The Ghidra listing names the two routines
> `i2c_eeprom_read_byte` / `i2c_eeprom_write_byte` and names this whole caller
> **`cmd11_eeprom_selftest`**.
>
> R7 = 0x1F and R5 = 0xFF are the two EEPROM address bytes, so the target is
> address **0x1FFF** — the last byte of the 8 KB part. `CLR 0x16` at `0x04FD`
> therefore clears IRAM 0x22.6 on a successful **EEPROM write-verify**, not on a
> CS8427 response. The lines below annotating these calls as "read CS8427 reg
> 0x1F" are wrong and are kept only so the correction has something to point at.
>
> Note also that stock performs a **destructive write to EEPROM 0x1FFF** here.
> Anything assuming the EEPROM is read-only at runtime has to leave that byte
> alone.

Handler for code 0x0C (`0x0511`), reached when **P3.1 == 1**:

    0511  R7 = 1 ; LCALL 0x0728  ; clock mode 1  <-- EXTERNAL clock
    0516  SJMP 0x0564

## UPDATE 2026-07-30 — the edge latch changes the argument below

The section that follows argues the machine is incoherent because "slaving to an
external clock that is not there would leave the codec with no clock at all",
and that P3 idling high means it "would slave to nothing by default". **That
objection is wrong**, because `RAM[0x27]` is not just an edge latch — it is a
one-way gate, and it starts closed.

Read from the listing (Rev 20 `0x0AEC`, Rev 22 `0x0A96` — identical):

    0aec  JB 0x01,0x0afc      ; bit 0x01 = IRAM 0x20.1 = last sampled P3.1
    0aef  MOV A,0x27
    0af1  JNZ 0x0afc          ; code 0x0B needs 0x27 == 0
    0af3  MOV 0x27,#0x1
    0af6  MOV 0x0a,#0xb       ; post code 0x0B
    0afc  JNB 0x01,0x0b0d
    0aff  MOV A,0x27
    0b01  CJNE A,#0x1,0x0b0d  ; code 0x0C needs 0x27 == 1
    0b04  CLR A ; MOV 0x27,A
    0b07  MOV 0x0a,#0xc       ; post code 0x0C

So the guards are:

    code 0x0B  fires only when  P3.1 == 0 AND 0x27 == 0   (then 0x27 <- 1)
    code 0x0C  fires only when  P3.1 == 1 AND 0x27 == 1   (then 0x27 <- 0)

`main` initialises `0x27 = 0` as its first action (Rev 20 `0x0A95` `CLR A` /
`0x0A96` `MOV 0x27,A`). Therefore **at boot, with P3.1 idling high, neither
block fires** — the first is skipped on P3.1, the second on the latch. The device
does not slave to an absent clock by default. It does nothing at all.

And code 0x0C can never run until code 0x0B has run at least once, because only
code 0x0B sets the latch. The machine is strictly ordered: **P3.1 must fall
first, then rise.**

P3.1 is also a pure input — the only write to P3 in either image is
`MOV 0xB0,#0xFF` in hw_init (Rev 20 `0x08DC`, Rev 22 `0x07FD`), and there is no
`SETB`/`CLR`/`CPL` on bit 0xB1 anywhere.

### RETRACTED SAME DAY: the switched-jack reading. The S/PDIF is RCA.

Seth, from the hardware: **the S/PDIF connector is RCA**, which has no switch
contact. The reading below required a normally-closed switched jack, so it is
withdrawn. It is kept only because the transition table in it is still the
constraint any correct answer has to satisfy.

This is the second time in one day that a coherent story got built on top of a
physical assumption nobody had checked, and both times the story was internally
consistent. Coherence is not evidence.

### What the CS8427 configuration rules OUT

Decoding stock's ten boot writes against `reference/cs8427/alsa_cs8427.h`
(register numbers from Rev 20 `0x0855`-`0x08A2`):

    reg 0x04 = 0x00   CLOCKSOURCE, RUN clear
    reg 0x13 = 0x10   UDATABUF
    reg 0x04 = 0x00   again
    reg 0x04 = 0x40   CLOCKSOURCE, RUN set
    reg 0x01 = 0x01   CONTROL1
    reg 0x02 = 0x20   CONTROL2
    reg 0x03 = 0x0C   DATAFLOW
    reg 0x05 = 0x05   SERIALINPUT
    reg 0x06 = 0x05   SERIALOUTPUT
    reg 0x11 = 0xFF   RECVERRMASK

Two of these bear directly on P3.1:

  * **CONTROL1 = 0x01** puts INT at active high (INTMASK = 0), but **reg 0x09
    INT1MASK is never written by either image**. With no interrupt source
    unmasked, INT can never assert. **P3.1 is not the CS8427's INT pin.**
  * **CONTROL2 = 0x20** sets HOLD = 01, "replace the current audio sample with
    zero". Stock handles a receiver error by muting **in the data path**, not by
    signalling the MCU. That is the behaviour of a design that does *not* route
    an error pin to a GPIO.

`RECVERRMASK = 0xFF` unmasks every receiver-error source, but that governs the
status register and the HOLD behaviour; it is not by itself evidence of a pin.

One thing the decode does settle in the *other* direction: **CONTROL1 bit 7
SWCLK = 0 means RMCK carries the RECOVERED clock**. So clock mode 1, which
sources both codec clocks from MCLKI, is slaving to the CS8427's recovered
S/PDIF clock. The mode-1 story holds; only the trigger is unknown.

### Honest status of P3.1

**Undetermined.** The firmware evidence rules out INT, and the RCA connector
rules out jack presence. What remains determined is the state machine — pure
input, one-way gate, code 0x0B on a fall from the initial state, code 0x0C only
after that — and the requirement that whatever drives P3.1 must be **high when
an external clock is usable and low when it is not**, since that is the only
assignment under which both handlers do the right thing.

Candidates not yet excluded: a discrete carrier/lock detect on the S/PDIF input
(ordinary for a 2002 design), or a signal unrelated to S/PDIF entirely. Nothing
in either image names it, and no further progress is available from the
firmware alone.

**The bench test is unchanged and is now the only way forward.** Unit A has
S/PDIF out looped to S/PDIF in. Read telemetry block 9 byte 4 (live P3) with the
loop cable seated, then pulled. If P3.1 moves, it tracks S/PDIF carrier or lock
and the polarity falls straight out of the two readings. If it does not move,
P3.1 has nothing to do with S/PDIF and #145 needs a different question.

### The withdrawn reading, for the record: a normally-closed switched jack

With the gate understood, one physical arrangement makes every transition
correct, and it is the ordinary way a switched audio jack is wired — the contact
is **closed when no plug is inserted** and opens when a plug goes in:

    no plug   -> contact closed to ground -> P3.1 = 0
    plug in   -> contact opens, pull-up   -> P3.1 = 1

    boot, nothing plugged   P3.1 = 0, latch 0  -> code 0x0B: init the CS8427,
                                                  start its receiver, run
                                                  INTERNAL 48 kHz.  Correct
                                                  power-on behaviour.
    plug in S/PDIF          P3.1 = 1, latch 1  -> code 0x0C: clock mode 1,
                                                  EXTERNAL.  Correct.
    unplug                  P3.1 = 0, latch 0  -> code 0x0B: back to internal,
                                                  re-init.  Correct.

That is coherent in all three directions, and it explains why code 0x0B does the
initialise-and-probe work while code 0x0C is a bare two-instruction mode switch:
0x0B is the "we are on our own clock, get the receiver ready" path and 0x0C is
"the plug is in, use it".

**It also means the polarity recorded in this project is backwards.** The note
said "P3.1 = S/PDIF clock presence, low = present". The ordering says
**low = jack EMPTY, high = plug inserted**.

**Confidence.** The state machine, the guards, the latch initialisation and the
input-only status are *determined* — read directly from both listings. The
physical mapping to a switched jack is a **reading**: it is the only arrangement
found so far that makes all three transitions correct, but a CS8427 status pin
with the matching polarity would also fit, and nothing in the firmware names the
pin.

**How to settle it, cheaply.** Unit A on the bench has S/PDIF out looped to
S/PDIF in (`BENCH_WIRING.md`). Read telemetry block 9 byte 4 (live P3) with the
loop cable in, then pull it, then re-seat it. If P3.1 tracks the cable, it is
jack presence and the polarity falls straight out of the two readings. If it
does not move at all, it is a CS8427 status line and the loop keeps it in one
state. Either answer unblocks #145, and neither costs a power cycle.

## Why the recorded meaning of P3.1 cannot be right

*(Superseded by the section above — the "incoherent machine" argument here does
not survive the latch. Kept because the per-mode decode below is still correct
and is what the update builds on.)*

Project notes carry "P3.1 = S/PDIF clock presence", low = present. Substituting:

    S/PDIF present (P3.1 low)  -> code 0x0B -> INTERNAL 48 kHz
    S/PDIF absent  (P3.1 high) -> code 0x0C -> EXTERNAL clock

That is backwards in both directions, and the second line is worse than
backwards: slaving to an external clock that is not there would leave the codec
with no clock at all. So the "S/PDIF presence, active low" reading is wrong.

Note also that P3 has internal pull-ups and hw_init writes `P3 = 0xFF`, so an
unconnected or idle P3.1 reads 1 -- and 1 is the state that selects the external
clock. A presence detect wired that way would slave to nothing by default.

## A reading that IS coherent, and is not established

If P3.1 is the CS8427's lock/error indication rather than a jack presence
detect -- 0 meaning "not locked" -- then:

    not locked (0) -> code 0x0B: (re)init the CS8427, start its receiver,
                      probe that it answers, and run on INTERNAL 48 kHz
                      meanwhile
    locked     (1) -> code 0x0C: switch the codec clocks to the recovered
                      external clock (mode 1)

Every element of both handlers fits that: the init-if-needed, the RUN-set, the
presence probe, and the internal-clock fallback all belong to "we do not have a
usable external clock yet"; mode 1 belongs to "we do now".

**This is a reading, not a determination.** What would settle it: the CS8427
datasheet pinout against the board (which pin reaches P3.1), or a hardware
observation -- feed S/PDIF in and out and watch whether P3.1 tracks lock. One
detail that does not fit cleanly and should be explained before this is believed:
code 0x0B sets the "slaved" latch `0x25.5` while selecting the *internal* clock,
which reads more like intent-to-slave than a state description.

## Consequence for task #145

**Do not implement S/PDIF clock slaving yet.** The modes are understood well
enough to write, but the trigger is not, and getting the sense backwards switches
the codec clock source at exactly the wrong times -- the failure mode is audio
that works intermittently depending on what is plugged in, which is among the
hardest things to debug remotely at 2 km per power cycle.

Also worth stating plainly: mboxfw never using mode 1 is not obviously a defect.
A class-compliant device that is always internally clocked is a legitimate and
simpler design; exposing external sync properly would want a UAC clock-selector
unit, which is a descriptor-level feature (#46 territory), not a port of stock's
vendor protocol.

## What this did settle

  * Modes 1/2/3/5 and which clock each selects -- from the datasheet, not
    inference.
  * `RAM[0x08]` as the persisted mode, closing `WHAT_REMAINS_UNKNOWN.md` §2.1.
  * Mode 4 confirmed dead in stock (dispatch at 0x073C tests 2, 3, 5, 1 and falls
    through; work code 0x09 passes 4; nothing posts code 0x09).
  * The CS8427 write-readback presence probe at 0x04DE, previously unremarked.
  * That the "P3.1 = S/PDIF presence" note in the project's memory is wrong and
    should not be relied on.
