# The bring-up, measured at the pins

`tools/sim_p1_waveform.py`, 2026-07-31.

## Why this exists

Every check this project had on the two P1 shift chains was **static**.
`latch_word_bit_diff.py` reads C source. `verify_cs8427.py` reads a table of
register values. `check_citation_targets.py` reads bytes at a cited address.
None of them execute anything, so none of them can say what the CS8427 would
actually receive.

The project already knows that gap is not theoretical.
`FINDING_delay_calls_elided.md` records SDCC deleting every *call site* of
`settle_delay()` while leaving the body in the image — the source said one
thing, the image did another, and every source-reading gate sided with the
source. `tools/sim_smoke.sh` states the hole in its own header: *"the simulator
doesn't model the TAS1020A's USB SFRs or the CS8427 on P1.3/P1.4"*. That
sentence had been true and unaddressed since it was written.

So: run the real image in ucSim, break on every write to P1 (SFR 0x90), and
reconstruct both shift chains from the waveform.

The CS8427's chip select is not a port pin. It is bit 7 of the low byte of the
16-bit codec word, and it reaches the part only after being clocked out on
P1.0/P1.2 and latched by P1.1. So decoding a CS8427 transaction *requires*
decoding the codec latch chain first — which is exactly the coupling #166 and
#167 were defects in. A transaction that comes out the far end has proved the
RESET release, the select, the latch chain and the SPI framing together.

The decoder is not trusted on its own. It is run against Rev 20 and Rev 22 —
real stock bytes, real execution — and mboxfw's result is compared with theirs.
A decoder bug that swallowed mboxfw's stream would have to swallow stock's
identically to pass.

## Result 1 — #157 / #166 / #167 hold up

mboxfw's decoded transaction stream is **byte-identical to both stock images**:

    select pulse, 0 clocks     <- the all-zero word publish
    select pulse, 0 clocks     <- the SPI-mode select (DS477F5 s9)
     24 clocks  20 04 00
     24 clocks  20 13 10
     24 clocks  20 04 00
     24 clocks  20 04 40
     24 clocks  20 01 01
     24 clocks  20 02 20
     24 clocks  20 03 0C
     24 clocks  20 05 05
     24 clocks  20 06 05
     24 clocks  20 11 FF

Twenty-four clocks per transaction, every one opening with the chip address
0x20, framed by the select, with no ACK slot and no START/STOP. This is the
first evidence for that rewrite that does not consist of reading the code.

It is still not hardware. It says the image emits the right waveform, not that
the part received it — #165's register readback is what closes that.

## Result 2 — a divergence nothing static could see

The gate also reports the codec word in effect at the moment the CS8427 is
first selected. That single 16-bit value carries every other control line on
the latch:

    mboxfw  0x10C0     0x23 = 0x10
    rev20   0x1CC0     0x23 = 0x1C
    rev22   0x1CC0     0x23 = 0x1C

Two bits differ: **0x23.2 and 0x23.3**, the pair `codec.h` names
`CODEC23_MODE5_A` / `CODEC23_MODE5_B`. Stock's bring-up sets them *before* it
releases the external RESET:

    Rev 20  0x0831  SETB 0x1a          ; 0x23.2      Rev 22  0x09DC
            0x0833  SETB 0x1b          ; 0x23.3              0x09DE
            0x0835  LCALL 0x0e62       ; publish             0x09E0
            0x0838  MOV 0x2e,#0xff / DJNZ      ; settle
            0x083e  SETB 0x2f          ; CS_N                0x09E3
            0x0840  SETB 0x1c          ; RESET_N released    0x09E5
            0x0842  LCALL 0x0e62       ; publish             0x09E7

mboxfw sets the pair only in `streaming_set_rate()`. So from boot until the
first host-driven rate change it rests with both lines low — a state stock
never rests in, and never enters at all with RESET released.

`streaming.c` already argues at length that this pair is a mute or audio-path
enable rather than a rate selector (both stock images set it unconditionally in
straight-line code at Rev 20 0x07EE/0x07F0, Rev 22 0x07CF/0x07D1, with no
branch able to skip it). If that reading is right, mboxfw boots muted and stays
muted until the host changes rate.

**Recorded, not fixed.** POLICY is explicit that "stock does it" is a reason to
investigate and never on its own a reason to ship — `GLOBCTL |= 0x02` was
shipped on exactly that argument and made the device silent on USB. These two
lines have never been measured. Task **#171**.

Why no static gate could find this: `latch_word_bit_diff.py` reports
`g_codec_state_23` with a settable mask of 0x5C, which *includes* both bits. It
is right — mboxfw contains code that sets them. It cannot tell "can set" from
"has set by the time it matters", because it never runs anything.

## Result 3 — a tooling trap

**Do not pass `-t 51` to `s51`.** With it, mboxfw's boot diverges into unmapped
ROM around 0xB379 and never reaches `cs8427_boot_init()`; without it (the
invocation `sim_smoke.sh` already uses) the same image runs to the main loop.
Several hours went into "cs8427_boot_init is never entered" before the cause
turned out to be the CPU-model flag. Any new ucSim harness should copy
`sim_smoke.sh`'s `s51 -q <image>` exactly.

## Other stock bring-up steps mboxfw omits

Visible in the same trace, not yet assessed:

- Rev 20 `0x080B-0x080E` zeroes **both** halves of the codec word at the top of
  the routine. mboxfw relies on `codec_init()` having done it.
- Rev 20 `0x081B` `LCALL acg_set_freq_48k_family` and `0x081E`
  `LCALL acg_dptr_inc_then_set_both_dctl_10`.
- Rev 20 `0x0827` `ACGCTL |= 0xC0`. mboxfw does this in `streaming_set_rate()`
  only.

All three sit between the first publish and the mode-5 pair, so they are part
of the same ordering question as Result 2.

## Reproducing

    python3 tools/sim_p1_waveform.py [--verbose]

Stock is entered at its bring-up routine (Rev 20 0x080B, Rev 22 0x09B6): from
the reset vector it stalls, because stock reaches that routine only from its
SET_INTERFACE handlers and ucSim cannot enumerate a USB device. Entering
mid-firmware leaves P1 at its reset value 0xFF, and CCLK is a P1 pin, so the
first rising edge of the first transaction never happens and the first byte
decodes one bit short. The harness fixes that with a measurement rather than a
chosen constant: it runs the image from reset first, lets stock's own init
drive P1 until it stalls, and seeds P1 with the value that code left (0xC1 in
both images). mboxfw needs none of this — it runs from the reset vector, so its
own `main()` ordering is what gets measured.
