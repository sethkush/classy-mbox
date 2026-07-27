# safety_net EP0 + interrupt setup vs Rev 20 — byte-level diff

**Date:** 2026-07-26
**Purpose:** Rev 20 was flashed to the real Mbox on 2026-07-25 and enumerated
perfectly (`idVendor 0x0DBA`, `idProduct 0x1000`, `bcdDevice 0x0020`,
registered + matched, 37 ms). safety_net, flashed to the *same* hardware
through the *same* two-stage bootstrap, attaches D+ but never answers
GET_DESCRIPTOR (`UsbEnumerationState = 1`, no `idVendor`).

Hardware, EEPROM flash path, and `mboxflash` are therefore all proven good.
The defect is in code shared by safety_net and mboxfw. This document diffs
safety_net's EP0 and interrupt bring-up against the 61-row verified SFR-write
table in `firmware_stock/disasm/rev20_STARTUP_TRACE.md`.

**Result: no smoking gun.** Every EP0 and interrupt register safety_net writes
is at the right address with a value that is either identical to Rev 20's or
identical to TI's boot ROM — and both of those are proven to enumerate on this
part. The diff's value is negative: it eliminates the EP0/interrupt-setup
hypothesis space. Details below so this is not re-litigated.

---

## 1. Register addresses — all verified correct

Checked every SFR safety_net defines against TI `ROM/Reg_stc1.h` (the
authoritative header shipped with the reference UAC firmware):

| safety_net name | Address | Reg_stc1.h | Match |
|---|---|---|---|
| `IEPCNF0` | 0xFF68 | `IEPCNF0` 0xFF68 | ✅ |
| `IEPBBAX0` | 0xFF69 | `IEPBBAX0` 0xFF69 | ✅ |
| `IEPBSIZ0` | 0xFF6A | `IEPBSIZ0` 0xFF6A | ✅ |
| `IEPBCTX0` | 0xFF6B | `IEPDCNTX0` 0xFF6B | ✅ (name differs, address identical) |
| `OEPCNF0` | 0xFFA8 | `OEPCNF0` 0xFFA8 | ✅ |
| `OEPBBAX0` | 0xFFA9 | `OEPBBAX0` 0xFFA9 | ✅ |
| `OEPBSIZ0` | 0xFFAA | `OEPBSIZ0` 0xFFAA | ✅ |
| `OEPBCTX0` | 0xFFAB | `OEPDCNTX0` 0xFFAB | ✅ (name differs, address identical) |
| `MEMCFG` | 0xFFB0 | 0xFFB0 | ✅ |
| `GLOBCTL` | 0xFFB1 | 0xFFB1 | ✅ |
| `VECINT` | 0xFFB2 | 0xFFB2 | ✅ |
| `USBCTL` | 0xFFFC | 0xFFFC | ✅ |
| `USBIMSK` | 0xFFFD | 0xFFFD | ✅ |
| `USBFADR` | 0xFFFF | 0xFFFF | ✅ |
| `SETPACK` | 0xFF28 | 0xFF28 | ✅ |

**Retraction.** An earlier session note claimed safety_net "does not write
`IEPDCNTX0 = 0x80` / `OEPDCNTX0 = 0x00` at init." That was wrong — it writes
both, under the names `IEPBCTX0` / `OEPBCTX0`, which are the same addresses.
`safety_net/src/main.c:508-509`.

## 2. EP0 buffer geometry — identical to Rev 20

`EP_BBAX(a) = (a - 0xF800) >> 3`, so:

| | safety_net | Rev 20 (trace rows 33-34) | Match |
|---|---|---|---|
| `OEPBBAX0` | `EP_BBAX(0xFA10)` = **0x42** | 0x42 → 0xFA10 | ✅ |
| `IEPBBAX0` | `EP_BBAX(0xFA18)` = **0x43** | 0x43 → 0xFA18 | ✅ |
| `OEPBSIZ0` | `EP_BSIZE(8)` = **0x01** | 0x01 | ✅ |
| `IEPBSIZ0` | `EP_BSIZE(8)` = **0x01** | 0x01 | ✅ |
| `OEPCNF0` | **0x84** | 0x84 | ✅ |
| `IEPCNF0` | **0x84** | 0x84 | ✅ |
| `USBFADR` | **0x00** | 0x00 | ✅ |

Same buffer addresses, same sizes, same config bytes. `bMaxPacketSize0 = 8` in
`DevDesc` is consistent with `BSIZ = 1`.

## 3. The four real divergences — each individually cleared

### 3a. `IEPDCNTX0` init value: safety_net 0x80, Rev 20 0x00

Bit 7 is the NAK bit. Datasheet §2.2.7.1.1 *Initialization Stage* says to
program the endpoint "…and clearing the NACK bit for both IN endpoint 0 and
OUT endpoint 0" — which is Rev 20's 0x00.

But TI's `engUsbInit` (`ROM/UsbEng.c:620`) writes **0x80** with the comment
*"Set NAK bit for IEP, share memory is not clear by reset"*, and that is the
boot ROM, which enumerates fine as 0xFFFF:0xFFFE. So **both values are
empirically proven to work on this silicon.** Not the bug.

In any case the value is overwritten before the first IN token: `reply_desc`
writes `IEPBCTX0 = chunk` (bit 7 clear) while the SETUP bit still holds off the
bus.

### 3b. `USBIMSK`: safety_net 0xE5, Rev 20 0x9F

Datasheet bit map (p. 91) confirmed:
`7 RSTR │ 6 SUSR │ 5 RESR │ 4 SOF │ 3 PSOF │ 2 SETUP │ 1 — │ 0 STPOW`

- safety_net `0xE5` = RSTR + SUSR + RESR + SETUP + STPOW
- Rev 20 `0x9F` = RSTR + SOF + PSOF + SETUP + STPOW + reserved bit 1

**Both set RSTR (7), SETUP (2) and STPOW (0)** — everything enumeration needs.
safety_net swaps Rev 20's streaming interrupts (SOF/PSOF) for suspend/resume,
which is correct for a firmware that does not stream. safety_net's inline
comment is accurate. Not the bug.

### 3c. `GLOBCTL`: Rev 20 writes 0x06, safety_net leaves boot ROM's 0x04

Investigated as a candidate for gating the USB engine's INT0 output.
**It is not.** Datasheet p. 114 bit map:

```
7 MCUCLK │ 6 XINTEN │ 5 P1PUDIS │ 4 VREN │ 3 RESET │ 2 LPWR │ 1 P3PUDIS │ 0 CPTEN
```

Bit 1 is **P3PUDIS** — port-3 pull-up disable, a board-level concern (the Mbox
has external pull-ups on the front-panel button lines). Nothing to do with USB.
`XINTEN` is bit 6 and gates the *external* XINT pin, not the USB engine, which
ORs into INT0 unconditionally (datasheet §2.2.4). TI's `RomBoot.c:33` comment
*"12Mclk, Ext int off, LPWR on, CODEC is off"* refers to bit 6, not bit 1.

Leaving GLOBCTL at 0x04 is correct for safety_net (no codec). Not the bug.

### 3d. Y-buffer counts: Rev 20 clears `OEPDCNTY0`/`IEPDCNTY0`, safety_net does not

Rev 20 trace rows 37-38 write `OEPDCNTY0` (0xFFAF) = 0 and `IEPDCNTY0`
(0xFF6F) = 0. Neither safety_net nor TI's `engUsbInit` does.

`IEPCNF0/OEPCNF0 = 0x84` selects single-buffer (X-only) mode — TI's own comment
on that exact value is *"EP0 Enabled, ISO off, Toggle=0, XBuff only"* — so the
UBM never consults the Y counts for EP0. TI's boot ROM omits these writes and
enumerates. **Still the only unexplained delta against proven-working Rev 20**,
and it is two register writes, so it is cheap to add if a later round needs a
candidate. Ranked lowest because the mechanism is absent.

## 4. Interrupt plumbing — verified correct in the built image

Vector table read directly out of `safety_net/build/safety_net.ihx`:

```
:060000000200A00204D979
  0x0000: 02 00 A0   LJMP 0x00A0   → crt0 / main
  0x0003: 02 04 D9   LJMP 0x04D9   → _usb_isr
:03000B000205 1A     0x000B → stub (RETI)
:030013000205 1B     0x0013 → stub
:03001B000205 1C     0x001B → stub
:030023000205 1D     0x0023 → stub
:03002B000205 1E     0x002B → stub
```

The ISR is installed at 0x0003. `IT0 = 0` (level), `EX0 = 1`, `EA = 1`, all
present and in the right order relative to the `USBCTL |= 0x80` attach.

**`FEN` dependency checked and found sound.** safety_net attaches with CONN
only (0x80), leaving FEN (bit 6) clear, and relies on its `VEC_RSTR` handler to
set `USBCTL |= 0xC0`. Datasheet p. 91 on FEN is stark — *"If this bit is
cleared to 0, the UBM ignores all USB transactions"* — so this looked fatal.
It is not: §2.2.4 states that with FRSTE clear (safety_net never sets it) and
RSTR set in USBIMSK, a USB reset "is treated as interrupts to the MCU (via
INT0)", and `RSTR in register USBSTA is cleared when the MCU writes to the
interrupt vector register VECINT while in the USB reset interrupt service
routine (VECINT = 0x17)". safety_net's `VEC_RSTR` is 0x17 and it writes
`VECINT = 0`. This is exactly Rev 20's design, which works.

## 5. SETUP-handling order vs TI

`handle_setup` performs TI's `engEp0SetupDone` prologue —
`STALLClrInEp0`/`STALLClrOutEp0` (`&= ~0x08`) then
`TOGGLEInEp0Data`/`TOGGLEOutEp0Data` (`|= 0x20`) — with values matching
`ROM/hwMacro.h:27-28,46-47` exactly.

Two deltas, both benign:
- safety_net omits `EMPTYOutEp0`/`EMPTYInEp0` (`OEPDCNTX0 = 0` / `IEPDCNTX0 =
  0x80`). Every path that follows writes `IEPBCTX0` before returning, so the
  flush is redundant.
- `VECINT = 0` is written by the caller after `handle_setup()` returns, whereas
  TI clears it at the end of `engEp0SetupDone`. Same ordering relative to
  staging the IN data — arm first, then release the SETUP hold-off.

Descriptors were re-checked byte by byte: `DevDesc` is 18 bytes with
`bMaxPacketSize0 = 8`; `ConfigDesc` is 9 + 9 = 18 with `wTotalLength = 18`;
`StringMfr` is `"DigiSafe"` (8 chars → 2 + 16 = 18, `bLength` 18 correct);
`StringProduct` is `"MBOX-SAF"` (8 chars → 18, correct). `reply_desc` clamps to
the host's `wLength` from SETPACK 0xFF2E/0xFF2F (correct offsets) and chains
continuation packets from `VEC_IEP0`.

---

## 6. What this rules out, and what is left

**Ruled out** (each on datasheet text plus a proven-working reference, not
inference):

- EP0 register addresses, buffer bases, sizes, config bytes
- `USBIMSK` value — both variants enable RSTR/SETUP/STPOW
- `GLOBCTL` bit 1 — P3PUDIS, unrelated to USB
- `IEPDCNTX0` init value — both 0x00 and 0x80 proven on hardware
- FEN-via-RSTR attach strategy — datasheet-sanctioned, same as Rev 20
- Interrupt vector table, `IT0`/`EX0`/`EA` sequencing — verified in the image
- SETUP prologue values and VECINT clear ordering
- Descriptor content and lengths

**Still open**, in rank order:

1. **Something outside EP0/interrupt setup entirely.** This diff was scoped to
   EP0 and interrupts because that is where the symptom points, and it came
   back clean. The next scope should be the boot-ROM → app handoff: what state
   the ROM actually leaves the part in when it jumps to our image, versus what
   it leaves when it jumps to Rev 20. Both firmwares are loaded by the same
   ROM, but Rev 20's image is loaded via `dataType=0x01` after a *replug*,
   whereas safety_net ran as the bootstrap stage.
2. **Whether safety_net's code reaches `main` at all.** Everything above
   assumes it does. D+ asserting proves *something* wrote `USBCTL |= 0x80`, but
   that is one instruction; it does not prove the ISR ever fires. A runtime
   canary (task #56 built one) toggling a front-panel LED from inside
   `usb_isr` would settle this in one flash and is the cheapest next
   experiment.
3. **Y-buffer counts** (§3d) — the one unexplained delta vs Rev 20, but with no
   known mechanism in X-only buffer mode.

Do not spend another hardware cycle on EP0 register values. The diff is done
and it is clean.
