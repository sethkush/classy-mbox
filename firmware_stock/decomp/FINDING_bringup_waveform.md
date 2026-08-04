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

---

# Extension, same day: timing and the third chain

The first version of this gate checked bit VALUES and threw away everything
else the trace contained. Two things were already in the captured waveform and
simply unused.

## Timing

ucSim reports a cycle count at every stop, so the gaps between CS8427
transactions are measurable for free. Measured:

    mboxfw  19044  19164  19164  19164  19164  19368  19164 ...
    rev20   11772   5856   5880   5880   5880   5880   5880 ...
    rev22   11376   5364   5376   5388   5376   5376   5376 ...

mboxfw's settle delays are about 3.2x LONGER than either stock image's, which
is a difference between SDCC's loop and Keil's `DJNZ 0x2e,$` rather than a
defect.

This matters because `verify_reachability.py` catches a delay whose call site
has been deleted outright, and cannot catch one that is present but wrong. The
mutation that drops `volatile` from `settle_delay()` -- the exact
`FINDING_delay_calls_elided.md` failure -- collapses mboxfw from 19044 cycles
to **3672**, and the source still reads correctly at every line.

The bar is stock's own shortest gap, measured in the same run. The first
version used half of it, on the reasoning that a bar should be generous about
compiler differences, and 3672 sailed straight under 2682. A generous bar
calibrated against nothing is not a bar. "At least as long as the vendor
firmware waits" can be defended, and mboxfw clears it by 3.5x.

## The panel chain, and a correction

The project's shorthand for the mux chain has been "IRAM 0x22 plus mono as a
ninth bit". Read literally that is wrong. Rev 20's
`shiftreg8_commit_p1_7_6_5` (0x0F0C) clocks eight bits and then, at 0x0F32,
drives DATA to the mono value and raises LATCH *without another clock*:

    0f32  JNB 0x1e,0x0f39    ; 0x1e = IRAM 0x23.6
    0f35  ORL P1,#0xC0       ; DATA high AND LATCH high in one write, RET
    0f39  ANL P1,#0x7F / ORL P1,#0x40 / ANL P1,#0xBF

Mono is a ninth OUTPUT presented at the latch edge, not a ninth shifted bit.
mboxfw already ports this correctly, including the asymmetry where the mono-set
branch returns with LATCH still high. Measured: eight clocked bits per latch in
mboxfw and in both stock images.

So the new check is the mono line itself -- the end of the path that moved out
of `__bit g_mono` and into the codec word earlier today. At each latch edge the
published line is compared against the mirror the firmware actually read.

## Three ways this gate was wrong before it was right

Every one of these produced a green or a red that meant nothing, and each was
caught only by mutating the firmware and watching what the gate did.

**The delay bar was calibrated against nothing** -- see above. Green on a real
collapse.

**The mono check was vacuous.** It compared the published mono line against the
last codec word seen on the wire. Both of mboxfw's boot-time mux latches happen
*before* the first codec-word publish, so every comparison was skipped and the
check reported "0 disagree" on an image whose mono condition had been inverted
outright. A check that cannot fire is not a check.

**It then read the wrong byte.** Reading the mirror directly out of the
simulator fixed the vacuity, but `g_codec_state_23` is at IRAM **0x0A**, not
0x23 -- the name encodes *stock's* address, and SDCC puts mboxfw's copy where
it likes. Sampling 0x23 reported a phantom mismatch on a correct image. It now
resolves the address from the linker map, which is the same lesson
`sim_smoke.sh` learned when it anchored to a compiler-generated label.

The tell that the third one was an instrument fault rather than a firmware
fault: baseline, the delay mutation, and the inverted-mono mutation all
reported the *identical* single mismatch. A real mono defect would have changed
between them.

## And one on the harness

`make` was not rebuilding between mutation and measurement -- edits landing
within the same second as the previous build left the old image in place, so
two mutation runs measured an unchanged binary and "passed". Mutation testing
now does `make clean` and prints the image hash at every step, because a
mutation that does not change the image tests nothing.

---

# #171 — where the mute pair actually rests, and why it has never shown

2026-08-04, static. No hardware: neither host had an Mbox on the bus when this
was written (`lsusb` on 192.168.1.76 and .86 shows no `0dba` device), so the
measurement below is designed but **not taken**.

## The complete write set for 0x23.2 / 0x23.3 in mboxfw

Three sites, and only three:

| site | effect |
|---|---|
| `codec.c:151` (`codec_boot_init`) | `g_codec_state_23 = 0` — pair LOW |
| `streaming.c:180` (`streaming_set_rate`) | `|= 0x0C` — pair HIGH |
| `power.c:73` (`do_suspend`) | `g_codec_state_23 = 0` — pair LOW |

`streaming_set_rate()` has exactly one caller: `usb.c:945`, the data stage of
**SET_CUR(SamplingFrequency)**. `SET_INTERFACE` does *not* reach it — the alt
handlers call `streaming_capture_enable()` / `streaming_playback_enable()`,
which never touch the codec word.

So mboxfw rests with the pair LOW in three windows:

1. from boot until the first `SET_CUR` — **including the moment
   `cs8427_boot_init` releases the external RESET** (`cs8427.c:186`), which is
   the divergence `sim_p1_waveform.py` reports as `0x10C0` vs stock's `0x1CC0`;
2. after every USB suspend, until the next `SET_CUR`;
3. never during a stream.

## Why this has never produced a symptom

**Both** audio endpoints declare the UAC1 sampling-frequency control —
`descriptors.c:180` (playback) and the interface-2 class-specific EP descriptor
(capture) both carry `bmAttributes = 0x01`. `snd-usb-audio` therefore issues
`SET_CUR(SamplingFrequency)` on every `hw_params`, before any isochronous data
flows. The pair is raised by the very request that precedes streaming.

That is the whole explanation for "audio works despite booting with the pair
low": in normal use, nothing ever streams while the pair is low. The mute
reading and the observed behaviour are consistent — the mute is simply always
lifted before there is anything to mute.

## What stock does differently, restated precisely

Stock never *rests* with the pair low while running. Its two low windows are
both brackets that close in straight-line code:

    bring-up   0x080E clear (with the whole word)  ->  0x0831/0x0833 set,
               THEN 0x0840 release RESET, 0x0842 publish
    clock mode 0x072F/0x0731 clear  ->  0x07EE/0x07F0 set (unconditional,
               every mode), 0x07F2 publish

The ordering difference that matters is in bring-up: **stock raises the pair
before releasing the external RESET; mboxfw releases RESET with it low.** If
the external chip samples those two lines at reset release, mboxfw's chip comes
up having seen a different pin state — and no later `SET_CUR` would undo a
latched-at-reset decision.

## The measurement, and why the obvious ones are closed

- **CS8427 register readback** — closed. Telemetry block 10 (#165) was retired
  on 2026-08-03: no P3 pin varied across the eight read clocks, so CDOUT is not
  readable from here. There is no software route to ask the chip what it
  latched.
- **`SET_CUR` A/B on the running device** — cannot isolate. Any ALSA stream
  sends `SET_CUR` first, so the pair is high in both arms.
- **Suspend to clear the pair** — would work (`do_suspend` zeroes the word) but
  requires forcing a USB suspend, and #149 ("verify suspend/resume on
  hardware") is still open. Wedging on a failed resume costs a physical trip.
  Not to be attempted casually.

**What does isolate it, with no flash and no power cycle:** drive the stream
from raw pyusb instead of ALSA, so `SET_CUR` becomes an independent variable.

    claim interface, SET_INTERFACE(iface 1, alt 1), push isochronous
    tone packets, measure the loopback capture on src2

    arm A: no SET_CUR first   -> pair LOW  (confirm via block 9 byte 2)
    arm B: SET_CUR(48000)     -> pair HIGH (confirm via block 9 byte 2)

Silence in A and tone in B names the pair a mute directly, on the running
image, with one variable. Tone in both says the pair is not an output mute and
the bring-up ordering matters only to whatever the external chip latches — at
which point the remaining question is narrow enough to be worth a build.

Block 9 byte 2 reports `g_codec_state_23` live (`telemetry.c:242`), so each arm
states its own pair state rather than having it assumed.

## Not yet assessed

The rest of stock's `audio_path_reconfig_ext_chips` (0x080B) that mboxfw does
only inside `streaming_set_rate()`: the `CLR A / MOV 0x25,A / MOV 0x23,A`
zeroing at 0x080B-0x080E, `LCALL acg_set_freq_48k_family` at 0x081B,
`LCALL acg_dptr_inc_then_set_both_dctl_10` at 0x081E, and `ACGCTL |= 0xC0` at
0x0827. Same shape as the pair: stock establishes a running clock during
bring-up, mboxfw waits for the host to ask.
