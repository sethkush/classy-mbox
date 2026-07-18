# mboxfw — Mbox 1 class-compliant firmware

Custom class-compliant USB Audio Class 1 firmware for the Digidesign
Mbox 1 (TAS1020A + CS8427 + audio codec), replacing Digi's vendor-specific
Rev 20 / v22 firmware.

## Status

**Skeleton builds.** ~721 bytes of code out of the 8 KB EEPROM budget.
Hardware init, CS8427 boot sequence, 74HC595 input-mux driver, and
button poller are ported verbatim from the Rev 20 disassembly.

**Not yet:**
- USB descriptor set (UAC1 for 2 ch × 24 bit × 44.1/48 kHz)
- EP0 setup handling (standard UAC1 control interface)
- EP3 audio isochronous streaming
- Codec (CS4272-like) register programming

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
