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
