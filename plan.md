# Mbox 1 Reverse-Engineering + Custom UAC Firmware — Implementation Plan

> **Endgame:** replace the Mbox 1's stock Digidesign firmware with a
> class-compliant USB Audio Class firmware, so the device works on
> every OS forever with no driver, no daemon, no HAL plug-in.
>
> **Reference point:** the UA-101 sister project (`~/Desktop/UA101`)
> built a full userspace-driver stack (USB daemon + shm IPC + HAL
> plug-in). Its architecture, shm layout, and Linux-mirror lessons
> are still relevant IF we ever need to fall back to a driver-based
> solution — e.g. as a lab tool while iterating on custom firmware.

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

Turn the original Digidesign Mbox (Mbox 1, 2002, USB 1.1) into a
class-compliant USB Audio Class device by replacing its firmware.
Deliverables in order:

1. **Reverse-engineer Digi's stock flash protocol** enough to
   reimplement it as a native arm64 macOS CLI (`mboxflash`) that
   speaks IOUSBHost directly. This unblocks flashing on the user's
   Apple Silicon audio machine (Digi's own flasher is i386 + PPC —
   won't run on modern macOS).
2. **Flash the stock v22 firmware** using `mboxflash` to confirm the
   protocol is understood end-to-end AND fix the buggy Rev-20
   firmware currently on the unit. Provides a safe recovery path
   before we start iterating on custom firmware.
3. **Reverse-engineer the stock v22 firmware image** (dumped from
   EEPROM after flashing) to learn the board's TAS1020A pinout —
   which GPIOs drive S/PDIF, which I²S port talks to which converter,
   which pins read the front-panel switches, etc.
4. **Write class-compliant firmware** for the TAS1020A that:
   - Enumerates as a standard USB Audio Class 1.0 device (2ch in /
     2ch out, 24-bit, 44.1 + 48 kHz).
   - Uses correct explicit-feedback endpoint pattern.
   - Optionally exposes S/PDIF I/O as additional channels or a
     switchable input.
   Starting point: TI's own TAS1020A UAC reference firmware (was
   part of TI's evaluation kit for the chip).
5. **Flash and validate** — should just work on macOS, Linux,
   Windows, iOS, Android with no driver.

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

There is no runtime architecture on the host — that's the whole point.
Once the Mbox has UAC firmware:

```
   USB bus              OS's native USB Audio stack
      │                 (CoreAudio / ALSA / WASAPI /
      │                  UsbAudio DDK on Windows)
   ┌──┴───┐                     │
   │ Mbox │ ← UAC 1.0 →   ┌─────┴──────┐
   │(TAS1020A│               │  Any DAW    │
   │+ Cirrus)│               │(Logic/PT/…)│
   └───────┘                └────────────┘
```

The tooling we build is entirely off the audio path:
- `mboxflash` — arm64 macOS CLI to program the TAS1020A EEPROM.
  Runs infrequently (only when updating firmware).
- Firmware source tree — 8051 C, built with SDCC (open-source,
  cross-platform) rather than Keil C51 (proprietary, Windows-only).

## 6. Directory layout

```
mbox/
├── plan.md
├── mboxflash/                  ← native arm64 macOS flasher
│   ├── main.m                    (IOUSBHost)
│   ├── protocol.{h,c}            (reimplements Digi's flash protocol)
│   └── Makefile
├── firmware_stock/             ← RE artifacts from Digi's v22 flasher
│   ├── strings_i386.txt
│   ├── protocol_notes.md         (bmRequestType/bRequest/wValue map)
│   ├── flash_payload.bin         (extracted firmware blob when found)
│   └── disasm/                   (radare2 project + annotations)
├── firmware_stock_dumped/      ← EEPROM contents pulled from device
│   ├── v22_dump.bin
│   ├── disasm/                   (8051 disassembly + annotations)
│   └── pinout.md                 (board wiring derived from firmware)
├── firmware_uac/               ← our custom class-compliant firmware
│   ├── src/                      (8051 C, SDCC toolchain)
│   ├── descriptors.c             (UAC 1.0 device+config descriptors)
│   ├── isoc.c                    (isochronous audio streaming)
│   ├── i2s.c                     (I²S peripheral driver)
│   ├── spdif.c                   (CS8427 control via I²C)
│   └── Makefile
├── tools/
│   ├── descriptor_dump.sh
│   └── (misc helpers)
└── reference/
    ├── firmware/                 (Digi flasher .dmg + Read Me — gitignored)
    ├── phase0/                   (device descriptor dumps)
    ├── mbox1_mixer_quirks.c.snippet
    ├── mbox1_quirks-table.h.snippet
    └── tas1020a/                 (TI datasheet, UAC reference, if we can find it)
```

## 7. Implementation phases

### Phase 1: Reverse-engineer the stock flasher (in progress)
- Extract i386 slice from `Update Mbox Firmware v22.app`.
- Static analysis with radare2: locate `PrepareForDownload`, find
  every `IOUSBDeviceInterface` control-transfer call site, note the
  bmRequestType / bRequest / wValue / wIndex / payload pattern.
- Locate the firmware payload in the binary — 32 KB or so of 8051
  code destined for TAS1020A EEPROM. Not yet found in obvious
  places; may be compressed or in a resource we haven't opened.
- Deliverable: `firmware_stock/protocol_notes.md` fully specifying
  the flash sequence, and `firmware_stock/flash_payload.bin`.

### Phase 2: Native macOS flasher (1 week)
- Write `mboxflash` in ObjC using IOUSBHost.
- Implement the protocol from Phase 1's notes.
- First run: flash the ORIGINAL Digi v22 payload back onto the
  device (safest possible test — should end up in the same state).
- Confirm `bcdDevice` reads 0x22 after re-enumeration.

### Phase 3: Fix this unit's firmware (1 day)
- Run `mboxflash` on the actual Mbox with the Digi v22 payload.
- Confirm playback is clean (no more Rev-20 white-noise bug).
- From this point on, this Mbox can be used safely with the Linux
  driver / any UAC-emulating tool, so the flasher is proven safe
  even if the class-compliant firmware effort stalls.

### Phase 4: Reverse-engineer the stock firmware (2–4 weeks)
- After flashing, dump the EEPROM contents back through `mboxflash`
  read path (if the Digi protocol supports READ_EEPROM — some do).
- If not, extract the payload from the flasher binary as canonical
  reference — it's what got written.
- Disassemble the 8051 image (SDCC comes with a disassembler;
  IDA / Ghidra also handle 8051).
- Reverse: USB descriptors, EP0 handlers, isoc audio path, I²S
  configuration, CS8427 init, GPIO mapping to front-panel switches
  and the S/PDIF chip.
- Deliverable: `firmware_stock_dumped/pinout.md`.

### Phase 5: Class-compliant firmware (4–8 weeks)
- Start from TI's TAS1020A UAC reference (part of the eval kit,
  should be recoverable from TI archives — may need another
  research fork). Fallback: write from scratch using the TAS1020A
  datasheet + open-source 8051 USB stacks.
- Match the board pinout learned in Phase 4.
- Enumerate as UAC 1.0: 2ch S24_3LE at 44.1 + 48 kHz.
- Get through iterating without bricking — validate on the device,
  fall back to Digi v22 via `mboxflash` if the custom firmware is
  unbootable.

### Phase 6 (optional): Feature parity + polish
- S/PDIF I/O as switchable input source (mirror Linux quirk's
  input-source concept, but exposed as a UAC selector unit).
- Clock-source selector (internal vs. S/PDIF sync) as a UAC unit.
- Support 44.1 kHz explicitly (Digi's firmware might only do 48).
- Firmware version bumps signaled cleanly through bcdDevice.

## Bricking / recovery plan

Every custom-firmware attempt risks writing an image that the
TAS1020A can't boot from. Before flashing anything but Digi's
signed v22 image, we need a recovery path:

- **TAS1020A ISP mode.** The chip has a boot-loader mode entered
  by pulling specific pins low at reset. If it's accessible on
  the Mbox 1's board (traces to a header, or possible with a clip
  probe), we can always reflash even from a completely bricked
  state. TODO: locate on the PCB.
- **Digi's original v22 payload** as the "known-safe" image.
  Keep it around forever.
- Iterate custom firmware in a dry-run mode first (compile,
  simulate under uVision / SDCC's mcs51 sim) before ever flashing.

## 8. Lessons carried from UA-101

Most of the UA-101 architecture is off the table (no driver). What
DOES carry over:

1. **Reference the Linux driver source seriously.** Even for the
   custom-firmware effort, `sound/usb/mixer_quirks.c` documents
   the four vendor control transfers Digi's stock firmware honors —
   if we want ours to be a drop-in replacement for people already
   running the Linux driver, ours should honor them too.
2. **Verify firmware version before assuming anything works.**
   Rev-20 vs v22 wasted half a day; TAS1020A firmware iterations
   will be worse. Read `bcdDevice` at every step.
3. **Save every reference artifact.** `reference/` structure paid
   off on UA-101, keep the discipline here.

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
