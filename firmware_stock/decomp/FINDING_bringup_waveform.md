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

2026-08-04. The static analysis came first and the measurement followed the
same day, on Mbox A / 192.168.1.76, build 0x001D. **Both are now confirmed on
hardware** — see "Measured" at the end.

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

## Measured — 2026-08-04, Mbox A on 192.168.1.76, build 0x001D

### The boot divergence is real on hardware, not just in the simulator

`sim_p1_waveform.py` has reported `0x10C0` vs stock's `0x1CC0` for months. The
device now says the same thing.

Getting a clean reading took three power cycles, because **the host contaminates
the boot state within seconds**. `snd-usb-audio` binds on hotplug and issues
SET_INTERFACE + SET_CUR on both interfaces, so every previous session has read
the post-`SET_CUR` value and never the boot value. `modprobe -r` is not enough:
the replug re-autoloads the module via MODALIAS before a read can land. What
worked was blocking autoload outright:

    echo "install snd_usb_audio /bin/true" > /etc/modprobe.d/zz-mbox-test.conf
    modprobe -r snd_usb_audio
    <replug>

Fresh boot, driver blocked (bus resets 3, suspends 0):

    codec word = 0x10C0     mute pair LOW, RESET_N high
    alt_seen   = 0x00       <- the control: nothing set an alt setting
    setup      = 18

Then, **same boot, single variable** — remove the blocker and load the driver:

    codec word = 0x1CC0     mute pair HIGH
    alt_seen   = 0x03       playback-on|capture-on
    setup      = 36

`alt_seen` moving 0x00 -> 0x03 and the setup count 18 -> 36 are what make this a
controlled result rather than a coincidence: the pair rises exactly when the
class driver binds and issues its SET_CUR, which is the single writer the binary
analysis predicted (`orl _g_codec_state_23,#0x0c`, one site, streaming.rst
0x0D7A).

So the causal chain is closed end to end on hardware:

    boot                      -> 0x10C0   (pair low; stock rests at 0x1CC0)
    class driver binds        -> 0x1CC0   (SET_CUR raises the pair)

**What is still NOT measured** is what the two lines actually do. Establishing
where they rest is not the same as naming them, and the functional test still
needs a stream with the pair low — which means raw isochronous I/O, since any
ALSA path sends SET_CUR first. `pyusb` 1.3.1 (the only USB package in
`~/mbox-venv`) cannot do isochronous transfers; that needs `python-libusb1`.
#171 stays open on that question. Do not patch on the strength of the state
reading alone.

### The suspend path loses the codec word — measured, and it is a real defect

Found while chasing the above. One controlled suspend/resume cycle, before and
after, nothing else touched:

    codec word   0x1CC0 -> 0x0000
    suspends          3 -> 4
    susr/resr       7/7 -> 9/9
    mux word     0xF6   -> 0xF6      (restored)

`do_suspend()` zeroes both halves of the codec word. The resume path calls
`hw_init()`, which re-seeds `g_mux_state` to 0xF6 — so the **panel** chain comes
back — but nothing re-publishes the **codec** chain. `cs8427_boot_init()` is the
only setter of `RESET_N` (`orl ...,#0x10`, one site, cs8427.rst 0x0003D1) and is
called from `main.c:427` alone, never on resume.

Consequence: after any USB suspend the external chip is **held in reset for the
rest of the attach**, the source nibble reads MIC whatever was selected, and only
the mute pair ever comes back (via the next SET_CUR). Recoverable only by a power
cycle. Tracked as #175.

This also gives #149 most of its answer: resume itself works — four cycles
completed, the device answers EP0 after each. What fails is state restoration,
which is not the failure that task was watching for.

### One thing that does not reconcile

Before the suspend test, the device read `codec word = 0x1CC0` with
`suspends = 3` already counted, on a 13-minute-old boot. By the mechanism
measured above that is impossible: bit 4 comes only from `cs8427_boot_init()`,
which runs once, at boot. A host re-bind after resume restores the mute pair
(0x0C) but cannot restore RESET_N (0x10).

Either something else re-published the word, or those three increments took a
path that skipped the clear. Recorded as an open discrepancy rather than
explained away — the measured before/after is solid on its own, and inventing a
reconciliation for the earlier reading would be exactly the move this project
keeps having to undo.

## The pair CANNOT be isolated from the host — negative result, 2026-08-04

The experiment designed above was built and run. It does not work, and the
reason is worth recording because it rules out the whole host-side approach.

`tools/iso_loopback.py` (python-libusb1, the project's first raw-isochronous
rig) streams a 1 kHz tone on ch2 into the `out2 -> src2` loopback and captures
it back, with ch1 as a silent control. On a clean boot with `snd-usb-audio`
blocked it correctly observed:

    initial codec word = 0x10C0   pair LOW
    after setmux line line: 0x10CF   pair still LOW   <- assumption confirmed

then streamed arm A with the pair low, sent `SET_CUR(48000)`, confirmed
`0x1CCF` (pair HIGH), and streamed arm B.

    arm A (pair LOW ):  captured 0 bytes
    arm B (pair HIGH):  ch2 1 kHz -26.29 dBFS, ch1 control -98.22 dBFS

**Arm A captured nothing, and that is not silence.** `SET_INTERFACE(alt=1)`
does arm the endpoints (`IEPCNF1 = 0xC5`, `DMACTL1 |= DMA_EN`,
`streaming_capture_enable()`), so the endpoint was live. But
`streaming_set_rate()` is the **only** code that programs the adaptive clock
generator — `ACG1FRQ*`, `ACG2FRQ*`, `ACGCTL = 0x06` with DIVEN. Without
`SET_CUR` there is no MCLKO, the codec is never clocked, no I2S frame reaches
the C-port, the DMA never clears its NACK flag, and the UBM answers every IN
token with a NULL packet — the exact zero-length-isoc signature this file's
sibling comment in `streaming.c` records from the DIVEN bug.

So arm A was dead for want of a clock, not muted. **`SET_CUR` raises the pair
and starts the clock in one indivisible request**, and no other host request
touches either. The pair is not separable from the outside.

The first version of the harness printed
`delta +213.71 dB => the pair is an output mute. READING CONFIRMED.`
It reached that by treating "no data" as -240 dBFS. A measurement that cannot
tell a muted stream from a stream that never ran is not a measurement, and this
one would have written a false confirmation into a document that four other
files cite. `iso_loopback.py` now aborts if an arm captures under 100 ms of
audio, and says why.

### What would actually settle it

A build, not a host request — the same method that settled #161. Build 0x001E
= 0x001D with exactly one line removed from `streaming_set_rate()`:

    g_codec_state_23 |= (unsigned char)0x0C;     /* delete this, nothing else */

Then `SET_CUR` does everything it does today *except* raise the pair, and the
clock comes up either way. Single variable, one flash:

  * audio still works  -> the pair is NOT an output mute on this path, and the
    reading carried since `FINDING_open_questions.md` §1.6 is wrong
  * audio dies         -> the pair IS required for the audio path, reading
    confirmed, and stock's bring-up ordering should be ported

Either outcome is decisive, and the failure mode is benign: a build that boots
and enumerates but is silent is exactly the state 0x001C already occupied, and
it is recoverable by reflashing.

## SETTLED — 0x23.2/0x23.3 gate the audio data path. Measured, 2026-08-04.

Build 0x001E (`make MBOX_NO_MUTE_PAIR=1`) is 0x001D with exactly one
instruction removed — `orl _g_codec_state_23,#0x0c`, 3 bytes, verified absent
in the listing with every other write to the word intact. Flashed to Mbox A,
manifest reached, block 0 confirms 0x001E running.

Same rig, same tone, same code path (`iso_loopback.py --single`), one variable
— the image:

    build 0x001D   codec word 0x1CCF   pair HIGH
      ch1 (unconnected)  1 kHz  -98.30 dBFS   rms -96.27 dBFS
      ch2 (looped)       1 kHz  -26.25 dBFS   rms -29.25 dBFS
      95232 frames

    build 0x001E   codec word 0x10CF   pair LOW
      ch1  0 non-zero samples / 95232      min +0.000000000 max +0.000000000
      ch2  0 non-zero samples / 95232      min +0.000000000 max +0.000000000
      95232 frames

**The pair is required for audio.** The reading carried on inference since
`FINDING_open_questions.md` §1.6 — and leaned on by `streaming.c`, `codec.h`
and two findings — is confirmed by measurement rather than by argument.

### It is a DIGITAL gate, not an analog mute

The stream is healthy in both arms: 95232 frames each, identical count, clock
up, DMA cycling. Only the sample values differ. And the discriminator is ch1,
which carries no tone and whose cable runs to a disconnected unit:

  * 0x001D ch1 = -98.30 dBFS — that is A's own ADC noise floor on an open input
  * 0x001E ch1 = **exactly zero, 0 of 95232 samples non-zero**

An analog output mute would leave the ADC's self-noise untouched, because the
converter keeps running. Zeroing even the noise floor means the **capture data
path itself is gated** — at or upstream of the codec's serial output, not in
the analog domain. Hard `0x000000`, not attenuation.

### What this does NOT establish

Whether the pair also gates PLAYBACK. The bench loopback `out2 -> src2` puts
A's output and A's input in series, so a null result implicates both and names
neither — the same limitation recorded in BENCH_WIRING.md for #147. What is
proven is that the capture side is zeroed; the output side is untested and
would need unit B (`A out1 -> B src1`) to isolate.

That question is now optional rather than blocking: for mboxfw's purposes the
pair must be set either way, and it already is on every path that streams.

### Consequences

  * The bring-up ordering divergence is no longer cosmetic. Stock raises the
    pair BEFORE releasing the external RESET (Rev 20 0x0831/0x0833 then 0x0840);
    mboxfw releases RESET with it low and raises the pair only on the first
    SET_CUR. Now that the lines are known to gate real data, porting stock's
    order into `cs8427_boot_init()` has a measured motive rather than a
    "stock does it" one.
  * #175 gets sharper. A USB suspend zeroes the codec word, and only the mute
    pair ever comes back (via the next SET_CUR) — never RESET_N. So post-suspend
    the audio path is restored but the external chip stays in reset.
  * The earlier host-side attempt could never have answered this. Its arm A
    captured 0 BYTES, a dead stream; this arm captured a full 571392 B of
    zeros. Same dBFS reading, completely different meaning, which is exactly
    why `iso_loopback.py` now refuses to report on a starved arm.

## COMPLETE — the pair gates BOTH directions. Measured 2026-08-04.

The capture half was settled above by holding the pair low on unit A. The
output half is invisible to a self-loop (`out2 -> src2` puts one unit's DAC and
ADC in series), so it was measured across the two units, with the experiment
moved onto **B** so A stayed on working firmware throughout.

B flashed to 0x001E at `MBOX_PID=0x2001` — 5308 bytes against B's 0x001D at
5311, the same 3-byte `orl _g_codec_state_23,#0x0c` removed, all 32 gates
passing with `MBOX_EXPECT_BUILD_ID=0x001E`. Then `B out1 -> A src1`, B playing,
A (on 0x001D, capture path known good) listening:

    B on 0x001D, pair HIGH  ->  A ch1 = -28.12 dBFS
    B on 0x001E, pair LOW   ->  A ch1 = -99.21 dBFS   (A ch2 idle: -94.96)

71 dB. Same cable, same tone, same listener, one variable: the instruction.

**The stream genuinely ran** — the check the earlier host-side attempt failed
to make. `aplay` returned clean, and B's own telemetry moved across the tone:

    B sof_count  52421 -> 55683    (3262 SOFs ~ the 3.26 s tone)
    B alt_seen   0x03              (playback alt was set)
    B codec word 0x10C0 throughout (pair never rose; SET_CUR did not raise it)

So B was streaming, with a running clock, and its output carried nothing.

### The two signatures differ, and both fit

| arm | what was measured | result |
|---|---|---|
| A on 0x001E, own capture | A's ADC data with the gate closed | **exact zeros**, 0/95232 non-zero |
| B on 0x001E, heard by A | B's analog output with the gate closed | **noise floor**, -99.21 dBFS |

Not a contradiction — the difference is which converter is live. In the first
arm the gated path *is* the one producing the samples, so the samples are
digital zero. In the second, A's ADC is healthy and simply hears analog
silence, which renders as A's own noise floor rather than as zeros. Both say
the same thing: with the pair low, no audio crosses in either direction.

### Settled

`0x23.2`/`0x23.3` are a **global audio-path enable**, gating playback and
capture together — not an output-only mute. The reading in `codec.h` ("mute /
audio-path enable pair") and in `streaming.c` is correct, and is now measured
in both directions rather than inferred from where stock happens to set it.

`BENCH_WIRING.md`'s claim that the cross-links buy what the self-loop cannot is
no longer a design note; this is the measurement that needed them.

## #175 fixed and verified — build 0x001F, 2026-08-04

The suspend defect recorded above is fixed, and the fix is stock's own
mechanism rather than an invention.

**Root cause: the guard was implemented, the recall was not.**
`cs8427_boot_init()` returns early once IRAM 0x25.6 is set, and the comment
above that test already described the intended design — that `do_suspend()`
zeroing the codec word clears the bit, and that this is how stock re-arms the
bring-up. Nothing ever called the function a second time, so the re-arm never
fired. Half a pattern, with the half that was present documenting the half that
was missing.

Stock's side, in both images:

    cmd2_apply_iface1_alt   Rev 20 0x038F  JB 0x2e,0x0395
                                   0x0392  LCALL 0x080b
    (also 0x0416 iface 2, 0x04C8; Rev 22 0x0363/0x0393/0x0416/0x04C8)

`SET_INTERFACE(alt != 0)` now posts `WORK_BRINGUP`, dispatched to
`cs8427_boot_init()` from the main loop — deferred because the SETUP handler is
ISR context and that function bit-bangs SPI with settle delays, which is the
same reason stock runs cmd2 from its dispatcher. +18 bytes.

Measured, one full cycle:

    baseline                 codec=0x1CCF   RESET_N=SET   pair=SET
    after suspend+resume     codec=0x0000   RESET_N=LOW   pair=LOW
    after next stream start  codec=0x1CC0   RESET_N=SET   pair=SET
    after setmux             codec=0x1CCF   mux=0xED      suspends=1
    B out1 -> A src1         -28.12 dBFS — identical to the pre-suspend baseline

On 0x001D the same sequence ended at `0x0C00`: the mute pair returned via
SET_CUR and RESET_N was gone until a power cycle. Suspend still clears the word
— that is correct and matches stock. What changed is that the next stream start
rebuilds it.

This also closes #149. Resume always worked; five cycles completed across the
session and the device answered EP0 after every one. The defect was state
restoration, which the question "does suspend/resume work?" could not have
found — only watching the codec word across a deliberate cycle did.

One caution for whoever reads block 7 next: `tlm_suspends` is byte **4**, not
byte 5. Byte 5 is `tlm_playback_resyncs`, and reading it instead produced a
confident "suspends=0" on a cycle that had plainly happened.
