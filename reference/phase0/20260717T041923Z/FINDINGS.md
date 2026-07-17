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

## Firmware version — resolved (2026-07-16, deep dive)

`bcdDevice = 0x0020` = **firmware Rev 20**. Confirmed by cross-check:
Digidesign shipped exactly two field updaters, named "Mbox Firmware
Updater Rev 20" and "Mbox Firmware Updater v22" — the numbers line
up 1:1 with the BCD values `0x0020` and `0x0022`. Multiple Linux
`lsusb -vv` dumps from other users' pre-flash Mboxes show the same
`bcdDevice 0.20`. Zammit's own tutorial reads FW version directly
from `bcdDevice`.

**Our unit is on the buggy revision.** The Rev-20 defect manifests
as sporadic bursts of loud static during **playback** (loud enough
that Zammit warns of speaker damage). No workaround by mode/rate
selection — Rev 20 must be flashed to v22 before we can safely
drive the outputs.

**Capture likely unaffected** — every failure report describes
playback. So Phase 1 (capture-only skeleton) can proceed safely
without flashing, as long as we don't route audio out of the box.

**No alternative FW probe.** The four vendor control transfers in
Linux's `mixer_quirks.c` (`snd_mbox1_*`) are all class-standard
GET/SET on clock and input selectors — none read the EEPROM.

**Flashing.** `MboxFirmware22_33860.dmg` is Mac-only, PPC+Intel
universal Mach-O. Original Avid download links are dead. Mirrors to
try (in order of likelihood):
1. `web.archive.org` snapshots of
   `archive.digidesign.com/download/mbox/` and
   `secure.digidesign.com/services/avid/kb/downloads.cfm?digiArticleId=24602`.
2. The DUC (Digidesign User Conference) forum archives.
3. `macos9lives.com` thread 1946.
4. `linuxmusicians.com` thread 11339 (may have a user-hosted mirror).
5. Personal request on Reddit r/protools.

Once flashed on an old Intel Mac (needs an OS that still runs
Universal Mach-O — 10.14 Mojave was the last, so likely 10.13 High
Sierra or earlier for stability), re-run `descriptor_dump.sh` and
confirm `bcdDevice = 0x0022`.

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
