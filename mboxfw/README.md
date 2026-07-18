# mboxfw — Mbox 1 class-compliant firmware

Custom class-compliant USB Audio Class 1 firmware for the Digidesign
Mbox 1 (TAS1020A + CS8427 + audio codec), replacing Digi's vendor-specific
Rev 20 / v22 firmware.

## Status

**Full skeleton builds — 1888 bytes of code (~23% of the 8 KB EEPROM
budget).** Every major module is in place and every SDCC compile is
clean.

| Module                | What it does                                        |
|-----------------------|-----------------------------------------------------|
| `hw_init.c`           | 8051 SFRs, C-port, DMA — verbatim port of Rev 20's `fcn.0x08CB` |
| `cs8427.c`            | Bit-banged I²C + 10-register boot sequence          |
| `codec.c`             | Codec/mux state init — port of Rev 20 `fcn.0x0970`  |
| `mux.c`               | 74HC595 input-mux shift register                    |
| `buttons.c`           | P3.3 / P3.4 source cycle + P3.5 phantom toggle      |
| `descriptors.c`       | Full UAC1 descriptor bundle (2ch × 24-bit × 44.1/48) |
| `usb.c`               | EP0 SETUP dispatcher, VECINT poll, descriptor push  |
| `streaming.c`         | EP1 IN + EP2 OUT activation on SET_INTERFACE        |
| `main.c`              | Reset entry + main loop                             |

## What should happen when flashed

1. TAS1020A boot ROM loads our firmware from EEPROM.
2. `main()` runs `hw_init()` → `cs8427_boot_init()` → `usb_init()`.
3. USB enumerates as VID:PID `0x0DBA:0x1000` with two AudioStreaming
   interfaces + one AudioControl interface. macOS should recognise it
   as a class-compliant audio device with no vendor driver.
4. Pressing the front-panel source buttons cycles the mux state.
5. Pressing the 48V button toggles phantom power.

## Pre-flash verification

Two host-side checks catch the highest-frequency bug classes without
touching hardware:

```
tools/sim_smoke.sh                # ucSim/s51 boot smoke — proves the init
                                  # sequence reaches the main loop with no
                                  # trap or infinite loop
python3 tools/verify_descriptors.py   # walks the compiled UAC1 descriptor
                                      # bundle: bLength / wTotalLength /
                                      # cross-refs / channel & subframe sanity
```

## Realistic risks on first flash (ordered)

| Risk | Likely cause | Where to look |
|------|--------------|---------------|
| Won't enumerate | EP0 buffer address (0xFA10) wrong for this TAS1020A variant | Watch `usb_service()`, verify with a USB analyser. IEPCNF0/OEPCNF0 = 0x84 (matches Rev 20 disasm @ 0x099e/0x09a7). |
| Enumerates but distorted | codec state adjuster (`fcn.0x0E62` — 16-bit shift + latch on P1.1) not yet ported. `codec_init()` clears the state RAM and kicks the mux but skips the final shift. | Port `cs_state_adjust()` from `fcn.0x0E62` body |
| Audio pitched wrong at 44.1 kHz | `dma_program_44k1()` uses Rev 20 mode-2 constants (0x20_4B_6A) — confirmed identical in v22 too, but "mode 2 = 44.1 kHz" is inferred, not fully traced | If pitched wrong, swap the two DMASRC value sets in `streaming.c` — that's the fastest falsification |
| Random weirdness | Interrupt vectors — we're currently polling, not using ISRs | Rev 20's ISRs at 0x03 (INT0) and 0x0B (Timer 0); we skip both |

## Build

```
brew install sdcc          # SDCC 4.6+ (mcs51 target)
make                       # → build/mboxfw.ihx + build/mboxfw_flasher.bin
```

## Flash (untested end-to-end)

```
# 1. Put the Mbox into DFU mode: hold front-panel source button
#    while plugging the USB cable in
# 2. Verify with:
../mboxflash/mboxflash --probe
# 3. Push the firmware:
../mboxflash/mboxflash --flash mboxfw/build/mboxfw_flasher.bin
# 4. Power-cycle the Mbox
```

## Build

```
brew install sdcc          # SDCC 4.6+ (mcs51 target)
make
```

Output: `build/mboxfw.ihx` (Intel HEX). Still needs to be re-wrapped in
Digi's TI Intel-HEX record format (`{u32 length BE}{u32 addr BE}{u32
type BE}{data}`) before `mboxflash --flash` can push it to EEPROM. That
wrapper is TODO under `tools/`.

## Layout

```
mboxfw/
├── Makefile           # SDCC build
├── include/
│   ├── regs.h         # TAS1020A UIFR register subset + P1/P3 pin masks
│   ├── mux.h
│   ├── cs8427.h
│   └── buttons.h
└── src/
    ├── main.c         # Reset entry + main loop
    ├── hw_init.c      # Master hardware init (ports Rev 20 fcn.0x08CB)
    ├── mux.c          # 74HC595 input-mux driver (ports fcn.0x0F0C)
    ├── cs8427.c       # CS8427 bit-banged I²C + boot sequence
    ├── buttons.c      # P3 button poller (ports fcn.0x0ED5)
    └── usb.c          # STUB — descriptors + streaming TODO
```

## Where each hardware register/pin comes from

Every register value and every P1/P3 pin binding in this firmware was
verified against Rev 20's disassembly. See
`firmware_stock/disasm/NOTES.md` for the register table, function map,
and reasoning behind each choice.
