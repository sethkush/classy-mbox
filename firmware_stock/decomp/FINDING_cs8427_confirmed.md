# The external chip is a CS8427, and every register write now decodes

Established 2026-07-28 against `reference/cs8427/alsa_cs8427.h`. This closes the
largest remaining semantic gap in the decompilation: the part's identity was
rated only "likely", and every register *name* in the annotations was flagged as
inference while only the numbers and values were certain.

## The identification is no longer an inference

The firmware's control-port writes lead with a constant `0x20` byte (rev20
`0x0C4B`, rev22 `0x0C35`). ALSA gives `CS8427_BASE_ADDR = 0x10` as the part's
base I²C address, and an I²C write to slave `0x10` is exactly
`(0x10 << 1) | 0 = 0x20`.

That alone would be suggestive. What settles it is that **every** register the
firmware touches decodes to something coherent under the CS8427 map, with values
that match what an S/PDIF transceiver in this product would need. A wrong
identification would have to survive nine independent register writes all
landing on sensible fields, which it does not.

## What the bring-up sequence actually does

The ten writes in `audio_path_reconfig_ext_chips` (rev20 `0x080B`) /
`audio_hw_bringup` (rev22 `0x09B6`), in order, decoded:

| # | Reg | Value | ALSA name | Meaning |
|---|-----|-------|-----------|---------|
| 1 | `0x04` | `0x00` | CLOCKSOURCE | `RUN = 0` — **stop the clock** before reconfiguring |
| 2 | `0x13` | `0x10` | UDATABUF | user-data buffer control |
| 3 | `0x04` | `0x00` | CLOCKSOURCE | clock still stopped |
| 4 | `0x04` | `0x40` | CLOCKSOURCE | `RUN = 1`, `CLK256` (256·Fso), `OUTC = 0` (OMCK), `RXD = ILRCK` |
| 5 | `0x01` | `0x01` | CONTROL1 | `TCBLDIR = 1` — TCBL is an **output**; no mutes, INT active high |
| 6 | `0x02` | `0x20` | CONTROL2 | `HOLD = 01` — **replace sample with zero (mute) on receiver error**; stereo RX and TX |
| 7 | `0x03` | `0x0C` | DATAFLOW | `TXD = 01` serial-input-port → AES3 transmitter; `SPD = 10` AES3 receiver → serial output port |
| 8 | `0x05` | `0x05` | SERIALINPUT | slave, 24-bit, left-justified + one-clock delay + LRCK polarity — **I²S** |
| 9 | `0x06` | `0x05` | SERIALOUTPUT | slave, 24-bit — **I²S**, same format |
| 10 | `0x11` | `0xFF` | RECVERRMASK | **mask every receiver error** — no interrupts from the AES3 receiver |

Three previously hedged claims are now facts:

* **The `0x04 = 0x00` … `0x04 = 0x40` bracket is stop-clock / start-clock.**
  `extchip_write_reg4_zero.c` guessed "disable while a register is changed, then
  re-enable", calling it "an inference stacked on an inference". Bit 6 of
  CLOCKSOURCE is `RUN`, 0 = clock off. The guess was right.
* **Registers 5 and 6 really are the serial input and output format registers**,
  and both are configured as 24-bit I²S in slave mode — consistent with the
  TAS1020B driving the clocks and the 24-bit format in the stock descriptors.
* **Register 4 really is the clock-source register.**

And one thing nobody had claimed: **register 7 (DATAFLOW) is the S/PDIF routing
switch.** `0x0C` sends the serial audio input port to the AES3 transmitter and
the AES3 receiver to the serial audio output port. That is the whole S/PDIF
path — playback out to S/PDIF, S/PDIF in to capture — in one byte.

## cmd7 / cmd8 write the transmitted sample rate

`cmd7_set_cpt_mode2_progchip` (rev20 `0x047D`) and `cmd8` (`0x049F`) write
registers `0x12`, `0x23` and `0x24`. Their annotations called "transmitted
sample rate" an inference from the two handlers' mode association.

ALSA names `0x12` `CSDATABUF` (channel-status data buffer) and `0x20`
`CORU_DATABUF`, a **24-byte buffer area** spanning `0x20..0x37`. So `0x23` and
`0x24` are **channel-status bytes 3 and 4**, and in AES3/S-PDIF consumer channel
status byte 3 carries the sample-frequency field.

The two handlers are therefore writing the sample rate into the outgoing S/PDIF
channel status, which is exactly what the inference said. It is now supported by
the register map rather than by the handlers' names.

## What this does NOT settle

* **IRAM 0x23.2, 0x23.3 and 0x23.4** are still unidentified. They are set during
  bring-up in both images and are not CS8427 traffic — they go to the panel
  shift registers. Nothing here touches them.
* **The bare chip-select pulse** before the ten writes (rev20 `0x084B..0x0854`)
  is still unexplained. It clocks no data. A protocol-mode select remains the
  obvious guess and is still a guess: ALSA's header covers registers, not the
  part's mode-selection pin behaviour.
* **Register `0x13 = 0x10`** is named (UDATABUF) but the *choice* of `0x10` is
  not decoded here; the U-data mode bits are not in the ALSA header.
* **What `0xFF` means on shift-register chain A** is unrelated to this part.

## Consequence for task #145 (S/PDIF clock slaving)

Mode 5's reading as "externally clocked / S/PDIF-slaved" was an inference from
`CPTRXCNF4` alone. DATAFLOW `0x0C` and CLOCKSOURCE `RXD = ILRCK` now give the
routing and clock-source side of that picture from the chip's own map, which is
the missing half of what #145 needs.
