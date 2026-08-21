# mboxfw — Mbox 1 class-compliant firmware

USB Audio Class 1 firmware for the Digidesign Mbox 1, replacing Digi's
vendor-specific Rev 20 / Rev 22 images. See the [top-level README](../README.md)
for the project as a whole and [`POLICY.md`](../POLICY.md) for the process rules.

## Status

Shipping `0x0061`. 5814 of 6016 bytes of program RAM, 41/41 preflight gates.

Both units stream 2 ch x 24-bit at 44.1 and 48 kHz on Linux and macOS, with
S/PDIF I/O, a UAC Selector for analog-vs-S/PDIF, Feature Units with mute,
front-panel source and mono control, and serial numbers served from EEPROM.

| module | what |
|---|---|
| `main.c` | reset entry, init order, main loop |
| `hw_init.c` | 8051 SFRs, C-port, DMA, ACG clock words |
| `usb.c` | EP0 SETUP dispatch, class requests, the Selector and Feature Units |
| `descriptors.c` | the UAC1 descriptor bundle and all string descriptors |
| `streaming.c` | EP1 IN / EP2 OUT activation on SET_INTERFACE, feedback endpoint |
| `codec.c` | the 16-bit control word shifted into the HEF4094 pair |
| `cs8427.c` | CS8427 boot sequence (SPI, chip-select on the expander) |
| `mux.c`, `buttons.c` | 74HC157 source muxes and the front-panel buttons |
| `eeprom.c` | I2C reads, and the provisioning write path for serials |
| `telemetry.c` | 9 diagnostic blocks, compiled out of release builds |
| `isr.c`, `power.c` | interrupt vectors, suspend/resume |

## Build

```sh
make MBOX_PID=0x2000      # -> build/mboxfw.ihx + build/mboxfw_flasher.bin
make CANARY_LED=1         # LED progress-ladder diagnostic build
```

Needs SDCC 4.6+ with the mcs51 target (`brew install sdcc`). `wrap_hex.py`
converts the Intel HEX into the TI record format the boot ROM expects; it runs
as part of the build.

Always build with `MBOX_PID=0x2000`. At the default `0x1000` the Linux kernel's
`mbox1` quirk claims the device and `snd-usb-audio` never binds, so no ALSA card
appears. EP0 telemetry keeps working, which makes this easy to misdiagnose.

## Before flashing

```sh
../tools/preflight.sh build/mboxfw_flasher.bin
```

41 gates covering descriptors, SFR writes against both stock images, citation
targets, reachability, init order, code size and the flasher's own wire format.
Run this rather than individual gates, and do not flash without it green.

## Flashing

The button-hold trigger that earlier versions of this file recommended does not
work. It never once succeeded and was removed on 2026-08-05; `main.c` records
three attempts in `BRICK_LOG.md`.

DFU is entered by invalidating the EEPROM header checksum and power-cycling:

```sh
mboxflash --enter-dfu      # or: tools/mboxflash_linux.py
# then UNPLUG AND REPLUG. A bus reset is not enough.
mboxflash --flash build/mboxfw_flasher.bin
# then power-cycle again -- the post-manifest bus reset does not deliver the
# app switch on either flasher.
```

With two units on one bus, select with `--serial <SN>`. Both flashers refuse to
guess: the PID identifies the product, not the unit.

There is no backup to take. Earlier text here recommended dumping the EEPROM
first, but `backups/` never held a genuine device dump. The two stock payloads
(`../firmware_stock/rev20_flasher_payload.bin` and `rev22_flasher_payload.bin`)
restore the device completely, and both have been written back to this unit
successfully.

Flash `safety_net/` first on any device not already running mboxfw (POLICY §4).

## Init order differs from stock

Stock does hardware init and then brings USB up. mboxfw calls `usb_init()`
first, so enumeration starts early. Any stock write copied into `hw_init()`
therefore lands on a live USB engine. Copying a write without copying its
ordering has bitten this project more than once.

## Telemetry

`TELEMETRY.md` documents 9 blocks read with `../tools/mboxtlm.py`. Every read is
exactly 8 bytes (one EP0 packet) and every vendor request is DEVICE recipient.
An interface-recipient request gets EBUSY once `snd-usb-audio` binds the
device.

Bump `TLM_BUILD_ID` when flashing and read block 0 to prove which build is
running. The build id also rides in `bcdDevice` as `1.NN`, so it stays readable
when a class driver owns every interface. That constrains build ids to valid
BCD, so they skip A-F (`0x0059` -> `0x0060`).

Release builds serve none of it. `../tools/check_release_surface.py`
enumerates what a shipping unit still answers; as of `0x0061` that is
`TLM_REQ_ENTER_DFU` and nothing else. Any question phrased "read block N" needs
a diagnostic build, and therefore a flash and a power cycle.

## Citations are enforced

Every SFR-touching line carries `/* Rev 20 fcn.0xXXXX @ 0xYYYY */`, a TI source
reference, or `/* NOVEL -- reason: ... */`. A pre-commit hook and a preflight
gate verify the cited address actually holds that write, in both stock images.
`../tools/rev20_diff_justifications.md` justifies every mboxfw-vs-stock
difference. A gate reads that table, so a wrong row is worse than no row.
