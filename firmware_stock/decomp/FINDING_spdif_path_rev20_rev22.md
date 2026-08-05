# How Rev 20 and Rev 22 actually do S/PDIF — the complete path

Reference-first RE for task #145, done before any mboxfw code is written. Every
address below is read from the Ghidra listings for both images; every CS8427
register meaning is from `reference/cs8427/alsa_cs8427.h`; every TAS register
meaning is from the datasheet sections already cited in `regs.h`.

**Headline: Rev 20 and Rev 22 do S/PDIF identically.** The one apparent
difference is a refactor, not a behaviour change — see §6, which is written up
because it took a helper-body read to see it and the wrong version of that
paragraph was one edit away from being committed here.

## 1. The three moving parts

S/PDIF on this board is three independent mechanisms that stock drives together:

| part | what it is | who controls it |
|---|---|---|
| **routing** | which source feeds the codec port | codec-word bit `0x25.4` |
| **clock** | where the codec's master clock comes from | `ACGCTL` + CS8427 `CLOCKSOURCE` |
| **channel status** | the rate the transmitter *declares* | CS8427 regs `0x23` / `0x24` |

Getting S/PDIF input working means all three, and the clock is the one that
cannot be skipped: the CS8427 has no sample-rate converter (that is the CS8420),
so received audio has to be clocked by the recovered clock.

## 2. Source selection — work codes 0x04 and 0x05

These are the host-driven Selector Unit handlers, and they are the entire
"switch to S/PDIF" path. Structurally identical in both images:

    Rev 20 cmd4 @0x0454            Rev 22 cmd4 @0x045A       ANALOG
      CLR  0x2c                      CLR  0x2c               ; 0x25.4 = 0
      SETB 0x16                      SETB 0x16               ; 0x22.6 = 1 (panel)
      LCALL 0x0e62                   LCALL 0x0e56            ; publish codec word
      LCALL 0x0f0c                   LCALL 0x0efc            ; publish panel word
      MOV  R7,0x08                   MOV  R7,0x08            ; the PERSISTED mode
      LCALL 0x0728                   LJMP 0x0512 -> 0x070f   ; apply it

    Rev 20 cmd5 @0x0466            Rev 22 cmd5 @0x0469       S/PDIF
      SETB 0x2c                      SETB 0x2c               ; 0x25.4 = 1
      CLR  0x16                      CLR  0x16               ; 0x22.6 = 0
      LCALL 0x0e62                   LCALL 0x0e56
      LCALL 0x0f0c                   LCALL 0x0efc
      MOV  R7,#1                     MOV  R7,#1              ; FORCE clock mode 1
      LCALL 0x0728                   LJMP 0x0512 -> 0x070f

Bit `0x2C` is IRAM `0x25.4` (bit addresses 0x28–0x2F map to 0x25.0–0x25.7). Two
facts fall straight out:

  * **Selecting S/PDIF as the source forces clock mode 1**, unconditionally and
    in the same handler. Routing and clocking are not separable in stock's design
    — which is correct, because they are not separable in the hardware.
  * **Selecting analog restores `RAM[0x08]`**, the persisted mode, rather than a
    constant.

> **CORRECTION, 2026-08-04, found while implementing #177.** The sentence that
> stood here — "the device remembers which internal rate it was on across an
> excursion to S/PDIF and back" — is **wrong**, and it is wrong in a way that
> would have been copied straight into mboxfw.
>
> Mode 1 writes `MOV 0x08,#0x1` at Rev 20 `0x0753`. So after any excursion to
> S/PDIF the persisted mode *is* 1, and cmd4's `MOV R7,0x08` re-applies the
> slaved clock. **Selecting analog does not restore an internal rate; it
> restores whatever mode ran last, which is mode 1.**
>
> Stock never shows this because the kernel quirk always follows a source
> change with an explicit set-clock-source request, so the stale value is
> overwritten before it matters. mboxfw keeps a separate `g_internal_rate`
> instead of mirroring `RAM[0x08]` here — see the comment on that variable in
> `usb.c`. Mirroring a write without mirroring what overwrites it mirrors
> nothing.
>
> The error came from reading cmd4 and cmd5 without reading the mode arms they
> dispatch into. Same shape as the Rev20-vs-Rev22 near-miss in §6: the fact was
> one indirection away from where I was looking.

### cmd6 — the host's third source-and-clock handler, and the one §7 needed

Omitted from the first version of this section, which covered only cmd4/cmd5 and
so left the "rate = 0 means slaved" encoding resting on the kernel quirk alone.
It is in the firmware, explicitly. Rev 20's SET_CUR data handler
`ep0_out_data_handler` @`0x0D25` dispatches on the pending-control byte
`RAM[0x0D]` and then on the payload's **low byte**:

    0d2a  CJNE A,#0x1,0x0D45      ; RAM[0x0D] == 1 -> sampling frequency
    0d32  CJNE A,#0x44,0x0D38  ->  0x0a = 7   ; 0x44 = low byte of 44100
    0d39  CJNE A,#0x80,0x0D3F  ->  0x0a = 8   ; 0x80 = low byte of 48000
    0d40  JNZ  0x0D45          ->  0x0a = 6   ; ZERO
    0d47  CJNE A,#0x2,0x0D59      ; RAM[0x0D] == 2 -> Selector Unit
    0d4e  CJNE A,#0x1,0x0D56  ->  0x0a = 4 (analog) else 5 (S/PDIF)

and cmd6 @`0x0478` is two instructions: `MOV R7,#1; LCALL 0x0728`. **A sample
rate of zero selects clock mode 1 and nothing else.**

That closes the loop the RE had left open. The kernel's
`snd_mbox1_set_clk_source(chip, 0)`, `setup_get_sample_freq` @`0x008A` reporting
0,0,0 whenever `RAM[0x08] == 1`, and this dispatch are three artifacts agreeing
on one encoding — and only the third of them is a *write* path, so without it
"rate 0 means slaved" was a read-side claim being used to justify a write.

Note also that stock tests only the low byte, so 0x10044 and 44100 are the same
request to it. mboxfw parses all three bytes (`usb.c`), which is a deliberate
divergence and pre-dates #177.

## 3. Clock modes — `audio_clock_mode_apply`, Rev 20 `0x0728` / Rev 22 `0x070F`

Every mode is bracketed by the `0x23.2`/`0x23.3` mute pair that #171 settled:

    0728  MOV 0x2e,R7                  ; save mode
    072f  CLR 0x1a ; CLR 0x1b          ; 0x23.2 / 0x23.3 -> MUTE
    0733  LCALL 0x0e62                 ; publish (mute takes effect)
    0736  DPTR=0xFFE2 ; LCALL 0x0e18   ; ACG1DCTL = 0x10, ACG2DCTL = 0x10
    073c  <dispatch on mode: 2, 3, 5, 1; anything else falls through>
    ...
    07c5  <shared tail>
    07c9  LCALL 0x0c45                 ; CS8427 write of the queued (reg,val)
    07cc  ACGCTL |= 0xC0               ; enable both MCLKO outputs
    07d3  IEPDCNTX1/Y1, OEPDCNTX2/Y2 = 0
    07e4  IEPCNF1 = 0xC5 ; OEPCNF2 = 0xC5
    07ee  SETB 0x1a ; SETB 0x1b        ; UNMUTE
    07f2  LCALL 0x0e62                 ; publish

The clock is disturbed only while muted, and the endpoints are re-armed before
the unmute. mboxfw's `streaming_set_rate()` already mirrors this shape.

### Mode 1 — the slave mode. This is S/PDIF sync.

    074d  ACGCTL (0xFFE1) = 0x0D
    0753  RAM[0x08] = 1
    0756  0x31 = 0x04 ; 0x32 = 0x41     ; queued for the shared tail

`ACGCTL = 0x0D` decodes (datasheet §6.5.3.11) as `MCLKO1S = 01`, `DIVEN = 1`,
`MCLKO2S = 01` — **both codec master clocks sourced from MCLKI**, the external
clock input. No frequency word is programmed, because nothing is being
synthesized.

`CLOCKSOURCE = 0x41` decodes as `RUN` | `RXD = 01` = `CS8427_RXDAES3INPUT`,
"256*Fsi from AES3 input" — **the PLL recovers its clock from the incoming
S/PDIF stream.** With `CONTROL1 = 0x01` (`SWCLK = 0`) from the boot init, RMCK
carries that recovered clock.

**These two writes are in the same basic block and are meaningless apart.** One
says "TAS, take your master clock from MCLKI"; the other says "CS8427, recover a
clock from AES3 and put it on RMCK". They are only jointly coherent if **RMCK is
wired to MCLKI on this board.** That is still a board inference — no schematic
has been read — but it is a much stronger one than "plausible": it is the only
wiring under which stock's own mode 1 does anything at all.

### Modes 2 and 3 — internal 44.1 kHz and 48 kHz

    mode 2 @0x075F   ACG1FRQ = 0x6A/0x4B/0x20   (0xFFE5/E6/E7)
                     ACG2FRQ = 0x6A/0x4B/0x20   (0xFFF7/F8/F9)
                     RAM[0x08] = 2 ; queue reg 0x04 = 0x40
    mode 3 @0x078E   both = 0x61/0xA8/0x0F via acg_set_freq_48k_family @0x0DEC
                     RAM[0x08] = 3 ; queue reg 0x04 = 0x40

Both fall into `acg_commit_and_ctl` @0x0E0F, which writes `ACGCTL = 0x06` =
`MCLKO1S = 00` (acg_clk), `DIVEN`, `MCLKO2S = 10` (acg2_clk) — **both clocks from
the internal synthesizers.** Note both ACGs get the *same* word; the two
synthesizers are programmed identically, not split.

`CLOCKSOURCE = 0x40` is `RUN` | `RXD = 00` = `CS8427_RXDILRCK`, "256*Fsi from the
ILRCK pin" — the receiver PLL follows the TAS-driven word clock instead of the
AES3 input. **0x40 vs 0x41 is the entire internal-vs-slaved distinction**, one
bit.

Note `acg_set_freq_48k_family` @0x0DEC *falls through* into `acg_commit_and_ctl`
@0x0E0F — the `MOV A,#0xf` at 0x0E0D is stored by the `MOVX @DPTR,A` at 0x0E0F.
This is the DPTR-arithmetic idiom CLAUDE.md warns about; a scanner that stops at
the function boundary loses the `ACGCTL` write.

### Mode 5 — present in the image, unreachable in the machine

Mode 5 @0x0799 clears `CPTEN`, writes `CPTRXCNF4 (0xFFD4) = 0x01`, restores
`CPTEN`, writes `0xFFF6 = 0x10`, and **sets `0x23.0` and `0x23.1`**.

Only work code 0x0A reaches mode 5, and nothing posts code 0x0A
(`WHAT_REMAINS_UNKNOWN.md` dispatch table: source *none*). So the bits are
written in the image but never at runtime.

> **Flag for `tools/latch_word_bit_diff.py`.** Its `EXPECTED_GAPS` records
> 0x23.0/0x23.1 as "dead in stock". That is true of *execution* and false of the
> *bytes* — `SETB 0x18` / `SETB 0x19` exist at Rev 20 0x07B8/0x07BA. The gate's
> conclusion stands; its wording invites a future reader to re-derive it wrongly
> from a byte scan. Worth a precision edit, not a behaviour change.

## 4. Channel status — what the transmitter declares

`cmd7` (mode 2) and `cmd8` (mode 3) each branch on the Selector bit `0x2C`:

    IF 0x25.4 == 1 (S/PDIF is the source):
        CS8427 reg 0x04 = 0x41      ; re-assert "recover clock from AES3"
        CS8427 reg 0x12 = 0x00      ; CSDATABUF control
    ELSE (analog is the source):
        CS8427 reg 0x23 = 0x00 (mode 2) / 0x40 (mode 3)
        CS8427 reg 0x24 = 0x80

Registers 0x20+ are `CS8427_REG_CORU_DATABUF`, the 24-byte channel-status / user
buffer, so reg `0x20 + n` is channel-status **byte n**:

  * **reg 0x23 = CS byte 3 = sampling frequency.** Top nibble MSB-aligned:
    `0x00` = 0000 = 44.1 kHz, `0x40` = 0100 = 48 kHz, matching IEC 60958
    consumer. The values track the clock mode exactly.
  * **reg 0x24 = CS byte 4 = word length**, written 0x80 in both rate branches.

The logic is exactly right: when the device is **internally clocked** it is the
master and must *declare* its rate downstream; when it is **slaved to S/PDIF** it
declares nothing and instead re-asserts the recovery source.

## 5. What stock does NOT do

Worth stating plainly, because it bounds what "port stock's behaviour" can mean:

  * **No lock detection.** `INT1MASK` (reg 0x09) is never written in either
    image, so the CS8427's INT pin can never assert. `RECVERRORS` (reg 0x10) is
    never read — stock cannot read the CS8427 at all, since its only readback
    probe is an EEPROM verify on the hardware I²C peripheral
    (`FINDING_cs8427_is_spi_not_i2c.md`).
  * **No fallback.** Nothing detects "slaved but no carrier" and reverts.
  * **No automatic switching.** Work codes 0x0B/0x0C are unreachable — P3.1 is
    TXD and never falls (`FINDING_p31_is_txd.md`, measured 2026-08-04).

So stock's S/PDIF is **entirely host-driven and entirely open-loop.** If the host
selects S/PDIF and no carrier is present, stock slaves to nothing and stays
there. That is the design, not a gap in the RE.

## 6. Rev 20 vs Rev 22 — identical behaviour, restructured code

The `cmd7`/`cmd8` bodies look different and are not:

    Rev 20  cmd8 analog branch:  0x2c=0x23 ; 0x2d=0x40 ; LCALL 0x0582
            0x0582 = serial_ctl_write_caller_pair_then_24_80
                     writes (0x2c,0x2d), THEN writes reg 0x24 = 0x80

    Rev 22  cmd8 analog branch:  0x2c=0x23 ; 0x2d=0x40 ; LCALL 0x0575
                                 0x2c=0x24 ; 0x2d=0x80 ; -> shared tail 0x0509

Rev 20 hides the `reg 0x24 = 0x80` write inside the helper; Rev 22 inlines it and
routes both writes through one tail. **Same two register writes, same values,
same order.** The same holds for the S/PDIF branch: Rev 20's
`serial_ctl_write_04_41_then_12_00` @0x0568 does both writes internally, while
Rev 22 splits it into `0x0567` plus `stage_ctrl_pair_12_00` @0x0FFA and the
shared tail.

Rev 22 also replaces several `LCALL x ; LJMP tail` pairs with `LJMP`/`SJMP` into
a merged tail (0x0509 / 0x0512), which is a size optimisation.

**Method note.** Reading only the `cmd7`/`cmd8` bodies gives "Rev 22 adds a
reg 0x24 = 0x80 write that Rev 20 lacks" — a clean, plausible, wrong
Rev20-vs-Rev22 behavioural difference. It survives until the helper *bodies* are
read. This is the same class of error as the four scanners that missed writes by
not following DPTR arithmetic: the write was real, it was just one indirection
away from where it was being looked for.

## 7. What mboxfw needs, derived from the above

**IMPLEMENTED 2026-08-04 in build 0x0020 (#177).** All four items below are in
the firmware; what remains is the hardware measurement. The one correction the
implementation forced on this document is the `RAM[0x08]` note in §2.

This is the specification the implementation followed.

1. **`clock_mode_1()`** — `ACGCTL = 0x0D`, no frequency word, CS8427
   `CLOCKSOURCE = 0x41`, inside the existing mute bracket. Cites Rev 20 0x074D /
   Rev 22 equivalent.
2. **Selector Unit control** — set/clear `0x25.4`, publish the codec word, and
   force mode 1 on S/PDIF / restore the persisted mode on analog. This is
   `cmd4`/`cmd5` and it is small.
3. **Channel-status writes** — reg 0x23 per rate and reg 0x24 = 0x80 when
   internally clocked; reg 0x04 = 0x41 and reg 0x12 = 0x00 when slaved.
4. **Host requests** — the four in `FINDING_host_control_protocol.md`, including
   the all-zeroes sample rate that means "S/PDIF-synced" and that the kernel
   quirk already expects.

**Safety property to preserve, and it is what makes this testable remotely:**
mode 1 must never be the boot default. The CPU is clocked from the oscillator,
not MCLKO, so if the RMCK→MCLKI inference is wrong the device loses audio but
keeps answering EP0 — recoverable with another vendor request, no power cycle.
Making slaving host-selectable only keeps that property.
