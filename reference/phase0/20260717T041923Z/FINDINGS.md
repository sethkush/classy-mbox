# Phase 0 findings — 2026-07-16

Source dumps in this directory:
- `mbox_full.txt` — `ioreg -p IOService -n "Mbox USB Audio Device …" -r -l -w0`
- `ioreg_full.txt` — `ioreg -p IOUSB -w0 -l`
- `mbox_subtree.txt` / `mbox_interfaces.txt` — extracted slices

## Confirmed

- **VID:PID = `0x0dba:0x1000`** — matches Linux quirks entry.
- **USB 1.1 full-speed** (bcdUSB=0x0110, USBSpeed=1).
- **Composite device**, 1 configuration, 2 interfaces (iface 0 + iface 1).
- **Currently active driver: `AppleUSBHostCompositeDevice` only.** This
  is macOS's generic composite handler — no audio kext, no DriverKit
  DEXT, nothing else is trying to bind the audio interfaces. We can
  claim them cleanly from IOUSBHost without eviction.
- **Both interfaces present on alt 0 with class 255 (vendor), 0 endpoints.**
  This matches expectation: audio lives at iface 1 alt 1, so we'll need
  to issue `SetAlternate(1, 1)` in the daemon to activate the ISOC EPs.
- **Bus-powered, 480 mA** — nothing unusual, standard USB bus power.

## Concerning: firmware version

`bcdDevice = 0x0020`. BCD interpretation = version **0.20**.

Zammit's Linux write-up warns that Mbox 1 units shipped with firmware
< 0.22 have a white-noise bug that a Windows/Mac-only upgrade tool
(`MboxFirmware22_33860.dmg`) fixes.

**Caveat:** it's not proven that USB `bcdDevice` == Digi's firmware
version. Two hypotheses to distinguish before panicking:

1. **`bcdDevice` IS the FW version.** Then this unit is on 0.20 and
   will produce white noise once we start streaming. Fix requires
   running Digi's flasher on an older macOS / Windows VM, or the
   desolder-EEPROM approach.
2. **`bcdDevice` is a fixed hardware revision, unrelated to FW.**
   Then it's meaningless and streaming will just work.

**Cheapest way to test:** proceed to Phase 1 (daemon skeleton + claim
iface 1 alt 1 + submit ISOC IN), and listen to the captured audio.
If it's white noise regardless of input, hypothesis 1 is confirmed.
If we can pass a clean signal, hypothesis 2 is confirmed.

## What's still unknown after Phase 0

- **Endpoint descriptors** (`0x02` OUT SYNC, `0x81` IN ASYNC, packet
  0x130) are declared by the Linux quirk but not visible in ioreg
  because we haven't selected iface 1 alt 1. Confirm by parsing the
  full config descriptor via `IOUSBHostInterface::copyConfigurationDescriptor`
  once we open the device in Phase 1.
- **+48V phantom switch state** — untestable without vendor control
  transfers we haven't issued yet.
- **Clock source / input source current values** — same.

## Verdict

Green light for Phase 1. The device is present, unclaimed, and matches
the expected VID/PID. The firmware question is worth clarifying but
not a blocker — it resolves itself the moment we try to stream.
