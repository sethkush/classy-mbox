# Firmware-change policy

This document is short by design. Read it before any change that
touches SFR-backed XDATA (0xFF00-0xFFFF) writes, USB behaviour,
EEPROM I/O, or the flash toolchain.

## 1. Reference first, code second

Every SFR-touching change must cite the reference that justifies it,
in a comment on or above the write. Accepted forms:

- **Rev 20 disassembly:** `/* Rev 20 fcn.0xXXXX @ 0xYYYY */`
- **TI reference source:** `/* TI UsbEng.c::engUsbInit line 647 */`
- **Novel:** `/* NOVEL — reason: <one-line explanation> */`

The pre-commit hook (`tools/check_sfr_citations.py`) rejects diffs
that add SFR writes without one of these markers within ~3 lines of
the write. Use `git commit --no-verify` only after explaining in the
commit body why the reference doesn't apply.

## 2. Never assign — always RMW — for boot-ROM-owned SFRs

Boot ROM configures these SFRs before handing off to firmware.
Assignment (`=`) clobbers whatever bits it set. Always use `|=` /
`&= ~`:

- `USBCTL`  (0xFFFC) — CONN, FEN, SDW confirm
- `MEMCFG`  (0xFFB0) — SDW bit swaps ROM/RAM
- `GLOBCTL` (0xFFB1) — CPU speed, USB engine enable

For interrupt masks (`USBIMSK`, `IE`), RMW is preferred but not
required — those are logically ours to fully own after boot.

### Carve-outs (assignments explicitly authorized)

- **A.** `USBCTL = 0` at the very top of `main()`, as an intentional
  pre-init USB disconnect. This is one of the two things Rev 20 does
  before doing anything else with USB (rev20_flat.asm 0x08E5, inside
  master-init sub 0x08CB): `clr a; mov dptr,#0xfffc; movx @dptr,a`.
  Using `&= ~0xC0` here does not work — boot ROM may have set bits
  we don't know about, and we want a fully-zero USBCTL before
  reconfiguring GLOBCTL/EP0/USBIMSK. Any assignment to USBCTL after
  init falls back under §2 general rule (RMW only).

- **B.** *(reserved for future use)*

- **C.** The `RESET_TO_BOOT_ROM()` macro in `mboxfw/include/regs.h`
  does a USBCTL SDW-confirm handshake bracket (`|= 0x01` then
  `&= ~0x01`) as part of re-entering boot ROM from app mode. Both
  writes look like forbidden RMW-on-boot-ROM-owned SFR under the §2
  general rule, but they are literally byte-for-byte the sequence
  TI's own boot ROM uses in Utils.SRC UtilResetBootCPU (lines
  119-160). Do not strip these writes: they wrap the MEMCFG.SDW flip
  and are the documented USB-engine shadow-view handshake. This
  carve-out fires only in the DFU-trigger recovery path (called from
  `handle_dfu_trigger` in safety_net, `check_boot_dfu_button` in
  mboxfw main.c, and the DFU-class request handler in mboxfw usb.c).
  See BRICK_LOG 2026-07-24 for the bug pattern this replaces.

Add carve-outs to this list only after citing a working reference
(Rev 20 disasm or TI reference source) that does the same thing, AND
after documenting the reason a plain RMW form does not suffice.

## 3. Run the gates before every flash

There are eleven pre-flash gates. If any fails, the flash does not
happen. Full list in `mboxfw/README.md` "Pre-flash verification".

- `sim_smoke.sh`  proves `main()` runs through to the polling loop
  AND USBCTL CONN gets set AND all six phase canaries fire.
- `verify_descriptors.py`  validates the compiled UAC1 descriptor
  bundle.
- `verify_usb_init.py`  pins the enumeration-critical EP0 register
  writes to the exact bytes Rev 20 uses.
- `verify_cs8427.py`  pins the 10-register CS8427 boot sequence.
- `verify_setup_paths.py`  proves SET_ADDRESS deferred-write, Digi
  DFU class request recognition, `reply_zero_length` presence, and
  boot-time button DFU trigger are all reachable in the image.
- `test_wrap_hex_golden.py`  proves `wrap_hex(rev20)` == the stock
  Rev-20 flasher payload byte-for-byte.
- `audit_sfr_writes.py`  fails on any drift from the committed
  `tools/sfr_writes.allowed` baseline.
- `diff_vs_rev20.py`  fails on any Rev-20-vs-mboxfw write diff
  without a justification row in `tools/rev20_diff_justifications.md`.
- `dfu_timing_profile.sh`  proves SETUP → ACK stage is under the
  4000-cycle budget (109 µs @ 12 MHz today).
- `mboxflash --validate`  static wire-format check on the wrapped
  binary.
- Post-flash: `e2e_flash_loop.sh` round-trips the toolchain.

## 4. Flash the safety net FIRST

Before flashing any mboxfw revision to a device that isn't already
running mboxfw, flash `safety_net/build/safety_net_flasher.bin`
first. Prove that:

1. Flash succeeds and the safety net enumerates (bcdDevice=0xDEAD).
2. `mboxflash --enter-dfu` cleanly re-enters DFU.
3. Boot ROM DFU dump matches the flashed image byte-for-byte
   (via `e2e_flash_loop.sh`).

Only then flash the full mboxfw. If mboxfw bricks, the recovery
path was proven working ~1 minute ago on the same hardware.

## 5. Weasel-word review

Before recommending a flash, scan the pre-flash summary for:

- "should" / "should be fine"
- "probably" / "likely"
- "seems like" / "looks right"

Every one of these words is either replaced with "verified by X"
or removed. `tools/lint_weasel.py` scans commit messages
automatically.

## 6. Pre-flash checklist

Do not proceed with a flash without explicitly stating each of:

1. **Changed since last flash:** a bullet list of every source
   modification since the last successful flash of the same target.
2. **Unknowns:** what we haven't tested, verified, or understood.
3. **Rollback plan:** what to do if the flash bricks. Includes
   which backup image restores what.
4. **Recovery paths active in the flashed firmware:** class-request
   DFU, button-hold DFU, safety net (yes/no).

Missing any item = do not flash.

## 7. Flashing from bulletproof-DFU REQUIRES two-stage bootstrap

**If the device is in bulletproof-DFU (VID 0xFFFF PID 0xFFFE — entered
via SDA-short), you CANNOT flash a dataType=0x01 image directly.**
Bulletproof-DFU only persists the 18-byte EEPROM header, not the code
region. A single-stage flash from bulletproof-DFU leaves the chip with
a valid header pointing at an unwritten code region → boot ROM fails
validation on cold boot → device re-enters bulletproof-DFU → silent
USB. See BRICK_LOG 2026-07-25 for the multi-hour hunt that finally
pinned this down.

**Correct sequence (do all steps in order):**
1. SDA-short → device enters bulletproof-DFU (verify with
   `ioreg -p IOUSB -l | grep "idVendor.*65535"`).
2. `mboxflash --flash safety_net/build/safety_net_bootstrap.bin` —
   this uses dataType=0x03 (APPCODE_UPDATING). Only the 18-byte
   header persists, but that's enough to signal "flash in progress"
   to the boot ROM.
3. Physically unplug/replug — device MUST come up as app-DFU
   (0x0DBA:0x1001). Verify with `ioreg -p IOUSB -l | grep
   "idProduct.*4097"`. If it's still 0xFFFF:0xFFFE or absent from
   ioreg, something went wrong — do NOT proceed.
4. `mboxflash --flash <real_image>` — from app-DFU, code WILL persist
   to EEPROM. Use `firmware_stock/rev20_flasher_payload.bin`,
   `firmware_stock/rev22_flasher_payload_raw.bin` (after wrapping),
   `mboxfw/build/mboxfw_flasher.bin`, or
   `safety_net/build/safety_net_flasher.bin`.
5. Physically unplug/replug — device boots the flashed firmware.

**If the device is already in APP-DFU (0x0DBA:0x1001) — e.g., because a
prior flash left dataType=0x03, or because the running firmware
accepted a `--enter-dfu` class request — skip step 2 and go straight
to step 4.**

## 8. mboxflash changes require hardware verification, not just fork audit

Four commits in July 2026 added "safety" logic to `mboxflash/main.m`
and `mboxflash/dfu.m` based on fork-audit speculation about
hypothetical DFU edge cases: `9787940` (per-block retry), `ddc50e9`
(scope-2 restart), `15fc73b` (scope-3 retry), `82042d0` (post-manifest
readback verify). None were tested against real hardware before
commit. `82042d0`'s post-manifest verify was actively harmful — it
issued `DFU_ABORT` during dfuMANIFEST, which per TI UsbDfu.c
transitions the boot ROM into dfuERROR, corrupting the state machine.

**Rule: any change to mboxflash's DFU flow — retry logic, poll
intervals, request sequencing, error handling — must be verified on
hardware end-to-end before commit.** "Verified via fork audit" or
"reviewed against DFU 1.1 spec" is NOT sufficient. The wire-level
IOKit behavior on modern macOS + the specific TAS1020B boot-ROM DFU
implementation together form a system with quirks that neither the
spec nor code review can predict.

Digidesign's own updaters (RE'd this session — see
`reference/firmware/rev20/digidesign_flasher_dfu_flow.md` and
`reference/firmware/rev22_updater_dfu_flow.md`) use a MINIMAL DFU
sequence: only DNLOAD and GETSTATUS, no ABORT/CLRSTATUS/UPLOAD, no
forced bus reset. They wait for the user to physically unplug/replug.
That minimal flow is what our mboxflash should stay close to.

## History of what NOT to do

Each of these was a real brick 2026-07-22:

- **Flash #1 brick:** `wrap_hex.py` padded to 8192 bytes. Boot ROM
  triggered `errFILE` on the overrun byte and left the flash
  incomplete → dataType stuck at APPCODE_UPDATING → device dropped
  to app-DFU on boot instead of running our firmware. Root cause:
  we didn't read `reference/tas1020a/ti_uac_reference/ROM/UsbDfu.c`
  before writing wrap_hex.py.

- **Flash #2 brick:** `USBCTL = 0xC0` in `usb_init` was an
  assignment, clobbering FEN/SDW state the boot ROM configured.
  Firmware ran through init but never attached to the bus. Root
  cause: we noticed the audit finding "USBCTL never written" and
  fixed with a plain `=` instead of reading Rev 20's disasm which
  does `USBCTL |= 0x80`.

Both bricks required physical SDA-shorting of the EEPROM to
recover — no fun with a soldered DIP-8 in a metal enclosure.
