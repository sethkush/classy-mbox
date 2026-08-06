# What is class-compliant, what is not, and what we can do about it

Inventory taken 2026-08-05 against build 0x0030 (5181 bytes, 835 free).
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

## 2. Declared but never verified against a host

**Both iso endpoints declare `SYNC_ADAPTIVE` (0x08).** This is the one
declaration in the descriptor set that is not yet tested against the hardware it
describes. If it is wrong, the symptom is long-run drift rather than an
immediate failure — a defect that a 3-second loopback measurement cannot see.

Adaptive means the endpoint slaves its converter rate to the other end. Our
converters are clocked by the TAS1020B's Adaptive Clock Generator running from a
24-bit frequency word (`ACG1FRQ`, 0x0FA861 for 48 kHz). Whether that generator
phase-locks to the USB SOF — which is what would make "adaptive" honest — or
free-runs from the crystal is **not established anywhere in this repo**.
`FINDING_179` says so explicitly: "whether the capture endpoint's fixed-size
packets drift against the host's buffer over long captures is a separate,
unmeasured question."

Three outcomes, and the fix differs for each:

- **ACG locks to SOF** → the honest label is `SYNC_SYNC` (0x0C). Two byte
  values change, no code. Adaptive is close enough that no host will complain.
- **ACG free-runs, playback** → the honest label is `SYNC_ASYNC` (0x04), which
  obliges us to publish an explicit feedback endpoint. We have no spare endpoint
  budget and no feedback code. Declaring adaptive without adapting is what we
  have now; it presents as a slow xrun.
- **ACG free-runs, capture** → `SYNC_ASYNC` (0x04) alone is fine. IN endpoints
  need no feedback endpoint; the host reads whatever arrives. One byte, no code.

**The measurement costs no flash.** Stream for ten minutes and compare frames
delivered against wall clock:

    arecord -D hw:Mbox -f S24_3LE -c2 -r48000 -d 600 /tmp/drift.wav
    # frames/600s vs 48000 → ppm error; and count xruns in dmesg

A free-running crystal shows tens to hundreds of ppm and accumulates xruns; a
SOF-locked generator shows near-zero and none. Do this before changing any byte.

---

## 3. Live in hardware, absent from the descriptors

**The S/PDIF output is undeclared.** `cs8427.c:221` writes `DATAFLOW = 0x0C`,
which is `TXD = 01` (`CS8427_TXDSERIAL`) with `TXOFF` clear: the AES3
transmitter is on and sourced from the serial audio input port — the playback
side of the C-port. The RCA digital output is therefore carrying audio, and no
host can see it, because the only Output Terminal on the playback path is
`TERM_LINE_OUT` (0x0603).

Cost to declare: **9 bytes, no code** — one more Output Terminal of type 0x0605
sourced from `TERM_USB_OUT_STREAM`, plus the automatic `AC_BLOCK_LEN` and
`wTotalLength` updates. That is the cheapest capability in this entire document.

Caveat worth stating plainly: the routing above is read from the register we
write, not measured at the jack. Declaring a terminal that turns out to be
silent would be worse than not declaring it, so hang a scope or a second S/PDIF
input on the output first. Unit A's transmitter feeding unit B's receiver is
already the bench topology that `BENCH_WIRING.md` describes, so this is
measurable today.

---

## 4. Deliberate non-compliance

None of these is a defect. Each is a documented trade with a reason.

| deviation | why | risk |
|---|---|---|
| **Vendor requests at `bmRequestType = 0x40`** (telemetry read/reset, set-mux, set-clock, enter-DFU) | vendor space is *reserved* for exactly this; DEVICE recipient so `snd-usb-audio` cannot intercept it | none — a compliant host never sends these |
| **Digi enter-DFU accepted at device recipient, and with any wIndex** | the class-request form becomes undeliverable once a host claims the interface; being strict costs a screwdriver (`BRICK_LOG`) | a host sending class-request 0x00/wValue 0x000A would trip it. No host does |
| **SET_FEATURE / CLEAR_FEATURE always ACK** | a STALL here makes some hosts abandon the device; no features exist to set | spec-legal for iso endpoints (USB 2.0 §5.6.4 — iso endpoints cannot halt) |
| **GET_MIN / GET_MAX / GET_RES stall** | `bSamFreqType = 2` publishes discrete rates, so the host reads the list; UAC1 §5.2.2.1.1 doesn't require the range trio for discrete controls | Windows has been seen to probe these. Untested — we have no Windows host |
| **`iSerialNumber = 0` in the default build** | the serial only exists behind `MBOX_UNIT=A/B`; a default image cannot have a per-unit string | macOS keys per-device settings on the serial, so the default build makes two units indistinguishable. Build per-unit for bench work |
| **PID 0x2000 rather than 0x1000** | at the stock PID the kernel's `mbox1` quirk claims the device and `snd-usb-audio` never binds | this is us dodging a host workaround written for stock firmware, not a compliance issue. Correct as-is |

---

## 5. Class features we do not implement

| feature | status |
|---|---|
| **Feature Unit — Volume** | **impossible.** The codec has no register interface; its entire control surface is a 16-bit shift word with no gain field, and samples never pass through the 8051 (DMA moves them endpoint↔C-port). Front-panel gain is analog pots. See `FINDING_46_feature_unit_has_no_hardware.md` |
| **Feature Unit — Mute** | **exists in hardware, dishonest to declare.** IRAM 0x23.2/0x23.3 is a *global* audio-path enable measured across both directions (#171). UAC1 puts a Feature Unit on one path; ours would mute the other one too. One diagnostic build (`MBOX_MUTE_PAIR_MASK=0x04`/`0x08`, already written, unflashed) would show whether the bits separate. If they do: ~10 descriptor bytes + ~60 code bytes |
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
| ~~190~~ | declare Feature Units with Mute | **DONE, unflashed** — FU 8 (playback) + FU 9 (capture), master Mute each. Cost 175 bytes; the image is now **6016 of 6016** |

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
| **191** | GET_MIN/MAX/RES on the sampling-frequency control | 192 |
| **193** | decide the default-build iSerialNumber story | — |
| **194** | gate every declared terminal against a measured hardware path | 187, 190 |

**Everything above this line is our own reading of the spec.** USB20CV is the
authority, exercises Chapter 9 exhaustively, and will find what this inventory
missed — including whatever 188 and 191 are guessing at. It needs a Windows
host, and a VM with USB passthrough is enough. Do not spend 191's ~50 bytes on
speculation about a host we do not own; let 192 say whether they are needed.

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

The cheapest honest removal is `TLM_REQ_SET_MUTE` (~40 bytes). Its stated
justification was that the class control is interface-recipient and unreachable
once `snd-usb-audio` binds -- but with the Feature Units declared, ALSA exposes
the mute as an ordinary mixer control, which is reachable from the bench with
the driver bound. For mute specifically the fallback argument no longer holds
either: with no driver bound there is no streaming, `g_path_enabled` is 0, and
the pair is down regardless. It is kept for now only because it is the proven
instrument #189 used and removing it before the class control is confirmed on
hardware would leave no way to test the thing that replaced it.

### Budget (original estimate)

Worst case with every conditional taken — 9 + 30 + 200 + 70 + 50 — is ~360
bytes against 835 free. Compliance is not what this image is short of.
