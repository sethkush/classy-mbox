# What every declared terminal and unit is backed by — #194

**This file is machine-read by `tools/check_terminal_evidence.py`. A wrong row
here is worse than no row**, exactly as `rev20_diff_justifications.md` records
for the SFR diff table: it reads as verified, so nobody re-derives it.

## Why this gate exists

`verify_descriptors.py` proves the bundle is internally consistent — every
`bSourceID` resolves, every length adds up, no duplicate IDs. It cannot tell
that a declared terminal corresponds to hardware that carries audio. Those are
different questions, and this repo has now hit the failure in **both**
directions:

- **Real hardware, no descriptor.** The S/PDIF transmitter ran from the first
  build and no host could see it. Found by inventory (#187), not by any gate.
- **A declared control that might not have meant what it said.** #46 refused to
  declare a Feature Unit for months precisely because the mute pair was measured
  as one global gate, and a UAC1 Feature Unit sits on one path. Declaring it
  would have shipped a control that silently muted the other stream. Only #189's
  measurement made the declaration honest.

So: every terminal and unit that ships must name the measurement that proves the
hardware behind it, and the gate fails on an uncited entry — the same discipline
`check_citation_targets.py` applies to SFR writes.

## What counts as evidence

A FINDING that **measured the path on hardware**. Not a register write we
perform, not a datasheet claim, not a stock disassembly. The register we write
is what we *intend*; the measurement is what the hardware *did*.

`UAC_TT_USB_STREAMING` terminals (0x0101) are the exception and are marked
`protocol`: they are the USB endpoint itself, not a physical path, so what
proves them is that the stream carries audio at all.

## The table

| ID | kind | type | evidence | what was measured |
|---|---|---|---|---|
| 1 | IT | 0x0101 | `FINDING_170_audio_works.md` | protocol — the playback stream reaches the DAC; analog loopback at both rates |
| 2 | IT | 0x0601 | `FINDING_170_audio_works.md` | 1 kHz tone through the analog input appears in capture, and moves when the cable moves. **Typed 0x0601 ANALOG CONNECTOR, not 0x0603 LINE, since #228:** the front-panel buttons choose which connector (mic XLR / line TRS / instrument Hi-Z) feeds this terminal, and the host is not told which, so naming one would be a claim the device cannot stand behind. The line, instrument and microphone measurements that justified the three former terminals all still hold and are all evidence for this one — rows 10 (instrument, 18.9 dB hotter than LINE, `FINDING_196`) and 11 (microphone XLR, 69.9 dB at 1234 Hz, `FINDING_224`) were removed with the terminals they described, not because the measurements lapsed |
| 6 | IT | 0x0605 | `FINDING_spdif_input_works.md` | the CS8427 receiver locks to an external carrier and the recovered audio reaches capture |
| 5 | SU | — | `FINDING_224_the_xlr_input_carries_audio.md` | SET_CUR moves the capture source between analog and S/PDIF; macOS names both items. #224 appends position 4 = MICROPHONE, measured before declaring |
| 8 | FU | — | `FINDING_189_the_mute_pair_separates.md` | 0x23.3 kills playback and leaves capture intact, on both units, bracketed |
| 9 | FU | — | `FINDING_189_the_mute_pair_separates.md` | 0x23.2 kills capture (exact digital zeros) and leaves playback intact |
| 3 | OT | 0x0603 | `FINDING_170_audio_works.md` | playback reaches the analog line output; measured at the far unit over the crossed TS pair |
| 7 | OT | 0x0605 | `FINDING_187_spdif_output_is_real.md` | far unit slaved to our carrier returned peak exactly 0.5000, bit-identical; silent control returned exact zeros |
| 4 | OT | 0x0101 | `FINDING_183_packet_sizes_track_the_rate.md` | protocol — the capture stream delivers samples at the declared rate, 44.1 and 48 kHz |
