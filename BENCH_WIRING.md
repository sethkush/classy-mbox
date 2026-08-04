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

## Cable-type caveat on level comparisons

Source 1 is fed over **TS** (unbalanced) and source 2 over **TRS**. A level
difference between the two paths is expected from the cabling alone — a TS plug
in a balanced input shorts ring to sleeve — and is not a firmware finding. Any
comparison across the two paths has to be relative-to-its-own-baseline, not
absolute dBFS against each other.

## Identifying the units

Both units enumerate as audio-mode Mboxes. Build with distinct `MBOX_PID`
values in `0x2000..0x200F` (the flasher treats the whole range as audio-mode
aliases) and distinct `TLM_BUILD_ID`, so block 0 names which unit answered.
