# Rev 20 firmware RE notes

All addresses are code-relative (start of firmware, after 18-byte EEPROM header).
Cross-reference with `rev20_flat.asm` (radare2 8051 flat disasm).

## Entry / vectors
- `0x0000  LJMP 0x0A09` — reset → main.
- `0x0003  LJMP 0x0DAC` — INT0.
- `0x000B  LJMP 0x101E` — Timer0.
- `0x001B  LJMP 0x000E` (RETI stub) — Timer1.
- `0x0023  LJMP 0x000F` (RETI stub) — UART.

## USB setup-packet dispatcher (0x0026)
Called from EP0 SETUP interrupt. Reads `bmRequestType` from `SETPACK+0`
(`0xFF28`) and uses the ADD-A/JZ idiom to dispatch on:

| bmReqType | Handler | Meaning                                          |
|-----------|---------|--------------------------------------------------|
| 0x21      | 0x0055  | class / OUT / interface  → **SET Input Source**  |
| 0x22      | 0x006B  | class / OUT / endpoint   → **SET Clock Source**  |
| 0xA1      | 0x0073  | class / IN  / interface  → **GET Input Source**  |
| 0xA2      | 0x008A  | class / IN  / endpoint   → **GET Clock Source**  |
| other     | 0x0118  | standard-request path                            |

Setup handlers just latch a **command code** into RAM byte `0x0D`:
- `0x0D = 1` → SET clock source pending
- `0x0D = 2` → SET input source pending

Flag bits: `0x21.3` = "OUT data phase pending", `0x21.4` = "IN data
phase pending".

## Class GET handlers

### GET Input Source @ 0x0073
Returns `0x01` (Analog) or `0x02` (S/PDIF) based on state bit `0x25.4`.
Matches Linux `snd_mbox1_is_spdif_input()` exactly.

### GET Clock Source @ 0x008A → jumps to 0x1009 when wValueH=0x01
Not yet fully traced. Should return 3 bytes; all-zero = S/PDIF locked,
nonzero = internal (per Linux `snd_mbox1_is_spdif_synced()`).

## Class SET data-phase handler @ 0x0D37
Runs when EP0 OUT completes with data. Uses `0x0D` (command code) to
route:

**SET Clock Source (0x0D == 1):**
Reads first payload byte from EP0 OUT buffer via `lcall 0x0B11`, then:
- byte == `0x44` (44100 & 0xFF) → state `0x0A = 0x07`  (44.1 kHz internal)
- byte == `0x80` (48000 & 0xFF) → state `0x0A = 0x08`  (48 kHz internal)
- byte == `0x00`                → state `0x0A = 0x06`  (S/PDIF slave)

**SET Input Source (0x0D == 2):**
Reads first payload byte, then:
- byte == `0x01` → state `0x0A = 0x04` (analog)
- else          → state `0x0A = 0x05` (S/PDIF)

Then clears `0x21.3 / 0x21.4` and re-arms EP0 IN status stage
(`IEPCNF0 |= 0x20`).

`0x0A` is the **main-loop action register**. Reset seeds it to `0x0E`
at `0x0006`. Main loop dispatches on `0x0A` and physically reconfigures
the audio path.

## I²C usage
Every access to `I²CADR (0xFFC3)` writes `0xA0` = 7-bit address `0x50`
(the boot EEPROM 24C64). **Rev 20 does NOT touch the CS8427 via I²C.**
The S/PDIF chip is either strap-configured on the board or driven via a
GPIO/mux + channel-status polling — need to confirm from the Mbox
schematic (not yet located).

I²C primitives:
- `0x0C00` — I²C **write** (device addr in R7, register in R3/RAM[5], value in R6).
- `0x0CEF` — I²C **read** (device addr in R7, register in R5, returns byte in R6/R7).

Uses standard TAS1020A I²C flow:
1. `[0xFFC0] &= 0xFC`  — clear low 2 status bits
2. `[0xFFC3] = 0xA0`   — target device
3. `[0xFFC1] = addr`   — subaddress
4. Poll `[0xFFC0].3`   — wait for ACK
5. `[0xFFC1] = data`   — payload
6. Poll `[0xFFC0].3`
7. Read: `[0xFFC0] |= 0x02` (start read), poll `.7`, read `[0xFFC2]`.

## EP0 helpers
- `0x0B17` — points DPTR at EP0 IN buffer (0xFA10-ish), used by GET handlers.
- `0x0B3E` — zeros `IEPBCTX0 (0xFF6B)` and `OEPBCTX0 (0xFFAB)`; clears
  IN/OUT enable bits `& 0xD7` in `IEPCNF0` and `OEPCNF0`. Prep for reply.
- `0x0B45` — commits the reply (kicks EP0 IN).
- `0x0B11` — points DPTR at EP0 OUT buffer, used by SET data-phase handler.
- `0x0B50` — sets `RAM[1B:1C] = 0xFA18` (EP0 OUT buffer base pointer).

## State byte reference (best guesses)
| RAM  | Purpose                                        |
|------|------------------------------------------------|
| 0x09 | EP0 IN transfer byte count (working)           |
| 0x0A | Main-loop action code (0x04-0x08 = mode set)   |
| 0x0B | EP0 IN transfer byte count (remaining)         |
| 0x0D | Pending class-request command (1=clk, 2=src)   |
| 0x18 | EP0 IN packet-fill counter                     |
| 0x19..0x1C | Source/dest pointer high/low bytes       |
| 0x21.3 | OUT data phase pending                       |
| 0x21.4 | IN  data phase pending                       |
| 0x25.4 | Current input source = S/PDIF                |
| 0x25.7 | I²C phase toggle (used in reg 0x0C57)        |

## Main loop @ 0x0AE5
Reset LJMPs 0x0000 → 0x0A09 (state clear), falls through to 0x0A1B
which zeroes IRAM 0..0x7F, sets `SP=0x33`, then `LJMP 0x0A50 → 0x0A95`
which runs a MOVC-driven config-block writer (walks a length-prefixed
byte stream and pushes bytes into IRAM or XDATA — probably USB descriptor
and endpoint init pulled from code space). Falls into the main loop at
`0x0AE5`:

```
0x0AE5: jb 0x24.0, 0x0AF1     ; EP0 event pending → handle in 0x0AF1
0x0AE8: mov a, 0x0A            ; load action byte
0x0AEA: jz 0x0AE5              ; idle → spin
0x0AEC: lcall 0x02EE           ; dispatch
0x0AEF: sjmp 0x0AE5
```

## Action-code dispatch — partial
`0x02EE` is only two bytes long (`sjmp 0x02F3 → ljmp 0x0B5F`) and
`0x0B5F` is just `setb 0x21.4; ret` — it flags "IN data phase
pending", not a real handler. The **actual** hardware-touching
dispatcher lives at `0x0300`:

```
0x0300: mov a, 0x0A ; dec a ; cjne a,#0x0E,... ; jc 0x030B
0x0308: ljmp 0x0564          ; overflow → soft-reset path
0x030B: mov dptr,#0x0300 ; A=3A ; jmp @a+dptr
```

Table entries (each LJMP is 3 bytes, base=0x0300, computed
`0x0300 + 3*(RAM[0x0A]-1)`):

| N (RAM[0x0A]) | LJMP target | Suspected role                       |
|---|---|---|
| 7  | 0x032A | ? (from earlier trace: 44.1k branch)     |
| 8  | 0x0386 | ? (48k branch)                            |
| 9  | 0x03FD | ?                                         |
| 10 | 0x0454 | ?                                         |
| 11 | 0x0466 | mode 5 = **input=S/PDIF** (clr 0x25.4)   |
| 12 | 0x0478 | mode 6 = **input=Analog** (setb 0x25.4)  |
| 13 | 0x0480 | R7=1 → `lcall 0x0728` (parameterized cfg)|
| 14 | 0x049A | `lcall 0x0568` (48k-specific?)           |

**Caveat:** the SET data-phase handler at 0x0D37 loads RAM[0x0A] with
codes 4-8, but the table above indexes 7-14. The offset suggests my
earlier decoding of the SET data-phase state-machine numbering is off
by 3, OR there's an additional queue that remaps command codes before
they hit the dispatcher. Confirming this needs an interactive CFG view
(load `rev20_firmware_code.bin` into r2 with `-a 8051 -b 8 -m 0` and
step through with `pdf`).

## Handler internals (confirmed)
- **0x0466 (input=S/PDIF suspected):**
  `clr 0x25.4 ; setb 0x22.6 ; lcall 0x0E62 ; lcall 0x0F0C ;
   mov r7,#0x08 ; lcall 0x0728`
  Consistent with GET Input Source at 0x0073 reading bit `0x25.4` as
  the analog/spdif discriminator (clear = spdif → returns 0x02).

- **0x0478 (input=Analog suspected):**
  `setb 0x25.4 ; clr 0x22.6 ; lcall 0x0E62 ; lcall 0x0F0C ;
   mov r7,#0x01 ; lcall 0x0728`
  Mirror of the above — sets 0x25.4 (analog).

- **`0x0728`** — parameterized "apply mode" subroutine, R7 = mode index.
  Called from every input/clock handler. Likely writes the audio path
  configuration (I²S/C-port + GPIO mux). Highest-value next target.

- **`0x0E62`** — called from every mode-change handler. Probably kicks
  the endpoint (halts/re-arms streaming endpoints when the sample rate
  or source changes).

## Physical hardware bindings still unknown
- Which port pin drives the S/PDIF-vs-analog input mux?
- Which pins read the front-panel source buttons + 48V switch?
- **What `0x0728` (parameterized by R7) actually writes to XDATA/ports.**
  This is the next-most-valuable trace.
- Where the C-port (I²S) sample-rate registers get programmed
  (`CPTCNF0..3 = 0xFFDB..0xFFDE`, `CPTBTRX/BTX = 0xFFD5/0xFFD6`).
