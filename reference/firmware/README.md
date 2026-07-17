# Mbox 1 firmware v22 flasher

## Files
- `MboxFirmware22_33860.dmg` — the official Digidesign flasher, 1,143,646 bytes,
  SHA-256 `3fa31796422e89711c101dc2797e5aba36017a0252fda6ceacf603de989e3998`.
  Retrieved 2026-07-16 from the Wayback Machine snapshot
  `https://web.archive.org/web/20170124021507id_/http://akmedia.digidesign.com/support/compressed/MboxFirmware22_33860.dmg`
  (originally hosted at `akmedia.digidesign.com/support/compressed/`; Avid
  took it down between 2017 and 2026). This is the *only* known live copy;
  worth mirroring to archive.org as a preservation piece.
- `Mbox Firmware v22 Read Me.pdf` — official Digidesign release notes,
  extracted from the .dmg.

## Compatibility
The flasher binary (`Update Mbox Firmware v22.app/Contents/MacOS/Update Mbox Firmware`)
is a **universal Mach-O with ppc + i386 slices only** — no x86_64, no arm64.
This means:
- **Will not run** on any Apple Silicon Mac.
- **Will not run** on any Intel Mac past macOS 10.14 (Mojave), since 10.15
  Catalina dropped 32-bit i386 support.
- Runs on: Intel Mac 10.14 Mojave or earlier; PowerPC Mac (G4/G5) on
  any OS X that hosts the universal binary.
- The flashing operation communicates with the Mbox over USB, so the
  host machine must have physical USB access to the device (no VMs
  without USB passthrough).

## After flashing
Re-run `tools/descriptor_dump.sh` and confirm `bcdDevice = 0x0022`.

## Not committed to the repo
The .dmg itself is proprietary Digidesign software and is `.gitignore`d.
Anyone re-cloning this repo will need to re-download it from the URL above.
