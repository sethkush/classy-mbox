# mboxfw — Mbox 1 class-compliant firmware

Custom class-compliant USB Audio Class 1 firmware for the Digidesign
Mbox 1 (TAS1020A + CS8427 + audio codec), replacing Digi's vendor-specific
Rev 20 / v22 firmware.

## Status

**Full skeleton builds — 2064 bytes of code (~25% of the 8 KB EEPROM
budget).** Every major module is in place, every SDCC compile is
clean, and all four pre-flash checks pass.

| Module                | What it does                                        |
|-----------------------|-----------------------------------------------------|
| `hw_init.c`           | 8051 SFRs, C-port, DMA — verbatim port of Rev 20's `fcn.0x08CB` |
| `cs8427.c`            | Bit-banged I²C + 10-register boot sequence          |
| `codec.c`             | Codec/mux state init (`fcn.0x0970`) + state adjuster (`fcn.0x0E62`) + 16-bit bit-serial write (`fcn.0x0E74`) |
| `isr.c`               | Minimal INT0 + Timer 0 ISR stubs — reload TH0, count firings |
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

Six host-side checks catch the highest-frequency bug classes without
touching hardware:

```
tools/sim_smoke.sh                # ucSim/s51 boot smoke — proves the init
                                  # sequence reaches the main loop with no
                                  # trap or infinite loop
python3 tools/verify_descriptors.py   # walks the compiled UAC1 descriptor
                                      # bundle: bLength / wTotalLength /
                                      # cross-refs / channel & subframe sanity
python3 tools/verify_usb_init.py      # confirms the enumeration-critical
                                      # SFR writes (EP0 buffer addrs, CNF
                                      # bytes) appear in the compiled image
                                      # with the exact values Rev 20 uses
python3 tools/verify_cs8427.py        # confirms the 10-register CS8427 boot
                                      # sequence appears in the image
python3 tools/verify_setup_paths.py   # confirms USBFADR is written from the
                                      # deferred-address slot (SET_ADDRESS
                                      # actually lands), Digi's enter-DFU
                                      # class request is recognised, and
                                      # reply_zero_length is present
mboxflash --validate FILE             # static wire-format check on the
                                      # wrapped .bin: record alignment,
                                      # header chksum, VID/PID, size limits
```

**Never flash without all six green.** verify_setup_paths.py in
particular guards against the exact class of enumeration bug that
soft-bricked the device on 2026-07-18 (SET_ADDRESS ACKed but USBFADR
never written).

## Flash-morning safety net

Before running `--flash`, dump the current EEPROM so a bad flash is
recoverable:

```
# Enter DFU, then dump:
mboxflash --enter-dfu
# (wait for the device to re-enumerate as 0xFFFF:0xFFFE)
mboxflash --dump backups/rev20-$(date +%s).bin
# If mboxfw is broken, restore with:
mboxflash --flash backups/rev20-YYYYMMDD-HHMMSS.bin
```

If enumeration fails after flashing, capture and diff the USB traffic
against a known-good UAC1 device to see which SETUP is going wrong:

```
sudo tools/capture_enum.sh baseline 8   # capture a working UAC1 device
sudo tools/capture_enum.sh mboxfw   8   # then the Mbox after flashing
diff -u captures/baseline-*.setup.txt captures/mboxfw-*.setup.txt
```

## Realistic risks on first flash (ordered)

| Risk | Likely cause | Where to look |
|------|--------------|---------------|
| Won't enumerate | EP0 buffer address encoding mismatch (was a real bug — EP0 IN/OUT bases had been swapped in an earlier draft; fixed 2026-07-18 after `verify_usb_init.py` caught OEPBBAX0 = 0x43 instead of 0x42). Now `verify_usb_init.py` pins all four critical writes to Rev 20's exact values. | Watch VECINT + USBSTA in `usb_service()`; verify with a USB analyser if enumeration hangs before SET_ADDRESS. |
| Enumerates but silent/distorted | With `fcn.0x0E74` confirmed dead in Rev 20 (see NOTES.md), the codec chip auto-configures from I²S clocks — so if audio is broken, the fault is more likely in: (a) mux state @ RAM[0x22] not reaching the 74HC595, (b) CS8427 boot sequence misfiring, (c) wrong DMASRC constants for the reported rate, or (d) C-port I²S clock misprogrammed. `verify_cs8427.py` covers (b); (a) and (c) verify against Rev 20's exact bytes. (d) needs a scope. | Run `verify_cs8427.py`; inspect CPTCNF*/CPTBRT/CPTBRR writes in hw_init.c against fcn.0x08CB. |
| Audio pitched wrong at 44.1 kHz | `dma_program_44k1()` uses Rev 20 mode-2 constants (0x20_4B_6A) — confirmed identical in v22 too, but "mode 2 = 44.1 kHz" is inferred, not fully traced | If pitched wrong, swap the two DMASRC value sets in `streaming.c` — that's the fastest falsification |
| Random weirdness | ISR stubs (`isr.c`) now claim vectors 0x03 (INT0) and 0x0B (Timer 0) so `EA = 1` no longer risks jumping into random code memory. Handlers just reload TH0 and bump a counter — real USB SOF handling is still done by polling in `usb_service()`. | If the CPU hangs after ~1 s of run time, disable EA in `main()` and see if the hang goes away — that would implicate an ISR frequency issue. |

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
