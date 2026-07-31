# IRAM 0x22 — the input source mux / panel shift word

Derived by byte-scanning BOTH stock images for every instruction that touches
direct address 0x22 and every bit operation on bit addresses 0x10-0x17 (the
bits of byte 0x22), then reading the Ghidra listing at each site. Every claim
below cites Rev 20 and Rev 22 addresses.

Method note: the scan enumerates instruction *starts* as decoded by the
recursive-traversal Ghidra listing, so operand bytes that happen to equal 0x22
are not counted. Both images produced exactly 4 byte-write sites and 33 bit-op
sites, in one-to-one correspondence — the two images do the same things at
shifted addresses.

## The shift-out routine

    Rev 20  0x0F0C   MOV R6,#0x8       Rev 22  0x0EFC
            0x0F0E   MOV R5,0x22               0x0EFE  MOV R7,0x22
            0x0F10   ANL 0x90,#0xBF            (P1 &= ~0x40)

`0x90` is P1. The routine loads 0x22, drops P1.6, and clocks 8 bits out — this
is the panel/mux shift register on P1.5/6/7. It is the ONLY consumer of 0x22:
there is no `MOV A,0x22`, no `MOV 0x22,dir`, no `PUSH 0x22`, and no `MOV
Rn,#0x22` anywhere in either image, so 0x22 is never read indirectly either.

Callers (Rev 20): 0x03A2, 0x03E8, 0x045B, 0x046D, 0x04FF, 0x0943, 0x0964,
0x0AE6.

## Bit map

**The whole byte is ACTIVE-LOW** — see the dedicated section below, which is what
makes the rest of this map read cleanly. A bit CLEAR means the line is asserted.

    b0   ch1 MIC          b3   ch2 MIC
    b1   ch1 LINE         b4   ch2 LINE
    b2   ch1 INST         b5   ch2 INST
    b6   external (S/PDIF) clock in use
         = !(IRAM 0x25.4) && !(IRAM 0x25.5), so asserted when either is set
    b7   streaming active
         cleared (asserted) on stream start  — Rev 20 0x03A0 / Rev 22 0x03A4
         set     (released) on stream stop   — Rev 20 0x03E6 / Rev 22 0x03EA

Each source field holds exactly one bit low ("one-cold"), so the three patterns
are three per-source lines rather than a binary code.

Bit 6 is derived, not stored independently. Rev 20 0x0E52-0x0E61:

    0e52  JB  0x2c,0x0e57     ; 0x2c = IRAM 0x25.4
    0e55  SETB 0x16           ; 0x22.6 = 1   (when 0x25.4 clear)
    0e57  JNB 0x2c,0x0e5c
    0e5a  CLR 0x16            ; 0x22.6 = 0   (when 0x25.4 set)
    0e5c  JNB 0x2d,0x0e61     ; 0x2d = IRAM 0x25.5
    0e5f  CLR 0x16            ; 0x22.6 = 0   (when 0x25.5 set)
    0e61  RET

Rev 22 identical at 0x0E46-0x0E55.

## The three source patterns

Both images use exactly three 3-bit patterns, written bit-by-bit:

    0x05  (b2=1 b1=0 b0=1)   Rev 20 0x0E2E-0x0E32   Rev 22 0x0E22-0x0E26
    0x03  (b2=0 b1=1 b0=1)   Rev 20 0x0E40-0x0E44   Rev 22 0x0E34-0x0E38
    0x06  (b2=1 b1=1 b0=0)   Rev 20 0x0E4C-0x0E50   Rev 22 0x0E40-0x0E44

and the same three on bits [5:3] for channel 2:

    0x05   Rev 20 0x0EA4-0x0EA8   Rev 22 0x0E96-0x0E9A
    0x03   Rev 20 0x0EB3-0x0EB7   Rev 22 0x0EA5-0x0EA9
    0x06   Rev 20 0x0EBF-0x0EC3   Rev 22 0x0EB1-0x0EB5

The set {0x05, 0x03, 0x06} matches what `mboxfw/src/buttons.c` `cycle_source()`
knows. That much of mboxfw is right.

**The mapping IS deducible.** Two facts do it:

  1. Stock's hw_init leaves 0x22 = 0xF6, i.e. pattern 0x06 on both channels
     (see the corrected boot-value section below), and leaves the state bits at
     (0,0) -- Rev 20 0x03A5-0x03AD / Rev 22 0x03A9-0x03B1 clear 0x25 bits
     0,1,2,3,4 explicitly.
  2. Seth reports the hardware boots to MIC, and the source button cycles
     mic -> line -> inst -> mic.

From state (0,0) the machine below emits 0x05 on the first press, 0x03 on the
second, 0x06 on the third. Boot is 0x06 and boot is mic, so:

    0x06 = MIC     (boot value)
    0x05 = LINE    (first press)
    0x03 = INST    (second press)

`mboxfw/src/buttons.c` asserted 0x05=mic, 0x06=line, 0x03=inst. **mic and line
were swapped there.** Combined with the cycle-order divergence below, mboxfw
walked mic -> inst -> line where stock walks mic -> line -> inst. Both are
**fixed** as of 2026-07-29; the corrected `cycle_source()` cites the byte
sequences above.

## The state machine, and a divergence

Rev 20 0x0E27 (called from 0x0EF4), channel 1. State is two bits, IRAM 0x25.0
(bit addr 0x28) and 0x25.2 (bit addr 0x2a):

    0e27  JB  0x28,0x0e36
    0e2a  SETB 0x28 / SETB 0x2a   -> pattern 0x05
    0e36  JNB 0x28,0x0e48 ; JNB 0x2a,0x0e48
    0e3c  SETB 0x28 / CLR 0x2a    -> pattern 0x03
    0e48  CLR 0x28 / CLR 0x2a     -> pattern 0x06

So the stock cycle is

    0x05  ->  0x03  ->  0x06  ->  0x05

`cycle_source()` in `mboxfw/src/buttons.c` implemented

    0x05  ->  0x06  ->  0x03  ->  0x05

**The middle two positions were swapped.** Pressing a source button on mboxfw
therefore landed on a different source than stock would from the same starting
point. Fixed 2026-07-29.

## Boot values — corrected 2026-07-29

**This section previously got the site attribution wrong in three ways, and the
errors mattered because a defect list was built on them. All three are
retracted below.**

Four sites write 0x22. Listed in the order the boot path reaches them:

    site                    Rev 20            Rev 22            result
    hw_init, 1st publish    0x093F            0x0860            0x00
      (CLR A / MOV 0x22,A, then SETB bit 0x1e = 0x23.6 -> mono ON)
      publish: LCALL 0x0F0C at Rev 20 0x0943 / Rev 22 0x0864
      then a delay loop on RAM[0x2E]:RAM[0x2F]
    hw_init, 2nd publish    0x095B + clears   0x087C + clears   0xF6
      (CLR b0, b3, and CLR bit 0x1e -> mono OFF)
      publish: LCALL 0x0F0C at Rev 20 0x0964 / Rev 22 0x0885
    SET_INTERFACE, stream   0x0397 + clears   0x039B + clears   0x76
      START branch            (CLR b0, b3, b7, and CLR bit 0x1e)
      publish: LCALL 0x0F0C at Rev 20 0x03A2
      (this row said "alt teardown" until the active-low section below
       corrected it -- 0x0397 is the stream-START branch)
    suspend                 0x053B            0x053A            0xFF
      (no bit clears follow; then PCON |= 0x01)

**Retraction 1.** This document called 0x0397 the "boot init". It is not:
0x0397 sits inside `cmd2_apply_iface1_alt` @ Rev 20 0x0386 (Rev 22 0x038A),
the SET_INTERFACE alt-setting handler. 0x76 is a streaming state, not a boot
state. (This first read it as "teardown"; the branch analysis in the active-low
section below shows it is the stream-START branch.) The real boot value is **0xF6**, from what this document called the
"second init" — which is the ONLY immediate write to this byte inside stock's
master hw init (Rev 20 fcn.0x08CB / Rev 22 fcn.0x07EC).

**Retraction 2.** "Neither image ever writes 0x00 to this byte" is false. Both
do, at Rev 20 0x093F and Rev 22 0x0860, as the first of hw_init's two
publishes.

**Retraction 3.** That write was listed as "a computed store, not yet traced".
It is traced: the instruction immediately before it is `CLR A`, so it stores
zero. With `SETB 0x1e` following, the pair is a deliberate all-on panel flash
(0x00 selects nothing on the source fields, mono asserted), held for the
delay-loop interval, and then replaced by the real 0xF6 / mono-off state.

Decoding the actual boot value 0xF6:

    bits [2:0] = 6  -> pattern 0x06 on channel 1  = MIC
    bits [5:3] = 6  -> pattern 0x06 on channel 2  = MIC
    bit 6 = 1, bit 7 = 1

The mapping conclusion above is unaffected: 0x76 and 0xF6 differ only in bit 7,
and both carry pattern 0x06 on both source fields.

**Consequence for the defect list.** `hw_init.c` was flagged as defective for
booting `g_mux_state = 0x00` and for that value being "not any of the three
legal patterns". Both charges are dropped — mboxfw's two-publish sequence
(0x00 with mono set, delay, 0xF6 with mono clear) already matched stock exactly.
The only change made there was the `g_phantom_48v` -> `g_mono` rename.

## Consequences for the capture measurements

The 44.1 kHz loopback on 2026-07-29 cannot be used as evidence about the audio
path, the #147 mute bits, or anything else — but **not** for the reason first
given here, which was that mboxfw's mux word sat at an illegal value. It did
not; per the retractions above, mboxfw's resting value after hw_init is 0xF6,
the same as stock.

The measurement is void for a simpler reason: 0xF6 selects **mic on both
channels**, which is what Seth observed from the front-panel LEDs, while the
loopback was wired into source 2's line input. The selected source was never
carrying the test signal. Same conclusion, correct mechanism.

The 8-frame corruption is a separate matter and stands on its own: silence does
not read as +/-full-scale at Fs/8, whichever source is selected.

See `FINDING_capture_8frame_artifact.md`, whose conclusions about "no audio in
capture" are superseded on this point.

## The byte is ACTIVE-LOW, and that settles bits 6 and 7 — 2026-07-29

Three observations that were each recorded separately here turn out to be one
fact.

**1. The source patterns are one-cold, not arbitrary.** Each of the three legal
3-bit patterns has exactly one bit CLEAR:

    0x06 = 110   b0 clear    -> MIC   (established as boot = mic)
    0x05 = 101   b1 clear    -> LINE
    0x03 = 011   b2 clear    -> INST

So the three bits of a source field are not a binary code; they are three
per-source lines, asserted LOW. That is also why the set {0x05, 0x03, 0x06} looked
arbitrary: it is just "one of three lines pulled down".

**2. Under that convention every immediate this byte ever receives makes sense.**
All four, with their asserted (low) bits:

    site                       value   asserted bits    reading
    hw_init 1st publish        0x00    all eight        lamp test / all-on flash
    hw_init 2nd (boot rest)    0xF6    b0, b3           mic on both channels
    SET_INTERFACE, stream up   0x76    b0, b3, b7       mic/mic + b7
    suspend                    0xFF    none             everything off

The all-on flash at boot and the all-off state at suspend are the two extremes,
and they are the two values that were hardest to explain while the byte was read
as active-high.

**3. Bit 7 tracks streaming, and it is cleared to assert it.** From
`fcn.0x0386` (Rev 20; Rev 22 `fcn.0x038A` identically):

    0389  JNB 0x21.6,0x03D6      ; 0x21.6 = "an alt setting was selected".
                                 ;   NOT selected -> 0x03D6, the STOP branch
    --- START branch, 0x038F-0x03D4 ---
    0397  0x22 = 0xFF ; CLR b0, b3, mono, and CLR b7 (bit 0x17)  -> 0x76
    03a2  publish
    03b2  IEPCNF1 = 0xC5 ; 03B8 mode 3 ; 03BD DMACTL1 |= 0x80    ; capture up
    03c7  OEPCNF2 = 0xC5 ; 03CD DMACTL0 |= 0x80                  ; playback up
    --- STOP branch, 0x03D6-0x03EE ---
    03df  DMACTL1 &= 0x7F                                        ; capture down
    03e6  SETB b7 (bit 0x17)
    03e8  publish
    03ee  LCALL 0x1001  ->  DMACTL0 &= 0x7F                      ; playback down

**Bit 7 is cleared on stream START and set on stream STOP.** Active-low, so
asserted means "streaming". Boot leaves it high (0xF6), i.e. idle, and suspend
leaves it high too. What a panel line asserted only while audio is streaming
drives is not settled by the firmware -- an output mute/relay released while
streaming, or a "USB active" indicator, both fit -- but the CONDITION is now
determined, which is what the reimplementation needs.

**Correction to this document, introduced earlier the same day.** The section
above called 0x0397 a "stream-teardown state". That is wrong and the branch
analysis shows why: 0x0389's `JNB` sends the NOT-selected case to 0x03D6, so
falling through to 0x0397 is the SELECTED case, and the code there enables both
endpoints and both DMA channels. **0x76 is the streaming-active panel state.**
The teardown branch never writes this byte with an immediate at all; it only sets
bit 7 and republishes. The earlier correction was right that 0x76 is not a boot
value and right about the mic mapping; it was wrong about the direction.

**Bit 6 is the external-clock line, also active-low.** Its derivation
`22.6 = !(25.4) && !(25.5)` means it is ASSERTED (low) exactly when 0x25.4 or
0x25.5 is set. The two work codes that drive 0x25.4 directly confirm the sense:

    code 0x04 @ 0x0454: CLR 0x25.4 ; SETB 22.6 (de-assert) ; mode = RAM[0x08]
    code 0x05 @ 0x0466: SETB 0x25.4 ; CLR 22.6 (assert)    ; mode 1

Mode 1 is the S/PDIF-slave clock path (`ACGCTL = 0x0D`, CS8427 CLOCKSOURCE via
the 0x31:0x32 pair), and 0x25.5 is already established as the "clock slaved to
S/PDIF" latch. So 0x25.4 is "external clock selected", code 0x04 restores the
internal clock at the previously persisted mode, and **0x22.6 asserted = external
clock in use** -- the S/PDIF-lock indicator or the clock-source mux line.

## Consequence for bits 0-5 versus the LEDs

The "still open" item below asked which bits drive panel LEDs versus the analog
mux. One-cold source fields make bits 0-5 a clean one-to-one map onto six
per-source lines:

    b0 = ch1 mic    b1 = ch1 line    b2 = ch1 inst
    b3 = ch2 mic    b4 = ch2 line    b5 = ch2 inst

Whether each line drives an LED, a mux enable, or both in parallel still needs
the board -- but there is no longer any question about which bit belongs to which
source, and no bit is left over.

## Still open on this byte

  * ~~Bit 7's meaning.~~ RESOLVED: asserted (low) exactly while streaming. See
    the active-low section. What the line physically drives — output mute relay,
    "USB active" LED, or both — needs the board; the condition does not.
  * ~~Bit 6's meaning.~~ ~~RESOLVED: asserted (low) when the external S/PDIF
    clock is in use, corroborated by work codes 0x04 and 0x05.~~
    **REOPENED 2026-07-30 — incomplete, and the corroboration was misread.**
    Bit 6 has (at least) two unrelated writers. One is the derived
    `0x22.6 = !(0x25.4) && !(0x25.5)` in the source-cycle tail. The other is
    `CLR 0x16` at Rev 20 `0x04FD`, inside `cmd11_eeprom_selftest`, which fires
    only when a read-complement-write-readback of **EEPROM address 0x1FFF**
    verifies. That probe was previously read as a CS8427 presence check — it is
    not; it uses the hardware I²C peripheral at slave address 0xA0. See
    `FINDING_cs8427_is_spi_not_i2c.md`. A single "external clock in use" reading
    does not account for an EEPROM write-verify gating the same bit.
  * ~~Which bits drive the panel LEDs versus the analog mux.~~ PARTLY RESOLVED:
    one-cold source fields give a one-to-one map of b0-b5 onto six per-source
    lines with no bit left over. Whether each line drives an LED, a mux enable,
    or both in parallel still needs the board.
  * What bit 7 and bit 6 are physically wired to. This is now a board question,
    not a firmware question — every condition under which each is asserted is
    determined.
