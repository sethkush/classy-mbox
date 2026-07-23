# Pre-flash audit — mboxflash / wrap_hex / mboxfw vs. TI ROM reference

Written 2026-07-22 after the wrap_hex.py padding bug bricked mboxfw's first
flash. Goal: catch every OTHER ungrounded assumption before we flash again.

Everything below is grounded in `reference/tas1020a/ti_uac_reference/ROM/`.
Ranked by severity. **F**=flash-blocking, **B**=boot-blocking (mboxfw
loads but hangs), **R**=runtime-blocking (mboxfw runs but misbehaves),
**C**=cosmetic.

---

## SEVERITY B — mboxfw's I²C EEPROM driver is broken

Both software-recovery paths (`handle_digi_enter_dfu` + `check_boot_dfu_button`)
call `eeprom_smoke_test()` → `eeprom_invalidate_signature()`. That code
was written from disassembly guesses, never runtime-tested, and diverges
from TI's reference driver in ways that matter.

### #1 — `eeprom_read_byte` never triggers the read cycle
- **Reference:** `I2c.c:99-111`. After setting `I2CADR = I2C_READ_ADDR(...)`
  for the read-phase repeated-START, TI writes `I2CDATO = 0xFF` as a
  DUMMY byte — this is what actually fires the read. Comment at line
  91-93 is explicit: *"Need to just put some value into I2CDAT0 address
  to fire off the write."*
- **Ours:** `mboxfw/src/eeprom.c:98-100` sets `I2C_SADDR = EEPROM_ADDR_READ`,
  then `I2C_CTRL |= I2C_CTRL_STARTRX (0x02)`, then waits for
  `I2C_CTRL_RXREADY`. **No `I2C_TX = 0xFF` write.** The read never
  starts → `wait_bit(RXREADY)` times out.
- **Failure:** `eeprom_smoke_test()` will fail on the read-back step
  and return 0. Both button-hold and class-request DFU triggers then
  skip signature invalidation and just `ljmp 0` — same state as before,
  no recovery. **This is why holding source-1 tonight did nothing.**
- Fix: write `I2C_TX = 0xFF` between the read-mode addr and the
  wait_bit; also check the ERROR bit (see #2).

### #2 — Missing ERROR-bit polling and wrong CLEAR_ALL pattern
- **Reference:** `I2c.h:38` `ERROR = 0x20`; `I2c.h:42` `CLEAR_ALL = 0x54`.
  TI's `WaitOnI2C` (`I2c.c:33`) exits on **either** the wanted status
  bit OR the ERROR bit, and returns 0 if ERROR is set. TI clears state
  between transactions via `I2CSTA &= CLEAR_ALL` (preserves only bits
  0x40/0x10/0x04 — clears STOP flags, ERROR, done flags).
- **Ours:** `mboxfw/src/eeprom.c:51-58` `wait_bit` only checks the wanted
  bit — never ERROR. Between transactions we do `I2C_CTRL &= 0xFC`
  (clears only bits 0/1), leaving stale ERROR / DONE flags that can
  make the NEXT wait return spuriously fast.
- **Failure:** A previous NACK's ERROR sticks around; subsequent
  transactions think they succeeded when the bus is broken. Silent
  data corruption during signature invalidation → EEPROM header might
  end up with valid signature but corrupt other bytes → boot into
  half-broken state.
- Fix: replace `& 0xFC` with `& 0x54`, add ERROR check to `wait_bit`.

### #3 — Smoke test doesn't cover the real risk
- `eeprom_smoke_test()` writes-and-reads back a scratch byte to detect
  a broken I²C driver. But if the driver is broken in a way where
  READ always returns 0xFF (which it will, given #1), the smoke test
  passes (0xA5 written, 0xFF read back ≠ 0xA5 → returns 0 → we skip
  invalidation). So actually the smoke test SAVES us here — good.
  But if #1 gets fixed and #2 doesn't, smoke test could pass while
  invalidation silently corrupts other bytes. Fix #1 and #2 together.

---

## SEVERITY B — USB engine handoff conventions

### #4 — `IEPDCNTX0` (aka our `IEPBCTX0`) should init with NAK set
- **Reference:** `UsbEng.c:621` `IEPDCNTX0 = 0x80`. Bit 7 is the NAK
  flag; on boot, TI sets NAK so the first IN token gets NAK'd rather
  than sending a garbage packet.
- **Ours:** `usb.c:363` `IEPBCTX0 = 0` — sends a zero-length IN
  immediately when the first IN token arrives. Some hosts treat a
  spurious ZLP as a protocol violation.
- **Failure:** Might confuse strict hosts (Windows more than macOS)
  during early enumeration.
- Fix: `IEPBCTX0 = 0x80` in `usb_init`.

### #5 — `USBCTL` never written — no explicit bus attach
- **Reference:** `UsbEng.c:647` `USBCTL = 0xC0` — sets CONN (bit 7 =
  enable pull-up, connect to bus) + FEN (bit 6 = function address
  enable). Also `Utils.c:37-39` shows USBCTL bit 0 = SDW confirm.
- **Ours:** `usb.c:usb_init` and `hw_init.c` never touch `USBCTL`.
  Boot ROM's `engUsbInit` set it before handing off, so the bit is
  probably still set — but we shouldn't rely on inheriting state.
- **Failure:** If boot ROM cleared USBCTL before our warm-reset (e.g.
  after our failed flash), our firmware wouldn't reconnect to bus.
  Would look like "mboxfw runs but no USB".
- Fix: write `USBCTL = 0xC0` at end of `usb_init`.

### #6 — `USBIMSK` value differs from TI (0xFF vs 0xE5)
- **Reference:** `UsbEng.c:640` `USBIMSK = 0xE5` — enables Reset, Resume,
  Suspend, SETUP, STPOW only.
- **Ours:** `usb.c:376` `USBIMSK = 0xFF` — enables everything.
- **Failure:** Non-fatal — we just get more ISR firings. But we ARE
  enabling SOF interrupts (bit 0x04) that we don't handle usefully;
  `isr_int0` just increments a counter. Extra CPU load per SOF (1kHz).
- Fix: match TI's 0xE5 unless we have a reason to differ.

---

## SEVERITY F — mboxflash manifest polling gap

### #7 — Manifest exit condition misses valid states
- **Reference:** `UsbDfu.c:436-460`. After zero-length DNLOAD:
  `dfuIDLE → dfuMANIFEST_SYNC`. First GETSTATUS: → `dfuMANIFEST` (returns
  bwPollTimeout). Later SOF ISR (line 690-695): `dfuMANIFEST →
  dfuMANIFEST_SYNC` (if ManTolBit) or `dfuMANIFEST_WAIT_RESET` (else).
  Later GETSTATUS: `dfuMANIFEST_SYNC → dfuIDLE` when MnfPhase completed
  AND ManTolBit set. TI ROM has ManTolBit set (`UsbDfu.c:113`).
- **Ours:** `main.m:397-402` polls waiting for `dfuMANIFEST` OR `dfuIDLE`
  and gives up after 100 × 20ms = 2 sec. If the polls happen to catch
  `dfuMANIFEST_SYNC` between transitions, we don't recognize it and
  just keep polling — usually fine but if manifest takes >2 sec we
  timeout with a misleading "final state" print.
- **Failure:** False negatives in flash log; user sees "final state:
  dfuMANIFEST_SYNC" and panics. Doesn't cause actual data corruption.
- Fix: add `dfuMANIFEST_SYNC`, `dfuMANIFEST_WAIT_RESET` to exit list,
  and honor the returned `bwPollTimeout` between polls (per DFU spec
  §5.1.1.2 — host MUST honor bwPollTimeout).

### #8 — Manifest polling uses hardcoded delay, not `bwPollTimeout`
- **Reference:** DFU spec §5.1.1.2 mandates host wait `bwPollTimeout`
  ms between GETSTATUS during any *_SYNC state.
- **Ours:** `main.m:401` `usleep(20 * 1000)` hardcoded. Data-phase
  polling (`main.m:383-386`) correctly reads `bwPollTimeout` — just
  the manifest-phase polling doesn't.
- **Failure:** With very short bwPollTimeout, we poll too slowly and
  hit the 100-iteration cap. With very long, we poll too fast and
  waste USB traffic (but TAS1020A ROM likely returns 0, so we default
  to 5ms).
- Fix: mirror the data-phase pattern in the manifest loop.

---

## SEVERITY R — mboxfw runtime posture

### #9 — INT0 ISR is a stub while USBIMSK enables interrupt sources
- **Reference:** `UsbEng.c:33-136`. `engEx0()` INT0 handler is the
  primary USB event handler — reads VECINT, dispatches, clears.
- **Ours:** `isr.c:33-36` `isr_int0` just does `g_int0_ticks++`.
  `usb_service()` polls VECINT from main loop.
- Concern: is polling fast enough? On a class-compliant enumeration
  Logic can send ~100 SETUP+data-phase transactions/sec. Our main
  loop also calls `buttons_poll` — probably fast enough.
- **Failure:** Occasional missed setups if main loop lags → timeout
  retries on host. Not a brick.
- Note: not necessarily a bug, but worth noting that we're diverging
  from TI's interrupt-driven model.

### #10 — `check_boot_dfu_button` runs BEFORE hw_init, reads P3 raw
- **Ours:** `main.c:check_boot_dfu_button` reads P3 immediately at
  main entry. On 8051, P3 defaults to 0xFF after hardware reset so
  buttons NOT held reads correctly (bit 3 = 1). Should work.
- Concern: SDCC's crt0 (`__sdcc_gsinit_startup`) runs BEFORE `main()`
  and could touch P3 (unlikely — crt0 only initializes globals). If
  it did, our check might be racy.
- **Failure:** Possible false trigger. Very unlikely.
- Note: consider adding a delay-then-read for pull-up stabilization,
  or move the check into an SDCC `_sdcc_external_startup` hook so it
  runs even before crt0.

### #11 — `ljmp 0` doesn't force boot ROM re-load
- **Reference:** `Utils.c:UtilResetCPU` — `MEMCFG |= SDW_BIT` (turn
  OFF shadow ROM) then `ljmp 0` → user firmware reset vector.
  `UtilResetBootCPU` — `MEMCFG &= ~SDW_BIT` (turn ON shadow ROM) then
  `ljmp 0x8000` → boot ROM reset vector.
- **Ours:** `main.c` and `usb.c` do bare `__asm__("ljmp 0")` — jumps to
  our own reset vector (SDW off, from initial UtilResetCPU). Does
  NOT drop into boot ROM. Only takes user through main() again.
- **Effect:** After successful EEPROM signature invalidation, this
  re-runs mboxfw. mboxfw re-runs check_boot_dfu_button. If button
  still held, tries invalidate again (no-op since already 0). Loops
  until user releases OR unplugs. Only unplug forces boot ROM to
  re-read EEPROM header, see invalid sig, drop to bulletproof DFU.
- **Failure:** Documentation gap — user might expect immediate DFU
  drop; instead they need to unplug/replug. Not a functional bug.
- Fix: make the docs clear that button-hold + unplug/replug is the
  full sequence.

---

## SEVERITY C — cosmetic / verified-inert

### #12 — regs.h C-port register names don't match TI Reg_stc1.h
- **Reference:** TI defines `CPTVSLH/CPTVSLL/CPTDATH/CPTDATL/CPTADR/
  CPTSTA/CPTCTL` at 0xFFD7-0xFFDC. Nothing at 0xFFD4-0xFFD6 or
  0xFFDD-0xFFDF.
- **Ours:** `regs.h` `CPTCTL=0xFFD4, CPTBRRX=0xFFD5, CPTBRTX=0xFFD6,
  CPTCNF1..4=0xFFDC..DF`. Copied from Rev 20 disassembly (fcn.0x08CB).
- **Effect:** Rev 20 writes these addresses and works, so the hardware
  DOES have registers there — TI's public header just omits them.
  Not a bug, but "grounded in Rev 20 disasm not TI reference" — same
  class of assumption we're trying to eliminate. Verified working
  because Rev 20 does the same thing.

### #13 — Boot-ROM fallback `lcall 0x2F00` is Rev-20-grounded speculation
- **Ours:** `usb.c:handle_setup default: __asm__("lcall #0x2f00")`.
  Rev 20 does the same thing (per fork's earlier report on the 0x0118
  shim), so it works empirically.
- **Reference:** TI's `PublicRom.a51` might have the exact entry
  point definitions — worth checking. Actual `Tas1020` binary in
  reference/ROM/ could be inspected to confirm 0x2F00 is really the
  standard-request handler entry.
- **Failure risk:** Unknown. Rev 20 uses it and works.
- Note: harmless if boot ROM ignores unknown callers; potential SP
  corruption if boot ROM doesn't expect to be called from firmware
  context. Low probability given Rev 20 does it.

### #14 — wrap_hex.py's `usbAttribute = 0x04` says SELF_POWER
- **Reference:** `eeprom.h:36-38` — `EEPROM_SELF_POWER = 0x04`,
  `EEPROM_BUS_POWER = 0x08`. The Mbox 1 is bus-powered.
- **Ours:** `wrap_hex.py:107` `0x04, # usbAttribute` — but Rev 20's
  header also has 0x04, and Rev 20 works. Maybe this flag only affects
  the boot ROM's early-connect timing (`Eeprom.c:40-46` sets up USB
  engine early if BUS_POWER bit set) and either value works for our
  case.
- Note: mirror Rev 20 exactly to avoid regressing.

### #15 — Program RAM ceiling is 0x1800 (6 KB), not 0x1F00
- **Reference:** `Mmap.h:44` `PROG_RAM_ADDR_END = 0x1800`.
- **Ours:** `mboxfw/Makefile` `--code-size 0x1F00` (7936 B). mboxfw
  is 2890 B so it fits, but we'd silently over-commit if the code
  grows past 6 KB.
- **Failure:** Only matters if code grows. Not a current issue.

### #16 — `--code-size 0x1F00` also assumes 8 KB code space
- Same as #15. mboxfw is well under, non-issue for now.

---

## Answered questions

- **DFU spec bwPollTimeout compliance:** partial. Data-phase polling
  in `main.m:383-386` honors it correctly. Manifest-phase polling
  (line 401) uses hardcoded 20ms. See #8.
- **VEC_RSTR handler:** correctly present (`usb.c:421`), calls
  `usb_init()` which re-does EP0/USBFADR/EP-disable — matches Rev 20's
  0x0F43 shape.
- **Reset vector at 0x0000:** SDCC's crt0 emits `ljmp
  __sdcc_gsinit_startup` at 0x0000, which is correct 8051 boot flow.
- **EP0 buffer address hardcoding (0xFA10/0xFA18):** Rev 20 uses the
  same fixed offsets (`INPACK_OFFSET = 0x42 * 8 = 0xFA10` derived
  from XDATA_BUFFER_START + ROM_CONFIG_SIZE + padding, coincidence
  or by-design). Verified working; not a bug.

---

## Pre-flash go/no-go

**No-go until fixed** (any could brick or block recovery):
- **#1 + #2**: eeprom.c I²C read is broken → both software recovery
  paths are dead → SDA short is the only escape hatch. FIX BEFORE
  FLASHING.

**Fix strongly recommended before flash** (soft failures, not bricks):
- **#4**: IEPBCTX0 NAK init.
- **#5**: explicit USBCTL = 0xC0 in usb_init.

**Can defer to post-flash** (cosmetic or non-blocking):
- Everything else. #7/#8 are flash-log confusion only. #9-#16 are
  runtime posture or documentation-grade.

Ship blocker #1 + #2 requires ~30 lines of eeprom.c change and a
runtime test (impossible without hardware). At minimum, port TI's
`I2c.c::I2CAccess` byte-for-byte using our register aliases, and
delete the disassembly-derived version.
