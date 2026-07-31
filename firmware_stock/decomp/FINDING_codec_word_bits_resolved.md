# Four codec-word bits resolved, and the bring-up sequence reads cleanly now

Follow-on to `FINDING_cs8427_is_spi_not_i2c.md`, 2026-07-30. Once IRAM 0x25.7 is
known to be the CS8427's chip select, the external-chip bring-up at Rev 20
`0x080B` stops being a list of unexplained bit pokes and becomes a textbook
sequence — and three more bits fall out of it. A fourth comes from the EP0
dispatcher.

All addresses read from `rev20_ghidra.txt` / `rev22_ghidra.txt`.

## The bring-up, annotated

`audio_path_reconfig_ext_chips` @ Rev 20 `0x080B` (Rev 22 `audio_hw_bringup`
@ `0x09B6`), called from `0x0360`, `0x0392`, `0x0419`, `0x04C7`:

    080b  CLR A
    080c  MOV 0x25,A     ; whole low byte 0 -> CS LOW, selector 0, ...
    080e  MOV 0x23,A     ; whole high byte 0 -> RESET LOW, mutes ASSERTED
    0810  SETB 0x2e      ; 0x25.6 = 1, the "bring-up has run" guard
    0812  delay
    0818  LCALL 0x0e62   ; publish -- everything held asserted
    081b  LCALL 0x0dec   ; 48 kHz frequency word
    081e  LCALL 0x0e17   ; ACG divider control
    0821  MOV 0x08,#0x3  ; persisted mode = 3
    0824  ACGCTL |= 0xC0 ; enable BOTH MCLKO outputs
    082b  delay          ; let the master clock settle
    0831  SETB 0x1a      ; 0x23.2  }  release the pair
    0833  SETB 0x1b      ; 0x23.3  }
    0835  LCALL 0x0e62   ; publish
    0838  delay
    083e  SETB 0x2f      ; 0x25.7 = 1  -- park CS high
    0840  SETB 0x1c      ; 0x23.4 = 1  -- release RESET
    0842  LCALL 0x0e62   ; publish
    0845  delay
    084b  CLR 0x2f       ; CS low   }
    084d  LCALL 0x0e62   ; publish  }  bare CS pulse, NO data clocked
    0850  SETB 0x2f      ; CS high  }
    0852  LCALL 0x0e62   ; publish  }
    0855  LCALL 0x08a6   ; first CS8427 register write

Rev 22 `audio_hw_bringup` @ `0x09B6` is the same sequence, instruction for
instruction, at: mute release `0x09D8`/`0x09DA`, CS park `0x09E3`, reset release
`0x09E5`, bare CS pulse `0x09EE`/`0x09F3`, then a run of `LCALL 0x0C31` —
`spi3wire_write_3bytes` — for the register writes. Its XREFs are `0x0366`,
`0x0396`, `0x0419`, `0x04CB`, matching Rev 20's four callers.

## 0x23.4 — external-chip RESET, active low. Released once.

Held low by the wholesale zeroing at `0x080E`, raised at `0x0840` after the
master clock is up and settled, immediately before the chip is addressed for the
first time, and never cleared anywhere else (single `SETB`, no `CLR`, in both
images). That is a reset line, and the ordering — clock first, then release
reset, then talk — is the order every codec and receiver datasheet asks for.

This supersedes the "moderate confidence, reset release or static enable" reading
in `FINDING_open_questions.md` §1.6. The confidence is now high, and the
candidate is named: it is released one settle-delay before the CS8427's own
mode-select pulse.

## The bare CS pulse at 0x084B/0x0850 — SPI mode select

`FINDING_open_questions.md` logged "the bare chip-select pulse at 0x084B" as an
open question, and `IRAM23_IRAM25_ANNOTATION.md` then said "It is not a chip
select: it is `CLR 0x2F`... what the bit means is still open."

Both are now answered, and the first one was right after all. It **is** a chip
select — `CLR 0x2F` *is* how this firmware drives it, because the select line is
a bit of the latch word. The pulse clocks **no data**: CS goes low, the word is
published, CS goes high, the word is published, and only then does the first
register write happen. A select strobe with no transaction in it is how a Cirrus
control port is told which serial mode it is in.

So the pulse is the CS8427 SPI-mode select, and it is independent corroboration
of `FINDING_cs8427_is_spi_not_i2c.md`: an I²C part would have no use for it.

## 0x23.2 / 0x23.3 — the mute pair, and the timing is now unambiguous

The pair is asserted by the zeroing at `0x080E` and released at
`0x0831`/`0x0833` — **after** `ACGCTL |= 0xC0` enables both MCLKO outputs and
**after** a settle delay. Released once the master clock is stable, asserted
before it is touched.

`FINDING_open_questions.md` §1.6 argued this from the clock-mode path alone and
called it moderate-to-high. The bring-up path shows the same shape independently,
so the reading — an output mute / audio-path enable pair, pop suppression — now
rests on two separate sequences in both images. Still not *named* by anything in
the firmware, so it stays a reading, not a determination.

## 0x25.4 — the UAC1 Selector Unit position: analog vs S/PDIF

This one is not a guess. `0x0073` is a **USB request handler**, and the listing
names it `setup_get_input_source`:

    0073  LCALL 0x0b3e   ; ep0_ptr_set_in_buf -> 0xFA18
    0076  JNB 0x2c,0x0081  ; 0x25.4
    0079  LCALL 0x0b17 ; MOV A,#0x2 ; MOVX @DPTR,A   -> report 2
    0081  LCALL 0x0b17 ; MOV A,#0x1 ; MOVX @DPTR,A   -> report 1
    0087  LJMP 0x0b45    ; ep0_send_1byte

The dispatcher at `0x003F` reads **0xFF28 = bmRequestType** (0xFF29 is bRequest,
as `0x0055` confirms) and compares by accumulated addition:

    0043  ADD A,#0xde    JZ 0x006b   ->  bmRequestType 0x22  class OUT, endpoint
    0047  ADD A,#0x81    JZ 0x0073   ->  0xA1  class IN,  interface
    004b  DEC A          JZ 0x008a   ->  0xA2  class IN,  endpoint
    004e  ADD A,#0x81    JZ 0x0055   ->  0x21  class OUT, interface

0xA1 is a class GET to an interface and 0xA2 a class GET to an endpoint — which
is exactly where UAC1 puts unit controls and sampling frequency respectively.
The reply is one byte, value **1 or 2**, which is a UAC1 Selector Unit's CUR:
a 1-based input-pin index.

`disasm/rev20_descriptors_decoded.md` already recorded the matching descriptor —
Selector Unit, `bNrInPins=2`, `baSourceID=[2 (Analog), 6 (S/PDIF)]` — and
already mapped bmRequestType 0x21 to `0x0055`. What had never been connected is
that **the stored position is IRAM 0x25.4**, and that 0x25.4 is bit 4 of the
codec word's low byte, so selecting the source shifts a bit out to real hardware
rather than only updating bookkeeping.

The write path completes the loop: `0x0055` posts work code 0x0D, and the EP0-OUT
completion posts work code **0x04** (`0x0454`, clears 0x25.4) or **0x05**
(`0x0466`, sets it), each of which also fixes 0x22.6 and republishes both words.

**0x25.4 = 0 → analog (pin 1); 0x25.4 = 1 → S/PDIF (pin 2).** Work code 0x05,
which sets it, also selects clock mode 1 — the external-clock mode — which is
what an S/PDIF input requires and is a consistency check on the polarity.

## 0x23.0 / 0x23.1 — set only by unreachable code. Always 0 in practice.

The pair has exactly one writer in each image: `SETB 0x18` / `SETB 0x19` at
Rev 20 `0x07B8`/`0x07BA` (Rev 22 `0x0796`/`0x0798`), inside the mode-5 branch of
`audio_clock_mode_apply`. Mode 5 is reached only by `MOV R7,#0x5` at `0x04BC`,
whose sole XREF is `0x031B`.

`0x031B` is a slot in `event_jump_table` @ `0x0300`. The table is 3-byte `LJMP`
entries indexed by work code minus one, so `0x0300 + 3*(code-1) = 0x031B` gives
**code 0x0A**. And a byte scan for the posting idiom finds:

    MOV 0x0a,#imm posted in Rev 20:  0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08
                                     0x0B 0x0C 0x0D 0x0E
    missing:                         0x09, 0x0A

**Nothing in either image ever posts work code 0x0A.** (0x09 was already known
dead — it passes mode 4, which the dispatch does not implement.) So the mode-5
branch never executes, and 0x23.0/0x23.1 are never set at runtime.

That completes the high byte. Two bits are always 0 because no instruction can
set them (0x23.5, 0x23.7); two more are always 0 because the only instruction
that sets them is unreachable (0x23.0, 0x23.1). **In stock, the codec word's high
byte is built from bits 2, 3, 4 and 6 only** — which is a useful simplification
for mboxfw, since it means the whole word is: mute pair, reset, mono.

For the record, what mode 5 would have done, since the branch is intact:

    0799  GLOBCTL &= 0xFE       ; CPTEN off
    07a0  CPTRXCNF4 = 0x01      ; receive path divider
    07a6  GLOBCTL |= 0x01       ; CPTEN back on
    07af  VECINT = 0
    07b2  ACG2DCTL = 0x10       ; program the SECOND synthesizer
    07b8  SETB 0x23.0 / 0x23.1
    07bf  RAM[0x08] = 5

i.e. put the capture path on ACG synthesizer 2 at a divided rate, independent of
playback, and tell the codec about it with the bit pair. That is consistent with
mode 5 being the "one IN and one OUT at different frequencies" case. It is a
reading of dead code and nothing depends on it.

## 0x25.5 — "the S/PDIF receiver has been engaged". Cleared by either stream.

Exactly five bit operations in each image, one-to-one (bit address 0x2D):

    CLR   Rev 20 0x0395   Rev 22 0x0399    cmd2_apply_iface1_alt
    CLR   Rev 20 0x041C   Rev 22 0x041C    cmd3_apply_iface2_alt
    SETB  Rev 20 0x04CA   Rev 22 0x04CE    cmd11, the P3.1-fall handler
    JNB   Rev 20 0x0E5C   Rev 22 0x0E50    source-cycle tail, channel 1
    JNB   Rev 20 0x0ECF   Rev 22 0x0EC1    source-cycle tail, channel 2

**It is never tested for a branch.** The only two `JNB`s are the pair that
computes `0x22.6 = !(0x25.4) && !(0x25.5)`. So inside the firmware, 0x25.5's
entire software effect is on that one panel bit; everything else it does, it does
as bit 5 of the codec word's low byte — a hardware line.

All three writers share one idiom, the bring-up guard on 0x25.6:

    JB 0x2e,<skip>     ; has bring-up run?
    LCALL 0x080b       ; if not, run it
    <skip>:
    CLR/SETB 0x2d

**Set** in `cmd11` (work code 0x0B — the handler the P3.1 *fall* posts), right
before clock mode 3 (internal 48 kHz) and the CS8427 `CLOCKSOURCE = 0x41` write
that sets RUN, i.e. starts the receiver.

**Cleared** in both stream-start paths, identified by the endpoint each goes on
to configure:

    cmd2 @ 0x0386  -> IEPCNF1 (0xFF60)  = SET_INTERFACE iface 1, CAPTURE
    cmd3 @ 0x03FD  -> OEPCNF2 (0xFF98)  = SET_INTERFACE iface 2, PLAYBACK

So the bit means **"the S/PDIF receiver has been started and we are running on
the internal clock while it settles"**, and starting either audio stream cancels
it. That is a real behavioural fact worth stating on its own: a host that begins
capture or playback drops the device out of S/PDIF-engaged state.

`0x22.6 = !(0x25.4) && !(0x25.5)` therefore asserts the panel line whenever
S/PDIF is in play **by either route** — the host selected it through the Selector
Unit (0x25.4), or the firmware engaged the receiver on the P3.1 event (0x25.5).
Two independent conditions OR-ed into one indicator.

That also dissolves the oddity flagged in `FINDING_cs8427_is_spi_not_i2c.md`:
`cmd11`'s direct `CLR 0x16` at `0x04FD` is just applying the derived rule
eagerly, since `cmd11` has already set 0x25.5 and the derivation is only
recomputed on a source-button press. It sits inside the EEPROM-verify branch,
which still looks like a stock bug — the eager apply has nothing to do with
whether the EEPROM verified — but the intent is now clear.

**What 0x25.5 physically drives is still a board question**, exactly like the six
one-cold source lines. The condition under which it asserts is fully determined;
the wire it asserts on is not.

## 0x22.7, found on the way — it tracks the capture stream

Two sites in Rev 20, both inside `cmd2_apply_iface1_alt`:

    CLR  0x17  @ 0x03A0   start branch (goes on to arm IEPCNF1)
    SETB 0x17  @ 0x03E6   stop branch, immediately after
                          DMACTL1 (0xFFEE) &= 0x7F clears capture DMAEN

So **0x22.7 is asserted (low) while the capture stream runs and deasserted when
it stops**, and nothing else in either image touches it. It tracks capture only —
`cmd3`, the playback path, never writes it.

`PANEL_LEDS.md` calls 0x22.7 a "run/stop-like line", which the condition now
confirms precisely. Whether it drives an LED, a mute, or something else remains
the board question; note only that stock's post-init `0x22 = 0xF6` leaves both
0x22.6 and 0x22.7 high, which is consistent with the observed idle panel however
they are wired.

## Status of the codec word after this pass

    0x23.0  RESOLVED — always 0: its only writer is in the mode-5 branch,
                       reachable only from work code 0x0A, which nothing posts
    0x23.1  RESOLVED — as above
    0x23.2  mute / audio-path enable pair (reading, two independent sequences)
    0x23.3  as above
    0x23.4  RESOLVED — external-chip RESET, active low, released once
    0x23.5  RESOLVED — provably always 0 (no bit op; every byte store is CLR A)
    0x23.6  RESOLVED — mono (#144)
    0x23.7  RESOLVED — provably always 0

    0x25.0-3 RESOLVED — the two per-channel source state machines
    0x25.4  RESOLVED — UAC1 Selector Unit position, analog vs S/PDIF
    0x25.5  RESOLVED — "S/PDIF receiver engaged": set only by the P3.1-fall
                       handler, cleared by either stream start, never branched
                       on. Physical target still a board question.
    0x25.6  RESOLVED — "bring-up has run" guard (set 0x0810, cleared 0x037B)
    0x25.7  RESOLVED — CS8427 chip select, active low

## Consequence for mboxfw

mboxfw models 0x23.2/0x23.3 and 0x23.6 only. It therefore never releases the
external chip from reset (0x23.4), never asserts the chip select (0x25.7), and
has no Selector Unit state (0x25.4) — while its descriptors, ported from stock,
advertise a Selector Unit the host can query. A host GET on that unit currently
has nothing behind it.

Ordering matters as much as the bits: stock releases the mutes only after the
master clock is stable, and releases reset only after that. Any implementation
that sets these bits at the wrong point in bring-up reproduces the GLOBCTL bit 1
lesson.
