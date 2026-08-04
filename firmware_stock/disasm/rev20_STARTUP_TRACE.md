# Rev 20 — startup execution trace (reset vector → main loop)

Instruction-level trace of Digidesign Mbox 1 firmware **Rev 20** on the
TAS1020B (8051 core), from `LJMP 0` to the first iteration of the main
polling loop.

**Source image.** `firmware_stock/rev20_firmware_code.bin`, 8174 bytes,
load address 0x0000 (CPU address == file offset).

**Method.** Every claim below was checked against the raw listing bytes in
`rev20_ghidra.txt` and, where the meaning of a value is at stake, against
`reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h` (SFR names, live
`#define`s only) and the TI SDK C sources. Statements are tagged
**certain** / **likely** / **UNKNOWN**. `rev20_ANNOTATED.md` was used only
as a pointer; nothing from it is repeated here without byte-level
verification, because an audit found roughly one in five of its cited
claims to be wrong.

---

## 0. Entry state

The boot ROM reaches user code via `LJMP 0` and leaves (verified earlier
from the TI ROM sources, treated as given here):

| SFR | Value at handoff |
|---|---|
| USBCTL (0xFFFC) | 0x00 — not attached, no pull-up |
| USBIMSK (0xFFFD) | 0x00 — all USB interrupt sources masked |
| USBFADR (0xFFFF) | 0x00 — unaddressed |
| MEMCFG (0xFFB0) | SDW = 1 — code fetches routed to the RAM copy |
| GLOBCTL (0xFFB1) | 0x04 — 12 MHz, LPWR on, CPTEN clear (codec port off) |
| IE | 0x00, EA = 0 — all interrupts disabled |
| DMA / codec port | **undefined** — the boot ROM does not reset them |

Note the address of MEMCFG: `Reg_stc1.h` puts it at **0xFFB0**, and
GLOBCTL at **0xFFB1**. (certain — `Reg_stc1.h`, live defines)

---

## 1. Step-by-step walk

### Step 1 — reset vector

```
0x0000  02 0a 09   LJMP 0x0A09        -> c51_startup
```
(certain)

### Step 2 — `c51_startup` (0x0A09): clear IRAM, set stack

```
0x0A09  78 7f      MOV  R0,#0x7F
0x0A0B  e4         CLR  A
0x0A0C  f6         MOV  @R0,A          ; loop body
0x0A0D  d8 fd      DJNZ R0,0x0A0C
0x0A0F  75 81 33   MOV  SP,#0x33       ; SFR 0x81 = SP
0x0A12  02 0a 50   LJMP 0x0A50
```

Zeroes IRAM **0x7F down to 0x01** through `@R0`. `DJNZ R0` exits when R0
reaches 0, so **IRAM 0x00 is never written** by this loop. (certain)

Stack pointer set to 0x33, so the first push lands at 0x34. Everything at
and below 0x33 is therefore static data, which is why 0x33 is the highest
non-stack byte. (certain)

`0x7F` here is **not a variable** — it is the top-of-IRAM address the loop
counts down from. It appears nowhere else as a direct-address operand.
(certain)

### Step 3 — `c51_init_interpreter` (0x0A50 → 0x0A53 …): run the init table

```
0x0A50  90 0f 9c   MOV DPTR,#0x0F9C    ; init opcode stream base
0x0A53  e4         CLR A
0x0A54  7e 01      MOV R6,#0x01
0x0A56  93         MOVC A,@A+DPTR      ; read control byte
0x0A57  60 bc      JZ  0x0A15          ; 0x00 => end of table
...
0x0A15  02 0a 95   LJMP 0x0A95         -> main
```

The control byte is decoded as: low 6 bits = byte count, top 2 bits =
record type (`ANL A,#0x3F` at 0x0A5B, `ANL A,#0xC0` at 0x0A6A). Type 00
dispatches to 0x0A18 which writes with `MOV @R0,A` (IRAM); type 10 reaches
the same loop with carry set and writes with `MOVX @R0,A` (XDATA); type 11
goes to 0x0A2A, a bit set/clear path. (certain for type 00; **likely** for
the type 10/11 encoding — Rev 20's table only contains type 00 records, so
the other paths are not exercised at startup and were read, not observed)

**Decoded init stream at 0x0F9C — 13 records, 40 bytes, terminator at
0x0FC3:** (certain — decoded byte by byte from the image)

| Offset | Control | Writes |
|---|---|---|
| 0x0F9C | 01 | IRAM[0x22] = 0x00 |
| 0x0F9F | 01 | IRAM[0x20] = 0x00 |
| 0x0FA2 | 01 | IRAM[0x25] = 0x00 |
| 0x0FA5 | 01 | IRAM[0x23] = 0x00 |
| 0x0FA8 | 01 | IRAM[0x24] = 0x00 |
| 0x0FAB | 01 | IRAM[0x21] = 0x00 |
| 0x0FAE | 01 | IRAM[0x09] = 0x00 |
| 0x0FB1 | 01 | IRAM[0x0C] = 0x00 |
| 0x0FB4 | 01 | IRAM[0x0B] = 0x00 |
| 0x0FB7 | 01 | IRAM[0x0E] = 0x00 |
| 0x0FBA | 01 | IRAM[0x0A] = 0x00 |
| 0x0FBD | 01 | IRAM[0x0D] = 0x00 |
| 0x0FC0 | 01 | **IRAM[0x08] = 0x03** |
| 0x0FC3 | 00 | terminator |

Only one non-zero initialiser: `clock_mode_id` (IRAM 0x08) = 3. Every
other record re-zeroes a byte the startup clear already cleared. (certain)

### Step 4 — `main` (0x0A95): pre-init

```
0x0A95  e4         CLR  A
0x0A96  f5 27      MOV  0x27,A         ; IRAM[0x27] = 0
0x0A98  74 ff      MOV  A,#0xFF
0x0A9A  f5 28      MOV  0x28,A         ; IRAM[0x28] = 0xFF  delay hi
0x0A9C  f5 29      MOV  0x29,A         ; IRAM[0x29] = 0xFF  delay lo
0x0A9E  75 2a 00   MOV  0x2A,#0x00
0x0AA1  75 2b 10   MOV  0x2B,#0x10
0x0AA4  c2 af      CLR  EA             ; bit 0xAF — interrupts OFF
0x0AA6  90 ff fd   MOV  DPTR,#0xFFFD
0x0AA9  e4         CLR  A
0x0AAA  f0         MOVX @DPTR,A        ; USBIMSK = 0x00
0x0AAB  c2 22      CLR  0x22           ; bit 0x22 = IRAM byte 0x24 bit 2
0x0AAD  12 08 cb   LCALL 0x08CB        -> hw_master_init
0x0AB0  12 09 70   LCALL 0x0970        -> usb_ep_dma_init
```

Interrupts are explicitly disabled and USBIMSK re-zeroed before any
peripheral setup. (certain)

### Step 5 — `hw_master_init` (0x08CB)

Executes in this order (certain throughout; SFR names from `Reg_stc1.h`):

```
0x08CB  CLR A
0x08CC  MOV 0x2E,A / MOV 0x2F,A        ; scratch counters = 0
0x08D0  USBCTL   = 0x00                ; explicit disconnect
0x08D4  MEMCFG   = 0x01                ; SDW set (RAM code), all else clear
0x08DA  P1       = 0x00                ; SFR 0x90
0x08DC  P3       = 0xFF                ; SFR 0xB0 — inputs/pull-ups high
0x08DF  TH0      = 0xCE
0x08E2  TL0      = 0x00
0x08E4  TH1      = 0x00
0x08E6  TL1      = 0x00
0x08E8  TMOD     = 0x11                ; both timers mode 1 (16-bit)
0x08EB  TCON     = 0x00                ; timers stopped, IT0 = 0 (level)
0x08ED  CLR  EA   (IE.7 = 0)
0x08EF  CLR  ES   (IE.4 = 0)
0x08F1  CLR  EX1  (IE.2 = 0)
0x08F3  SETB ET0  (IE.1 = 1)           ; Timer 0 interrupt enabled
0x08F5  CLR  ET1  (IE.3 = 0)
0x08F7  SETB EX0  (IE.0 = 1)           ; INT0 = USB engine interrupt
0x08F9  IP       = 0x00                ; SFR 0xB8, no priority overrides
0x08FB  INC DPTR                       ; DPTR 0xFFB0 -> 0xFFB1
0x08FC  GLOBCTL  = 0x06
0x08FF  CPTCNF1  = 0x0D
0x0905  CPTCNF2  = 0xE5
0x090B  CPTCNF3  = 0xAC
0x0911  CPTCNF4  = 0x03
0x0917  CPTCTL   = 0x50
0x091D  CPTRXCNF2= 0x25
0x0923  CPTRXCNF3= 0xAC
0x0929  A = 0x03, DPTR = 0xFFD4, LCALL 0x0DEB
```

`IE` therefore ends this block at **0x03** (EX0 + ET0) with **EA still 0**.
(certain)

The `INC DPTR` at 0x08FB relies on DPTR still holding 0xFFB0 from 0x08D4 —
nothing between them touches DPTR. (certain, verified by reading every
instruction in the span)

**`LCALL 0x0DEB` — falls through two functions:** (certain)

```
0x0DEB  MOVX @DPTR,A                   ; CPTRXCNF4 = 0x03
        --- falls into acg_set_freq_48k_family ---
0x0DEC  ACGFRQ1  = 0xA8                ; 0xFFE6
0x0DF2  ACGFRQ2  = 0x61                ; 0xFFE5
0x0DF8  ACGFRQ0  = 0x0F                ; 0xFFE7
0x0DFE  ACG2FRQ1 = 0xA8                ; 0xFFF8
0x0E04  ACG2FRQ2 = 0x61                ; 0xFFF7
0x0E0A  A = 0x0F, DPTR = 0xFFF9
        --- falls into acg_commit_and_ctl ---
0x0E0F  MOVX @DPTR,A                   ; ACG2FRQ0 = 0x0F
0x0E10  ACGCTL   = 0x06                ; 0xFFE1
0x0E16  RET
```

Both clock generators are loaded with the same 24-bit word
(0x61, 0xA8, 0x0F). (certain that the values are identical; the frequency
they encode is **UNKNOWN** without working the datasheet's ACG divider
formula)

```
0x0931  LCALL 0x0E17
0x0E17  INC DPTR                       ; DPTR 0xFFE1 -> 0xFFE2
0x0E18  A = 0x10
0x0E1A  MOVX @DPTR,A                   ; ACGDCTL  = 0x10
0x0E1B  DPTR = 0xFFF6
0x0E1E  MOVX @DPTR,A                   ; ACG2DCTL = 0x10
0x0E1F  RET
```

`0x0E17` is an alternate entry that exists purely to advance DPTR from
ACGCTL to ACGDCTL for callers arriving from `acg_commit_and_ctl`.
(certain)

```
0x0934  GLOBCTL |= 0x01                ; read-modify-write, sets CPTEN
0x093B  IRAM[0x08] = 0x03              ; clock_mode_id
0x093E  IRAM[0x22] = 0x00              ; shift-register image
0x0941  SETB bit 0x1E                  ; IRAM byte 0x23 bit 6
0x0943  LCALL 0x0F0C                   -> shiftreg8_commit_p1_7_6_5
0x0946  delay: count IRAM[0x2E]:[0x2F] up to 0x0FFF  (4095 iterations)
0x095B  IRAM[0x22] = 0xFF
0x095E  CLR bit 0x10                   ; byte 0x22 bit 0
0x0960  CLR bit 0x13                   ; byte 0x22 bit 3
0x0962  CLR bit 0x1E                   ; byte 0x23 bit 6
0x0964  LCALL 0x0F0C                   -> shiftreg8_commit_p1_7_6_5
0x0967  IRAM[0x25] = 0x00
0x096A  IRAM[0x23] = 0x00
0x096C  LCALL 0x0E62                   -> shiftreg16_commit_p1_0_1_2
0x096F  RET
```

**CPTEN is set only after all seven CPT configuration registers are
programmed.** (certain — the `GLOBCTL |= 0x01` at 0x0934 follows every
CPTCNF/CPTRXCNF/CPTCTL write)

The delay loop increments 0x2F, carrying into 0x2E, exiting when the pair
equals 0x0FFF — so 0x2E is the **high** byte. (certain, from the compare
sequence at 0x0946–0x094F)

### Step 6 — `usb_ep_dma_init` (0x0970)

Straight-line SFR programming, no branches (certain):

```
0x0970  OEPBBAX0  = 0x42      ; EP0 OUT buffer base -> 0xFA10
0x0976  IEPBBAX0  = 0x43      ; EP0 IN  buffer base -> 0xFA18
0x097B  OEPDCNTX0 = 0x00
0x0980  IEPDCNTX0 = 0x00
0x0984  OEPDCNTY0 = 0x00
0x0988  IEPDCNTY0 = 0x00
0x098C  OEPBSIZ0  = 0x01
0x0991  IEPBSIZ0  = 0x01
0x0995  OEPCNF0   = 0x84
0x099B  IEPCNF0   = 0x84
0x099F  OEPBBAX2  = 0x44
0x09A5  IEPBBAX1  = 0x94
0x09AB  OEPBSIZ2  = 0x50
0x09B1  IEPBSIZ1  = 0x50
0x09B5  OEPDCNTX2 = 0x00
0x09BA  IEPDCNTX1 = 0x00
0x09BE  OEPCNF2   = 0xC5
0x09C4  IEPCNF1   = 0xC5
0x09C8  DMATSL0   = 0x03
0x09CE  DMATSH0   = 0x80
0x09D4  DMATSL1   = 0x03
0x09DA  DMATSH1   = 0x80
0x09E0  DMACTL0   = 0x02
0x09E6  DMACTL1   = 0x09
0x09EC  USBIMSK   = 0x9F
0x09F2  USBFADR   = 0x00
0x09F7  CLR bits 0x0A, 0x0E, 0x08, 0x09    ; IRAM byte 0x21 bits 2,6,0,1
0x09FF  IRAM[0x09] = 0x00
0x0A01  IRAM[0x0B] = 0x00
0x0A03  IRAM[0x0C] = 0xFE
0x0A06  IRAM[0x0A] = 0x00
0x0A08  RET
```

Two points that matter for any reimplementation:

- **The streaming endpoints are enabled here, at boot** — `OEPCNF2` and
  `IEPCNF1` are both set to 0xC5, not left dormant until
  `SET_INTERFACE(alt=1)`. (certain)
- **`usb_ep_dma_init` never touches USBCTL.** The bus attach happens later,
  in `main`. (certain)

`USBIMSK = 0x9F` is a plain assignment, not a read-modify-write. Compare
TI's `engUsbInit`, which writes 0xE5 with the comment *"Enable Reset,
Resume, Suspend, SETUP and STPOW"* (`UsbEng.c:640`). Bit 4 (0x10) is SOF,
confirmed by `UsbDfu.c:319` (`USBIMSK |= 0x10; // SOF INT on`). So relative
to TI's default Rev 20 **adds SOF** and **drops SUSR and RESR**, which fits
an audio device that needs a frame tick. Bits 1 and 3 of 0x9F are
**UNKNOWN** — no source on disk maps them. (bit 4 certain; the SUSR/RESR
difference certain; bits 1 and 3 UNKNOWN)

### Step 7 — settle delay, timer start, interrupt enable, USB attach

```
0x0AB3  16-bit down-count on IRAM[0x28]:[0x29] from 0xFFFF   ; 65535 iters
0x0AC8  SETB TR0        (bit 0x8C = TCON.4)   ; Timer 0 starts running
0x0ACA  SETB EA         (bit 0xAF)            ; interrupts globally enabled
0x0ACC  MOV  DPTR,#0xFFFC
0x0ACF  MOVX A,@DPTR
0x0AD0  ORL  A,#0x80
0x0AD2  MOVX @DPTR,A                          ; USBCTL |= 0x80
```

(certain — every instruction read from the listing)

---

## 2. Ordered SFR write table

Every SFR write executed before the main loop, in execution order. This is
the table a reimplementation must reproduce.

| # | Addr | SFR | Value | Meaning |
|---|---|---|---|---|
| 1 | 0x0AAA | USBIMSK | 0x00 | mask all USB interrupt sources |
| 2 | 0x08D3 | USBCTL | 0x00 | disconnect: no pull-up, FEN clear |
| 3 | 0x08D8 | MEMCFG | 0x01 | SDW set — code fetches from RAM copy (`MEMCFG_SDW_BIT`=0x01, certain) |
| 4 | 0x08DA | P1 | 0x00 | all P1 outputs low |
| 5 | 0x08DC | P3 | 0xFF | all P3 high (button inputs + pull-ups) |
| 6 | 0x08DF | TH0 | 0xCE | Timer 0 reload high |
| 7 | 0x08E2 | TL0 | 0x00 | Timer 0 reload low |
| 8 | 0x08E4 | TH1 | 0x00 | Timer 1 high |
| 9 | 0x08E6 | TL1 | 0x00 | Timer 1 low |
| 10 | 0x08E8 | TMOD | 0x11 | both timers 16-bit mode 1 |
| 11 | 0x08EB | TCON | 0x00 | timers stopped; IT0=0 → INT0 level-triggered |
| 12 | 0x08ED–0x08F7 | IE | 0x03 | EX0+ET0 set, EA still 0 (six bit ops) |
| 13 | 0x08F9 | IP | 0x00 | no interrupt priority overrides |
| 14 | 0x08FE | GLOBCTL | 0x06 | LPWR on (bit 2); bit 1 **UNKNOWN**; CPTEN still clear |
| 15 | 0x0904 | CPTCNF1 | 0x0D | codec port config — field meaning UNKNOWN |
| 16 | 0x090A | CPTCNF2 | 0xE5 | codec port config — UNKNOWN |
| 17 | 0x0910 | CPTCNF3 | 0xAC | codec port config — UNKNOWN |
| 18 | 0x0916 | CPTCNF4 | 0x03 | codec port config — UNKNOWN |
| 19 | 0x091C | CPTCTL | 0x50 | codec port control — UNKNOWN |
| 20 | 0x0922 | CPTRXCNF2 | 0x25 | codec RX config — UNKNOWN |
| 21 | 0x0928 | CPTRXCNF3 | 0xAC | codec RX config — UNKNOWN |
| 22 | 0x0DEB | CPTRXCNF4 | 0x03 | codec RX config — UNKNOWN |
| 23 | 0x0DF1 | ACGFRQ1 | 0xA8 | clock gen 1 frequency word |
| 24 | 0x0DF7 | ACGFRQ2 | 0x61 | clock gen 1 frequency word |
| 25 | 0x0DFD | ACGFRQ0 | 0x0F | clock gen 1 frequency word |
| 26 | 0x0E03 | ACG2FRQ1 | 0xA8 | clock gen 2 — same word as gen 1 |
| 27 | 0x0E09 | ACG2FRQ2 | 0x61 | clock gen 2 |
| 28 | 0x0E0F | ACG2FRQ0 | 0x0F | clock gen 2 |
| 29 | 0x0E15 | ACGCTL | 0x06 | clock gen control — bit meaning UNKNOWN |
| 30 | 0x0E1A | ACGDCTL | 0x10 | clock gen 1 divider control |
| 31 | 0x0E1E | ACG2DCTL | 0x10 | clock gen 2 divider control |
| 32 | 0x093A | GLOBCTL | \|= 0x01 | **CPTEN set — codec port enabled, after all CPT config** |
| 33 | 0x0975 | OEPBBAX0 | 0x42 | EP0 OUT buffer base → 0xFA10 |
| 34 | 0x097A | IEPBBAX0 | 0x43 | EP0 IN buffer base → 0xFA18 |
| 35 | 0x097F | OEPDCNTX0 | 0x00 | clear EP0 OUT count/NAK |
| 36 | 0x0983 | IEPDCNTX0 | 0x00 | clear EP0 IN count/NAK |
| 37 | 0x0987 | OEPDCNTY0 | 0x00 | clear EP0 OUT Y-buffer count |
| 38 | 0x098B | IEPDCNTY0 | 0x00 | clear EP0 IN Y-buffer count |
| 39 | 0x0990 | OEPBSIZ0 | 0x01 | EP0 OUT max packet (8 bytes, size>>3) |
| 40 | 0x0994 | IEPBSIZ0 | 0x01 | EP0 IN max packet (8 bytes) |
| 41 | 0x099A | OEPCNF0 | 0x84 | EP0 OUT enable + interrupt-on-transaction |
| 42 | 0x099E | IEPCNF0 | 0x84 | EP0 IN enable + interrupt-on-transaction |
| 43 | 0x09A4 | OEPBBAX2 | 0x44 | EP2 OUT buffer base |
| 44 | 0x09AA | IEPBBAX1 | 0x94 | EP1 IN buffer base |
| 45 | 0x09B0 | OEPBSIZ2 | 0x50 | EP2 OUT buffer size |
| 46 | 0x09B4 | IEPBSIZ1 | 0x50 | EP1 IN buffer size |
| 47 | 0x09B9 | OEPDCNTX2 | 0x00 | clear EP2 OUT count |
| 48 | 0x09BD | IEPDCNTX1 | 0x00 | clear EP1 IN count |
| 49 | 0x09C3 | OEPCNF2 | 0xC5 | **EP2 OUT enabled at boot** |
| 50 | 0x09C7 | IEPCNF1 | 0xC5 | **EP1 IN enabled at boot** |
| 51 | 0x09CD | DMATSL0 | 0x03 | DMA ch0 transfer size low |
| 52 | 0x09D3 | DMATSH0 | 0x80 | DMA ch0 transfer size high |
| 53 | 0x09D9 | DMATSL1 | 0x03 | DMA ch1 transfer size low |
| 54 | 0x09DF | DMATSH1 | 0x80 | DMA ch1 transfer size high |
| 55 | 0x09E5 | DMACTL0 | 0x02 | DMA ch0 control |
| 56 | 0x09EB | DMACTL1 | 0x09 | DMA ch1 control |
| 57 | 0x09F1 | USBIMSK | 0x9F | STPOW+SETUP+SOF+RSTR (bits 1,3 UNKNOWN); SUSR/RESR **off** |
| 58 | 0x09F6 | USBFADR | 0x00 | device unaddressed |
| 59 | 0x0AC8 | TCON.TR0 | set | Timer 0 starts |
| 60 | 0x0ACA | IE.EA | set | **interrupts globally enabled** |
| 61 | 0x0AD2 | USBCTL | \|= 0x80 | **bus attach — CONN only** |

---

## 3. IRAM state at entry to the main loop

**Cleared by the startup loop:** IRAM 0x01–0x7F, all zero. IRAM 0x00 is
*not* touched. (certain)

**Set by the init opcode stream (0x0F9C):** twelve bytes re-zeroed, plus
`IRAM[0x08] = 0x03`. (certain — see the decode table in step 3)

**Set afterwards by code:**

| Byte | Value | Written at | Role |
|---|---|---|---|
| 0x08 | 0x03 | init stream; again 0x093B | clock/codec-port mode id |
| 0x09 | 0x00 | 0x09FF | EP0 IN remaining length, low |
| 0x0A | 0x00 | 0x0A06 | pending main-loop event code |
| 0x0B | 0x00 | 0x0A01 | EP0 IN remaining length, high |
| 0x0C | 0xFE | 0x0A03 | (role **UNKNOWN**) |
| 0x22 | 0xFF then bits 0 and 3 cleared | 0x095B–0x0960 | 8-bit shift-register image |
| 0x23 | 0x00 | 0x096A | 16-bit control-latch image |
| 0x25 | 0x00 | 0x0967 | audio-path state/flags |
| 0x27 | 0x00 | 0x0A96 | front-panel edge-tracking state |
| 0x28 | 0xFF → 0x00 | 0x0A9A, consumed by delay | startup delay counter high |
| 0x29 | 0xFF → 0x00 | 0x0A9C, consumed by delay | startup delay counter low |
| 0x2A | 0x00 | 0x0A9E | (role **UNKNOWN** — see note) |
| 0x2B | 0x10 | 0x0AA1 | (role **UNKNOWN** — see note) |
| 0x2E | 0x0F | delay loop 0x0946 | scratch counter high |
| 0x2F | 0xFF | delay loop 0x0946 | scratch counter low |

Bits cleared individually: 0x22 (byte 0x24 bit 2) at 0x0AAB; 0x0A, 0x0E,
0x08, 0x09 (byte 0x21 bits 2, 6, 0, 1) at 0x09F7–0x09FD; 0x10 and 0x13
(byte 0x22 bits 0, 3) at 0x095E–0x0960; 0x1E (byte 0x23 bit 6) set at
0x0941 then cleared at 0x0962. (certain)

**Note on 0x2A / 0x2B.** Rev 22 has the structurally identical pair — same
values 0x00 and 0x10, same position at main entry — and an operand scan of
Rev 22 found no reader anywhere in that image. Whether Rev 20 reads them
was not separately scanned here, so their role is **UNKNOWN**.

---

## 4. The USB attach — exact moment and state

The attach is a single read-modify-write at **0x0AD2**:

```
0x0ACC  MOV  DPTR,#0xFFFC
0x0ACF  MOVX A,@DPTR          ; A = current USBCTL (0x00)
0x0AD0  ORL  A,#0x80          ; set CONN
0x0AD2  MOVX @DPTR,A          ; USBCTL = 0x80
```

**FEN (bit 6, 0x40) is NOT set here.** The value written is 0x80, CONN
only. (certain — the immediate operand is `#0x80`, verified in the raw
bytes `44 80` at 0x0AD0)

This differs from TI's reference, which writes `USBCTL = 0xC0` in
`engUsbInit` (`UsbEng.c:647`, *"connect PUR, enable function address,
disable FRSTE"*). Rev 20 asserts only the pull-up at boot. (certain)

**Where FEN gets set — resolved.** (certain) FEN is not set anywhere on the
reset→main-loop path, and the image contains **no `ORL A,#0x40` at all**.
It contains three `ORL A,#0xC0` sites, and only one of them targets USBCTL:

| Site | DPTR at the OR | Register |
|---|---|---|
| 0x07D0 | 0xFFE1 | ACGCTL — clock generator, not USB |
| 0x0828 | 0xFFE1 | ACGCTL — clock generator, not USB |
| **0x0F60** | **0xFFFC** | **USBCTL** |

0x0F60 sits inside the handler at **0x0F43**, which is the target of
**VECINT slot 23 — `RSTR_INT` (0x17), the bus reset**, per the dispatch
table at 0x0C93 and the event codes in `Reg_stc1.h`. The handler re-arms
EP0 and then asserts both bits:

```
0x0F52  OEPCNF0 = 0x84
0x0F58  IEPCNF0 = 0x84
0x0F5C  MOV  DPTR,#0xFFFC
0x0F5F  MOVX A,@DPTR
0x0F60  ORL  A,#0xC0        ; CONN | FEN
0x0F62  MOVX @DPTR,A
```

So the division of labour is: **boot asserts CONN only (0x80); the
bus-reset handler asserts CONN|FEN (0xC0) on every reset.** This matches
the datasheet behaviour that a bus reset clears FEN, and means FEN is
first set only after the host issues its initial reset — never before.

**Full SFR state at the instant of attach:** rows 1–60 of the table in
section 2, i.e. USBIMSK = 0x9F, USBFADR = 0x00, both EP0 configs 0x84,
both streaming EP configs 0xC5, DMA channels 0 and 1 armed, CPTEN set,
both clock generators loaded and their divider controls at 0x10, Timer 0
running, IE = 0x03 with EA = 1.

---

## 5. Interrupt enable ordering

| Point | Action |
|---|---|
| 0x0AA4 | `CLR EA` — interrupts off before any setup |
| 0x08ED | `CLR EA` again (redundant, inside `hw_master_init`) |
| 0x08EF–0x08F7 | individual masks set: ES=0, EX1=0, **ET0=1**, ET1=0, **EX0=1** → IE = 0x03 |
| 0x08EB | `TCON = 0x00` leaves IT0 = 0, so INT0 is **level**-triggered |
| 0x09F1 | USBIMSK = 0x9F — selects which USB sources drive INT0 |
| 0x0AC8 | `SETB TR0` — Timer 0 begins counting |
| **0x0ACA** | **`SETB EA` — interrupts globally enabled** |
| **0x0AD2** | **USBCTL \|= 0x80 — bus attach** |

**Interrupts are enabled two instructions before the device attaches.**
(certain) So the USB engine is fully armed and able to service a SETUP
packet from the very first moment the host can see the pull-up. Level-
triggered INT0 is required because the engine ORs all unmasked USBIMSK
sources into one line; edge triggering would drop re-assertions. (the
mechanism is **likely** — inferred from IT0=0 plus TI's matching
`IT0 = 0` at `UsbEng.c:642`, not from the datasheet directly)

---

## 6. Main loop (0x0AD3)

```
0x0AD3  JB   bit 0x20,0x0ADF     ; byte 0x24 bit 0 — front-panel scan due?
0x0AD6  MOV  A,IRAM[0x0A]        ; pending event code
0x0AD8  JZ   0x0AD3              ; nothing pending -> spin
0x0ADA  LCALL 0x02EE             -> device_event_dispatch
0x0ADD  SJMP 0x0AD3
```

Poll order, per iteration: **(1)** the front-panel-scan flag, bit 0x20;
**(2)** the pending-event byte IRAM[0x0A]. If neither is set the loop spins
tightly. USB traffic is handled entirely under interrupt — the main loop
never polls VECINT. (certain)

Event dispatch goes to `device_event_dispatch` (0x02EE), which indexes the
LJMP table at **0x0300** (14 entries) via `JMP @A+DPTR`. (certain that the
table is at 0x0300 with 14 entries; the per-slot handlers are outside this
trace)

Front-panel branch:

```
0x0ADF  LCALL 0x0ED5             -> p3_button_scan, returns flags in R7
0x0AE2  MOV  A,R7
0x0AE3  JNB  ACC.0,0x0AEC        ; nothing changed -> skip commit
0x0AE6  LCALL 0x0F0C             -> shiftreg8_commit_p1_7_6_5
0x0AE9  LCALL 0x0E62             -> shiftreg16_commit_p1_0_1_2
0x0AEC  JB   bit 0x01,0x0AFC
0x0AEF  MOV  A,IRAM[0x27] ; if zero: IRAM[0x27]=1, IRAM[0x0A]=0x0B, dispatch
0x0AFC  JNB  bit 0x01,0x0B0D
0x0AFF  if IRAM[0x27]==1: IRAM[0x27]=0, IRAM[0x0A]=0x0C, dispatch
0x0B0D  CLR  bit 0x20            ; clear scan-due flag
0x0B0F  SJMP 0x0AD3
```

Bit 0x01 (byte 0x20 bit 1) gates a pair of edge-triggered events 0x0B and
0x0C, with IRAM[0x27] acting as the one-shot latch so each edge fires once.
(certain for the control flow; the physical meaning of bit 0x01 is
**UNKNOWN** — it is a P3 input state captured by `p3_button_scan`)

---

## 7. Hardware bring-up summary, in order

1. **USB disconnected first** — `USBCTL = 0x00` at 0x08D3, before anything
   else is configured.
2. **Memory config** — `MEMCFG = 0x01`, keeping SDW set so code runs from
   the RAM copy.
3. **Port pins** — `P1 = 0x00` (all outputs low, including the three
   shift-register control lines), `P3 = 0xFF` (inputs high).
4. **Timers** — TMOD = 0x11, TH0 = 0xCE reload, TCON = 0x00 (stopped).
5. **Interrupt masks** — EX0 and ET0 enabled, EA still off.
6. **Codec port** — seven configuration registers written (CPTCNF1–4,
   CPTCTL, CPTRXCNF2–4), *then* CPTEN set via `GLOBCTL |= 0x01`. The
   ordering is deliberate: enable last.
7. **Clock generators** — both ACG and ACG2 loaded with the same 24-bit
   frequency word (0x61, 0xA8, 0x0F), ACGCTL = 0x06, then both divider
   controls set to 0x10.
8. **Shift registers** — the 8-bit image (IRAM 0x22) is driven twice, first
   as 0x00 then as 0xFF with bits 0 and 3 cleared, separated by a ~4095-
   iteration delay; then the 16-bit image (IRAM 0x23) is cleared and
   clocked out. The 8-bit chain uses P1.7/P1.6/P1.5, the 16-bit chain uses
   P1.0/P1.1/P1.2. (certain from the `ORL/ANL 0x90` masks in
   `shiftreg8_commit_p1_7_6_5` and `shiftreg16_commit_p1_0_1_2`; what the
   register outputs physically drive is **UNKNOWN**)
9. **USB endpoints and DMA** — EP0 in/out buffers, sizes and configs; EP1
   IN and EP2 OUT enabled at 0xC5; DMA channels 0 and 1 armed; USBIMSK =
   0x9F; USBFADR = 0.
10. **~65535-iteration settle delay.**
11. **Timer 0 started, EA set, then the D+ pull-up asserted.**

---

## Open items

> **RESOLVED 2026-07-31 — GLOBCTL bit 1 is P3PUDIS.** Datasheet §6.5.7.4: "Pullup resistor disable. If set to 1, disables on-chip pullup resistors on P3 GPIO pins." TI's ROM sources document only LPWR and MCUCLK, which is why it was never found there. The measured silent-USB result is explained without any USB-engine theory: `check_boot_dfu_button()` (main.c:48) depends on the internal P3 pull-ups, and with them disabled it wipes the EEPROM signature and spins forever without attaching. See FINDING_globctl_bits_named_and_cpten_missing.md and #169.

> **CORRECTED 2026-08-03 — the pull-up reading above is right, the button
> polarity behind it was not.** The paragraph saying `check_boot_dfu_button()`
> "depends on the internal P3 pull-ups" assumes the buttons are active-low.
> They are **active HIGH**: the board holds P3.3/P3.4/P3.5 low and a press
> drives them high. Proof from the image — `p3_button_scan` fires on
> `prev==0 && cur==1`, and Keil's `?C_INITSEG` zeroes the shadow at IRAM 0x20
> (record `01 20 00`), so idle-high pins would fire all three handlers on the
> first scan of every boot; the hardware boots to MIC instead. So P3PUDIS is
> **required** for the buttons to work at all, not merely tolerable, and build
> 0x0010 went silent because an active-low test met an active-high button, not
> because the pull-ups were needed. #169 answered. See
> `FINDING_buttons_are_active_high.md`.

- **GLOBCTL bit 1** (value 0x06 at step 14) — TI's ROM only ever documents
  bit 2 (LPWR) and bit 7 (CPU speed). Bit 1's function is **UNKNOWN**.
- ~~**USBIMSK bits 1 and 3** in the 0x9F written at 0x09F1~~ — **RESOLVED
  2026-07-29** from the datasheet register table (§6.5.1.3, USBIMSK 0xFFFD):
  bit 1 is **Reserved, type R (read-only)**, so stock setting it in 0x9F is
  inert; bit 3 is **PSOF, pseudo start-of-frame**. Full mask decode of stock's
  0x9F: RSTR + SOF + PSOF + SETUP + STPOW enabled, SUSR and RESR masked off.
  mboxfw writes 0xF5 = RSTR + SUSR + RESR + SOF + SETUP + STPOW, i.e. it enables
  suspend/resume (deliberate, it has a suspend path where stock's cannot suspend
  twice) and leaves PSOF masked. Leaving PSOF masked is benign: PSOF substitutes
  for a corrupted SOF packet, but the PSOF vector (VECINT 0x13) is a bare `RET`
  in BOTH images — Rev 20 0x1033, Rev 22 0x102B — so not even Rev 22's SOF
  watchdog runs on a PSOF frame. Stock enables an interrupt it then discards.
- **Does a USB reset clear USBIMSK?** — **RESOLVED 2026-07-29: no.** Both images
  rewrite USBIMSK = 0x9F in the bus-reset handler (Rev 20 0x0F6E, Rev 22
  0x0F8F), which looked like a write mboxfw was missing. Datasheet §2.1.9 Reset:
  with FRSTE clear, "USB resets have no effect on the TAS1020B, other than
  resetting the USB serial interface engine (SIE) and the USB buffer manager
  (UBM)". USBIMSK is neither. Stock's write is redundant re-assertion, and
  mboxfw's VEC_RSTR is right to omit it. What the reset DOES clear is FEN
  (documented per-bit) and UBM-owned endpoint config, which mboxfw re-arms.
- **All CPT* and ACG* register field meanings** — the values are recorded
  exactly, but decoding them into sample rates, frame formats and clock
  divisors needs the datasheet register tables, which were not worked
  through for this trace.
- ~~Where FEN is set~~ — **resolved in section 4**: the bus-reset handler
  at 0x0F43 (VECINT slot 23, RSTR) does `USBCTL |= 0xC0`. Boot sets CONN
  only.
- **IRAM 0x0C = 0xFE and IRAM 0x2A/0x2B** — written during init, roles
  undetermined.
- **What the shift-register outputs drive** — bit-to-net mapping unknown,
  which is the same gap as the unverified external chip identities.
