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
| A S/PDIF out | **B** S/PDIF in | coax | yes — crossed 2026-08-04 |
| B S/PDIF out | **A** S/PDIF in | coax | yes — crossed 2026-08-04 |
| A line out 2 | A line source 2 | TRS 1/4" | no — self-loop |

A carries the only remaining self-loop (out2 -> src2). Everything else is
crossed between the units.

    A out1 ──────TS─────► B src1
    B out1 ──────TS─────► A src1
    A out2 ──TRS──► A src2
    A spdif out ──coax──► B spdif in
    B spdif out ──coax──► A spdif in

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

**A real external S/PDIF clock — the self-loop was retired 2026-08-04.**
A's S/PDIF used to loop back on itself, which is circular for the only thing it
was needed for: if the firmware slaves its clock to S/PDIF in, A's receiver is
watching A's own transmitter, whose clock derives from the thing being slaved.
Lock proves nothing in that configuration.

The two S/PDIF ports are now crossed, so each unit's receiver sees a genuinely
independent transmitter. That is what #145 needs.

**When slaving is implemented, only ONE unit may slave.** Both units currently
run on their internal clocks, so the crossed pair is safe as it stands; if both
were told to slave to S/PDIF in, the topology would be circular again with no
master anywhere. Master/slave, not peer/peer.

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

## The GAIN dials act on the LINE inputs, and firmware cannot see them

**The per-channel Gain knob applies to all three source positions, LINE
included.** From the *Mbox Basics Guide* 6.4 (`reference/mbox1/`), which gives
the same instruction for each source in turn:

| source | page | instruction |
|---|---|---|
| Mic (XLR) | 5 | "carefully turn the Gain control to the right to increase the input level of your microphone" |
| Inst (DI) | 6 | "…to increase the input level of your guitar" |
| **Line** | **7** | "…to increase the input level of your **keyboard**" — with the **Line LED lit** in the accompanying "Source selector and Gain control" figure |

Page 9 says the same for recording generally, on an input the software labels
**Mic/Line 1**: "Use the Gain controls on Mbox to maximize the signal going into
Pro Tools while avoiding clipping."

So LINE is not a fixed-gain bypass path on this unit. Every loopback level this
document records was taken through a variable analog gain stage.

**Nothing in firmware can read or set it.** There is no control ADC and no pot
wiper anywhere in the design as reverse-engineered: mboxfw references "analog"
only as descriptor terminal names, and neither Rev 20 nor Rev 22 reads a gain
position. Every `ADC` in the RE notes is the *audio* converter. There is
therefore no telemetry block that can report gain, and no `mboxtlm.py` command
that can normalise it — unlike the source mux, which #150 moved off the panel
and onto the host precisely so no measurement would depend on a knob.

### Where the dials have actually been — MINIMUM, throughout

**Seth, 2026-08-06: both gain dials have been at minimum for the entire history
of this bench, and have never been moved.** That is testimony rather than a
readback — nothing can read the dial — but it is the only evidence that exists,
and it is worth recording because it *rescues* the absolute levels below. If the
dials have not moved, the dBFS figures taken on different dates are comparable
after all, and the cross-date rule in the next section is a precaution against a
future change rather than a retraction of past work.

Two consequences, both measured in `FINDING_196`:

- The loop is **perfectly linear** over the full 60 dB swept — every 3 dB in
  gives 3 dB out — so nothing in the recorded levels is compressed.
- Minimum gain costs **20.2 dB of converter range**: a full-scale playback
  signal reaches the ADC at −20.2 dBFS. Every absolute level in this file
  therefore sits ~20 dB below where the converter would like to be. That is the
  dial doing its job, not a fault, but it explains why the loopback figures
  cluster in the −26 to −30 dBFS region.

### NEVER analyse the first 3 seconds of a capture

Every capture starts with a DC step settling through the codec's DC-blocking
high-pass — `FINDING_147`'s τ ≈ 171 ms transient, re-armed on every stream
start because alt 0 tears the input path down and alt 1 re-enables it. Measured
by stepping a 1 s window through a 10 s idle capture (`FINDING_196`):

| window | LF 1–15 Hz | total RMS |
|---|---|---|
| 0–1 s | **−25.5 dBFS** | −28.2 |
| 1–2 s | −76.4 | −79.1 |
| 2–3 s | −119.9 | −101.7 |
| 3–9 s | ≈ −118 | −101.7 |

The first second is at **−28 dBFS RMS** — the level of a test tone. Analysing
inside it inflated a THD measurement from 0.0032% to 0.208% and the noise floor
from −142 to −107 dBFS/bin, and the wrong figure was published before the cause
was found. `tools/sweep.py` now captures 6 s and analyses from 4 s.

This is *not* a level-comparison caveat like the two above — it is 70 dB, and it
lands on whichever measurement is unlucky enough to start early.

### Consequence for every level in this file

**Absolute dBFS is only comparable within one session, with the dials
untouched.** The mux hazard has a host-side fix; this one does not. A dial
nudged between two runs reproduces exactly the failure mode the 2026-07-29
measurement had — a clean table that means nothing — except that no readback
exists to catch it.

Rules that follow:

- Bracket every level sweep with a repeat of its own first condition, and quote
  the two. `test_mute_pair.sh` already does this (`both` first and last, agreeing
  to 0.01 dB); that bracket is now also the dial-drift check.
- Never compare a dBFS figure against one recorded on an earlier date. Compare
  ratios within a run.
- The ~66 dB fed-vs-unfed discrimination above is a *ratio* and survives dial
  changes. The -26 / -29 dBFS absolutes do not.

## Identifying the units — SERIAL NUMBERS

**The serial is the identity. The PID is not.**

| unit | serial | sysfs (STALE — see below) | notes |
|---|---|---|---|
| **A** | **`RK10874600Q`** | `2-1.3` | carries the self-loops |
| **B** | **`RK1672500M`** | `2-1.4` | crossed pair only |

> **The sysfs column above is already wrong**, and is left in place as the
> demonstration. On 2026-08-05 the units answered at `2-1.4` (A) and `2-1.2`
> (B) — neither matches the table, and A now sits on the path the table
> assigns to B. Anything that had keyed off the path would have silently
> swapped the two units and produced a clean, wrong A/B.
>
> Do not repair this column. It cannot be kept correct: the path is a property
> of which socket a cable is in, and every replug is a chance to change it.
> Resolve serial → sysfs → ALSA card at use time instead, as
> `tools/test_mute_pair.sh` does.

> Build 0x0020 briefly served NO serial on B — it was built with
> `MBOX_PID=0x2000` but without `MBOX_UNIT=B`, so `--serial RK1672500M` matched
> nothing and read as "unit absent" rather than as an error. Fixed in 0x0021.
> **Always pass `MBOX_UNIT=` when building an image destined for a specific
> unit**; the omission is silent at build time and only shows up 1 km away.
>
> The ALSA card **numbers** are not listed here on purpose. They are assigned in
> enumeration order and moved when the host was rebooted on 2026-08-04 —
> `Mboxclassc` went from card 0 to card 1, and the two units swapped which card
> index each held. Resolve them at use time instead:
>
>     for c in /proc/asound/card*/usbid; do echo "$c $(cat $c)"; done
>     ls -l /proc/asound/ | grep card      # names -> indices
>     cat /proc/asound/cards               # shows the usb-...-1.3 / -1.4 path
>
> The **sysfs path is the reliable link** between an ALSA card and a physical
> unit, because it names the socket.

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

Bus addresses change on every replug and on every host reboot, so re-read them
rather than reusing a number from an earlier session:

    lsusb | grep -i 0dba

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
