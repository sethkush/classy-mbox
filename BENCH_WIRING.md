# Bench wiring on the void box (192.168.1.76)

Recorded 2026-07-30 from Seth. **Two** Mbox units are on 1.76 now, not one.
Nothing in the firmware or the traces reveals this; every loopback measurement
has to be read against it, and one measurement has already been voided for
getting the routing wrong.

## Topology

Call the units **A** (the "first unit", the one carrying the self-loops) and
**B**.

| from | to | cable | crossed? |
|---|---|---|---|
| A line out 1 | **B** line source 1 | TS 1/4" | yes |
| B line out 1 | **A** line source 1 | TS 1/4" | yes |
| A S/PDIF out | A S/PDIF in | coax | no — self-loop |
| A line out 2 | A line source 2 | TRS 1/4" | no — self-loop |

B carries no self-loops. Its only connection is the crossed pair on channel 1.

    A out1 ──────TS─────► B src1
    B out1 ──────TS─────► A src1
    A out2 ──TRS──► A src2
    A spdif out ──► A spdif in

## What this newly buys

**A signal source that is not the unit under test.** Every previous loopback
put mboxfw's playback path in series with its capture path, so a null result
implicated both and named neither. With B feeding A's source 1, A's capture can
be measured with A's playback entirely out of the circuit — which is what #147
needs, since capture returns the 8-frame artifact regardless of whether anything
is playing.

**Two units carrying opposite build-time settings, compared over one cable.**
`hw_init.c` already anticipates exactly this: `MBOX_PLAYBACK_BYOR` exists as a
build switch so "two units can carry opposite settings and be compared over a
loopback cable", and the comment says to delete the switch once a loopback
settles it. This rig is what settles it — flash A with `MBOX_PLAYBACK_BYOR=1`
and B with `=0`, cross-feed, and the direction that produces a tone names the
playback BYOR value.

**A closed S/PDIF clock loop on A.** Relevant to #145, with the caveat that if
the firmware slaves its clock to S/PDIF in, A's receiver is looking at A's own
transmitter, whose clock derives from the thing being slaved. Lock is not
evidence of correct slaving in that configuration; use B as the S/PDIF source
if #145 needs a real external clock.

## Selecting the source — CLOSED 2026-07-30, build 0x0013

**Both `src 1` and `src 2` are LINE inputs, and mboxfw boots selecting MIC.**

`hw_init` seeds the mux word to `0xF6`, which is pattern `0x06` = MIC on both
channels (`buttons.c:51`, `MUX_IRAM22_ANNOTATION.md`). LINE is `0x05`, one
button press away per channel:

    0x06 MIC (boot) --press--> 0x05 LINE --press--> 0x03 INST --press--> 0x06

This is precisely what voided the 2026-07-29 measurement: the mux sat at mic on
both channels while the loopback fed a line input, so the selected source never
carried the test signal. The new cabling does not change that — it adds a second
line-input path with the same problem.

Both halves of that are now handled from the host, so no one has to touch the
panel and no measurement depends on the unverified buttons (#150):

    mboxtlm.py setmux line line     # select LINE on both channels

# This is the ONLY way to set the mux without physical access. The UAC
# Selector Units that briefly provided `amixer` control were removed in build
# 0x001A (see FINDING_macos_one_input_selector.md); the front-panel buttons
# and this request are what remain. The mux resets to MIC on every power
# cycle, so re-run this after every flash or replug before trusting any
# capture measurement.
    mboxtlm.py read 9               # confirm what is actually selected

`TLM_REQ_SET_MUX` (0x13) reaches the same states the buttons reach, by the same
publish path — `codec_source_changed()` → `mux_write()` → `codec_write_word()`,
which is stock's order at Rev 20 `0x0AE3-0x0AE9` / Rev 22 `0x0A8D-0x0A93`. It
takes only the six source bits from the host, preserving bit 0x22.6 (derived) and
0x22.7 (a control line no stock source handler writes), and it **rejects** any
value that is not one of the three one-cold patterns. That rejection is the point:
`g_mux_state = 0x00` is precisely the illegal state that invalidated the earlier
measurements, and a request able to re-enter it would be a trap.

Block 9 reports the published mux word, the decoded per-channel source, both
codec-chain bytes, live P3, and separate accepted/rejected counters — separate
because a rejected request leaves the mux unchanged and is otherwise
indistinguishable from one that never arrived. `mboxtlm.py` flags a channel that
is not on `line` and names the fix.

**Read block 9 alongside every capture.** The measurement should state its own
input routing rather than have it reconstructed from the front panel afterwards.

## The back-panel jack order is not intuitive — verify, do not assume

**2026-08-03: the out2 self-loop was in `src1`, not `src2`, and this document
said otherwise.** It was found by measurement, not by looking: the first tone
test after the #170 fix put a clean 1 kHz tone on capture **ch1** — the channel
whose cable runs to the disconnected Mbox B — while the supposedly-looped ch2
sat at the noise floor. Moving the cable to src2 moved the tone to ch2, every
other condition held constant.

That is the second measurement in this project derailed by the input routing
not being what it was believed to be (the first was the 2026-07-29 mic/line
mix-up above). The jack order on the back panel does not read the way the
channel numbering suggests, so **confirm which physical jack a cable is in
before trusting any loopback result**, and prefer a test that moves the cable
and watches the signal move — that is what settled this one.

The accident was useful in the end: a signal that moves when the cable moves,
and vanishes when its own channel's selector goes to MIC, is demonstrably
travelling through the selected line input rather than leaking across inside
the chip.

## Cable-type caveat on level comparisons

Source 1 is fed over **TS** (unbalanced) and source 2 over **TRS**. A level
difference between the two paths is expected from the cabling alone — a TS plug
in a balanced input shorts ring to sleeve — and is not a firmware finding. Any
comparison across the two paths has to be relative-to-its-own-baseline, not
absolute dBFS against each other.

## Identifying the units

Both units enumerate as audio-mode Mboxes, told apart by `MBOX_PID` in
`0x2000..0x200F` (the flasher treats the whole range as audio-mode aliases):

| unit | PID | sysfs | ALSA card |
|---|---|---|---|
| **A** | `0dba:2000` | `2-1.3` | `Mboxclassc` |
| **B** | `0dba:2001` | `2-1.4` | `Mboxclassc_1` |

The PID is what distinguishes them, not `TLM_BUILD_ID`. An earlier version of
this section advised distinct build ids too; do not do that while both units
run the same firmware — the build id states which *code* is running, and
faking a difference to label a *unit* makes block 0 lie about the thing it
exists to prove. Address a specific unit by PID (`usb.core.find(idProduct=…)`),
which is also the only safe way to send one of them an enter-DFU trigger.

## B is NOT stock — corrected 2026-08-04

This document previously described B as a stock unit and warned that its
source select was front-panel only. **B has been running mboxfw all along.**
It was found on build **`0x000B`**, which is why it looked broken: `0x000B`
predates the ACGCTL/DIVEN fix, the CS8427 SPI framing and RESET release, and
#170's codec source nibble. It also predates telemetry block 9, so reads of
that block returned the all-`0xFF` unknown-block sentinel — numbers that look
like a live reading and are not one.

**Both units now run `0x001D`**, flashed 2026-08-04 (B at `MBOX_PID=0x2001`;
the two images differ in exactly one byte, the PID, at offset `0x1B53`).

Consequences for measurement:

  * B takes `setmux` over the wire like A does. It still **boots to MIC on
    both channels** (`mux=0xF6`) while the loopbacks feed LINE inputs, so
    `setmux line line` is required on B after every power cycle, exactly as
    for A. This is the 2026-07-29 trap and it now applies to both units.
  * B answers the full telemetry block map, so a measurement involving B can
    state its own routing instead of having it assumed.
  * B is a usable signal source and capture reference. `A out1 -> B src1`
    measures A's output with A's input out of the circuit — which is the one
    thing A's `out2 -> src2` self-loop structurally cannot do, since it puts
    A's DAC and ADC in series. That is what would separate the output half of
    the #171 mute pair from the capture half already proven.
