# mboxfw never releases the external-chip RESET, and configures the CS8427 before it exists

2026-07-31. Found by re-examining the *method* behind
`FINDING_cs8427_chip_select_never_driven.md` rather than its conclusion, after
"any more digging?" was asked a second time in identical words. That document
asserted a pin state without checking when the pin is first driven. Checking it
turns up something that makes the whole SPI-vs-I²C fork moot.

## mboxfw holds the reset asserted forever

IRAM 0x23.4 is the external-chip RESET, **active low**, which stock releases
exactly once at boot:

    SETB bit 0x1c   (IRAM 0x23.4)    Rev 20 0x0840      Rev 22 0x09E5, 0x0BB5

Byte-scanned both images for every opcode that can write that bit
(`D2`/`C2`/`92`/`B2` + `1C`); those are the only sites.

In mboxfw, the mirror is `g_codec_state_23`, and **every** write to it is:

    codec.c:26     g_codec_state_23 = 0;     (initialiser)
    codec.c:95     g_codec_state_23 = 0;     (codec_init)
    power.c:73     g_codec_state_23 = 0;     (resume path)
    streaming.c:179 g_codec_state_23 |= 0x0C; (the #147 mute/enable pair)

**Bit 4 (0x10) is never set.** So the reset line is driven low the first time
the codec word is published and stays there for the life of the firmware.

DS477F5 §15.1, verbatim:

> When RST is low, the CS8427 enters a low power mode and all internal states
> are reset, **including the control port and registers**, and the outputs are
> muted.

That is decisive on its own terms: with RST held low the CS8427's control port
and registers are continuously reset, so **no register write of any framing can
stick**, and its outputs are muted regardless of what the control port did.

## And the bring-up runs before the word that carries RST and CS is ever published

`main.c:310` calls `cs8427_boot_init()`. `main.c:315` calls `codec_init()`.
`codec_write_word()` — which shifts the 16-bit word carrying **both** the reset
(0x23.4) and the CS8427 chip select (0x25.7) into the latch chain on
P1.0/P1.1/P1.2 — is called only from `codec_init()` (`codec.c:97`) and from
later streaming/USB paths. `hw_init()` calls `mux_write()`, which is the *other*
chain (P1.5/6/7, the 8-bit panel word), not this one.

So at the moment all ten CS8427 register writes are attempted, **the 16-bit latch
has not been clocked at all in this boot.** Both RST and CS are at whatever the
shift-register chain carries — undefined on a cold boot, and left over from the
previous run on a warm one. Then `codec_init()` publishes a word that asserts
RST, and nothing ever releases it.

Stock's order is the exact inverse and is deliberate:

    0x0840   SETB 0x23.4          release RESET
    0x084B   CS pulse             (already identified in this project as the
    0x0850                         CS8427 SPI-mode select — and §9 confirms it:
                                   SPI mode is selected by a high→low transition
                                   on AD0/CS *after* RST has been brought high)
    0x0855+  ten register writes

Stock releases reset, then produces the transition that selects SPI, then writes.
mboxfw writes first, with the part's reset and chip select in an unpublished
state, and then asserts reset permanently.

## What this corrects

`FINDING_cs8427_chip_select_never_driven.md` says "the chip select sits asserted
for the entire life of the firmware". **Wrong**, and wrong in the direction that
matters: it is asserted only from `codec_init()` onward, which is *after* every
CS8427 write has already been attempted. Before that it is indeterminate.

The SPI-vs-I²C fork in that document is also **not the operative question**. It
asked which bus the writes go out on; the answer is that it does not matter,
because the part's control port is held in reset either way. The simulation
result there stands as far as it goes — mboxfw's stream decodes to chip address
0x90 where 0x20 is required, and `tools/sim_cs8427_bitstream.py` now
**self-validates** by running stock's own bit stream (RL A then test ACC.0 =
MSB-first, 24 clocks, no ACK slot) through the same decoder and asserting it
yields (0x20, MAP, data) for all ten writes. It does. The decoder model is sound;
its subject was moot.

## Scope caveat, stated rather than glossed

Whether 0x23.4 is wired **only** to the CS8427 or is a shared reset that also
holds the audio codec is not established from the firmware alone. What is
established: it is the reset stock releases immediately before its CS8427
bring-up, and mboxfw never releases it. If it is shared, mboxfw has been running
the entire audio path with the codec held in reset too — which would be a much
larger finding, and is exactly the kind of claim this project requires hardware
to settle rather than inference.

## Consequence for #147

The CS8427 is out. It is held in reset with its outputs muted, so it is neither
a contending driver nor a configured source, and the "second driver on CDATI"
reading in `FINDING_147_cport_and_ep_buffer_divergences.md` Part 2 has no
candidate left in the CS8427. The 3-in-8 duty is still underived.

Fixes are #166 (release the reset, in the right order) and #167 (the ordering
itself). #165's readback becomes even more valuable: it now distinguishes
"reset held" from "configured" in one byte.
