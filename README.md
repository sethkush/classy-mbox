<img src="docs/mbox.gif" alt="Digidesign Mbox 1" align="right" width="180">

# classy-mbox

Class-compliant USB Audio firmware for the Digidesign Mbox 1, and a byte-exact
reverse engineering of the stock firmware it replaces.

The Mbox 1 (2002) is a two-channel interface with a Focusrite front end. The
hardware still works. The problem is that it speaks a vendor-specific USB
protocol, and nobody has shipped a driver for it in years.

This project rewrites the firmware in the device's EEPROM as USB Audio Class 1,
so the box enumerates as a standard audio interface on any OS.

Status: working. Both units here run build `0x0061` and stream 2 ch × 24-bit at
44.1 and 48 kHz on Linux and macOS, with S/PDIF I/O, front-panel source
selection, and serial numbers served from EEPROM.

---

## Hardware

| part | role |
|---|---|
| TI TAS1020B | 8051 core + USB peripheral. Boots from EEPROM into 6016 bytes of program RAM. |
| AKM AK5383 | 24-bit ADC (capture) |
| AKM AK4393 | 24-bit DAC (playback) |
| Cirrus CS8427 | S/PDIF transceiver, on SPI, chip-select on a GPIO expander |
| 24C64 | 8 KB I²C EEPROM holding the firmware image |

There is no codec. Capture and playback are two separate AKM chips. The name is
baked into about 170 identifiers and docs here (`codec_write_word`,
`g_codec_state_23`), so read it as "the analog front end".

The 16-bit "codec control word" is two cascaded HEF4094 shift-and-store
registers. Every bit is a pin rather than a register field, which is why it is
write-only and why the CS8427's chip-select rides on it. Input source select is
74HC157 muxes.

## What works

UAC1 capture and playback, 2 ch × 24-bit, 44.1 and 48 kHz. S/PDIF in and out.
Feature Units with mute, and `GET_MIN`/`MAX`/`RES` on the sample-rate control.

The UAC Selector switches analog against S/PDIF. The front panel owns
mic/line/instrument, because the S/PDIF swap is one global bit with no panel
button, while the analog choice is per-channel.

Endpoints are asynchronous, with a feedback endpoint on playback. The clock
generator free-runs from the crystal: the two units differ by 4.263 ± 0.989 ppm
measured host-side, and ACGCAP agrees at 4.53 ppm device-side.

Serial numbers live in EEPROM, written over a vendor request. They survive
reflashing, because DFU writes exactly `payloadSize` bytes and the record sits
past the end.

Both stock images recompile byte-for-byte. `link51.py rev20` and `rev22` rebuild
the original 8174-byte ROMs bit-for-bit from C.

There are DFU flashers for macOS (IOKit) and Linux (pyusb), and 41 verification
gates.

## Quick start

```sh
tools/setup.sh                        # pre-commit hook + toolchain check
cd mboxfw && make MBOX_PID=0x2000     # -> build/mboxfw_flasher.bin
tools/preflight.sh <image.bin>        # the gate runner; run this, not individual gates
```

Flashing:

```sh
sudo tools/mboxflash_linux.py probe | validate | info | flash <img> [--yes]
mboxflash [--serial SN] --probe | --enter-dfu | --flash PATH      # macOS
```

Build with `MBOX_PID=0x2000`. At the default `0x1000` the Linux kernel's `mbox1`
quirk claims the device and `snd-usb-audio` never binds, so no ALSA card
appears. `0x2000..0x200F` are audio-mode aliases the flashers understand.

## Booting and DFU

The TAS1020B boot ROM reads an 18-byte EEPROM header (signature, checksum,
`dataType`, VID/PID), copies the image into RAM and jumps to it.

`dataType` decides the DFU target. The PID does not, and both DFU modes
advertise the same `ffff:fffe` descriptor set.

| EEPROM state | target |
|---|---|
| `dataType` = APPCODE | run the app |
| readable, not APPCODE | app-DFU, writes EEPROM |
| unreadable | bulletproof-DFU, a RAM loader that never writes EEPROM |

Reaching bulletproof-DFU takes a genuinely dead EEPROM: a real SDA short, or a
blank part.

To enter DFU, invalidate the header checksum and power-cycle. A bus reset will
not do it, and a power cycle with a valid image just boots the app.

To recover, flash `firmware_stock/rev20_flasher_payload.bin` or
`rev22_flasher_payload.bin`. Both have been written back to this unit
successfully. On any device not already running mboxfw, flash `safety_net/`
first.

## Layout

| path | what |
|---|---|
| `mboxfw/` | the replacement firmware (SDCC/mcs51) |
| `safety_net/` | minimal provably-flashable image, `bcdDevice=0xDEAD` |
| `sigkill/`, `ramflash/`, `ramloader/` | smaller special-purpose images |
| `firmware_stock/` | RE of stock Rev 20 and Rev 22 |
| `firmware_stock/decomp/` | the byte-exact recompilation, and every `FINDING_*.md` |
| `mboxflash/` | macOS flasher (IOKit, Obj-C) |
| `tools/` | Linux flasher, telemetry reader, 41 gates |
| `plan.md` | phase status and what is left |
| `POLICY.md` | process rules. Read before touching SFR writes, USB, or EEPROM I/O |
| `BRICK_LOG.md` | every soft-brick and its cause |

## Hardware behaviour that looks like bugs

The first capture after power-up starts with 183 ms of digital silence. That is
the AK5383's offset calibration. It costs 8960 LRCK edges and can only be spent
while a stream is running, so stock pays it on every capture.

Every capture opens with a small transient that is gone within 400 ms. The ADC's
high-pass is re-converging, because the converter is not clocked between
streams. Skip the first 400 ms of any measurement.

Audio is clean about 15 s after power-up and fully settled at 30 s. The analog
reference needs that long, and a calibration taken early latches a constant that
can be off by 0.12 of full scale.

## Two rules

Every SFR-touching line carries a citation: `/* Rev 20 fcn.0xXXXX @ 0xYYYY */`,
a TI source reference, or `/* NOVEL — reason: ... */`. A pre-commit hook and a
preflight gate check that the cited address really holds that write, in both
stock images.

"Stock does it" is a reason to investigate, not a reason to ship. `GLOBCTL |= 0x02`
went in on that argument alone and made the device silent on USB.

Every measurement carries an arm whose answer is known in advance. A null from
an instrument that was never connected looks exactly like a null from a refuted
hypothesis. Five measurements died in one session because the stimulus never
fired. If the reference arm shows nothing, the run is void.

The `FINDING_*.md` files are dated records rather than live docs, and some are
superseded. The current inventory is
`firmware_stock/decomp/WHAT_REMAINS_UNKNOWN.md`.

## What is left

1. DAW validation. macOS works at the CLI: enumeration, serial, Selector,
   capture, playback. Logic is untested, and that is the real use case.
2. A 16-bit alternate setting. No modern host needs it; Mac OS 9's Sound Manager
   does. The mechanism is the DMA slot size (`DMATSL 0x03 -> 0x02`, plus the
   endpoint BPS field), leaving the C-port untouched. One measurement gates it:
   whether the DMA takes the first two bytes of each slot or the last two.
3. Parked: the EP0 Y-count at boot-ROM handoff, and naming the vendor part
   behind the codec control word.

## How this was built

Two units I own. The firmware, the reverse engineering, the flashers, the gates
and the docs were written by Claude (Claude Code, Anthropic), working to my
direction over a long series of sessions.

Claude can read both 8 KB stock images end to end, hold the whole register map
at once, and rebuild the ROMs byte-for-byte. It cannot see an LED, power-cycle a
unit a kilometre away, or remember what was working last week. So the rule is
that my observations of the hardware beat its inferences from the documents.
Three times that changed the outcome:

- An evening of findings concluded that mboxfw's I²C access "does not work at
  all" and that the DFU trigger was broken. I said DFU had been working for
  weeks. The instrument had been reading dead memory. The findings were
  retracted that night, and the real bug, XDATA not being implemented on this
  board, turned up the next day.
- The 16-bit mode was written off as unprovable until I mentioned that Sound On
  Sound had run OS 9 screenshots of a driver offering 16 or 24 bit. The
  mechanism was sitting in the stock images.
- The bench has two cross-wired units and a source mux that boots to MIC while
  both wired inputs are LINE. Measurements kept dying until that went into
  `BENCH_WIRING.md`.

Each rule in `POLICY.md` was written after a specific failure, most of them
Claude's. `BRICK_LOG.md` has the ones that reached the hardware.

Header image: Digidesign Mbox press photo.
