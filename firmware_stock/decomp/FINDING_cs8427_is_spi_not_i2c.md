# Stock talks to the CS8427 over SPI, not I²C — mboxfw talks I²C

Found 2026-07-30 by reading the routine in the Ghidra listings rather than
trusting the existing description. This is a candidate root cause for the dead
audio path, and it is decidable from the bytes.

Rev 20 `0x0C45`, named `cs8427_ctl_write`. Rev 22 `0x0C31`, which Ghidra
independently names **`spi3wire_write_3bytes`** — the other image's listing
reached the same conclusion this document argues for, and nothing in the project
had picked it up.

Rev 22 body confirmed identical in shape: `MOV R3,#0x20` at `0x0C35`,
`CLR 0x2f` at `0x0C39`, data on P1.4 (`ORL 0x90,#0x10` / `ANL 0x90,#0xef` at
`0x0C50`/`0x0C55`), clock on P1.3 (`0x0C58`/`0x0C5B`), three-phase byte counter
at `0x0C61`/`0x0C6C`, `SETB 0x2f` at `0x0C77`.

## What stock actually does

    CODE:0c45  MOV 0x33,R7      ; save the register number (MAP)
    CODE:0c47  MOV R1,0x05      ; R1 = R5 = the value
    CODE:0c49  MOV R4,#0x8      ; 8 bits
    CODE:0c4b  MOV R3,#0x20     ; FIRST byte out = 0x20, the chip-address byte
    CODE:0c4d  MOV R2,#0x1      ; phase 1 of 3
    CODE:0c4f  CLR 0x2f         ; IRAM 0x25.7 = 0
    CODE:0c51  LCALL 0x0e62     ; publish the 16-bit latch word  <-- ASSERT
    ; --- per-bit loop, run three times (0x20, MAP, data) ---
    CODE:0c63  JNB 0xe0,0x0c6b  ; ACC.0
    CODE:0c66    ORL 0x90,#0x10 ;   P1.4 = 1     data
    CODE:0c6b    ANL 0x90,#0xef ;   P1.4 = 0
    CODE:0c6e  ORL 0x90,#0x08   ; P1.3 = 1       clock
    CODE:0c71  ANL 0x90,#0xf7   ; P1.3 = 0
    ; --- after the third byte ---
    CODE:0c8d  SETB 0x2f        ; IRAM 0x25.7 = 1
    CODE:0c8f  LCALL 0x0e62     ; publish                        <-- DEASSERT
    CODE:0c92  RET

Three properties, all read off the listing:

  1. **There is no START and no STOP.** Data is written only while the clock is
     low (`ORL 0x90,#0x10` / `ANL 0x90,#0xef` at `0x0C66`/`0x0C6B`, both before
     the clock pulse at `0x0C6E`). An I²C START requires SDA to fall *while SCL
     is high*, and a STOP requires SDA to rise while SCL is high. Neither
     transition exists anywhere in this routine.
  2. **There is no ACK slot.** Between bytes (`0x0C77`-`0x0C8B`) the code
     reloads the byte and the bit counter and jumps straight back into the loop.
     24 clocks total for three bytes, not 27.
  3. **The transaction is framed by IRAM 0x25.7**, driven through the 16-bit
     latch: cleared and published before the first byte, set and published after
     the last. That is a chip select, held low for exactly the length of one
     register write.

`0x20` as the leading byte, then MAP, then data, framed by an active-low select,
with data clocked on a rising edge and no bus arbitration — that is the CS8427's
**SPI control-port mode**, not its I²C mode.

## What mboxfw does

`mboxfw/src/cs8427.c` implements I²C:

    P1 |= (SCL | SDA); P1 &= ~SDA;   /* SDA falls while SCL high = START */
    ...
    P1 &= ~SDA; P1 |= SCL; P1 |= SDA; /* SDA rises while SCL high = STOP */

on the same two pins, with the same `0x20` address byte — and it **never touches
IRAM 0x25.7**. `IRAM23_IRAM25_ANNOTATION.md` already recorded that nothing in
mboxfw corresponds to 0x25 bits 4-7; this is what that costs.

So mboxfw drives the right pins with the wrong framing, and leaves the chip
select unasserted for the whole conversation.

## Why this is the leading candidate for silent audio

If the board straps the CS8427 into SPI mode — which stock's framing says it
does — then every mboxfw CS8427 register write has been landing on a chip whose
select was never asserted, and whose control port is not looking for START/STOP.
`verify_cs8427.py` passes because it checks that the ten register *values*
appear in the image, which they do; no gate checks the framing.

That is consistent with what hardware shows: capture returns a fixed 8-frame
artifact with no audio content at either rate, and the artifact is locked to the
sample clock rather than to any input (`FINDING_capture_8frame_artifact.md`).

**Confidence: high on the protocol reading, which is a direct decode of the
bytes. Lower on "this is THE cause of silence"** — it is one necessary
condition, and #147's framing artifact may have a separate cause.

## What to change

`cs8427_write()` has to become: assert 0x25.7 low and publish the codec word;
shift 0x20, MAP, data MSB-first on P1.4 with P1.3 as the clock, no START, no
STOP, no ACK; then set 0x25.7 and publish again. That makes `cs8427.c` depend on
`codec_write_word()`, which is a real coupling and is exactly how stock is built
— the CS8427's chip select is a bit of the codec latch word.

A gate belongs here too: `verify_cs8427.py` currently proves the values are
present and should also prove the framing, or it will keep passing a driver that
cannot talk to the part.

## Second correction from the same read

`FINDING_clock_modes_and_p31.md` describes Rev 20 `0x04DE`-`0x04F8` as "a
write-readback presence probe" of the CS8427 — "read CS8427 reg 0x1F",
complement, write back, compare, and assert the external-clock panel line "only
if the CS8427 answered".

That is wrong. `0x0CDD` and `0x0BEE` both write slave address **0xA0** to
`I2C_SADDR` (0xFFC3) and use the TAS1020B's hardware I²C peripheral at
0xFFC0-0xFFC3. 0xA0 is the 24Cxx EEPROM; the CS8427 is 0x20 on the bit-banged
bus above. The Ghidra listing names the two routines `i2c_eeprom_read_byte` and
`i2c_eeprom_write_byte`, and names the caller `cmd11_eeprom_selftest`.

R7 = 0x1F and R5 = 0xFF are the two address bytes, so the target is EEPROM
address **0x1FFF** — the last byte of the 8 KB part. The sequence reads it,
complements it, writes it back, reads it again and compares.

    CODE:04fa  CJNE A,0x2c,0x04ff
    CODE:04fd  CLR 0x16            ; IRAM 0x22.6 = 0, only if it verified

So the panel bit is cleared on a successful **EEPROM write-verify**, not on a
CS8427 response. Two consequences:

  * `MUX_IRAM22_ANNOTATION.md`'s "bit 6 RESOLVED: asserted (low) when the
    external S/PDIF clock is in use" is at best incomplete. 0x22.6 has two
    unrelated writers: this one, and the derived
    `0x22.6 = !(0x25.4) && !(0x25.5)` in the source-cycle tail.
  * **Stock performs a destructive write to EEPROM 0x1FFF** as part of a work
    code. Anything that assumes the EEPROM is read-only at runtime, including
    our image-layout arithmetic, has to leave that byte alone.
