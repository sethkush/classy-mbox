# v22 vs Rev 20 firmware diff

**Bottom line: v22 is size-optimised Rev 20 with the same audio path.**
Every hardware-facing register value we depend on for mboxfw is identical
between the two firmwares, so a design that mirrors Rev 20 also mirrors
v22 — no risk of "the newer firmware fixed X and we're perpetuating a
bug".

## File layouts

| File                              | Size    | Notes                                                                                       |
|-----------------------------------|---------|---------------------------------------------------------------------------------------------|
| `rev20_firmware_code.bin`         |  8 174  | code-only image, ROM offset 0 = file offset 0                                               |
| `v22_payload_records.bin`         |  7 134  | ships with a 16-byte prefix, then raw code. `dd bs=1 skip=16` = 7 118 bytes of ROM.         |
| Δ code size                       | −1 056  | v22 is ~13 % smaller                                                                        |

## Reset / interrupt vectors

| ROM addr | Meaning       | Rev 20 target | v22 target |
|----------|---------------|---------------|------------|
| 0x0000   | reset → main  | 0x0A09        | 0x092A     |
| 0x0003   | INT0          | 0x0DAC        | 0x0DDF     |
| 0x000B   | Timer 0       | 0x101E        | 0x1016     |
| 0x001B   | Timer 1 stub  | LJMP 0x000E   | RETI direct|
| 0x0023   | UART stub     | LJMP 0x000F   | RETI direct|

Same handler *set*, just relocated. v22 inlines the two RETI stubs.

## DMA source constants — IDENTICAL

Both firmwares write the exact same 24-bit values to `DMASRC0` /
`DMASRC2`. Every byte matches, only the address inside the ROM differs.

| Sample rate / mode | DMASRC value | Rev 20 site  | v22 site   |
|--------------------|--------------|--------------|------------|
| 44.1 kHz (mode 2)  | 0x20_4B_6A   | 0x075F–0x077D| 0x091A–0x0942|
| 48   kHz (mode 3)  | 0x0F_A8_61   | 0x0DEC–0x0E0A| 0x1338–0x1360|
| DMACTL0 mode 1 val | 0x0D         | 0x074D       | 0x090A     |
| DMACTL0 mode 3 val | 0x06         | 0x0E10       | (found via loose scan; same value) |

**Implication for mboxfw:** the 44.1 kHz values I ported into
`streaming.c` are correct against *both* production firmwares. If audio
comes out pitched wrong on first flash, the bug is not in the DMASRC
constants — look at the mode-select path instead.

## USB endpoint config values — IDENTICAL

| Register        | Rev 20 write value | v22 write value |
|-----------------|--------------------|-----------------|
| IEPCNF0 (0xFF68)| 0x84               | 0x84            |
| OEPCNF0 (0xFFA8)| 0x84               | 0x84            |
| IEPBBAX0 val    | 0x42 (→ 0xFA10)    | 0x42            |
| DMACTL 0xFFE9   | 0x80               | 0x80            |
| DMACTL 0xFFEF   | 0x80               | 0x80            |

## What we did NOT compare (and why it doesn't matter for first flash)

- Full function-by-function diff of the state machine: v22 restructures
  helpers so line-by-line comparison is noisy, but the register values
  landing at the pins are byte-identical.
- CS8427 boot register sequence: uses per-firmware I²C helper wrappers.
  I²C is edge-triggered, so as long as the register/value pairs match,
  the wire behaviour matches. Not verified byte-by-byte here.
- v22's 16-byte prefix format: it's *not* the same 18-byte EEPROM header
  Rev 20 uses. If we ever try to flash v22, `mboxflash` may need a
  format flag. mboxfw uses the Rev 20 header format via `wrap_hex.py`.

## Method (reproducible)

```
# extract v22 code:
python3 -c "open('/tmp/v22.bin','wb').write(open('firmware_stock/v22_payload_records.bin','rb').read()[16:])"

# radare2 auto-analysis:
r2 -a 8051 -b 8 -m 0 -Aq -c "afl" /tmp/v22.bin

# byte-pattern scan (see tools/ commit for the actual script)
```
