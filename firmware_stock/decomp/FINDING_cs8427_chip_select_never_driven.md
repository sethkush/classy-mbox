# mboxfw never drives the CS8427's chip select — and that decides which bus the part is on

2026-07-31, following the #147 bit-level pass. Prompted by "any more digging?"
and by noticing that my own previous writeup asserted CS8427 power-on defaults
it had explicitly flagged as unverified. The CS8427 datasheet (DS477F5) is now
in hand and the answer is sharper than the assumption was.

`tools/sim_cs8427_bitstream.py` reproduces the result.

## The setup

`FINDING_cs8427_is_spi_not_i2c.md` (#157) established that stock frames its
CS8427 register writes as 3-wire SPI, with **IRAM 0x25.7 as an active-low chip
select** published through the 16-bit latch — `CLR 0x2f` before the first byte,
`SETB 0x2f` after the last.

`mboxfw/src/codec.c:27` declares `g_codec_state_25 = 0`, and **no code in mboxfw
ever sets bit 7**. Grep confirms: the only writers are the two zeroing sites
(`codec.c:96`, `power.c:74`) and `streaming.c:179`, which touches
`g_codec_state_23`. So the chip select sits **asserted for the entire life of
the firmware**, and `cs8427.c` bit-bangs P1.3/P1.4 underneath it.

## If the part is in SPI mode, nothing lands — ever

DS477F5 §9.1: data is clocked in on the **rising edge of CCLK**; one CS-low
period is one continuous shift; byte 0 is the chip address (`0010000b` + R/W),
byte 1 is the MAP, and every byte after that is data. **The address match happens
once per CS-low period.** With CS never deasserted, all ten of `cs8427_boot_init`'s
writes are a single transaction and the part gets exactly one chance to sync.

Simulating mboxfw's actual pin toggling — including the rising SCL edge inside
`cs8427_start()`, the ACK-slot clock in `cs8427_shift_byte()`, the stop-condition
clock, and the fact that `cs8427_stop()` leaves SCL **high** so the next
`start()` produces no edge at all:

    total bits clocked into the CS8427: 281
    90 41 20 11 04 4e 21 10 41 20 11 04 12 81 10 40 60 31 04 0a 41 10 40 e1 91 04 16 0b 10 41 a0 b1 04 47 ff

    byte0 (chip addr + R/W) = 0x90   -> must be 0x20   MISMATCH
    byte1 (MAP)             = 0x41

0x90 is `1001000` + R/W, not `0010000`. The transaction is rejected at the first
byte, and because CS never rises the part can never resynchronise. **In SPI mode
the CS8427 is guaranteed to remain at power-on reset for as long as mboxfw runs,
and no amount of retrying would change that.**

## But mboxfw's wiring means the part is probably NOT in SPI mode

DS477F5 §9, verbatim:

> SPI mode is selected if there is a high to low transition on the AD0/CS pin
> after the RST pin has been brought high. I²C mode is selected by connecting the
> AD0/CS pin to VL+ or DGND, thereby permanently selecting the desired AD0 bit
> address state.

**The mode is chosen by whether that pin ever moves.** Stock's `CLR 0x2f` /
`SETB 0x2f` pair gives a high→low transition on its *second* register write and
every one after, so stock selects SPI. mboxfw never sets bit 7, so after RST
rises (IRAM 0x23.4) the pin is static low forever — which is the datasheet's own
description of **selecting I²C mode with AD0 = 0**.

And in I²C mode mboxfw's framing is structurally correct:

  * `cs8427_start()` — `P1 |= SCL|SDA` then `P1 &= ~SDA`: SDA falls while SCL is
    high. That is a valid START.
  * `cs8427_shift_byte()` — data changes while SCL is low, one clock per bit.
  * `cs8427_stop()` — SDA low, SCL high, SDA high. A valid STOP.
  * The address byte 0x20 is 7-bit `0010000` + write. The CS8427's I²C address is
    `0010` + EMPH + AD1 + AD0, and AD0 = 0 here, so it matches **iff EMPH and AD1
    are both strapped 0** on this board.

So the honest position is a **fork**, and the previous writeup picked the wrong
branch by assumption:

| if the part is in… | then | consequence for #147 |
|---|---|---|
| SPI mode | address byte is 0x90, nothing lands, CS8427 at reset defaults | CS8427 is an unconfigured, non-driving part |
| I²C mode (likely, per §9) | all ten writes land with stock's values | **CS8427 is correctly configured and is exonerated** |

**This reframes #157.** The task is written as "rewrite `cs8427.c` to stock's SPI
framing". Doing only that would be wrong twice over: the essential missing piece
is *driving the chip select* (without the high→low transition the part is not in
SPI mode and SPI framing cannot work), and if the part is currently on I²C and
the writes are landing, switching framing without the select would **break a path
that currently works**.

## CS8427 facts now nailed down, replacing earlier assumptions

  * **`CLOCKSOURCE` (0x04) bit 6 RUN — "Default = '0'"**, and §15.1: "Writing a 1
    to the RUN bit will then cause the part to leave the low power state and
    begin operation. **After the PLL has settled, the AES3 and serial audio
    outputs will be enabled.**" So with RUN = 0 the serial audio output is *not*
    enabled — an unconfigured CS8427 is a **silent** part, not a contending one.
  * **`SERIALOUTPUT` (0x06) SOMS — "Default = '0'" = slave.** OSCLK and OLRCK are
    inputs at reset, so there is no clock contention with the TAS's SCLK2/LRCK2
    either.
  * **SDOUT has no tri-state control bit.** Pin 18 is "Serial Audio Output Data
    (Output)"; the register map has SOMS/SOSF/SORES/SOJUST/SODEL/SOSPOL/SOLRPOL
    and no output enable. Two permanently-driven sources therefore cannot both sit
    on CDATI — so the analog/S-PDIF selection has to be an external switch, which
    fits the HEF4094 shift register (U23) that a public Mbox 1 teardown describes
    as switching parts of the circuitry on and off, and fits IRAM 0x25.4 being the
    Selector Unit position.
  * **`SODEL` default 0 vs stock's 1.** SODEL = 1 puts the MSB in the *second*
    OSCLK period after the OLRCK edge — I²S, matching the TAS's DDLY = 1. The
    default puts it in the first. That is a one-bit-clock framing difference, and
    the #147 constant has exactly a one-bit-clock framing offset. Suggestive, and
    only that: it is downstream of RUN, which gates whether SDOUT is enabled at
    all.

## A red herring, recorded so it is not re-found

The datasheet's "12.5%" (§20.1) is the PLL's varispeed tracking range around its
nominal centre sample rate. It is **not** 1/8 of a frame count and has nothing to
do with the 3-in-8 duty cycle. I went looking at it precisely because 12.5% = 1/8.

## What settles the fork, cheaply

The CS8427 control port is **readable** in both modes (§9.1 for SPI, §9.2 for
I²C). A telemetry block that reads back one register — `CLOCKSOURCE` (0x04) is
the best single choice, since RUN answers "is it running" and the value answers
"did our writes land" in one byte — settles the mode question, the
did-it-configure question, and the CS8427's role in #147 together. One flash, no
ambiguity, and it should go in the same image as #157 rather than before it.
