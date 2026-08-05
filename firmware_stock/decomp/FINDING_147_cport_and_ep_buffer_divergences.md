# #147 from the disassembly: stock's C-port steady state is not its boot state

2026-07-30. The 8-frame capture artifact has been attacked from the measured
data four times (`FINDING_capture_8frame_artifact.md` addenda 1-5) and from the
static register diff once. This is the first pass that traces the *runtime*
paths that touch the codec port and the streaming endpoints, rather than the
boot-init block, in both images.

It does not name the cause of the 5:3 framing. It does retire one suspect,
overturn a comment in `hw_init.c` that is wrong in every particular, and turn
up a second quantitative divergence in the same subsystem that nobody had
looked at, because every previous scan compared mboxfw against stock's *boot
init* and stock does not stay there.

Everything below is verified in **both** Rev 20 and Rev 22, by reading
`rev2{0,2}_ghidra.txt` and by byte-scanning `rev2{0,2}_firmware_code.bin`.

---

## 1. IRAM 0x21's low three bits, and one branch that is genuinely dead

`cmd1_apply_clock_mode`, `cmd2_apply_iface1_alt` and `cmd3_apply_iface2_alt`
all branch on bits 0x08, 0x09, 0x0A and 0x0E — IRAM 0x21 bits 0, 1, 2 and 6.
Exhaustive scan of every 8051 opcode that can write a bit (`SETB` D2,
`CLR` C2, `CPL` B2, `MOV bit,C` 92) plus every direct byte write to 0x21
(`F5/75/42/52/43/53/62/63 21` — none exist in either image):

| bit | IRAM | written by | meaning |
|---|---|---|---|
| 0x08 | 0x21.0 | `MOV 0x08,CY` — Rev 20 `0x02C3`, Rev 22 `0x02C1` | interface **1** alt setting != 0 |
| 0x09 | 0x21.1 | `MOV 0x09,CY` — Rev 20 `0x02D7`, Rev 22 `0x02D5` | interface **2** alt setting != 0 |
| 0x0A | 0x21.2 | **never set — CLR only** | — |
| 0x0E | 0x21.6 | `SETB` — Rev 20 `0x027E`/`0x028D`, Rev 22 `0x027C`/`0x028B` | configured (bConfigurationValue 1 or 2) |

0x08 and 0x09 are set by `MOV bit,C` where the carry comes from
`A(=wValue) + 0xFF` in `std_set_interface` — carry set iff alt >= 1. That
opcode is why an earlier scan of this file for `SETB` found nothing; a
`SETB`-only scan reports these three bits as constant 0 and every stream-start
path as dead code. They are not.

**Bit 0x0A is a different matter and really is constant 0.** IRAM 0x21 is
zeroed by the Keil C51 init table at Rev 20 `0x0F9C` / Rev 22 `0x0FBA`
(`01 21 00` = "IRAM[0x21] = 0"; the same table also seeds `IRAM[0x08] = 3`,
the default clock mode), and no instruction in either image ever sets bit 0x0A.
Every `JB 0x0a` is never taken and every `JNB 0x0a` always is.

## 2. Stock's running CPTCNF3/CPTRXCNF3 is 0xA8 on BOTH — 0xAC is boot-only

Rev 20 has a helper the access map records only as "write-computed":

    ; ======== FUNCTION codec_port_cfg3_commit @ CODE:0ff4 ========
    0ff4  f0        MOVX @DPTR,A        ; caller's DPTR = 0xFFDE  (CPTCNF3)
    0ff5  90ffd5    MOV DPTR,#0xffd5    ;                          CPTRXCNF3
    0ff8  f0        MOVX @DPTR,A        ; ...the SAME value
    0ff9  90ffb1    MOV DPTR,#0xffb1    ; GLOBCTL
    0ffc  e0        MOVX A,@DPTR
    0ffd  4401      ORL A,#0x1          ; CPTEN on
    0fff  f0        MOVX @DPTR,A
    1000  22        RET

Rev 22 has it verbatim at `0x0FE2`. **The transmit and receive halves of
CPTCNF3 are never programmed independently: one value goes to both.**

Its only two call sites are inside `cmd1_apply_clock_mode`, which runs on every
SET_CONFIGURATION (`std_set_configuration` posts work code 1 at Rev 20
`0x0293`):

    032a  LCALL 0x1001            ; DMACTL0 &= 0x7F   (playback DMA off)
    032d  DMACTL1 &= 0x7F         ; capture DMA off
    0334  GLOBCTL &= 0xFE         ; CPTEN OFF
    033b  JB  0x0a,0x0341         ; 0x0a == 0, not taken
    033e  JNB 0x0e,0x0365         ; unconfigured -> skip
    0341  JB  0x08,0x0365         ; a stream is running -> skip
    0344  JB  0x09,0x0365         ; a stream is running -> skip
    0347  JNB 0x0a,0x0352         ; 0x0a == 0, ALWAYS taken
    034a  MOV A,#0xac ; LCALL 0x0ff4   <-- UNREACHABLE
    0352  JNB 0x0e,0x035d         ; configured, not taken
    0355  MOV A,#0xa8 ; LCALL 0x0ff4   <-- the live one
    035d  ...                     ; CS8427 boot init if not already done

Rev 22 is the same shape at `0x0347`-`0x0360`.

So stock's sequence is: **stop both DMAs, drop CPTEN, write 0xA8 to CPTCNF3
*and* CPTRXCNF3, raise CPTEN.** The 0xAC written by boot init (Rev 20 `0x090B`
/ `0x0923`, Rev 22 `0x082C` / `0x0844`) survives only until the host's first
SET_CONFIGURATION, which every host sends before any audio.

0xAC and 0xA8 differ in one bit: BYOR (bit 2). **Stock runs with BYOR clear in
both directions.**

The datasheet agrees. Its I2S Mode 5 walk-through (SLES025B, the figure whose
callouts read `NTSL = 00001b (2)`, `TSL0L = 11b (32)`, `TSLL = 101b (32)`,
`BPTSL = 100b (24)`, `CSYNCL = 1, CSYNCP = 0`, `DDLY = 1`) lists **`BYOR = 0`**.
That is 0xA8 exactly, on the very configuration the Mbox uses.

### What this retracts

`FINDING_capture_8frame_artifact.md` Addendum 4 and 5 make CPTRXCNF3 the leading
suspect for #147, on the reasoning that stock is symmetric at 0xAC/0xAC and
mboxfw broke the symmetry at 0xAC/0xA8. The symmetry claim is right; the value
is not. **Stock is symmetric at 0xA8/0xA8 whenever audio is actually running**,
which is mboxfw's *receive* value. So:

  * `CPTRXCNF3 = 0xA8` in mboxfw **matches stock's operating state**. It is not
    the cause of the capture artifact and Addendum 5's "restore CPTRXCNF3 =
    0xAC" recommendation is withdrawn.
  * `CPTCNF3 = 0xAC` in mboxfw is the divergence — on the **playback** path,
    which has never been measured.

### What this breaks that was load-bearing elsewhere

The comment block in `mboxfw/src/hw_init.c` says:

> Rev 20 toggles CPTCNF3 at runtime by direction — 0xAC (BYOR=1) when capture
> is requested (@0x035C) and 0xA8 (BYOR=0) when playback is (@0x0367)

Wrong in the addresses (`0x034A` / `0x0355`), wrong in the condition (bit 0x0A
and bit 0x0E, neither of which is a direction), wrong that it is a per-direction
toggle at all (both writes go to both registers), and wrong that 0xAC is
reachable. Corrected in place.

It also breaks the inference chain that read `SNDRV_PCM_FMTBIT_S24_3BE` in the
Linux quirk table backwards into "BYOR=1 gives big-endian". Stock is observed
big-endian by Linux **while running BYOR=0**, so on the datasheet's own wording
— BYOR=0 "preserves" the received order, which for MSB-first I2S is big-endian
— BYOR=0 is the big-endian setting and BYOR=1 is the little-endian one. mboxfw
declares S24_3LE, so if the wire order matters it wants BYOR **set**, which is
the opposite of what `7590af1` reasoned. That is a playback/capture format
question, still unmeasured, and is left as task #161 rather than changed blind.

## 3. The endpoint buffers are 640 bytes in stock and 512 in mboxfw

`usb_ep_dma_init`, Rev 20 `0x0970` (Rev 22 `0x0898`), byte-identical between the
two images:

    0xFFA9 OEPBBAX0 = 0x42   -> 0xFA10      0xFFAA OEPBSIZ0 = 0x01  -> 8 B
    0xFF69 IEPBBAX0 = 0x43   -> 0xFA18      0xFF6A IEPBSIZ0 = 0x01  -> 8 B
    0xFF99 OEPBBAX2 = 0x44   -> 0xFA20      0xFF9A OEPBSIZ2 = 0x50  -> 640 B
    0xFF61 IEPBBAX1 = 0x94   -> 0xFCA0      0xFF62 IEPBSIZ1 = 0x50  -> 640 B
    0xFF9B OEPBBAY2 = 0                     0xFF63 IEPBBAY1 = 0
    0xFF98 OEPCNF2  = 0xC5                  0xFF60 IEPCNF1  = 0xC5

(BBAX is `(addr - 0xF800) >> 3`; BSIZ is in 8-byte units, all 8 bits, no hidden
flags — §6.4.4.4.)

Stock's two audio buffers are **640 bytes each, packed contiguously from 0xFA20
to 0xFF1F**, ending immediately below the setup-packet window at 0xFF28.

mboxfw (`regs.h:97-99`) uses **512 bytes each**, at 0xFB00 and 0xFD00, with the
stated reason "to fit both buffers below the 0xFF00 SFR boundary". There is no
SFR boundary at 0xFF00. The buffer space runs to 0xFF1F and stock uses all of
it; the 0xFF00 line is self-imposed and it cost 128 bytes on each buffer.

Why this belongs to #147 rather than to housekeeping: §6.4.4.4 says that for
**isochronous** transactions BSIZ sets the size of a *single circular buffer*,
not of an X/Y pair. At 48 kHz a USB frame is 288 bytes, so stock's 640 holds
2.2 frames and mboxfw's 512 holds 1.8 — it cannot contain two frames' worth at
any moment. Telemetry read `IEPCNF1 = 0xC5` mid-stream with OVF (bit 5) clear,
so no overflow was *flagged*; that is evidence against the simplest version of
this story and it is recorded here rather than left out.

This is an un-cited, unjustified, quantitative divergence in exactly the
subsystem #147 lives in, and `tools/rev20_diff_justifications.md` has no row for
it because every previous audit diffed SFR *writes* and both firmwares do write
these registers — with different values that no gate compares. Task **#162**.

## 4. mboxfw re-writes BBAX/BSIZ per stream; stock writes them once, ever

Stock's only writes to IEPBBAX1/IEPBSIZ1/OEPBBAX2/OEPBSIZ2 are the boot-init
ones above — the access map lists no others in either image, and it tracks DPTR
arithmetic, so this is not a windowing artefact. `cmd2_apply_iface1_alt`'s start
path sets `IEPCNF1 = 0xC5` and arms `DMACTL1 |= 0x80`; it never re-bases the
buffer.

`mboxfw/src/streaming.c:307-308` and `285-286` re-write `BBAX` and `BSIZ` on
every `streaming_{capture,playback}_enable(1)`. The DMA read pointer
(Ch1RdPtr, 0xFFB5/0xFFB6) and the UBM write pointer (Ch1WrPtr, 0xFFB7/0xFFB8)
are **read-only** — there is no documented way to reset them from the MCU. So
re-declaring the buffer under live pointers leaves them wherever they were.

That mechanism predicts a deterministic, constant, sample-clock-locked region
of the buffer that the DMA never refills and the UBM keeps transmitting, which
is the *character* of the measured artifact. It does not predict its 48-byte
period, and no register in the C-port or DMA group has been found that carries
an 8-frame quantum. Stated as a lead, not a diagnosis. Task **#163**.

## 5. What is now known about stock's stream start, for the record

`cmd2_apply_iface1_alt` (Rev 20 `0x0386`), start path, in order:

    if (!bit 0x2e) LCALL 0x080b     ; CS8427 boot sequence, once
    CLR 0x2d
    MOV 0x22,#0xFF                  ; then CLR 0x10, 0x13, 0x17  -> mux = 0x76
    CLR 0x1e                        ; mono off
    LCALL 0x0f0c                    ; publish the mux word
    CLR 0x28..0x2c
    LCALL 0x0e62                    ; publish the codec word
    IEPCNF1 = 0xC5
    R7 = 3 ; LCALL 0x0728           ; apply clock mode 3 (internal 48 kHz)
    DMACTL1 |= 0x80                 ; arm capture DMA
    if (configured) { OEPCNF2 = 0xC5 ; DMACTL0 |= 0x80 }

Two things worth keeping. `MOV 0x22,#0xFF` with bits 0, 3, 7 cleared leaves
0x76 — **both channels forced to MIC** (0x76 & 7 = 6, (0x76 >> 3) & 7 = 6) on
every stream start, which is where mboxfw's boot-to-MIC behaviour comes from and
why `BENCH_WIRING.md`'s LINE loopbacks need `TLM_REQ_SET_MUX`. And interface 1
alt != 0 arms **both** DMAs, capture and playback together; stock has no
independent playback start.

## 5a. One naming correction, en route

`regs.h` calls 0xFFDC `CPTSTA`. §6.5.4.5 names it **CPTCTL**, "Codec Port
Interface Control and Status Register", and the 0x50 both firmwares write is
`RXIE | TXIE` — it *enables* the C-port receive-full and transmit-empty
interrupts. Calling a register "STA" invites the next reader to take that write
for a status clear. Parity with stock either way; task **#164**.

## 6. What #147 still needs

Nothing above accounts for a period of exactly 8 audio frames. The registers
that set framing geometry — CPTCNF1 `NTSL = 2, MODE = 5`, CPTCNF2 `0xE5`,
CPTRXCNF2 `0x25` (24 data bits in 32-clock slots, 2 slots), CPTCNF4 `DIVB = /4`,
CPTRXCNF4 `DIVB2 = /4`, DMATSH1 `BPTS = 3 bytes`, DMATSL1 `slots 0+1`,
IEPCNF1 `BPS = 6 bytes` — are all byte-identical to stock and all decode
consistently to 6 bytes per frame at Fs. The measured byte throughput confirms
LRCK2 is at the right rate. The corruption is inside the data, not in the frame
rate, and no configuration bit in either group has an 8-frame quantum.

The two register changes this pass produced (CPTCNF3, buffer size/layout) are
cheap and both go in the same flash. Whether either moves the artifact is a
hardware question; neither is being shipped on the strength of "stock does it".

---

# Part 2: what the constant's bit pattern says, and six mechanisms it kills

Added the same day, after the register work above. No new measurement — this is
the existing 18-byte constant read as bits instead of as samples, plus the
arithmetic each candidate mechanism has to satisfy.

## The constant is not audio, mis-ordered or otherwise

    00 00 80  00 00 80  ff ff 7f  ff ff 7f  00 00 80  00 00 80
       A         A         B         B         A         A

Two facts, both mechanical:

**B is the exact bitwise complement of A.** `~0x800000 & 0xFFFFFF == 0x7FFFFF`.
So the six words are `A A ~A ~A A A` — not two unrelated rail values.

**Every one of the six words has exactly one bit disagreeing with the other
23.** In binary, MSB first:

    100000000000000000000000     first bit 1, remaining 23 all 0
    011111111111111111111111     first bit 0, remaining 23 all 1

So each word is `[x][c × 23]` with `x = ~c`, and `c` is constant across both
slots of a frame and inverts between frames.

Read on the wire that is: **the CDATI line sits at one static logic level for
the whole of each 24-bit word, that level flips every frame, and the word
boundary is off by exactly one bit clock.** There is no audio in it, at any byte
order, at any word alignment. A line carrying real samples cannot produce 23
identical bits followed by 23 identical opposite bits, 24,000 times without
variation.

The one-bit disagreement at the boundary is a **framing offset of exactly one
SCLK**, which is the DDLY/SODEL question: the TAS is programmed DDLY = 1
(CPTRXCNF3 bit 7) and reads the first bit from a clock where the line is still
at the previous level.

## Six mechanisms that cannot produce 5-good-of-8, with the arithmetic

**1. A codec frame-length mismatch.** Our frame is 64 SCLK (NTSL = 2 slots ×
TSLL = 32). Eight of our frames = 512 SCLK. For the codec to have produced
exactly 5 frames in that window its frame would have to be 512/5 = **102.4
SCLK**. Non-integral, so no codec frame length gives this ratio — and the same
holds at 44.1 kHz, where the ratio is measured identical.

**2. A master/slave mixup on the receive clocks.** CPTRXCNF3 = 0xA8 has bits 1:0
(CSCLKD, CSYNCD) clear, so §6.5.4.12 says SCLK2 and LRCK2 are **outputs**. The
codec is a slave and is obliged to emit exactly one frame per LRCK2. Its frame
rate cannot be wrong. Combined with the measured byte throughput (exactly
44100 × 6 × 5 bytes in 5 s), the frame rate is confirmed correct on both sides.
**The corruption is inside the data, not in the timing.**

**3. Stale bytes in the endpoint circular buffer.** A region of the buffer the
DMA never refills would reappear in the output with a period of BSIZ = 512
bytes, and the UBM read pointer advances 288 bytes per USB frame at 48 kHz —
288 mod 512 ≠ 0, so the artifact's phase would shift every single frame.
Measured: **4 phase discontinuities in 220,500 frames.** Killed.

**4. C-port secondary communication stealing a slot.** CPTCNF4 = 0x03 sets
ATSL(3:0) = 0000b = **time slot 0** (§6.5.4.4), and CPTCTL = 0x50 enables both
C-port interrupts (RXIE | TXIE, VECINT 0x18/0x19). A secondary-communication
transaction would therefore corrupt **slot 0 only** — the left channel. The
artifact hits both channels equally and phase-locked. Killed.

**5. A walking/slipping sample window.** TSLL = 32 clocks holds BPTSL = 24 data
bits, leaving 8 pad clocks; a window slipping 2 clocks per slot would realign
every 16 slots = 8 frames, which is exactly the observed period and is why this
looked promising. It fails on content: a window straddling the data/pad boundary
yields words that are **part audio and part idle**. Every corrupt word here is
the pure constant and every clean word is plausible audio, with no gradient at
either edge. Killed.

**6. Byte order, slot geometry, word length, DMA slot mask, bytes per sample.**
CPTCNF1 (NTSL = 2, MODE = 5), CPTCNF2 = 0xE5, CPTRXCNF2 = 0x25 (24 data bits in
32-clock slots), CPTCNF4 (DIVB = ÷4), CPTRXCNF4 (DIVB2 = ÷4), DMATSH1
(BPTS = 3 bytes), DMATSL1 (slots 0+1), IEPCNF1 (BPS field 5 = 6 bytes) are all
byte-identical to stock and all decode consistently to 6 bytes per frame at Fs.
ACG1DCTL = ACG2DCTL = 0x10 is DIVM = ÷2, DIVI = ÷1, and the chain
acg → ÷2 → MCLKO → ÷4 → SCLK → ÷64 → LRCK lands on Fs. Nothing in this group
carries an 8.

## What survives: a second driver on CDATI, and it is unconfigured

The surviving shape is the one the bit pattern points at directly — for 3 frames
in 8 the codec is **not the thing driving CDATI**, and the line is held static
(undriven, or driven by something else) with a one-clock framing offset.

There is a second device on that net, and stock's own CS8427 programming says
so:

  * `DATAFLOW` (reg 0x03) = 0x0C → SPD(2:1) = 10b = `CS8427_SPDAES3RECEIVER`.
    Stock routes the **AES3 receiver** — S/PDIF in — to the CS8427's *serial
    audio output port*.
  * `SERIALOUTPUT` (reg 0x06) = 0x05 → SOMS = 0 = **slave**. That output port
    takes its OSCLK/OLRCK from outside, and the only I2S clock master in the
    system is the TAS (see mechanism 2 above).
  * `CLOCKSOURCE` (reg 0x04): stock writes 0x00, then 0x00 again, then **0x40 =
    `CS8427_RUN`** — an explicit clock-off → clock-on cycle.

So the CS8427 shifts S/PDIF audio out under the TAS's own SCLK2/LRCK2. That is
also the only way stock's source selector can work at all: IRAM 0x25.4 switches
between analog and S/PDIF and stock performs **no C-port reconfiguration** on
that event (`std_set_interface` posts work codes 2/3; neither touches
CPTCNF/CPTRXCNF). Both sources must already land on CDATI, with something
gating which one drives. This is an inference from the register programming plus
the descriptors' Selector Unit topology (`baSourceID = [2 Analog, 6 S/PDIF]`),
not from a schematic — we do not have one.

**And on mboxfw the CS8427 has never been configured at all.** Per
`FINDING_cs8427_is_spi_not_i2c.md` (#157), `cs8427.c` frames its writes as I2C
on P1.3/P1.4 where the part is 3-wire SPI, and it never drives the chip select
(IRAM 0x25.7, published through the 16-bit latch). Not one of the ten register
writes reaches the part. Whatever state the CS8427 powers up in is the state it
is in while these measurements were taken — including RUN, which stock takes the
trouble to cycle off and on.

This matches every measured property that the register-level candidates did not:

| measured | accounted for by |
|---|---|
| byte-identical in all 24,000 occurrences | a slave output port shifting a static register under an external clock |
| locked to the sample clock, not to USB | OSCLK/OLRCK come from the TAS |
| both channels equally, phase-locked | one device drives both slots |
| present with nothing playing | independent of the USB data path |
| ratio identical at 44.1 and 48 kHz | a divider ratio, not a frequency |
| one-bit word-boundary offset | SODEL vs the TAS's DDLY = 1 |

**What it does not account for is the 3-in-8 duty**, and I am not going to
invent a reason for it.

**PARTLY SUPERSEDED, 2026-07-31.** The CS8427 datasheet is now in hand and the
"unconfigured CS8427 is a second driver" reading does not survive it. RUN
defaults to 0, and §15.1 says the serial audio outputs are enabled only *after*
RUN is set and the PLL settles — so an unconfigured CS8427 is a **silent** part,
not a contending one. SOMS also defaults to slave, so it cannot fight on the
clock lines either, and SDOUT has no tri-state bit at all, which means two
permanently-driven sources cannot share CDATI and the analog/S-PDIF selection
must be an external switch. Separately, whether mboxfw's register writes land at
all now turns on a bus-mode question that was not previously asked. See
`FINDING_cs8427_chip_select_never_driven.md` and #165 — and then
`FINDING_cs8427_held_in_reset.md`, which supersedes both: mboxfw never sets
IRAM 0x23.4, so the external-chip RESET is held asserted for the life of the
firmware, which per §15.1 resets the CS8427's control port and registers
continuously and mutes its outputs. **The CS8427 is out as a #147 candidate
entirely**, and the "second driver on CDATI" reading has no candidate left.

## The test

#157 is already scoped and is the decisive experiment: rewrite `cs8427.c` to
stock's SPI framing so the ten register writes actually land, and re-measure. If
the artifact is the CS8427, it moves. If it does not move, the second driver is
the codec itself and the CS8427 is exonerated — which is also worth knowing, and
either outcome costs the same single flash.

A second discriminator rides along free once #157 is in: with DATAFLOW pointing
the CS8427's output at the AES3 receiver, driving the source selector (IRAM
0x25.4 / `TLM_REQ_SET_MUX`) should **change** the artifact if two drivers share
the net, and leave it untouched if they do not.

---

## RESOLVED 2026-08-04 — §3 and §4 shipped as #162 and #163 (build 0x0023)

Both divergences this document raised are now closed in code.

**§3, the 640-vs-512 size and the layout.** `EP_AUDIO_BUF_SIZE` is 0x280 and
the bases are stock's: EP2 OUT at 0xFA20, EP1 IN at 0xFCA0, contiguous through
0xFF1F. The "0xFF00 SFR boundary" this document called self-imposed was worse
than that — the comment asserting it listed a free tail at 0xFF00-0xFF27 three
lines below, contradicting itself in the same breath. Datasheet Figure 6-3
gives the region as 0xFA64-0xFF27 for endpoint data buffers, with the setup
packet buffer at 0xFF28-0xFF2F above it.

**§4, the re-declaration under live pointers.** The four base/size writes moved
out of `streaming_playback_enable()` / `streaming_capture_enable()` and into
`usb_ep0_setup()`, which is where stock has them (Rev 20 fcn.0x0970, Rev 22
fcn.0x0891) and which mboxfw already re-runs on resume exactly as stock does.
The stream-arm path now touches only xEPCNF and DMAEN, matching stock.

**An unusually strong check fell out of it.** SDCC's emitted code for the four
assignments is **byte-for-byte identical to stock's 22-byte block** at Rev 20
0x099F:

```
stock  0x099F:  90 ff 99 74 44 f0  90 ff 61 74 94 f0  90 ff 9a 74 50 f0  90 ff 62 f0
mboxfw 0x0BBD:  90 FF 99 74 44 F0  90 FF 61 74 94 F0  90 FF 9A 74 50 F0  90 FF 62 F0
```

Same registers, same order, same accumulator reuse for the second BSIZ write —
SDCC and Keil converged independently. That also explains why the access map
reports `IEPBSIZ1` as "write-computed" in *both* images rather than as a
literal 0x50: the constant is carried in `A` from the preceding write, so it is
not adjacent to the store. `diff_vs_rev20.py` now counts all four as matches
(73, up from 69; changed addresses 25 -> 21).

**What this does NOT establish.** The prediction in §4 — that re-basing under
read-only DMA/UBM pointers leaves a constant, sample-clock-locked region the
DMA never refills — is a prediction, not a diagnosis, and it never matched the
artifact's 48-byte period. Closing the divergence removes it as a variable; it
does not confirm it was the cause. The next capture decides that.
