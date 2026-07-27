# Rev 22 — startup execution trace, reset vector to main loop

Instruction-level trace of Digidesign Mbox 1 firmware **Rev 22** (TAS1020B, 8051 core)
from the reset vector at `0x0000` to the point it enters its steady-state polling loop.

**Source image.** `firmware_stock/rev22_firmware_code.bin`, 8174 bytes (0x1FEE).
Load address 0x0000, so CPU address == file offset throughout.

**How this was produced.** Every claim below was checked against the instruction bytes in
`rev22_ghidra.txt` and, where a register's meaning is load-bearing, against the TAS1020B
datasheet (SLES025B) or `reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h`. The prose in
`rev22_ANNOTATED.md` was **not** treated as authoritative — an audit found roughly 20% of
its cited claims wrong, and it was written against an earlier, lower-quality listing.
Confidence is marked on every non-obvious claim.

**Entry state** (given, verified previously from the TI boot-ROM sources): the boot ROM
hands off via `LJMP 0` leaving `USBCTL=0x00`, `USBIMSK=0x00`, `USBFADR=0x00`,
`MEMCFG.SDW=1` (code fetches routed to the RAM copy), `GLOBCTL=0x04` (CPTEN clear, LPWR
set, 12 MHz), `IE=0x00` with `EA=0`. DMA and codec-port state are **not** reset by the boot
ROM and are undefined on entry.

---

## 1. Execution walk

### Phase A — reset vector

| Addr | Bytes | Instruction | Effect |
|---|---|---|---|
| `0x0000` | `02 09 2a` | `LJMP 0x092A` | → `keil_c51_startup` |

Confidence: **certain**.

### Phase B — Keil C51 startup, `keil_c51_startup` @ `0x092A`

| Addr | Instruction | Effect |
|---|---|---|
| `0x092A` | `MOV R0,#0x7F` | top of IRAM |
| `0x092C` | `CLR A` | |
| `0x092D` | `MOV @R0,A` | clear IRAM byte |
| `0x092E` | `DJNZ R0,0x092D` | loop down to `0x01` |
| `0x0930` | `MOV SP,#0x32` | stack base; pushes begin at `0x33` |
| `0x0933` | `LJMP 0x0971` | → init-table interpreter |

The clear loop runs `R0 = 0x7F … 0x01`. `DJNZ` tests **after** decrementing, so IRAM
`0x00` is **not** cleared by this loop (it is bank-0 R0, in use as the pointer).
Confidence: **certain**.

`0x7F` is therefore not a variable — it is the loop's starting address. It appears nowhere
in the image as a direct-address operand. Confidence: **certain**.

### Phase C — C51 init-table interpreter, `keil_c_init_interpreter` @ `0x0939`

`0x0971` loads `DPTR = 0x0FBA` (the init table) and enters the dispatch loop at `0x0974`.
Each record starts with a command byte; `A = cmd & 0x3F` is the count and `cmd & 0xC0`
selects the mode. `0x0978 JZ 0x0936` exits on a zero command byte, and `0x0936` is
`LJMP 0x0A3F` — the jump into `main`.

For every record in this image `cmd & 0xC0 == 0`, so execution takes the `0x0939` path,
which writes to **IDATA** via `MOV @R0,A` (`0x0942`). The `MOVX @R0,A` alternative at
`0x0945` and the bit-manipulation path at `0x094B` are present in the interpreter but
**never taken by this table**. Confidence: **certain** (all 13 records decoded below).

### Phase D — `main` prologue @ `0x0A3F`

| Addr | Instruction | Effect |
|---|---|---|
| `0x0A3F` | `CLR A` / `MOV 0x27,A` | IRAM `0x27` = 0 |
| `0x0A42` | `MOV A,#0xFF` | |
| `0x0A44` | `MOV 0x28,A` | delay counter high = 0xFF |
| `0x0A46` | `MOV 0x29,A` | delay counter low  = 0xFF |
| `0x0A48` | `MOV 0x2A,#0x00` | write-once, never read (see §3) |
| `0x0A4B` | `MOV 0x2B,#0x10` | write-once, never read (see §3) |
| `0x0A4E` | `CLR EA` (bit `0xAF`) | global interrupts off |
| `0x0A50` | `MOV DPTR,#0xFFFD` / `CLR A` / `MOVX` | **USBIMSK = 0x00** — mask all USB interrupts |
| `0x0A55` | `CLR 0x22` (bit) | clears bit `0x22` = byte `0x24` bit 2 |
| `0x0A57` | `LCALL 0x07EC` | → `hw_clock_codec_init` (Phase E) |
| `0x0A5A` | `LCALL 0x0891` | → `usb_ep_dma_init` (Phase F) |

Note the ordering: interrupts are masked and USB interrupts disabled **before** any
hardware is touched. Confidence: **certain**.

### Phase E — `hw_clock_codec_init` @ `0x07EC`

Runs in this order:

1. `0x07ED` IRAM `0x2E`, `0x2F` = 0 (loop counters used at the end of this function).
2. `0x07F1` **`USBCTL = 0x00`** — explicit USB disconnect. On the expected cold-boot path
   the boot ROM already left it 0, so this is defensive. Confidence: **certain**.
3. `0x07F5` **`MEMCFG = 0x01`** (`A` was 0, `INC A` → 1). Sets SDW. The boot ROM already
   set SDW, so this is idempotent on the normal path. Confidence: **certain**.
4. `0x07FB` **`P1 = 0x00`**, `0x07FD` **`P3 = 0xFF`** — P1 driven low, P3 released high
   for the front-panel switch inputs (open-drain with internal pull-ups).
5. Timers: `0x0800` **`TH0 = 0xCE`**, `0x0803` `TL0 = 0`, `0x0805` `TH1 = 0`,
   `0x0807` `TL1 = 0`, `0x0809` **`TMOD = 0x11`** (both timers mode 1, 16-bit),
   `0x080C` `TCON = 0`.
6. Interrupt enables, bit by bit — `IE` is bit-addressable at `0xA8`:
   `0x080E CLR 0xAF` → `EA=0`; `0x0810 CLR 0xAC` → `ES=0`; `0x0812 CLR 0xAA` → `EX1=0`;
   `0x0814 SETB 0xA9` → **`ET0=1`** (Timer 0); `0x0816 CLR 0xAB` → `ET1=0`;
   `0x0818 SETB 0xA8` → **`EX0=1`** (INT0 — the USB interrupt).
   `0x081A MOV IP,A` with `A=0` → **`IP = 0x00`**, all priorities low.
   Confidence: **certain**.
7. `0x081C INC DPTR` (DPTR was `0xFFB0` from step 3) → `0xFFB1`; `0x081D A=0x06`;
   `0x081F MOVX` → **`GLOBCTL = 0x06`**.
8. Codec port block: `CPTCNF1=0x0D`, `CPTCNF2=0xE5`, `CPTCNF3=0xAC`, `CPTCNF4=0x03`,
   `CPTSTA=0x50`, `CPTRXCNF2=0x25`, `CPTRXCNF3=0xAC`.
9. `0x084A` `DPTR=0xFFD4`, `A=0x03`, `0x084F LCALL 0x0EC7`. **`0x0EC7` is a single
   `MOVX @DPTR,A` that falls through into `0x0EC8`** — so this one call both writes
   `CPTRXCNF4 = 0x03` and then programs both frequency synthesizers. Confidence:
   **certain** (no `RET` between `0x0EC7` and `0x0EC8`).
10. `0x0852 LCALL 0x0EF3`. **`0x0EF3` is a single `INC DPTR` that falls through into
    `0x0EF4`.** DPTR on entry is `0xFFE1` (left by `acg2frq0_load_and_acgctl`), so it
    becomes `0xFFE2`, and `0x0EF4` writes **`ACGDCTL = 0x10`** and **`ACG2DCTL = 0x10`**.
    Confidence: **certain**.
11. `0x0855` **`GLOBCTL |= 0x01`** (read-modify-write) — sets CPTEN, enabling the codec
    port *after* all seven CPT* registers are programmed. Confidence: **certain**.
12. `0x085C` IRAM `0x08` = 3 (clock mode), `0x0860` IRAM `0x22` = 0 (shift-register image),
    `0x0862 SETB 0x1E`, `0x0864 LCALL 0x0EFC` (`shiftreg_out8_p1hi` — clocks the 8-bit
    image out on P1).
13. `0x0867–0x087A` a busy-wait: increments the 16-bit pair IRAM `0x2E:0x2F` until it
    reaches `0x0FFF`.
14. `0x087C` IRAM `0x22` = `0xFF`, then `CLR 0x10`, `CLR 0x13`, `CLR 0x1E` (bits of `0x22`),
    `0x0885 LCALL 0x0EFC` — second shift-register flush with the new pattern.
15. `0x0888` IRAM `0x25` = 0, IRAM `0x23` = 0, `0x088D LCALL 0x0E56`
    (`shiftreg_out16_*` — clocks the 16-bit control latch).

### Phase F — `usb_ep_dma_init` @ `0x0891`

Straight-line, no branches. Programs EP0 (both directions), the two streaming endpoints,
the two DMA channels, then the interrupt mask and function address. Full ordered list in §2,
rows 12–37. Ends by clearing IRAM `0x0A`, `0x0E`, `0x08`, `0x09`, `0x0B`, setting
`0x0C = 0xFE`, and clearing `0x0A` again (`0x0918–0x0927`).

Two details worth calling out:

- **`OEPCNF2 = 0xC5` and `IEPCNF1 = 0xC5`** (`0x08E4`, `0x08E8`) — the streaming endpoints
  are configured and **enabled at init time**, not deferred to `SET_INTERFACE`.
  Confidence: **certain** (bit 7 of `*EPCNF` is the enable per datasheet §6.5).
- **`USBIMSK = 0x9F`** at `0x0912` — see §4 for the bit decode; this is what arms the SOF
  handler.

### Phase G — attach, back in `main`

| Addr | Instruction | Effect |
|---|---|---|
| `0x0A5D–0x0A70` | 16-bit `SUBB`/`DEC` loop on IRAM `0x28:0x29` | settle delay, `0xFFFF` iterations |
| `0x0A72` | `SETB 0x8C` | `TCON.4 = TR0` — **start Timer 0** |
| `0x0A74` | `SETB 0xAF` | **`EA = 1`** — global interrupts on |
| `0x0A76–0x0A7C` | `MOVX A,@DPTR` / `ORL A,#0x80` / `MOVX @DPTR,A` on `0xFFFC` | **`USBCTL \|= 0x80`** — assert CONT, attach to the bus |

Interrupts are enabled **two instructions before** the attach, so the device can service a
host reset the moment D+ is pulled up. Confidence: **certain**.

### Phase H — main loop @ `0x0A7D`

```
0x0A7D  JB   0x20, 0x0A89     ; bit 0x20 (byte 0x24 bit 0) set -> front-panel path
0x0A80  MOV  A, 0x0A          ; pending_action
0x0A82  JZ   0x0A7D           ; nothing pending -> spin
0x0A84  LCALL 0x02F3          ; usb_deferred_action_dispatch
0x0A87  SJMP 0x0A7D
```

Front-panel path (`0x0A89`): `LCALL 0x0F31` (P3 switch scan) → if `R7.0` set,
`LCALL 0x0EFC` + `LCALL 0x0E56` (flush both shift registers) → then two edge-triggered
blocks post `pending_action = 0x0B` or `0x0C` guarded by IRAM `0x27`, each dispatching via
`LCALL 0x02F3` → `CLR 0x20` → back to `0x0A7D`.

So the loop polls exactly two things, in this order: **bit `0x20`** (a flag set by the
Timer 0 ISR — see below), then **`pending_action`** (IRAM `0x0A`). All real work is done in
interrupt handlers or in `usb_deferred_action_dispatch`. Confidence: **certain** for the
structure; the claim that bit `0x20` is set by the Timer 0 ISR is **likely** — Timer 0 is
enabled and running and `0x100B → 0x1016` is its vector, but I did not trace the ISR body.

---

## 2. Ordered SFR writes before the main loop

This is the table a reimplementation must reproduce. Order is true execution order.
`RMW` marks a read-modify-write rather than a plain store.

| # | Addr | SFR | Value | Meaning |
|---|---|---|---|---|
| 1 | `0x0A54` | `USBIMSK` | `0x00` | mask every USB interrupt source before touching hardware |
| 2 | `0x07F4` | `USBCTL` | `0x00` | disconnect (CONT=0); defensive, boot ROM already left it 0 |
| 3 | `0x07F9` | `MEMCFG` | `0x01` | SDW — code fetch from RAM copy; idempotent here |
| 4 | `0x07FB` | `P1` | `0x00` | all P1 outputs low |
| 5 | `0x07FD` | `P3` | `0xFF` | P3 released high (switch inputs, internal pull-ups) |
| 6 | `0x0800` | `TH0` | `0xCE` | Timer 0 reload high |
| 7 | `0x0803` | `TL0` | `0x00` | Timer 0 low |
| 8 | `0x0805` | `TH1` | `0x00` | Timer 1 high |
| 9 | `0x0807` | `TL1` | `0x00` | Timer 1 low |
| 10 | `0x0809` | `TMOD` | `0x11` | both timers mode 1 (16-bit) |
| 11 | `0x080C` | `TCON` | `0x00` | timers stopped, all flags clear |
| 12 | `0x081A` | `IP` | `0x00` | all interrupt priorities low |
| 13 | `0x081F` | `GLOBCTL` | `0x06` | CPTEN still clear at this point |
| 14 | `0x0825` | `CPTCNF1` | `0x0D` | codec port config |
| 15 | `0x082B` | `CPTCNF2` | `0xE5` | codec port config |
| 16 | `0x0831` | `CPTCNF3` | `0xAC` | codec port config |
| 17 | `0x0837` | `CPTCNF4` | `0x03` | codec port config |
| 18 | `0x083D` | `CPTSTA` | `0x50` | codec port status/control |
| 19 | `0x0843` | `CPTRXCNF2` | `0x25` | codec receive config |
| 20 | `0x0849` | `CPTRXCNF3` | `0xAC` | codec receive config |
| 21 | `0x0EC7` | `CPTRXCNF4` | `0x03` | written by the fall-through entry (called from `0x084F`) |
| 22 | `0x0ECD` | `ACGFRQ1` | `0xA8` | synth 1 frequency, middle byte |
| 23 | `0x0ED3` | `ACGFRQ2` | `0x61` | synth 1 frequency, high byte |
| 24 | `0x0ED9` | `ACGFRQ0` | `0x0F` | synth 1 low byte — **writing FRQ0 latches all three** |
| 25 | `0x0EDF` | `ACG2FRQ1` | `0xA8` | synth 2, middle byte |
| 26 | `0x0EE5` | `ACG2FRQ2` | `0x61` | synth 2, high byte |
| 27 | `0x0EEB` | `ACG2FRQ0` | `0x0F` | synth 2 low byte — latches synth 2 |
| 28 | `0x0EF1` | `ACGCTL` | `0x06` | ACG control |
| 29 | `0x0EF6` | `ACGDCTL` | `0x10` | synth 1 divider control (via `0x0EF3` fall-through) |
| 30 | `0x0EFA` | `ACG2DCTL` | `0x10` | synth 2 divider control |
| 31 | `0x085B` | `GLOBCTL` | `\|= 0x01` **RMW** | **CPTEN set — codec port enabled, after all CPT* config** |
| 32 | `0x0896` | `OEPBBAX0` | `0x42` | EP0 OUT buffer base → `0xFA10` |
| 33 | `0x089B` | `IEPBBAX0` | `0x43` | EP0 IN buffer base → `0xFA18` |
| 34 | `0x08A0` | `OEPDCNTX0` | `0x00` | EP0 OUT X data count |
| 35 | `0x08A4` | `IEPDCNTX0` | `0x00` | EP0 IN X data count |
| 36 | `0x08A8` | `OEPDCNTY0` | `0x00` | EP0 OUT Y data count |
| 37 | `0x08AC` | `IEPDCNTY0` | `0x00` | EP0 IN Y data count |
| 38 | `0x08B1` | `OEPBSIZ0` | `0x01` | EP0 OUT buffer size |
| 39 | `0x08B5` | `IEPBSIZ0` | `0x01` | EP0 IN buffer size |
| 40 | `0x08BB` | `OEPCNF0` | `0x84` | EP0 OUT enable + interrupt-on-transaction |
| 41 | `0x08BF` | `IEPCNF0` | `0x84` | EP0 IN enable + interrupt-on-transaction |
| 42 | `0x08C5` | `OEPBBAX2` | `0x44` | EP2 OUT (playback) buffer base |
| 43 | `0x08CB` | `IEPBBAX1` | `0x94` | EP1 IN (capture) buffer base |
| 44 | `0x08D1` | `OEPBSIZ2` | `0x50` | EP2 OUT buffer size (80 bytes) |
| 45 | `0x08D5` | `IEPBSIZ1` | `0x50` | EP1 IN buffer size (80 bytes) |
| 46 | `0x08DA` | `OEPDCNTX2` | `0x00` | EP2 OUT data count |
| 47 | `0x08DE` | `IEPDCNTX1` | `0x00` | EP1 IN data count |
| 48 | `0x08E4` | `OEPCNF2` | `0xC5` | **EP2 OUT enabled at init** |
| 49 | `0x08E8` | `IEPCNF1` | `0xC5` | **EP1 IN enabled at init** |
| 50 | `0x08EE` | `DMATSL0` | `0x03` | DMA ch0 transfer size low |
| 51 | `0x08F4` | `DMATSH0` | `0x80` | DMA ch0 transfer size high |
| 52 | `0x08FA` | `DMATSL1` | `0x03` | DMA ch1 transfer size low |
| 53 | `0x0900` | `DMATSH1` | `0x80` | DMA ch1 transfer size high |
| 54 | `0x0906` | `DMACTL0` | `0x02` | DMA ch0 control |
| 55 | `0x090C` | `DMACTL1` | `0x09` | DMA ch1 control |
| 56 | `0x0912` | `USBIMSK` | `0x9F` | **unmask RSTR+SOF+PSOF+SETUP+STPOW** (see §4) |
| 57 | `0x0917` | `USBFADR` | `0x00` | device address 0 until `SET_ADDRESS` |
| 58 | `0x0A7C` | `USBCTL` | `\|= 0x80` **RMW** | **CONT — attach to bus. FEN NOT set here.** |

Rows 1–2 look out of numerical order because `main`'s prologue (row 1) runs before it calls
`hw_clock_codec_init`. Rows 21–30 sit inside `hw_clock_codec_init`'s call chain.

**ACG constant check.** The datasheet's worked example (SLES025B §2.2.6.1) for a 24.576 MHz
MCLKO gives `ACGFRQ2 = 0x61`, `ACGFRQ1 = 0xA8`, `ACGFRQ0 = 0x00`. Rev 22 writes `0x61`,
`0xA8`, `0x0F`. Taking the 24-bit value `0x61A80F = 6400015` and `N = value / 2^18`
gives `N ≈ 24.414082`, so `600/N ≈ 24.5758 MHz` — the datasheet example to within about
200 Hz. The arithmetic here is mine, not the datasheet's; confidence **likely** on the exact
figure, **certain** on the top two bytes matching the published 24.576 MHz example.
The datasheet also states that writing `ACGFRQ0` is what loads the synthesizer with all
three bytes, which is consistent with FRQ0 being written last in each group.

---

## 3. IRAM state initialised before the main loop

**Startup clear** (`0x092A–0x092E`): IRAM `0x7F` down to `0x01` set to zero. `0x00` is not
covered (it is the pointer register). Stack pointer set to `0x32`, so pushes start at `0x33`.

**Init table at `0x0FBA`**, decoded record by record. Every record is `cmd=0x01`
(count 1, IDATA mode), so each writes a single byte:

| Table offset | Bytes | IRAM | Value |
|---|---|---|---|
| `0x0FBA` | `01 22 00` | `0x22` | `0x00` |
| `0x0FBD` | `01 20 00` | `0x20` | `0x00` |
| `0x0FC0` | `01 25 00` | `0x25` | `0x00` |
| `0x0FC3` | `01 23 00` | `0x23` | `0x00` |
| `0x0FC6` | `01 24 00` | `0x24` | `0x00` |
| `0x0FC9` | `01 21 00` | `0x21` | `0x00` |
| `0x0FCC` | `01 09 00` | `0x09` | `0x00` |
| `0x0FCF` | `01 0C 00` | `0x0C` | `0x00` |
| `0x0FD2` | `01 0B 00` | `0x0B` | `0x00` |
| `0x0FD5` | `01 0E 00` | `0x0E` | `0x00` |
| `0x0FD8` | `01 0A 00` | `0x0A` | `0x00` |
| `0x0FDB` | `01 0D 00` | `0x0D` | `0x00` |
| `0x0FDE` | `01 08 03` | `0x08` | **`0x03`** |
| `0x0FE1` | `00` | — | terminator → `LJMP 0x0A3F` |

The only non-zero initial value in the whole image is **IRAM `0x08` = 3** (`clock_mode_id`).
Everything else is zeroed. Confidence: **certain**.

**Written afterwards during init:**

| IRAM | Value | Where |
|---|---|---|
| `0x27` | `0x00` | `0x0A40` |
| `0x28`, `0x29` | `0xFF`, `0xFF` | `0x0A44`, `0x0A46` — settle-loop counter, counted down to 0 |
| `0x2A` | `0x00` | `0x0A48` — **never read anywhere in the image** |
| `0x2B` | `0x10` | `0x0A4B` — **never read anywhere in the image** |
| `0x2E`, `0x2F` | `0x00` then counted to `0x0FFF` | `0x07ED`, `0x0867–0x087A` |
| `0x08` | `0x03` | `0x085C` (re-asserting the init-table value) |
| `0x22` | `0x00`, then `0xFF` with bits `0x10`/`0x13`/`0x1E` cleared | `0x0860`, `0x087C–0x0883` |
| `0x25`, `0x23` | `0x00` | `0x0889`, `0x088B` |
| `0x0A`,`0x0E`,`0x08`,`0x09`,`0x0B` | cleared | `0x0918–0x0922` |
| `0x0C` | `0xFE` | `0x0924` |

`0x2A` and `0x2B` being write-once with no reader is worth flagging: whether they were meant
as a 16-bit value cannot be determined, because nothing consumes them. Confidence on
"never read": **likely** — established by a direct-operand scan plus a check for indirect
pointers being loaded with those addresses, but indirect access through a computed pointer
cannot be fully excluded.

---

## 4. USB attach and the exact SFR state at that instant

The attach is **one instruction**, `0x0A7C`, the store half of the read-modify-write
beginning at `0x0A76`:

```
0x0A76  MOV  DPTR,#0xFFFC     ; USBCTL
0x0A79  MOVX A,@DPTR
0x0A7A  ORL  A,#0x80          ; CONT
0x0A7C  MOVX @DPTR,A
```

Per the datasheet, `USBCTL` bit 7 is **CONT** and bit 6 is **FEN**:

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|---|
| Name | CONT | FEN | RWUP | FRSTE | — | — | — | SDW_OK |

**FEN is not set at attach.** `USBCTL` becomes `0x80`, not `0xC0`.

FEN is set later, in the bus-reset handler `usb_rstr_handler` @ `0x0F64`, at `0x0F81`:

```
0x0F7D  MOV  DPTR,#0xFFFC
0x0F80  MOVX A,@DPTR
0x0F81  ORL  A,#0xC0          ; CONT | FEN
0x0F83  MOVX @DPTR,A
```

That handler is reached through VECINT slot `0x17` (RSTR), which the table at `0x0C7D`
points at `0x0F64`. Confidence: **certain** — both the table entry and the `ORL #0xC0`
were read directly.

So Rev 22's sequence is: **attach with CONT only, then set FEN on the first host bus
reset.** The RSTR handler also re-arms `OEPCNF0`/`IEPCNF0` to `0x84` and clears `USBFADR`,
which is consistent with FEN being cleared by hardware on every bus reset.

**Full SFR state at the moment of attach** (from the §2 table, all writes 1–57 having
completed):

```
USBCTL   = 0x80   (CONT set, FEN clear)
USBIMSK  = 0x9F
USBFADR  = 0x00
MEMCFG   = 0x01   (SDW)
GLOBCTL  = 0x07   (0x06 then |= 0x01 for CPTEN)
IE       = 0x83   (EA=1, ET0=1, EX0=1)
IP       = 0x00
TMOD     = 0x11 ; TH0 = 0xCE ; TCON.TR0 = 1 (Timer 0 running)
P1       = 0x00 (modified afterwards by the shift-register routines)
P3       = 0xFF
OEPCNF0  = IEPCNF0 = 0x84
OEPCNF2  = IEPCNF1 = 0xC5   (streaming endpoints already enabled)
DMACTL0  = 0x02 ; DMACTL1 = 0x09
ACGCTL   = 0x06 ; ACGDCTL = ACG2DCTL = 0x10
```

`GLOBCTL = 0x07` is inferred from `0x06` followed by `|= 0x01`; confidence **certain** for
the two writes, **likely** for the composite value (nothing else writes GLOBCTL in this
path, but I did not exhaustively prove no other writer runs before attach).

---

## 5. Interrupt enabling relative to attach

| Order | Addr | Action |
|---|---|---|
| 1 | `0x0A4E` | `EA = 0` — global off at entry to `main` |
| 2 | `0x0A54` | `USBIMSK = 0x00` — all USB sources masked |
| 3 | `0x080E–0x0818` | individual `IE` bits: `EX0=1`, `ET0=1`; `ES`, `EX1`, `ET1` cleared |
| 4 | `0x081A` | `IP = 0x00` |
| 5 | `0x0912` | `USBIMSK = 0x9F` — USB sources unmasked (still gated by `EA=0`) |
| 6 | `0x0A72` | `TR0 = 1` — Timer 0 starts |
| 7 | `0x0A74` | **`EA = 1`** — global on |
| 8 | `0x0A7C` | **`USBCTL \|= 0x80`** — attach |

Interrupts go live one instruction before the bus attach, so no host traffic can arrive
before the handlers are able to run. Confidence: **certain**.

**`USBIMSK = 0x9F` decoded** against the datasheet register map
(bit 7 RSTR, 6 SUSR, 5 RESR, 4 SOF, 3 PSOF, 2 SETUP, 1 reserved, 0 STPOW):

| Bit | Name | `0x9F` | Enabled? |
|---|---|---|---|
| 7 | RSTR — function reset | 1 | **yes** |
| 6 | SUSR — suspend | 0 | no |
| 5 | RESR — resume | 0 | no |
| 4 | SOF — start of frame | 1 | **yes** |
| 3 | PSOF — pseudo SOF | 1 | **yes** |
| 2 | SETUP | 1 | **yes** |
| 1 | reserved | 1 | (reserved, read-only per datasheet) |
| 0 | STPOW — setup over-write | 1 | **yes** |

Suspend and resume are deliberately **not** unmasked, even though a SUSR handler exists at
`0x0006` and the VECINT table points slot `0x16` at it. Confidence: **certain** on the bit
decode; **likely** on "deliberately" — the handler's presence with the mask bit clear is
suggestive but not proof of intent.

---

## 6. Main loop

```
main_loop:                          ; 0x0A7D
    JB   bit 0x20 -> front_panel    ; 0x0A7D
    A = pending_action (IRAM 0x0A)  ; 0x0A80
    JZ   main_loop                  ; 0x0A82  (spin when idle)
    LCALL usb_deferred_action_dispatch  ; 0x0A84 -> 0x02F3
    SJMP main_loop                  ; 0x0A87

front_panel:                        ; 0x0A89
    LCALL p3_switch_scan            ; 0x0F31
    if (R7 bit 0) { LCALL shiftreg_out8_p1hi   ; 0x0EFC
                    LCALL shiftreg_out16       ; 0x0E56 }
    ; two edge-guarded blocks, gated on IRAM 0x27:
    ;   bit 0x01 clear && 0x27 == 0 -> 0x27 = 1; pending_action = 0x0B; dispatch
    ;   bit 0x01 set   && 0x27 == 1 -> 0x27 = 0; pending_action = 0x0C; dispatch
    CLR  bit 0x20                   ; 0x0AB7
    SJMP main_loop                  ; 0x0AB9
```

Polled, in order: **bit `0x20`** (front-panel service request), then **`pending_action`**.
`usb_deferred_action_dispatch` at `0x02F3` is the consumer for all deferred work; it
dispatches through the 14-entry `LJMP` table at `0x030C`. Confidence: **certain**.

---

## 7. Hardware bring-up summary, in order

1. **USB off** — `USBCTL = 0`, `USBIMSK = 0`.
2. **Memory** — `MEMCFG = 0x01` (SDW; already set by boot ROM).
3. **Ports** — `P1 = 0x00`, `P3 = 0xFF`.
4. **Timers** — `TMOD = 0x11`, `TH0 = 0xCE`, everything else zeroed, timers stopped.
5. **Interrupt sources** — `EX0` and `ET0` enabled, `IP = 0`, `EA` still 0.
6. **Codec port** — `GLOBCTL = 0x06` (CPTEN still clear), then all seven `CPT*` registers
   programmed, and only then `GLOBCTL |= 0x01` to enable CPTEN. Enabling the port after
   configuring it, not before.
7. **Clock generators** — both frequency synthesizers loaded with the datasheet's
   24.576 MHz constants (`0x61`, `0xA8`, `0x0F`), FRQ0 written last in each group because
   that write latches the 24-bit value; then `ACGCTL = 0x06` and both divider controls
   set to `0x10`.
8. **Shift registers** — 8-bit image (IRAM `0x22`) flushed twice, once as `0x00` and once
   as `0xFF` with bits `0x10`, `0x13`, `0x1E` cleared; then the 16-bit control latch
   flushed with IRAM `0x25`/`0x23` zeroed.
9. **USB endpoints and DMA** — EP0 both directions, both streaming endpoints (enabled
   immediately at `0xC5`), both DMA channels.
10. **Attach** — settle delay, Timer 0 start, `EA = 1`, `USBCTL |= CONT`.

---

## 8. The SOF handler — Rev 22's addition

**Startup does arm it.** Two independent facts establish this:

- `USBIMSK = 0x9F` (write #56, `0x0912`) has **bit 4 SOF set** and **bit 3 PSOF set**.
- VECINT dispatch slot `0x14` (SOF) in the table at `0x0C7D` points at **`0x0D58`**.

Confidence: **certain** — the mask bit meaning is from the datasheet register map and the
table entry was decoded from the image bytes.

The handler body at `0x0D58`:

```
0x0D58  MOV DPTR,#0xFFEC ; MOVX A,@DPTR ; MOV R6,A    ; DMABCNT0H
0x0D5D  MOV DPTR,#0xFFEB ; MOVX A,@DPTR               ; DMABCNT0L
0x0D61  MOV R4,#0x00 ; ADD A,#0x00 ; MOV R7,A
0x0D66  MOV A,R4 ; ADDC A,R6 ; MOV R6,A               ; 16-bit assemble
0x0D6A  XRL A,0x1C ; JNZ ...                          ; compare vs saved low
0x0D6E  MOV A,R6 ; XRL A,0x1B                         ; compare vs saved high
0x0D71  JZ  0x0D9D                                    ; unchanged -> skip
0x0D73  MOV 0x1B,R6 ; MOV 0x1C,R7                     ; store new snapshot
0x0D77  MOV R5,#0x06 ; ...
```

It reads the **DMA channel 0 byte counter** (`DMABCNT0H`/`DMABCNT0L` at `0xFFEC`/`0xFFEB`),
assembles it into a 16-bit value, compares it against the previous snapshot held in IRAM
`0x1B:0x1C`, and takes an early exit at `0x0D9D` when it has not changed. That is a
per-frame rate-tracking mechanism.

Regarding startup specifically: **IRAM `0x1B` and `0x1C` are not explicitly initialised**
by any of the init paths traced above — they are covered only by the blanket C51 clear to
zero at `0x092A–0x092E`. So the first SOF after attach compares against `0x0000` and, unless
the counter happens to read zero, takes the "changed" branch and stores the first real
snapshot. Confidence: **certain** that they are not explicitly written during startup
(no `0x1B`/`0x1C` direct-address store appears in any init function traced here);
**likely** on the first-SOF behaviour, since that depends on hardware counter state.

What the handler does after `0x0D77` (the `R5 = 6` path) is **not traced here** — it falls
outside "reset to main loop" and I did not follow it.

---

## Open items from this trace

1. The Timer 0 ISR body (`0x000B → 0x1016`) was not traced. The claim that it is what sets
   the main loop's poll bit `0x20` is **likely**, not established.
2. The SOF handler's action path beyond `0x0D77` is untraced.
3. IRAM `0x2A` and `0x2B` are written once during `main` entry and appear to have no reader.
4. `USBIMSK = 0x9F` sets bit 1, which the datasheet marks reserved and read-only. Harmless,
   but it means the value was likely chosen as a mask pattern rather than bit by bit.
5. The identity of the parts on the codec port and the 3-wire serial link is **not**
   established by this trace, and nothing in the startup path reveals it — the firmware only
   shifts out bytes. The `CPT*` and shift-register values are transcribed here as bytes, not
   interpreted as any particular chip's register map.
