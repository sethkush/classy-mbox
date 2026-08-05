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

## All three links measured — 2026-08-04

The topology above was asserted from memory until now, and this bench has
already had one routing claim turn out wrong (the out2 self-loop was in src1,
found only because a tone appeared on the wrong channel). Every link has now
been driven with a 1 kHz tone at -6 dBFS on ch1 only, both units on 0x001D,
both `setmux line line`:

| link | level at the far end | unfed channel |
|---|---|---|
| `A out1 -> B src1` | **-28.07 dBFS** | B ch2 -93.83 dBFS |
| `B out1 -> A src1` | **-28.12 dBFS** | A ch2 -97.18 dBFS |
| `A out2 -> A src2` (self-loop) | **-26.33 dBFS** | A ch1 -95.31 dBFS |

The two crossed TS legs agree to 0.05 dB. The self-loop reads ~1.8 dB hotter,
consistent with the TS-vs-TRS note above rather than with any firmware
difference — a TS plug in a balanced input shorts ring to sleeve.

~66 dB between a fed and an unfed channel is the discrimination any loopback
result rests on. Quote it when a measurement claims a channel is silent.

**What the cross-links buy that the self-loop cannot.** `out2 -> src2` puts one
unit's DAC and ADC in series, so a null result implicates both and names
neither. `A out1 -> B src1` measures A's output with A's input entirely out of
the circuit. That is the only way to separate an output fault from an input
fault, and it is what #171 needs to finish: the mute pair is proven to gate the
capture path, and whether it also gates playback is invisible to a self-loop.

## Cable-type caveat on level comparisons

Source 1 is fed over **TS** (unbalanced) and source 2 over **TRS**. A level
difference between the two paths is expected from the cabling alone — a TS plug
in a balanced input shorts ring to sleeve — and is not a firmware finding. Any
comparison across the two paths has to be relative-to-its-own-baseline, not
absolute dBFS against each other.

## Identifying the units — SERIAL NUMBERS

**The serial is the identity. The PID is not.**

| unit | serial | sysfs | ALSA card | notes |
|---|---|---|---|---|
| **A** | **`RK10874600Q`** | `2-1.3` | `Mboxclassc` | carries the self-loops |
| **B** | **`RK1672500M`** | `2-1.4` | `Mboxclassc_1` | crossed pair only |

Both units build at **`MBOX_PID=0x2000`** — the same product, because they ARE
the same product. Each is built with `make MBOX_UNIT=A` / `=B`, which serves
its serial as USB string #3 (see `mboxfw/include/usb.h`). Read it with
`lsusb -v`, `cat /sys/bus/usb/devices/*/serial`, or `dev.serial_number`.

The sysfs paths are stable for the current cabling but are a property of which
socket the cable is in, not of the unit. If a cable moves, the serial is still
right and the path is not. **Trust the serial.**

### Why not the PID

Until 2026-08-04 the two were told apart by giving each a different `MBOX_PID`
(A `0x2000`, B `0x2001`). That works and is wrong twice over:

  * the PID says which **product** this is, not which **unit**, so two
    identical devices claimed to be different models;
  * it changes **driver binding** — the exact variable an A/B measurement has
    to hold still. Comparing two units on different PIDs means comparing them
    under potentially different quirk handling.

It also nearly caused a real accident: the #171 experiment image was built for
A's PID, and flashing it to B unchanged would have given both units the same
PID with no way to tell them apart.

### Addressing one unit

`mboxtlm.py` **refuses to guess** when more than one unit is attached — it
exits with a listing rather than picking the first match, because a reading
taken from the wrong unit looks exactly like a valid one:

    mboxtlm.py read 0 --serial RK10874600Q      # unit A
    mboxtlm.py read 0 --serial RK1672500M       # unit B
    mboxtlm.py read 0 --addr 2:21               # fallback for pre-0x001F builds

`mboxflash_linux.py` has the same discipline via `--addr bus:addr`, and keeps
it: **in DFU both units enumerate as `ffff:fffe` and carry no serial at all**,
so bus/address is the only discriminator there. The safe habit when flashing
one of two attached units is unchanged — trigger DFU by addressing the target
explicitly, then confirm exactly one `ffff:fffe` is on the bus before writing.

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
