# Brick log

Every time the Mbox 1 has been soft-bricked (unable to enumerate,
required physical recovery), the incident goes here. Symptom first
so future-me can grep for it, then root cause, then the fix commit.

Physical recovery for all entries below has been the same: SDA-short
recovery on the Atmel 24C64A EEPROM (pin 4 GND ↔ pin 5 SDA during
plug-in) → device enters bulletproof DFU at 0xFFFF:0xFFFE → flash a
known-good image.

## Entries (newest first)

### 2026-08-03 — Flash #3 — 6448-byte image into 6016 bytes of program RAM

**Symptom:** Mbox A flashed cleanly from `ffff:fffe` — 202/202 blocks, DFU
manifest reached — then came up silent on USB after a real power cycle. LEDs
lit, hub reports `power` with no `connect` bit: powered, never asserts the D+
pullup. Button-hold DFU produced nothing either. No software route back in.

**Root cause: the image is larger than the part's program RAM.** The TAS1020B
has **6016 bytes (0x1780)** of program RAM — datasheet features list and §1
overview, stated twice, recorded in `ramloader/DESIGN.md` "Verified
constraints". The boot ROM copies the EEPROM payload into that RAM and runs it
there. The flashed image was **6448 bytes — 432 over**. It was written to
EEPROM correctly and the header is valid; what runs is truncated, so execution
never reaches `usb_attach()` (main.c:297) and CONN is never set.

**How the guard came off.** On 2026-07-30 both `mboxfw/Makefile`
(`--code-size 0x1780 -> 0x1C00`) and `tools/check_code_size.py`
(`6144 -> 7168`) were raised, justified by: *"Rev 20 uses 8174 bytes — nearly
full EEPROM… so 6144 was never a hardware constraint."*

That premise is false. 8174 is the FF-padded EEPROM region, not code. Stock
Rev 20's last non-0xFF byte is at **0x103E — 4159 bytes** of real content;
Rev 22 is 4150. Both sit ~1850 bytes UNDER 6016. Padding was read as code, the
hardware limit was declared imaginary, and the guard was removed.

`ramloader/DESIGN.md` had predicted this exact failure while the build was
3399 B, under the heading "Latent bug this exposes":

> both link with `--code-size 0x1F00` against 6016 bytes of real RAM …
> **the linker will happily produce an image that cannot exist.**

`safety_net/Makefile` was never raised and still links at 0x1780, which is why
safety_net remains flashable and is the recovery image.

**What the gates did.** All 32 passed, including `check_code_size.py`, because
its budget had been raised past the hardware. The POLICY §6 checklist was
recited in full and named the risk as "six untested hardware changes" — the
actual risk was that the image could not run at all. Every executed gate runs
the image in a simulator with no program-RAM bound, so none could see it.

**Fixes (2026-08-03):**
- `mboxfw/Makefile` `--code-size` back to `0x1780`. The build now hard-errors:
  `?ASlink-Error-Insufficient ROM/EPROM/FLASH memory`.
- `tools/check_code_size.py` budget back to 6016, named `PROGRAM_RAM`, with
  `EEPROM_MAX` kept separate so the two limits cannot be confused again.
  Verified: it rejects the exact image that bricked A, `6448 > 6016`.

**Recovery — what actually worked.** The canonical two-stage bootstrap, from
the top of this file. Unchanged, still correct, no new tooling needed:

1. SDA short, plug in -> `ffff:fffe`, dfuIDLE
2. flash `safety_net_bootstrap.bin` (**dataType 0x03**) -> 54/54, manifest
3. replug -> **`0dba:1001` app-DFU**, dfuIDLE
4. flash `rev20_flasher_payload.bin` (dataType 0x01) -> 255/255, manifest
5. replug -> `0dba:1000`, bcdDevice 0.20, "Mbox USB Audio Device copyright
   Digidesign 2001". Stock Rev 20 running.

**ramflash was tried first and did not work.** Its first-ever hardware run:
download 146/146 + manifest, bus reset delivered, device dropped off the bus
(`USBCTL = 0` on entry, as designed) -- and a replug came back at `ffff:fffe`,
so the checksum byte never landed. It writes that byte last precisely so a
failed run lands in DFU rather than on a valid header over garbage, and that
is exactly what it did. No harm, but it cost a cycle and ~5 minutes.

Reaching for it at all was the mistake: the two-stage sequence above is four
lines from the top of this file, is hardware-proven, needs no new image, and
was passed over because POLICY §7's `DFU_TARGET_RAM` note was reasoned forward
from instead of checking what had actually worked. **Try the canonical
sequence FIRST.** ramflash is the fallback for the errPROG case it was written
for, not the default.

Note on reading DFU state: a device that has been handed a RAM image tells you
nothing by being absent from the bus -- ramflash halts in `for(;;)` and never
re-attaches, so success and failure look identical until you replug.


### 2026-07-25 — Rev 20 finally flashes cleanly. Multi-fault session, three real bugs found.

**Presenting symptom (start of session):** flashing safety_net or
Rev 20 stock via mboxflash produced silent USB on cold boot —
`ioreg` empty for VID 0x0DBA, `mboxflash --probe` said "no Mbox
found" or reported stale IOKit cache entries.

**Root cause 1 (the load-bearing one) — flashing from bulletproof-DFU
directly with a dataType=0x01 image only writes the HEADER to EEPROM,
never the CODE.** Bulletproof-DFU (0xFFFF:0xFFFE, entered via SDA-short)
is a minimal boot-ROM DFU implementation that writes user data to RAM
only, per TI UsbDfu.c:948-951. Only the 18-byte EEPROM header (chksum,
sigs, VID/PID, dataType, payloadSize) is persisted. Flashing the full
firmware image from bulletproof leaves the chip with a valid header
pointing at an unwritten code region → boot ROM validates on cold
boot, sees garbage in the code region → fails validation → enters
bulletproof again → silent USB.

The safety_net Makefile had documented this and provided
`safety_net_bootstrap.bin` (dataType=0x03 APPCODE_UPDATING) as the
first stage of a two-stage bootstrap. Multiple past sessions had
forgotten to use it — including several attempts this session before
we finally noticed the Makefile comment.

**The working sequence (CANONICAL — see POLICY §7):**
1. SDA-short → device enters bulletproof-DFU (0xFFFF:0xFFFE)
2. Flash `safety_net_bootstrap.bin` (dataType=0x03) — header written,
   telling boot ROM "flash in progress"
3. Replug → device comes up in app-DFU (0x0DBA:0x1001) — this is NOT
   bulletproof-DFU, it's the boot ROM's real DFU implementation which
   DOES persist code to EEPROM
4. Flash real firmware (`rev20_flasher_payload.bin` or
   `safety_net_flasher.bin`, both dataType=0x01) — code persists
5. Replug → boot ROM validates → device boots the firmware

**Root cause 2 — mboxflash post-manifest DFU_ABORT (introduced commit
82042d0):** even when the flash sequence was correct, our mboxflash
had a "post-flash readback verify" that issued `DFU_ABORT` while boot
ROM was in `dfuMANIFEST` state. Per TI UsbDfu.c DFU_ABORT handler,
only dfuDNLOAD_IDLE/dfuIDLE/dfuUPLOAD_IDLE are safe states for
DFU_ABORT — every other state hits `dfuErrStalledPkt()` → transitions
to `dfuERROR`. That corrupted the DFU state machine right before we
issued the bus reset. Removed in this session by reverting mboxflash
to commit `15fc73b` (last pre-post-verify commit).

**Root cause 3 — noise from four "fork audit" commits that added
speculative safety logic to mboxflash without hardware testing:**
`9787940` (per-block transport retry), `ddc50e9` (scope-2 whole-flash
restart), `15fc73b` (scope-3 retry), `82042d0` (post-manifest verify).
The retry logic never triggered under normal conditions, so it was
neutral, but the post-manifest verify was actively harmful (root cause 2).
General lesson: mboxflash changes must be hardware-tested end-to-end
before commit, not merely fork-audited.

**Confirmed working state (2026-07-25 end of session):** flashed Rev 20
stock via the two-stage bootstrap. `ioreg` shows:
- idVendor 0x0DBA, idProduct 0x1000
- bcdDevice 0x0020 (Rev 20)
- USB Product Name "Mbox USB Audio Device copyright Digidesign 2001"

**Session cost:** four SDA-shorts, several hours of chasing firmware
theories that were toolchain issues. Meta-lesson: when the presenting
symptom is a flash failure, verify the FLASH TOOLCHAIN against
known-good bytes (Rev 20 stock) before touching any firmware code.

**Historical fabrications this session dead-endified:**
- `GLOBCTL |= 0x01` claiming "enable USB" — actually CPTEN (codec).
  Removed from safety_net in 2b09379. Correct fix in isolation, did
  NOT resolve the presenting symptom (which was toolchain).
- USBIMSK bit-meaning comments in safety_net main.c — corrected in
  189c219 against datasheet §6.5.1.3. Correct in isolation, did NOT
  resolve the presenting symptom.
- "USBIMSK = 0x9F removed from VEC_RSTR" (2b09379) — argued in-code
  based on wrong bit meanings. Not harmful either way; not a fix.

Do NOT reflash these safety_net changes expecting them to solve
anything. They may or may not be the right thing depending on future
tests; they're comment-level clarifications, not silent-USB fixes.

### 2026-07-24 — Three `ljmp 0 with SDW=1` bugs (all caught pre-flash by fork audit)

**Symptom (would have been):** software DFU trigger appears to succeed
(EEPROM signature bytes invalidated at offsets 2 and 3, `ljmp 0`
executed), but device stays in the currently-running firmware instead
of entering DFU. User perceives the "trigger DFU" recovery button as
non-functional, has to unplug/replug for the invalidated signature to
take effect via the boot ROM's power-on signature check.

**Root cause:** three inline-asm sites did `__asm__("ljmp 0");` intending
to re-enter boot ROM, but with MEMCFG.SDW=1 (our runtime state) the
memory map is RAM 0x0000-0x1FFF / ROM 0x2000-0xFFFF. Address 0x0000 is
in RAM, so `ljmp 0` restarts the currently-running firmware's own reset
vector — it never reaches boot ROM. The invalidated EEPROM signature
only takes effect on the next physical power cycle, when boot ROM runs
its normal cold-start signature check.

**Sites:**
- `safety_net/src/main.c` handle_dfu_trigger (fixed 2026-07-24 first
  pass; then second-pass audit caught missing `clr ea` before the SDW
  flip — any USB interrupt firing between MEMCFG write and ljmp
  vectors into 0x0003 in boot ROM instead of app RAM, undefined code)
- `mboxfw/src/main.c` check_boot_dfu_button
- `mboxfw/src/usb.c` DFU-class request handler (arguably worst — USB
  fully live at this point, highest chance of ISR-during-transition)

**Fix:** shared `RESET_TO_BOOT_ROM()` macro in `mboxfw/include/regs.h`.
Byte-for-byte match to TI Utils.SRC UtilResetBootCPU (lines 119-160):
mask INT0 (`clr ea`) → USBCTL SDW-confirm ON → flip MEMCFG.SDW off →
USBCTL SDW-confirm OFF → `ljmp 0x8000`. The 0x8000 target is in ROM
under both SDW=0 (post-flip state) and SDW=1, so it always lands in
boot code.

**Prevention:**
- Full inline-asm audit fork sweep (task #112) enumerated every
  `__asm__` block in the tree and re-verified. Now 5 correct sites.
- Startup-code 4-way fork (safety_net vs mboxfw vs Rev 20 vs Rev 22,
  task #113) confirmed the fix matches TI reference byte-for-byte.
- POLICY §2 carve-out C — explicitly authorizes the RESET_TO_BOOT_ROM
  macro's USBCTL toggling (would otherwise be forbidden RMW-on-boot-
  ROM-owned SFR).

**Meta-lesson:** memory-map assumptions must cite the datasheet. An
earlier fork (#108) had claimed "RAM 0x0000-0x7FFF" based on
extrapolation; datasheet §6.5.7.5 says CODESZ=01b → RAM 0x0000-0x1FFF.
Fork #114 caught this while investigating a different question.

---

### 2026-07-23 — Near-recurrence of Flash #2 (caught pre-flash by fork audit)

**Symptom:** would have been identical to Flash #2 — silent USB after
`USBCTL = 0xC0` assignment. Caught in the safety_net firmware BEFORE any
flash attempt, during a multi-fork audit of everything shortcut in the
run-up to a proposed flash.

**Root cause:** safety_net/src/main.c had `USBCTL = 0xC0` (assignment)
plus `USBIMSK = 0x9F` justified by a fabricated theory that "IEP0
(bit 3) must be unmasked" — no source-side evidence exists for either.
The USBCTL assign is exactly Flash #2's pattern; the USBIMSK value
would have masked the SETUP interrupt paths the rest of the code
relies on. A prior fork also fabricated a `GLOBCTL |= 0x04; delay(10)`
"TI Device.c:75 canonical bring-up sequence" — no such code exists in
`reference/tas1020a/ti_uac_reference/Application/Device.c`.

**Fixes applied pre-flash (all cross-verified against source):**
- `USBCTL = 0` disconnect at top of main (POLICY §2 carve-out A;
  matches Rev 20 rev20_flat.asm 0x08E5 inside master-init sub 0x08CB)
- `USBCTL |= 0x80` RMW attach (matches Rev 20 0x0AE2)
- `USBIMSK = 0xE5` reverted from 0x9F (matches TI UsbEng.c:640 and
  Rev 20 0x091A)
- `GLOBCTL |= 0x01` USB-enable bit (matches Rev 20 0x100F and
  mboxfw/src/hw_init.c:54), not the fabricated `|= 0x04`
- `IT0 = 0` level-triggered INT0 (matches TI UsbEng.c:644)
- ~65k-iteration settle loop before EA=1/attach (matches Rev 20
  0x0AC5-0x0AD8)

**Fresh bug caught by the new gate:** safety_net/src/main.c has
`#define GLOBCTL XDATA(0xFFA1)` — wrong address. Correct is 0xFFB1
(Reg_stc1.h line 22, Rev 20 disasm 0x100F). `verify_safety_net.py`
flagged this as MISS on `0xFFB1 |= 0x01` before any flash.

**Prevention:**
- `tools/verify_safety_net.py` — new gate; parallel to
  `verify_usb_init.py` but scoped to safety_net's init sequence. Fails
  if any of the 17 enumeration-critical writes are missing from the
  emitted image.
- `tools/preflight.sh` — extended: safety_net_*.bin targets now route
  to the safety_net gate set instead of silently running mboxfw gates
  against a mismatched image.
- POLICY §2 carve-out A — explicitly authorizes `USBCTL = 0`
  disconnect at top of main() (with Rev 20 citation), so future
  assign-flavored disconnects don't get flagged as Flash #2 relapses.

**Meta-lesson:** fork reports are input, not truth. Every source
citation must be checked against the cited file before code changes
land. This session had three fabricated citations that would have led
to a brick if applied blindly.

---

### 2026-07-22 — Flash #2 — silent USB after `USBCTL = 0xC0` assignment

**Symptom:** flashed mboxfw, device came up with front-panel LEDs at
mic-default state (mux 0xF6) but NOTHING on USB — no VID/PID enumerated
anywhere. `ioreg` empty for VID 0x0DBA. `mboxflash --probe` reported
"no Mbox found". Button-hold DFU trigger didn't do anything.

**Root cause:** `usb_init` ended with `USBCTL = 0xC0` (assignment).
Boot ROM had FEN and/or SDW-confirm bits set before handoff; the plain
`=` clobbered them. Firmware ran through main() to completion (LEDs
proved hw_init ran, canaries would have shown all phases) but the
device was detached from the USB bus because USBCTL's state was
inconsistent with what the boot ROM had established.

**Compounding failure:** the FLASHED mboxfw also had a broken I²C
driver (see 2026-07-22 flash #1 root cause). So neither the
class-request DFU trigger nor the button-hold DFU trigger could
recover — both call `eeprom_smoke_test` which failed silently on the
missing `I2CDATO = 0xFF` write. SDA short required.

**Fix:** `USBCTL |= USBCTL_CONN` (RMW just the CONN bit, Rev 20's
`orl a, #0x80` pattern from disasm 0x0ADE-0x0AE4).

**Fix commit:** `e8172f1`
**Prevention:** `POLICY.md` §2 (never assign, always RMW for boot-ROM-
owned SFRs). `tools/audit_sfr_writes.py` locks the exact write pattern.
`tools/sim_smoke.sh` verifies USBCTL bit 7 is set after main-loop entry.

---

### 2026-07-22 — Flash #1 — dfuDNLOAD_IDLE after "successful" flash

**Symptom:** flashed mboxfw, `mboxflash --flash` reported all blocks OK
and printed "manifest complete", but final DFU state was
`dfuDNLOAD_IDLE` instead of the expected `dfuMANIFEST_WAIT_RESET` /
`dfuIDLE`. On unplug/replug device came up in app-DFU state
(0x0DBA:0x1001 with `bDeviceClass = 0xFE`) — boot ROM's DFU mode, not
our audio firmware. Front-panel LEDs at Rev 20 default (not touched
since our firmware never actually ran).

**Root cause:** `tools/wrap_hex.py` padded every image to the full 8192
bytes. Boot ROM's `dfuDnloadData` (TI `UsbDfu.c:966`) tracks
`dataRemain = payloadSize` and triggers `errFILE` if any byte arrives
after `dataRemain == 0`. mboxfw payload was 2890 bytes → dataRemain hit
0 partway through block 90 → we sent 5284 more bytes → errFILE →
`loadStatus = LOAD_ERROR` → boot ROM never restored the header's
`dataType` field from `EEPROM_APPCODE_UPDATING` (temporary) back to
`EEPROM_APPCODE_TYPE`. Next boot, boot ROM saw dataType == UPDATING
and refused to load the app.

**Why Rev 20 flash always worked:** Rev 20's `payloadSize` is exactly
8174, so with the 18-byte header we're at 8192 = a perfect fit for the
pad-to-8192 logic. Any other firmware payload overran silently.

**Fix:** wrap_hex now emits exactly `header + code` bytes with no
padding beyond that. Last record may be < 32 bytes (legal per DFU spec).

**Fix commit:** `74c4667`
**Prevention:** `tools/test_wrap_hex_golden.py` — golden regression
against Rev 20's stock TI-record file. Catches any wrap_hex change
that no longer round-trips.

---

### 2026-07-22 — I²C recovery driver silent failure (compounding)

**Symptom:** on both bricks above, the software recovery paths
(class-request DFU trigger, boot-time button-hold DFU trigger) did
nothing. `mboxflash --enter-dfu` accepted but device didn't drop to
DFU. Button hold on replug had no visible effect.

**Root cause:** `mboxfw/src/eeprom.c` `eeprom_read_byte` was missing
the DUMMY `I2CDATO = 0xFF` write that fires the read cycle after the
read-address load (TI `I2c.c:99-111` explicit comment: *"Need to just
put some value into I2CDAT0 address to fire off the write"*). Every
readback timed out → `eeprom_smoke_test` failed → both recovery paths
skipped the signature invalidation and just did `ljmp 0` (warm reset),
which re-ran the same broken firmware. Also, `wait_bit` never checked
the ERROR bit, so stale NACKs from prior transactions could make
subsequent waits return spuriously fast.

**Fix:** driver ported byte-for-byte from TI's `I2c.c::I2CAccess`,
including the DUMMY 0xFF write, ERROR-bit checking, and CLEAR_ALL
(0x54) between transactions.

**Fix commit:** `74c4667`
**Prevention:** `POLICY.md` §1 (reference-first, code-second).
`tools/check_sfr_citations.py` pre-commit hook requires every SFR
write in a diff to cite Rev 20 or TI reference.

---

### 2026-07-18 — SET_ADDRESS half-enumeration

**Symptom:** flashed mboxfw, device enumerated with VID 0x0DBA visible
but NO PID or bcdDevice — host got the device descriptor's first
packet, sent SET_ADDRESS, then the device never responded to the next
GET_DESCRIPTOR at the new address.

**Root cause:** USB 2.0 §9.4.6 requires the device address to change
ONLY after the SET_ADDRESS status stage completes. Original mboxfw
wrote `USBFADR = wValueL` immediately in the setup handler — so the
zero-length IN status packet went out at the NEW address, which the
host was still expecting at the OLD (0) address. Enumeration wedged.

**Fix:** defer USBFADR write to VEC_IEP0 completion via
`g_pending_address` static. Handler in usb.c stashes the pending
address, and `usb_service` latches USBFADR after the status stage
ACKs.

**Fix commit:** `5b4bf5a`
**Prevention:** `tools/verify_setup_paths.py` byte-scans the compiled
image for the `mov dptr,#0xffff` + `mov a,_g_pending_address` + `movx`
sequence. If the deferred-write pattern isn't in the .ihx, the gate
fails before flash.

---

## Search index

- "silent USB after a clean flash, LEDs on, hub shows power but no connect"
  -> flash #3 (image larger than the 6016-byte program RAM)
- "all gates passed and it still bricked" -> flash #3 (check_code_size budget
  had been raised past the hardware limit)
- "8174 bytes" -> that is FF padding; stock Rev 20 is 4159 bytes of real code

Symptom keywords → likely entries (grep-friendly):

- "device silent on USB" → flash #2 (USBCTL assignment)
- "app-DFU 0x0DBA:0x1001 after flash" → flash #1 (wrap_hex overrun)
- "dfuDNLOAD_IDLE at end of flash" → flash #1
- "button-hold DFU did nothing" → I²C driver silent failure
- "mboxflash --enter-dfu accepted but no DFU" → I²C driver silent failure
- "VID visible, no PID" → 2026-07-18 SET_ADDRESS
- "half-enumeration" → 2026-07-18 SET_ADDRESS
- "bcdDevice=0x0100" appearing without our flashing 0x0100 → boot ROM
  DFU default template (see `reference/tas1020a/ti_uac_reference/ROM/UsbDfu.c`
  DfuDeviceDesc — has DFU_BCDDEVICE = 0x0100 hardcoded). If you see
  0x0100 and think it's our firmware because MBOX_BCD_DEVICE is also
  0x0100, check `bDeviceClass` — if it's 0xFE that's boot ROM, not us.
