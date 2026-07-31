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

## The blocker this rig does NOT clear — read before measuring

**Both `src 1` and `src 2` are LINE inputs, and mboxfw boots selecting MIC.**

`hw_init` seeds the mux word to `0xF6`, which is pattern `0x06` = MIC on both
channels (`buttons.c:51`, `MUX_IRAM22_ANNOTATION.md`). LINE is `0x05`, one
button press away per channel:

    0x06 MIC (boot) --press--> 0x05 LINE --press--> 0x03 INST --press--> 0x06

This is precisely what voided the 2026-07-29 measurement: the mux sat at mic on
both channels while the loopback fed a line input, so the selected source never
carried the test signal. The new cabling does not change that — it adds a second
line-input path with the same problem.

Two things have to be true before the next measurement is worth anything:

1. **The source must actually be on LINE.** Today that means one front-panel
   press per channel — and button behaviour has never been confirmed on hardware
   (#150). If the buttons do not work, there is no way to reach line at all.
2. **Source selection must be readable over the wire.** It is not.
   `g_mux_state` appears in no telemetry block; block 4 offers live P1/P3 reads
   but not the published mux byte. Last time the selected source was established
   by Seth reading the front-panel LEDs, in person, after the fact. A remote
   measurement cannot currently state which input it was listening to.

Both belong in the same flash, since one power cycle buys one image: expose
`g_mux_state` in telemetry (#155), and give the mux a host-settable path or a
build-time line default so the measurement does not depend on #150 (#156).

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
