# Mbox 1 macOS Userspace Driver — Implementation Plan

> Companion to the UA-101 project (`~/Desktop/UA101`). Same overall shape:
> a userspace USB daemon feeds a HAL plug-in via POSIX shm, so the device
> appears as a native CoreAudio device on Apple Silicon without a kext.
> Target apps: Pro Tools (aspirational) and Logic Pro (primary).

## 0. Status

- **Repo:** initialized 2026-07-16, `~/projects/mbox`.
- **Reference research:** complete (2026-07-16). See §3, §10.
- **Hardware in hand:** Mbox 1, serial `RK10874600Q`.
- **Phase 0 dump:** complete (2026-07-16). VID/PID confirmed
  `0x0dba:0x1000`, device unclaimed by any audio kext (only
  `AppleUSBHostCompositeDevice` bound). `bcdDevice = 0x0020` confirmed
  to be firmware **Rev 20** — the buggy pre-v22 revision that
  produces loud static during playback. **Blocker for Phase 2
  onwards** unless we obtain `MboxFirmware22_33860.dmg` and flash on
  an old Intel Mac. Phase 1 (capture skeleton) can proceed safely
  since the bug appears playback-only. Full findings in
  `reference/phase0/20260717T041923Z/FINDINGS.md`.

## 1. Executive summary

Build a userspace macOS driver for the original Digidesign Mbox (Mbox 1,
2002, USB 1.1) that:

1. Claims `0x0dba:0x1000` from anything else on the bus (nothing on
   modern macOS binds it — it enumerates as a vendor-specific interface,
   not class-compliant audio).
2. Runs isochronous USB in/out from a userspace daemon (`mboxd`)
   at a fixed **48 kHz, 24-bit, 2 in / 2 out** — the device is hardcoded
   at this rate in the Linux driver and nobody has found otherwise.
3. Exposes a native CoreAudio device via an `AudioServerPlugIn` HAL
   plug-in loaded into `coreaudiod`, using shared memory + SPSC rings
   for IPC (`libmbox_shm` — same protocol as `libua101_shm`).
4. Provides stable 48 kHz stereo I/O suitable for tracking in Logic Pro.

## 2. Background: Mbox 1 hardware overview

Confirmed from ALSA thread + upstream Linux driver + Zammit write-up:

- USB 1.1 full-speed. **VID/PID `0x0dba:0x1000`.**
- USB device class = 0; interface class = `0xff` (vendor-specific).
  Despite that, control transfers follow USB Audio Class conventions
  (per Clemens Ladisch on alsa-devel).
- **Chipset:**
  - TI **TAS1020A** USB streaming controller (Digi firmware in on-board
    EEPROM).
  - Cirrus **CS8427** S/PDIF transceiver.
  - Separate A/D and D/A converters (not confirmed which parts).
  - Focusrite mic preamp on ch 1 (with insert), instrument DI on ch 2.
- **Audio format on the wire:** S24_3BE, 2 channels, 48 kHz (hardcoded
  in the Linux quirk entry — no evidence of other rates working).
- **Endpoints (iface 1, altsetting 1):**
  - Playback `0x02`, ISOC SYNC, `wMaxPacketSize = 0x130` (304 bytes/ms
    → 2ch × 3B × 48 = 288 + 16B framing).
  - Capture `0x81`, ISOC (added in the 2014 duplex patch).
- **Firmware:** lives in on-board EEPROM. **No runtime upload from
  the host.** BUT: devices shipped with firmware < 0.22 have a
  white-noise bug. Upgrade tool (`MboxFirmware22_33860.dmg`) is
  Mac/Windows only. **Verify FW version on our unit before writing
  a single line of streaming code** — a bad FW will look like a
  driver bug.
- **Front-panel monitoring mix knob:** analog on the hardware side per
  everything I can find — not exposed via USB. No driver work needed.
- **MIDI:** Mbox 1 has none. Skip that phase entirely.
- **Physical I/O on our unit (confirmed by inspection):**
  - Rear: 2× XLR/TRS combo inputs (mic/line/instrument), 2× TRS
    inserts (pre-ADC analog, invisible to the driver), 2× TRS line
    outputs, S/PDIF in/out on RCA, 1/4" TRS headphone jack, USB-B.
  - Front: 3.5 mm headphone jack. Both headphone jacks mirror the
    line output.
- **Front-panel controls (all analog, none exposed on USB — no
  driver-side work needed):**
  - Per-channel source-type switch (mic / line / inst).
  - Per-channel gain knob.
  - Input↔playback mix knob (the monitoring blend).
  - Headphone volume knob with a mono-sum switch.
  - +48V phantom switch (also analog — but verify in Phase 0
    whether its state happens to be queryable via a vendor
    control transfer; would be useful to surface if so).

## 3. Linux driver analysis (`sound/usb/`, mainline since 3.19)

Author: Damien Zammit. Two files matter:

**`sound/usb/quirks-table.h`** — search for `0x0dba, 0x1000`. Declares
a composite device:
- iface 0 = standard USB Audio mixer.
- iface 1, altsetting 1 = manually-specified audio format
  (S24_3BE, 2ch, 48 kHz, EP 0x02 + 0x81).

**`sound/usb/mixer_quirks.c`** — search for `snd_mbox1_`. Vendor
control protocol:

- **Clock source (Internal vs S/PDIF sync):**
  - `bmRequestType = USB_TYPE_CLASS | USB_RECIP_ENDPOINT`, `bRequest =
    0x01` (set) / `0x81` (get), `wValue = 0x0100`, `wIndex = 0x81`.
  - 3-byte payload = LE sample rate (e.g. `80 BB 00` for 48000) for
    internal, or `00 00 00` for S/PDIF sync.
- **Input source (Analog=1 / S/PDIF=2):**
  - Same bmRequestType style but `USB_RECIP_INTERFACE`, `wValue = 0x00`,
    `wIndex = 0x0500`, 1-byte payload.

Both need to fire during device init, before starting iso streams.
The Linux driver exposes them as ALSA mixer controls; we can wire them
to CoreAudio `kAudioStreamPropertyPhysicalFormat` variants or expose
them via a separate control socket / plug-in property. TBD in Phase 4.

**Feedback mechanism.** From the quirks-table entry (verified against
`sound/usb/quirks-table.h:2239–2294`):
- Playback EP `0x02` is `USB_ENDPOINT_SYNC_SYNC` — host paces at fixed
  48 samples/ms (144 bytes @ S24_3BE stereo, plus framing).
- Capture EP `0x81` is `USB_ENDPOINT_SYNC_ASYNC` — device clocks
  itself, so incoming packet sizes vary around 48 samples.

The mixed sync/async pair is essentially UA-101's implicit-feedback
pattern: capture arrival rate is the ground truth for the device's
clock; playback timing has to track it or the two directions drift
apart. **So we do need a feedback loop after all** — mirror the
UA-101 design where the daemon derives an effective sample rate from
capture packet counts and uses it to pace playback (or, more simply,
resamples playback into the capture-derived rate before writing to
the wire). Detail out in Phase 2.

**Two vendor-protocol gotchas** noted in the Linux source, worth
recording now so we don't hit them cold:

- `snd_mbox1_set_input_source()` comment
  (`mixer_quirks.c:909–910`): "Setting the input source to S/PDIF
  resets the clock source to S/PDIF." If we expose input-source and
  clock-source as independent CoreAudio properties, we must re-issue
  the clock-source control after any input-source change.
- `snd_mbox1_clk_switch_update()` at line 964 has a bare
  `FIXME: hardcoded sample rate` next to the `48000` literal. Even
  the Linux author isn't sure 48k is the only rate the hardware
  supports; the quirk just declares it that way. Not worth chasing
  early, but keep it in mind if a user ever asks about 44.1.

## 4. macOS userspace USB strategy

Same recommendations as UA-101 — reuse the analysis in
`~/Desktop/UA101/ua101_plan.md` §4:

- **IOUSBHost** in ObjC, not libusb.
- Personal signing for the daemon.
- If `AppleUSBAudio` or anything else binds the device, evict it with
  an IOKit re-probe at daemon start. Since Mbox 1 is vendor-specific
  (not class-compliant), likely nothing binds it and we can grab it
  directly. Confirm in Phase 0.
- Real-time thread policy on the isochronous callback thread.

## 5. Architecture

Single architecture, mirroring UA-101 Architecture B (HAL plug-in) —
skipping the BlackHole prototype since we already validated that path
on UA-101 and Mbox is proportionally cheaper (2ch, single fixed rate,
symmetric channel counts). Feedback loop still required — see §3.

```
        USB bus
           │
     ┌─────┴──────┐
     │   mboxd    │  (userspace daemon, IOUSBHost)
     │ ┌────────┐ │
     │ │libmbox_core│ ← isoc URBs, ring buffers, control xfers
     │ └───┬────┘ │
     │     │ shm  │
     └─────┼──────┘
           │
     ┌─────┴──────┐
     │Mbox.driver │  (HAL plug-in in coreaudiod)
     │  ┌───────┐ │
     │  │shm ring│  ← same layout as libua101_shm
     │  └───┬───┘ │
     └──────┼─────┘
            │
       CoreAudio HAL
            │
      Logic Pro
```

Shared-memory protocol: copy `libua101_shm` verbatim, rename symbols.
Mbox is symmetric 2/2, so the v2 `playback_channels` field is redundant
but keep it for consistency with UA-101's layout.

## 6. Directory layout (mirrors UA-101)

```
mbox/
├── plan.md                     ← this file
├── libmbox_core/               ← USB + rings + stream state (C)
│   ├── mbox.h
│   ├── mbox_stream.c
│   ├── mbox_ring.c
│   └── mbox_shm.{h,c}
├── mboxd/                      ← userspace daemon
│   └── main.m
├── mbox_plugin/                ← HAL plug-in
│   ├── mbox_plugin.c
│   └── Info.plist
├── tools/
│   ├── descriptor_dump.sh
│   ├── measure_quality.sh
│   └── test_routing.sh
├── reference/
│   ├── quirks-table.h.snippet  ← copy of the Mbox 1 quirk entry
│   ├── mixer_quirks.c.snippet  ← copy of snd_mbox1_* helpers
│   └── zammit_notes.md         ← distilled from the write-up
└── Makefile
```

## 7. Implementation phases

Shorter than UA-101 across the board — no rate switching, no feedback
loop, no asymmetric channel handling.

### Phase 0: Reconnaissance and firmware check (1–2 days)
- Full USB descriptor dump on macOS (`system_profiler SPUSBDataType`,
  `ioreg -p IOUSB -l`).
- Confirm VID/PID `0x0dba:0x1000` matches expectation.
- Confirm endpoints 0x02 (OUT) + 0x81 (IN) exist with wMaxPacketSize
  0x130 at iface 1 altsetting 1.
- **Read firmware version via control transfer** — must be ≥ 0.22.
  If lower, stop and upgrade first (proprietary tool on a Mac that
  still has Rosetta / old OS, or the desolder trick — decide then).
- Confirm nothing on macOS grabs the interface (probable, since
  vendor-specific).
- Save all dumps into `reference/phase0/`.

### Phase 1: IOUSBHost skeleton + capture (3–5 days)
- Claim device, open iface 1 alt 1.
- Fire the two control transfers to set clock=internal-48k and
  input=analog.
- Submit iso IN URBs on EP 0x81, dump raw payload.
- Verify format is S24_3BE 2ch by decoding to float and dumping a
  WAV — sine into ch 1 should look like a sine.

### Phase 2: Playback + implicit feedback (1–2 weeks)
- Iso OUT URBs on EP 0x02 at nominal 48 samples/pkt.
- Capture EP 0x81 is async — track packet-size deltas over a window
  to derive the device's effective sample rate (mirrors UA-101's
  implicit-feedback code in `libua101_core/ua101_stream.c`).
- Feed that back into playback pacing so the two directions stay
  locked. Simplest form: emit `48 ± δ` samples per playback URB
  based on capture arrival rate.
- Loopback test: sine → EP 0x02, monitor with headphones, verify
  no drift over 10 min.

### Phase 3: shm IPC layer (2–3 days)
- Port `libua101_shm` → `libmbox_shm`. Trivial rename + version bump.
- Verify with a standalone reader/writer test (copy UA-101's).

### Phase 4: HAL plug-in (1 week)
- Copy `ua101_plugin.c` → `mbox_plugin.c`, strip UA-101-isms:
  - Asymmetric channel count logic → gone (fixed 2/2).
  - Rate-switching / `RequestDeviceConfigurationChange` polling
    machinery → gone (fixed 48 kHz).
  - Anchor triple stays, IO callback stays.
- Expose clock-source and input-source as CoreAudio properties (or
  defer to a separate `mboxctl` CLI if the property model is painful).
- Install to `/Library/Audio/Plug-Ins/HAL/`.

### Phase 5: Logic Pro first-light (2–3 days)
- Round-trip test at 48 kHz.
- THD/SNR measurement via `measure_quality.sh` (copy UA-101's).
- Verify clock-source switch (Internal vs S/PDIF sync) works.
- Verify input-source switch (Analog vs S/PDIF).

### Phase 6 (optional): polish
- S/PDIF I/O routed as additional CoreAudio channels if the format
  descriptor supports it (may already be included in the 2ch stream
  when source=S/PDIF — need to check).
- No MIDI phase (Mbox has none).
- Pro Tools: modern PT probably ignores non-Avid devices, worth a
  5-minute test but not a blocker.

## 8. Lessons carried from UA-101

Applied to Mbox from day one:

1. **Plug-in reads shm on Initialize** to learn actual format, even
   though Mbox is fixed-rate. Habit worth keeping.
2. **Direct callback I/O in the daemon, no bridge rings.** Mirror
   the Linux-style design (see UA-101 `main.m` refactor).
3. **`.gitignore` and Makefile from day one** — copy UA-101's.
4. **Check firmware version in Phase 0** — a broken FW will look like
   a driver bug and eat days.

## 9. Open questions

- [ ] Actual firmware version on our unit? (Phase 0 answers this.)
- [ ] Does anything on macOS 15 bind `0x0dba:0x1000`, or is it free
      for the taking? (Phase 0.)
- [ ] Does clock-source=S/PDIF and input-source=S/PDIF change the
      channel content in the 2ch stream, or are S/PDIF and analog
      surfaced as separate channels somewhere? (Phase 5 or a
      Windows-driver reference.)
- [ ] Any drift observed with sync-endpoint approach? If yes, add a
      soft feedback loop from capture packet timing.

## 10. References

- Upstream Linux driver:
  https://github.com/torvalds/linux/blob/master/sound/usb/mixer_quirks.c
  (search `snd_mbox1_`) and `sound/usb/quirks-table.h` (search
  `0x0dba, 0x1000`). Local copies of the relevant sections are in
  `reference/mbox1_mixer_quirks.c.snippet` and
  `reference/mbox1_quirks-table.h.snippet`.
- Damien Zammit's write-up (mainline history, FW gotcha):
  https://www.zamaudio.com/?p=953
- Original ALSA-devel descriptor dump + endpoint discussion:
  https://alsa-devel.alsa-project.narkive.com/WNtPqKI6/digidesign-mbox-usb-audio-device
- Duplex-capture patch:
  https://mailman.alsa-project.org/pipermail/alsa-devel/2014-November/083709.html
- Sister project: `~/Desktop/UA101/ua101_plan.md`,
  `~/Desktop/UA101/libua101_core/ua101_shm.h`,
  `~/Desktop/UA101/ua101d/main.m`,
  `~/Desktop/UA101/ua101_plugin/ua101_plugin.c`.
