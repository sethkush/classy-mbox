# The "codec" is an AK5383 + AK4393, and the "codec word" is two 4094s

2026-08-08, read off the board by Seth. This repo has called the analog front
end "the codec" since the beginning and never had a part number for it. Every
claim about the 16-bit control word was inferred from what stock firmware
writes and what the audio does.

## The parts

| marking | what it is | what it means here |
|---|---|---|
| **AK5383VS** | AKM 24-bit ΔΣ **ADC** | the capture path |
| **AK4393VF** | AKM 24-bit ΔΣ **DAC** | the playback path |
| **HEF4094BT** | 8-stage shift-and-store bus register | ×2 cascaded = the 16-bit "codec word" |
| **74HC157** | quad 2-input multiplexer | the mic/line/inst source select |

## There is no codec, and that settles #189

Capture and playback are **two different chips**, which is why 0x23.2 and
0x23.3 separate the directions so cleanly. `FINDING_189_the_mute_pair_separates`
established that behaviourally -- each bit kills exactly one arm and leaves the
other untouched -- and could not say why. This is why. #171's reading of the
pair as one global enable was not just wrong, it was describing two chips as
one.

## The 16-bit word is a GPIO expander, not a control port

The HEF4094 is serial-in / parallel-out with a **strobe** that transfers the
shift register to the output latches. That is exactly `codec_write_word()`:
eight clocked bits per byte on P1.0/P1.2, then one pulse on P1.1. Two cascaded
parts give 16 outputs.

So the word does not address registers in anything. Each bit is a **pin**.
Three things this explains that were previously just observed:

- it is write-only, and #165 found no readable pin. A 4094 has no read path.
- bit 0x25.7 is the **CS8427's chip-select**. A codec's own control port would
  never carry another chip's CS; a GPIO expander obviously would.
- the bits are a grab-bag -- mute gates, RESET_N, mono, source state, a chip
  select -- which is what a board-level control latch looks like and is not
  what any codec register map looks like.

Neither AKM part has a serial control port at this position anyway: the AK5383
is pin-configured, so a GPIO latch is the only way to control it.

## 0x23.2 is the ADC's RST -- inferred here, PROVED 2026-08-09

**Correction.** This section originally called the pin **PDN**, power-down. The
AK5383 has no PDN pin. The pin is **RST (pin 10)**, and the datasheet is
explicit about what it does: *"When this pin is Low, the digital section is
powered down. When this pin returns to High, an offset calibration cycle
starts. An offset calibration cycle should always be initiated upon powering up
the device."* That last sentence is the whole bug -- see `FINDING_197`.

Reading 0x23.2 as RST accounts for every measurement in `FINDING_197`:

| measured | under the RST reading |
|---|---|
| gate low gives EXACT digital zeros | RST low powers the digital section down; there is no output |
| releasing it costs a FIXED 188.0 ms ± 0.2 of zeros, independent of hold length over a 32x range | the AK5383's tRTV after RST release -- 8960/fs, a fixed SAMPLE count, which is why hold length cannot move it |
| the transient decays with tau = 171 ms | the DC-blocking high-pass converging after power-up |
| **171 ms < 188 ms** | the high-pass converges *inside* the internal mute window, so nothing reaches the output |
| one pulse fixes it for the whole power-up | the DC stays converged as long as the ADC stays powered |

So the pulse does not cancel the transient. **It hides it behind the ADC's own
mute**, and it works only because the settling is faster than the mute. That is
a much better account than "an edge clears it", which is what this document
said for two days and which explained nothing.

## What it predicts, and what to check

Unverified, and both are checkable without a scope:

1. ~~**Continuity.** Trace the 4094 output that carries 0x23.2 to the AK5383's
   PDN pin with a multimeter.~~ **SETTLED 2026-08-09, and not this way.** A
   continuity beep proves a net exists, not that the far chip treats the edge as
   RST. The datasheet specifies tRTV as 8960/**fs** -- a sample count, not a
   time -- so counting the leading zero run in FRAMES at 48000 and at 44100
   discriminates directly. It is constant in frames (8787..8813) and differs by
   9 % in wall time, and 8813 > 8704 excludes tRCF, leaving tRTV = 8960 with a
   3.4 ms head loss that fits both rates to 0.01 ms. No meter was needed.
   `FINDING_197`, "0x23.2 IS the AK5383's RST".

   The general lesson: where a datasheet specifies an interval in sample clocks,
   the sample rate is a free variable this bench controls, and sweeping it
   identifies the PART rather than the net.
2. **Why early pulses fail.** Builds pulsing at 300 ms, 2.5 s and 8 s after
   boot were all inert; pulses tens of seconds later work. Under this reading
   the high-pass converges to whatever DC the analog input sits at -- so pulsing
   before the analog rails are stable converges to a value that then drifts.
   That predicts a threshold set by analog settling, not by anything digital,
   and it predicts the threshold is a property of the board rather than of the
   firmware.

Both AKM datasheets should be read before the next build. Every timing number
in `FINDING_197` was measured from the outside; the parts have specified
values for the mute interval and the high-pass corner, and if those match the
188 ms and the 171 ms, the mechanism is closed.

## Datasheet acquired 2026-08-09

`reference/AK5383_datasheet_M0049-E-03.txt`. It confirms the RST reading in the
part's own words -- *"Upon returning 'H', an offset calibration cycle is
started. An offset calibration cycle should always be initiated after
power-up."* -- and confirms tRCF = 8704/fs and tRTV = 8960/fs as LRCK-edge
counts, which is what made the sample-rate sweep decisive. Note 11 also fixes
**DFS = "L"** on this board: at DFS="H" the constants are 17408 and 17920, and
we measured ~8800.

Two corrections to what this document assumed, both in `FINDING_197`:

- the calibration and the high-pass filter are **independent blocks**. The
  calibration reference is VCOM or the AIN pins per ZCAL; the HPF is not
  involved in deriving it.
- ~~**HPFE is tied low.**~~ **Retracted 2026-08-10 — HPFE is HIGH.** The
  post-calibration DC that claim rested on is sub-1-Hz bench drift, which a 1 Hz
  high-pass barely attenuates. A mux-injected DC step recovers 8.55 % slower at
  44.1 kHz than at 48 kHz, against 8.84 % expected for an fs-clocked pole, so the
  filter is running inside the part.
  `FINDING_the_171ms_decay_is_the_ADC_high_pass.md`.
