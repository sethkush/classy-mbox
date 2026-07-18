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

## Bit-banged buses — verified via r2 auto-analysis ✅
Original flat disasm suggested entry points at `0x0C57` and `0x0E74`.
Interactive r2 (`r2 -a 8051 -b 8 -m 0 -c aaa`) shows those addresses
are **inside larger functions** — the actual entry points are:

- **`fcn.0x0C45`** — CS8427 bit-banged I²C write. **13 call sites.**
- **`fcn.0x0E62`** — codec bit-banged serial write + state
  propagation. **17 call sites.**

The P1-bit toggles previously identified are real, live within those
functions:

- **fcn.0x0C45 body** (CS8427 I²C):
  - `0x0C66: orl 0x90, #0x10` — P1.4 = 1 (SDA hi)
  - `0x0C6B: anl 0x90, #0xEF` — P1.4 = 0 (SDA lo)
  - `0x0C6E: orl 0x90, #0x08` — P1.3 = 1 (SCL hi)
  - `0x0C71: anl 0x90, #0xF7` — P1.3 = 0 (SCL lo)

- **fcn.0x0E62 body** (codec serial):
  - `0x0E7A: orl 0x90, #0x01` — P1.0 = 1 (data hi)
  - `0x0E7F: anl 0x90, #0xFE` — P1.0 = 0 (data lo)
  - `0x0E82: orl 0x90, #0x04` — P1.2 = 1 (clock hi)
  - `0x0E85: anl 0x90, #0xFB` — P1.2 = 0 (clock lo)
  - `0x0EA8: orl 0x90, #0x02` — P1.1 = 1 (latch strobe)
  - `0x0EAB: anl 0x90, #0xFD` — P1.1 = 0 (latch release)

The two "3-byte SPI-like" and "2-byte 16-bit" formats described below
still hold — those decoded from bit-loop counter and phase-machine
logic, and the r2 XREF graph confirms the surrounding bookkeeping.

### r2 command to reproduce
```
r2 -a 8051 -b 8 -m 0 firmware_stock/rev20_firmware_code.bin
> aaa
> afl        # list all discovered functions
> s 0x0c45 ; pdf   # CS8427 bit-banger
> s 0x0e62 ; pdf   # codec bit-banger + state prep
> axt 0x0c45 ; axt 0x0e62   # list callers
```

## I²C usage — hardware peripheral confirmed, others suspect
Rev 20 uses **at least one** confirmed I²C bus, and probably others
via bit-banged P1:

### Bus 1: hardware I²C peripheral (0xFFC0-C3) → boot EEPROM only
Every access to `I²CADR (0xFFC3)` writes `0xA0` = 7-bit address `0x50`
= the boot EEPROM 24C64. Read/write helpers at `0x0C00` / `0x0CEF`.
Used only for boot-time firmware load and mode-persistence.

### Bus 3: bit-banged serial on P1.0 / P1.1 / P1.2 → audio codec
Second bit-banger at `0x0E74`. Same rotate-and-clock pattern, but on
different pins:

- **P1.0 (SFR 0x90 bit 0)** = data (SDIN)
- **P1.2 (SFR 0x90 bit 2)** = clock (SCLK)
- **P1.1 (SFR 0x90 bit 1)** = latch pulse at end of transaction

Sends **2 bytes per transaction** (16 bits): first byte from `RAM[0x23]`,
second byte from `RAM[0x25]`. Loops through 8 bits per byte, then
pulses P1.1 as the latch/CS strobe.

Two-byte 16-bit control-word format matches the **Cirrus CS4272 (or
similar) audio codec** control register format. The codec's serial
control register write is: `1 R/W A6..A0 D7..D0` — 16 bits total.

The "prep bits" that get loaded into `RAM[0x22]` at `0x0EAF` (setting
`0x22.3/0x22.4/0x22.5` for one path, clearing them for the other, plus
propagating `0x25.4` into `0x22.6`) are the codec-register payload
bits — sample-rate, mute, input-select, etc.

### Bus 2: bit-banged software I²C on P1.3 / P1.4 → CS8427
**Discovered late** — this changes the earlier assumption that Rev 20
doesn't touch the CS8427. The bit-banger lives at `0x0C57`:

- **P1.3 (SFR 0x90 bit 3)** = SCL (clock line)
- **P1.4 (SFR 0x90 bit 4)** = SDA (data line)
- **RAM `0x25.7`** = idle/latch flag (probably /CS or START-condition flag)

The routine sends **3 bytes per transaction**:
1. `0x20` — the CS8427's 7-bit I²C write address `0b0010_000` shifted +
   R/W=0. (CS8427 with AD0=0 uses addresses 0x20/0x21.)
2. `RAM[0x33]` — the register subaddress (loaded from R7 at entry).
3. `RAM[0x01]` — the data byte.

Bit loop (per byte): rotates the byte MSB→LSB, sets P1.4 to the bit
value, then pulses P1.3 high/low as the clock. Between-byte and
start/stop delimiters use `lcall 0x0E62`, which is likely the port
bit-bang timing helper.

**This is the actual channel that programs the CS8427** — sample rate,
input select (analog TX vs external S/PDIF RX), mute, channel-status
bits. Every mode-change handler that calls `0x0728` will end up
generating a series of these 3-byte SPI-like packets to reconfigure the
CS8427 for the new sample rate / clock source.

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

## `0x0728` (`ApplyAudioMode(mode)`) — hardware config engine
Called by every mode-change path with `R7` set to a mode index.
Body starts at ~`0x0738` (bytes 0x0720-0x072F appear to be inline data
that r2 mis-decodes — the LCALL from mode handlers dispatches via the
`jmp @a+dptr` at 0x0728 into the real body). Confirmed side-effects on
XDATA:

**Streaming-endpoint config** (audio EP3, both directions):
- `IEPCNF3 (0xFF60) = 0xC5`   — enable IN EP3, IN-buffer valid
- `IEPBBAX3 (0xFF63) = 0`     — zero buffer offset lo (reset)
- `IEPBSIZ3 (0xFF67) = 0`     — reset byte-count
- `OEPCNF3 (0xFF98) = 0xC5`   — enable OUT EP3, ISO
- `OEPBBAX3 (0xFF9B) = 0`     — reset
- `OEPBSIZ3 (0xFF9F) = 0`     — reset

**C-port (I²S) / SOF-sync** (per-mode branches):
- Mode-5 branch (0x07AB): clears `GLOBCTL.0 (0xFFB1 &= 0xFE)`, writes
  `CPTCFG (0xFFD4) = 1`, re-enables `GLOBCTL.0 |= 1` → re-clocks C-port
  under a new master ratio.
- Common tail (0x07C5): `IEPBBAX2 (0xFFF6) = 0x10`, `DMACTL1 (0xFFE1) |=
  0xC0` (enable DMA0/1 channels).

**DMA channel setup** (per mode):
- Mode 1 (44.1k?): `DMACTL1 (0xFFE1) = 0x0D`, tag `RAM[0x08]=1`,
  I²C-persist target `(0x31, 0x32) = (0x04, 0x41)`.
- Mode 2 (48k?): writes `0xFFE5 = 0x4B`, `0xFFE6 = 0x6A`, `0xFFE7 = 0x20`
  (DMA source/dest ptrs) plus mirror `0xFFF7/8/9 = 0x4B/0x6A/0x20`
  (second DMA channel), tag `RAM[0x08]=2`.
- Mode 3 (S/PDIF slave?): just `lcall 0x0DEC` (probably kicks CS8427
  channel-status polling), tag `RAM[0x08]=3`.
- Mode 5: I²S clock re-init as above.

**EEPROM persist** (common tail 0x07D7):
`mov r5,0x32 ; mov r7,0x31 ; lcall 0x0C45` — writes the current mode
descriptor (2 bytes) to EEPROM 0x50 register 0x31, so the box comes up
in its last-used input/clock combination after a power-cycle. RAM 0x08
holds the current mode index.

## Reset / init block starting at `0x08DD`
Called during boot before the main loop enters:
- `GLOBCTL (0xFFFC) = 0`, `OEPCNF0-hi (0xFFB0) = 1` — USB glue.
- **`P1 (SFR 0x90) = 0x00`**, **`P3 (SFR 0xB0) = 0xFF`** — port pin
  boot state. **This is where the physical GPIO mapping lives.**
  Whichever P1/P3 bits later get toggled by the mode 4/5 handlers
  (input=analog vs S/PDIF) will name the mux GPIO.
- `TMOD (0x89) = 0x11`, `TH0 (0x8C) = 0xCE` — Timer 0/1 mode 1, 8-bit
  reload — this is the SOF/audio-tick timer.

## Key insight for custom firmware
**Rev 20 talks to the CS8427 via bit-banged I²C on P1.3/P1.4.**
All *audio streaming* configuration happens via TAS1020A internal
registers, and the S/PDIF chip is programmed alongside:
- **Endpoints 3 IN + 3 OUT** (`0xFF60/0xFF98`) carry USB isochronous audio.
- **DMA channels 0/1** (`0xFFE1-0xFFE7`) shuttle bytes between USB
  buffers and the C-port.
- **C-port** (`0xFFD4-0xFFDE`) is the I²S master, wired to the CS4272
  (or similar) codec and the CS8427 S/PDIF transceiver in parallel.
- The **analog-vs-S/PDIF input mux** must be a GPIO on P1 or P3 (not
  yet identified — needs the mode-4 vs mode-5 handler diff).

For a class-compliant replacement we can steal Rev 20's DMA + C-port
settings verbatim (they're not vendor-specific — they're the TAS1020A
UAC1 fast path).

**Full P1 pin map (confirmed):**
| Pin  | Function                                       |
|------|------------------------------------------------|
| P1.0 | Codec SDIN (bit-banged)                        |
| P1.1 | Codec latch/CS strobe                          |
| P1.2 | Codec SCLK (bit-banged)                        |
| P1.3 | CS8427 SCL (bit-banged I²C)                    |
| P1.4 | CS8427 SDA (bit-banged I²C)                    |
| P1.5 | toggled at 0x0F3C/F3F (unknown — LED? relay?)  |
| P1.6 | toggled at 0x0F4E/F51 + 0x0F22 (unknown)       |
| P1.7 | toggled at 0x0F34/F39 (unknown)                |

**Still unknown:**
1. What P1.5, P1.6, P1.7 do (LEDs, 48V relay, or other).
2. Front-panel button matrix (probably P3 inputs — read only, we've only
   seen P3 written to, not read).

## CS8427 boot sequence — DECODED ✅
`fcn.0x080B` runs the full CS8427 chip initialization at boot. Between
each phase it inserts a ~256-cycle `djnz` delay (needed for the CS8427
to internally settle after each register write). The sequence:

| # | Reg  | Value | Purpose (per Cirrus CS8427 datasheet)          |
|---|------|-------|------------------------------------------------|
| 1 | 0x04 | 0x00  | Clock Source Ctrl — reset (RUN=0)              |
| 2 | 0x13 | 0x10  | Channel Status Byte format                     |
| 3 | 0x04 | 0x00  | Clock Source Ctrl — still reset (re-armed)     |
| 4 | 0x04 | 0x40  | Clock Source Ctrl — RUN=1, clock enabled       |
| 5 | 0x01 | 0x01  | Chip Control 2                                 |
| 6 | 0x02 | 0x20  | Data Flow Control                              |
| 7 | 0x03 | 0x0C  | Clock Source Control 3                         |
| 8 | 0x05 | 0x05  | Serial Audio Input Format                      |
| 9 | 0x06 | 0x05  | Serial Audio Output Format                     |
| 10| 0x11 | 0xFF  | Interrupt Mask (enable all)                    |

**Calling convention for `fcn.0x0C45` (CS8427 write):** R7 = register
subaddress, R5 = value byte. Wire packet = 3 bytes over the bit-banged
I²C: `[0x20 (chip addr write)] [R7] [R5]`.

**Wrapper functions above `fcn.0x0C45`:**
- `fcn.0x0568` — pair-write (0x04=0x41, 0x12=0x00) — clock-source and
  audio-format setup for mode switching.
- `fcn.0x0582` — pair-write ([caller-reg]=[caller-val], 0x24=0x80) —
  parameterized register + channel-status byte-0 update.
- `fcn.0x08A6` — write CS8427[0x04] = 0x00 (reset clock control).
- `fcn.0x08B3` — write CS8427[RAM 0x2E] = 0x05 (used for regs 0x05/0x06).
- `fcn.0x08BD` — pass-through write (R7/R5 already set by caller).
- `fcn.0x08C4` — pass-through write (R7/R5 already set by caller).

This is enough to write the CS8427 side of custom firmware verbatim.
