# The analog path carries audio. #170 was the blocker.

2026-08-03, Mbox A on 192.168.1.76, build **0x001B**, `MBOX_PID=0x2000`,
image RAM-resident (see §5).

Supersedes `FINDING_147_capture_works_analog_path_does_not.md`, which measured
a working ADC, both DMA paths live mid-stream, and no tone through a line
loopback, and named #170 as the remaining suspect. It was.

## 1. The defect

The codec control word's low nibble — IRAM 0x25.0-.3 — carries the per-channel
source selection. mboxfw never wrote it. The panel/relay chain (IRAM 0x22)
followed `setmux` and the buttons correctly, so the relays routed LINE while
**the codec's own input select still said MIC on both channels.** An ADC
converting the mic input while the relay routes line in produces exactly the
"working ADC, no signal" signature #147 recorded.

Read live off the device before the fix: `codec word=0x1CC0` — low byte 0xC0 is
only `BRINGUP_DONE|CS_N`, low nibble 0 = MIC.

## 2. The map, from both images

The two state machines are identical instruction for instruction:

    Rev 20 button_a_cycle_3state @0x0E27   Rev 22 panel_state_cycle_A @0x0E1B
    Rev 20 button_b_cycle_3state @0x0E9D   Rev 22 panel_state_cycle_B @0x0E8F

Pairing each branch with the panel pattern it emits in the same basic block:

    pattern 0x06 MIC  (boot) -> (lo,hi) = (0,0)
    pattern 0x05 LINE        -> (1,1)
    pattern 0x03 INST        -> (1,0)

ch1 = (0x25.0 lo, 0x25.2 hi), ch2 = (0x25.1 lo, 0x25.3 hi). Bit addresses
0x28..0x2B are RAM[0x25].0..3.

Stock stores the 2-bit state and derives the 3-bit panel pattern;
`codec_source_changed()` does the inverse. Equivalent, because the pattern is
only ever one of the three legal one-cold values.

Boot is unaffected — MIC maps to (0,0), already what `codec_init()` publishes.
Confirmed on hardware: build 0x001B booted at `codec word=0x1CC0`, unchanged.

## 3. The measurement

1 kHz at -9 dBFS played to `hw:1,0` while recording, Goertzel at 1 kHz over
frames 48000-144000 (the first second is skipped — the startup transient of
§2 in the #147 finding is still present and still ~345 ms).

Publish works: `setmux line line` moved the word `0x1CC0` -> **`0x1CCF`**,
the predicted value.

**With the loop cable in src2** (A line out 2 -> A line source 2):

    mux          ch1 1 kHz        ch2 1 kHz
    line line    -96.7 dBFS       -29.2 dBFS   <-- tone
    mic  line    -96.9 dBFS       -29.2 dBFS   <-- tone
    line mic     -96.1 dBFS       -93.2 dBFS

-29.2 dBFS from a -9 dBFS source is ~20 dB of loss, which is a line-out into a
line-in with no gain. Before this build the same measurement read `mag=71.8`,
i.e. the noise floor; it now reads `291237`.

The tone tracks **ch2's own selector** and is unaffected by ch1's, so the two
channels select independently and the capture channel order is NOT swapped.

## 4. The control experiment, which was an accident

The first pass put the tone on **ch1**, the channel whose cable runs to the
disconnected Mbox B, while the supposedly-looped ch2 sat at the floor. The
cause was physical: the loop cable was in **src1**, not src2 as
`BENCH_WIRING.md` stated. Seth moved it to src2 and the tone moved to ch2 with
it, every other condition held constant.

That accident is better evidence than the measurement it interfered with. A
signal that moves when the cable moves, and disappears when its own channel's
selector moves to MIC, is travelling through the selected line input — not
internal DAC-to-ADC crosstalk, which at -29 dBFS would have been implausible
anyway (on-chip crosstalk lives near -80 to -100 dB).

## 5. Status of the running image, and what is NOT proven

**The EEPROM header checksum is zeroed and this image is RAM-resident.** The
enter-DFU trigger zeroed it; the device then enumerated `ffff:fffe`, which
`mboxflash_linux.py` labels bulletproof-DFU purely from the PID. Per CLAUDE.md
both DFU modes advertise the same `ffff:fffe`-style descriptors, so that label
does not establish which target was active, and whether the download reached
EEPROM is **unsettled**. The download reached the manifest phase at 166/166
blocks and the app started after a bus reset (`uhubctl -a cycle`, which on this
hardware does not switch VBUS — see the void-box notes — so the 8051 kept
running).

Settle it with a deliberate power cycle: if it comes back at 0x001B the write
persisted; if it comes back in DFU it did not, and the cost is one more
trigger-and-reflash.

Also still unproven:

  * **Playback BYOR (#161) in the playback direction.** Capture was settled by
    measurement in the #147 finding. Playback now demonstrably carries a
    recognisable 1 kHz sine through a real analog path, which is difficult to
    reconcile with a byte-swapped playback stream, but no A/B was run.
  * **#168** — CPTEN is still never set, and `CPTSTA` still reads 0x70 with
    bytes moving. Unchanged by this work and still odd.
  * **#171** — the 0x23.2/0x23.3 mute pair. mboxfw sets both; stock releases
    them only after the master clock settles. Audio works, so the current
    ordering is not fatal, but it was not the thing under test.
  * Absolute level, distortion, noise floor and channel balance were not
    characterised. `-29.2 dBFS` is consistent with the cabling and is not a
    calibrated figure.

## 6. Gate bug this exposed

`tools/latch_word_bit_diff.py` could not see the new write, and the reason was
in the gate. It scanned line-by-line with the RHS terminator `[^;\n]*` —
"semicolon **or end of line**" — so a statement wrapped across lines was
truncated to its first line. The first version of the fix captured only
`= (unsigned char)`: no literals, and no letters after cast-stripping, so it
passed `_is_pure_literal` and contributed **zero** to the settable mask. The
function's contract is "over-estimates, never under", and that path
under-estimated — the one direction that turns a real gap into a silent pass.

Fixed with two passes over disjoint text: statements (';'-terminated, newlines
spanned) and `#define` bodies (line-terminated, since a macro body carries no
semicolon and a ';'-terminated scan runs past the define into the next
statement). Verified by mutation: deleting one of the four bit writes takes the
gate from exit 0 to exit 1 with `IRAM 0x25.3 ... UNEXPLAINED`.

The C is an explicit clear-then-set with literal masks rather than a compound
`x = (x & ~0x0F) | <expr>`, because the compound form drives the gate onto its
conservative branch, masks the byte to 0xFF, and would report 0x25.4/0x25.5 as
driven when nothing drives them. Over-estimating is safe for the gap check but
destroys the gate as a signal.

Gate now reports 4 known gaps, down from 8: 0x23.0/0x23.1 (dead in stock too)
and 0x25.4/0x25.5 (both correct-as-clear for the analog path).
