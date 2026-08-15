# What is class-compliant, what is not, and what we can do about it

Inventory REFRESHED 2026-08-14 against build 0x0053 (5949 bytes, 67 free).

Sections 1-5 were originally taken 2026-08-05 against build 0x0030 and had gone
35 builds stale: they still described the sync types as unverified ADAPTIVE, the
S/PDIF output as undeclared, and Mute as dishonest to declare. All three were
closed by tasks #185-#190 and are corrected below. Section 6's roadmap was kept
current throughout.
Sources: `src/descriptors.c`, `src/usb.c` `handle_setup()`, `src/cs8427.c`,
`tools/verify_descriptors.py`. Host evidence is cited per row; anything not
cited is a declaration nobody has confirmed on the wire.

The reference is **USB Audio Class 1.0** (`audio10.pdf`) plus USB 2.0 §9. The
stock firmware is not a reference for any of this: stock serves a *vendor-class*
configuration and its UAC block is dead data that GET_DESCRIPTOR never returns
(`project_mbox_uac_descriptors_never_served`). Everything below is ours.

---

## 1. Compliant and confirmed working on a host

| what | evidence |
|---|---|
| Device descriptor, bcdUSB 1.10, bMaxPacketSize0 = 8 | enumerates on Linux (snd-usb-audio) and macOS 26.5.2 Core Audio |
| Config bundle, 3 interfaces, wTotalLength self-consistent | `verify_descriptors.py` walks and cross-references every unit |
| AC header, IT/OT topology, both paths terminate correctly | same gate; every `bSourceID` resolves |
| AS interfaces 1 and 2, alt 0 zero-bandwidth + alt 1 active | `SET_INTERFACE` drives `streaming_*_enable()`; `alt_seen` telemetry confirms both |
| Type I format, 2ch × 24-bit in 3-byte subframes, 44.1/48 kHz discrete | `FINDING_170_audio_works.md` — analog loopback at both rates |
| Iso endpoints EP1 IN / EP2 OUT, bInterval 1 | both stream |
| Endpoint sampling-frequency control (SET_CUR/GET_CUR) | `handle_class_endpoint_request()`; rate changes take effect |
| Selector Unit 5, analog vs S/PDIF, SET_CUR/GET_CUR | `FINDING_spdif_input_works.md`; macOS names both items from the terminal types it parsed |
| Standard requests: GET_DESCRIPTOR, SET/GET_CONFIG, SET/GET_INTERFACE, SET_ADDRESS, GET_STATUS | enumeration completes on two host OSes and three controller families |
| Unsupported requests STALL rather than execute garbage | the `lcall #0x2f00` fallback was removed 2026-07-26 |
| EP0 multi-packet continuation | `FINDING_ep0_multipacket_loss_is_fixed.md` — 21,600 packets, zero loss |

This is a real class-compliant audio device. It works with the in-box driver on
both operating systems with no kernel quirk and no vendor driver.

---

## 2. Declared but never verified against a host — CLOSED

**Was:** both iso endpoints declared `SYNC_ADAPTIVE` (0x08) with no evidence that
the Adaptive Clock Generator locks to SOF, so the label might have been a
fiction.

**Now:** both declare `SYNC_ASYNC` (0x04), and the playback side publishes the
explicit feedback endpoint that async obliges (EP2 IN, `bSynchAddress` set).
`FINDING_186_ti_softpll_is_the_feedback_endpoint.md`.

The drift question that motivated the doubt was measured rather than argued:
#181 and #182 ran ten-minute captures at both rates, and #183 confirmed packet
sizes track the declared rate (264/270 mixed 9:1 at 44.1 kHz, −9.0 ppm). The ACG
free-runs from the crystal; async is the honest label and is what is declared.

Nothing in the descriptor set is now an unverified declaration.

---

## 3. Live in hardware, absent from the descriptors — CLOSED

**Was:** the S/PDIF output carried audio (`cs8427.c` writes `DATAFLOW = 0x0C`,
transmitter on, sourced from the C-port playback side) while no host could see
it, because the only playback Output Terminal was `TERM_LINE_OUT`.

**Now declared.** `TERM_SPDIF_OUT` (type 0x0605) is in the descriptor set,
sourced from the playback Feature Unit. #187.

The caveat the old text insisted on was honoured rather than waived: the routing
was **measured at the jack** before the terminal was declared, using unit A's
transmitter into unit B's receiver — the topology `BENCH_WIRING.md` describes.
#184, `FINDING_spdif_input_works.md`. `check_terminal_evidence.py` now gates
every declared terminal against a measured path (#194), so this class of
mistake — declaring something silent — cannot recur silently.

---

## 4. Deliberate non-compliance

None of these is a defect. Each is a documented trade with a reason.

| deviation | why | risk |
|---|---|---|
| **Vendor requests at `bmRequestType = 0x40`** (telemetry read/reset, set-mux, set-clock, enter-DFU) | vendor space is *reserved* for exactly this; DEVICE recipient so `snd-usb-audio` cannot intercept it | none — a compliant host never sends these |
| **Digi enter-DFU accepted at device recipient, and with any wIndex** | the class-request form becomes undeliverable once a host claims the interface; being strict costs a screwdriver (`BRICK_LOG`) | a host sending class-request 0x00/wValue 0x000A would trip it. No host does |
| **SET_FEATURE / CLEAR_FEATURE always ACK** | a STALL here makes some hosts abandon the device; no features exist to set | spec-legal for iso endpoints (USB 2.0 §5.6.4 — iso endpoints cannot halt) |
| **GET_MIN / GET_MAX / GET_RES stall** | `bSamFreqType = 2` publishes discrete rates, so the host reads the list; UAC1 §5.2.2.1.1 doesn't require the range trio for discrete controls. Answering MIN=44100/MAX=48000 would imply 45000 is selectable, and SET_CUR rejects it | Windows has been seen to probe these. #191 closed this as NOT APPLICABLE with four independent lines of agreement, including that TI's own reference defines the three and implements none. `FINDING_191_min_max_res_do_not_apply.md` |
| **`iSerialNumber = 0` in the DEFAULT build only** | a single universal image cannot carry a per-unit string; the serial lives behind `MBOX_UNIT=A/B` | macOS keys per-device settings on the serial, so a default image makes two units indistinguishable to the host and their settings collide. **The per-unit serial is a FEATURE, not bench scaffolding** — a shipped image should be built per unit, and the 42 bytes it costs are not a strip candidate. A universal image would need the serial read from EEPROM at boot, which needs flasher support to write it; nothing requires that today |
| **PID 0x2000 rather than 0x1000** | at the stock PID the kernel's `mbox1` quirk claims the device and `snd-usb-audio` never binds | this is us dodging a host workaround written for stock firmware, not a compliance issue. Correct as-is |

---

## 5. Class features we do not implement

| feature | status |
|---|---|
| **Feature Unit — Volume** | **impossible.** The codec has no register interface; its entire control surface is a 16-bit shift word with no gain field, and samples never pass through the 8051 (DMA moves them endpoint↔C-port). Front-panel gain is analog pots. See `FINDING_46_feature_unit_has_no_hardware.md` |
| **Feature Unit — Mute** | **IMPLEMENTED.** The pair does separate — #189 measured 0x23.2 = capture, 0x23.3 = playback, so the #171 "global enable" reading was an artefact of a build that removed both bits at once. Feature Units declared and confirmed on hardware: snd-usb-audio parses both, each switch drives its own gate, and the mute survives a stream reopen. #190, `FINDING_190_feature_units_on_hardware.md` |
| **Feature Unit — Bass/Treble/EQ/AGC/Delay/Loudness** | no hardware exists for any of them |
| **88.2 / 96 kHz** | removed deliberately. The ACG synthesizer caps at 25 MHz, so the doubled rates come from halving the C-port divider — but the codec follows MCLK and has no way to be told, so everything above the base Nyquist folds. Proven: 30 kHz returns at 18 kHz with nothing at 30. See `FINDING_46_no_bandwidth_above_24k.md` |
| **Asymmetric rates** (96-in/48-out) | shown schedulable and the USB engine handles it, but the supporting code went with #46. Needs a divider split plus a relaxed cross-check. No use case, since the fast side carries no bandwidth |
| **Per-channel input selection** | removed. macOS creates every input selector as `kIOAudioControlChannelIDAll` and cannot express it; carrying ~350 bytes for a Linux-only convenience was the wrong trade. The front-panel buttons do both channels on both hosts. `FINDING_macos_one_input_selector.md` |

---

## 6. The route to maximum compliance

Ordered so that every measurement lands before the code it could invalidate,
and so that flash-requiring work batches onto as few power cycles as possible.
One power cycle buys one image and costs a 2 km round trip.

### Stage 1 — measure. No flash, no risk.

Nothing here changes the device. Each one decides a declaration we currently
make without evidence, and #181 is the only item in this whole document that
could be an outright bug rather than a missing feature.

| # | task | decides |
|---|---|---|
| **181** | iso clock drift at 48 kHz, 10-15 min | is `SYNC_ADAPTIVE` true |
| **182** | the same at 44.1 kHz | ditto, at the other ACG frequency word |
| ~~183~~ | capture packet sizes at 44.1 kHz via usbmon | **DONE** — they track it: 264/270 mixed 9:1, -9.0 ppm. `FINDING_183_packet_sizes_track_the_rate.md` |
| **184** | S/PDIF transmitter at the jack, A→B | whether §3's output is real |

Two traps this bench has already sprung: `dmesg_restrict` is set on 1.76, so an
unprivileged xrun count reads clean whatever happened; and the units are
cross-wired, so read `BENCH_WIRING.md` before designing #184.

### Stage 2 — the cheap, certain corrections.

| # | task | cost | blocked by |
|---|---|---|---|
| **185** | set the endpoint sync types to what was measured | 0 bytes | 181, 182 |
| **187** | declare the S/PDIF Output Terminal | 9 bytes | 184 |
| **188** | stall unsupported SET_FEATURE / CLEAR_FEATURE selectors | ~30 bytes | — |

**188 is the clearest outright violation left.** USB 2.0 §9.4.9 requires a
STALL for a feature that cannot be set or does not exist, and §9.4.1 the same
for ClearFeature; we ACK both unconditionally. The current comment justifies it
by a real symptom — hosts abandoning the device on a stall — but the fix
over-corrected. Re-enumerate on both host OSes after changing it, because that
symptom is the entire point of the test.

Ship 185, 187 and 188 in one image.

### Stage 3 — the two open capabilities.

| # | task | cost | blocked by |
|---|---|---|---|
| **186** | decide the playback feedback endpoint | 0-200 bytes | 181 |
| ~~189~~ | does the mute pair separate playback from capture | **DONE** — they separate: 0x23.2 = capture, 0x23.3 = playback. `FINDING_189_the_mute_pair_separates.md` |
| ~~190~~ | declare Feature Units with Mute | **DONE and CONFIRMED ON HARDWARE** — snd-usb-audio parses both; each switch drives its own gate; the mute survives a stream reopen. `FINDING_190_feature_units_on_hardware.md` |

186 only exists if #181 finds the ACG free-running. It is a design decision
before it is a coding task: an asynchronous OUT endpoint obliges a feedback IN
endpoint reporting consumption in 10.14 format every frame, and the honest
alternatives are to declare async without one (non-compliant, but no worse than
today) or to keep adaptive and document the drift.

189 no longer costs two flashes. The `MBOX_MUTE_PAIR_MASK` compile-time
variants are superseded by `TLM_REQ_SET_MUTE` (build 0x0035), which moves the
same two bits at runtime: all four mask states on ONE power cycle, on one unit,
repeatable in either direction, instead of two images giving one one-shot A/B
with a reflash between the halves of the comparison. Cheaper AND a stronger
experiment.

### Stage 4 — evidence, not belief.

| # | task | blocked by |
|---|---|---|
| **192** | USB-IF Command Verifier (USB20CV) on a Windows host | — |
| ~~191~~ | GET_MIN/MAX/RES on the sampling-frequency control | **CLOSED, not applicable** — our rates are discrete, so MIN/MAX/RES describe a range that does not exist. The device already stalls them, which is correct. `FINDING_191_min_max_res_do_not_apply.md` |
| ~~193~~ | decide the default-build iSerialNumber story | **DONE** — stays 0, as both stock images. `FINDING_193_the_serial_number_story.md` |
| ~~194~~ | gate every declared terminal against a measured hardware path | **DONE** — `check_terminal_evidence.py` + `terminal_evidence.md` |
| ~~195~~ | validate `wValue`/`wIndex` against what the descriptors declare | **DONE, confirmed on hardware** (build 0x0038, both units) — `ch9_probe.py` reads 40/41, up from 37/41. The one remaining failure is the deferred `wLength = 0` case. `FINDING_192_chapter9_probe_without_windows.md` |

**Everything above this line is our own reading of the spec.** USB20CV is the
authority, exercises Chapter 9 exhaustively, and will find what this inventory
missed — including whatever 188 was guessing at. It needs a Windows host.
**"A VM with USB passthrough is enough" was asserted here and never verified**;
USB-IF documents controller requirements and does not support virtualised
passthrough, so treat it as unknown until someone runs it. 191 is closed on its
own evidence rather than waiting for this (see the row above), and
`tools/ch9_probe.py` now covers the mechanical part of Chapter 9 from Linux —
which is how #195 was found.

194 is the discipline this inventory exposed the need for. The gate proves the
descriptor set is internally consistent but cannot tell that a declared
terminal corresponds to hardware carrying audio. §3 is the inverse failure
(real hardware, no descriptor) and 190 is the forward risk (a declared mute
that does not separate). Cite each terminal to the FINDING that measured it and
fail on an uncited entry, exactly as `check_citation_targets.py` does for SFR
writes.

### Budget — the image is FULL

**6016 of 6016 bytes as of build 0x0036.** #190 cost 175 bytes (20 descriptor,
155 code) against the 175 that were free, which is not a coincidence so much as
the ceiling arriving. Nothing further fits without removing something.

`TLM_REQ_SET_MUTE` was removed to pay for it, and the accounting is exact: the
per-unit **serial** descriptor costs 42 bytes, and without the removal #190 fit
only in the serial-less build -- which the bench cannot use, because two units
at the same PID are told apart by serial and nothing else
(`BENCH_WIRING.md`, "Trust the serial"). So the real trade was *the vendor mute
alias for the ability to address a specific unit*, and that is not close.

The alias also had no unique job left. The other vendor aliases exist because
their class equivalents are INTERFACE or ENDPOINT recipient and the host stack
rejects those with EBUSY once `snd-usb-audio` binds. Mute is different: with the
Feature Units declared, ALSA carries it as a mixer switch that works *with* the
driver bound, which is the bench case. And with no driver bound there is no
streaming, so `g_path_enabled` is 0 and the pair is down anyway.

Telemetry block 4 was then retired (build 0x0037) for another **43 bytes** --
stalls plus the live P1/P3 reads. It cost no capability twice over: P3 is
already reported by block 9 byte 4, and the stall counter's only reader was
that block, so keeping it would have left a write-only counter. Index 4 is not
reused.

    default build   5929 / 6016     87 free
    MBOX_UNIT=A     5971 / 6016     45 free
    MBOX_UNIT=B     5969 / 6016     47 free

#195 then spent all 43 of those bytes stalling the requests that name what we
do not declare, so the per-unit builds are back at the ceiling:

    default build   5974 / 6016     42 free
    MBOX_UNIT=A     6016 / 6016      0 free
    MBOX_UNIT=B     6014 / 6016      2 free

Build 0x0038 is **flashed to both bench units** (2026-08-06) and is what the
bench now runs. Unit A is at the ceiling exactly; there is no room left for
anything on unit A without removing something first.

**#191 turned out not to need any of it.** It closed as not-applicable rather
than unaffordable: our sampling frequencies are discrete, MIN/MAX/RES describe a
continuous range, and the device already stalls all three -- which is the
correct answer, not a gap. Measured on hardware, and corroborated by TI's own
reference implementing none of them and neither stock image dispatching on them.

So the remaining budget question is only about the stall counter block 4's
retirement spent, which #192 may want back: one struct byte plus one TLM_INC8.

### Budget (original estimate)

Worst case with every conditional taken — 9 + 30 + 200 + 70 + 50 — is ~360
bytes against 835 free. Compliance is not what this image is short of.
