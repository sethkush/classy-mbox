# Brick log

Every time the Mbox 1 has been soft-bricked (unable to enumerate,
required physical recovery), the incident goes here. Symptom first
so future-me can grep for it, then root cause, then the fix commit.

Physical recovery for all entries below has been the same: SDA-short
recovery on the Atmel 24C64A EEPROM (pin 4 GND ↔ pin 5 SDA during
plug-in) → device enters bulletproof DFU at 0xFFFF:0xFFFE → flash a
known-good image.

## Entries (newest first)

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
