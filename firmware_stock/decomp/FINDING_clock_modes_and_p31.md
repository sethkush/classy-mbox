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

## Why the recorded meaning of P3.1 cannot be right

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
