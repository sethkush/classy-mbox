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

    bits [2:0]   channel 1 source select
    bits [5:3]   channel 2 source select
    bit  6       = !(IRAM 0x25.4) && !(IRAM 0x25.5)   — see below
    bit  7       cleared at Rev 20 0x03A0 / Rev 22 0x03A4,
                 set     at Rev 20 0x03E6 / Rev 22 0x03EA

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

**Which pattern is which physical source is NOT established by this scan.** The
firmware only shifts bits; the mapping to mic/line/inst lives in the analog
hardware. mboxfw's comments assert 0x05=mic, 0x06=line, 0x03=inst, and nothing
in either image confirms or contradicts that. Treat it as unverified.

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

`cycle_source()` in `mboxfw/src/buttons.c:23-33` implements

    0x05  ->  0x06  ->  0x03  ->  0x05

**The middle two positions are swapped.** Pressing a source button on mboxfw
therefore lands on a different source than stock would from the same starting
point. This is a real divergence, found in the disassembly, not yet fixed.

## Boot values — and the one that matters

Three sites write 0x22 with an immediate. All three write `#0xFF` first and
clear bits down; **neither image ever writes 0x00 to this byte.**

    site                       Rev 20            Rev 22            result
    boot init                  0x0397 + clears   0x039B + clears   0x76
      (CLR b0, b3, b7, and CLR bit 0x1e = IRAM 0x23.6)
      publish: LCALL 0x0F0C at Rev 20 0x03A2
    second init                0x095B + clears   0x087C + clears   0xF6
      (CLR b0, b3 only — bit 7 left SET)
      publish: LCALL 0x0F0C at Rev 20 0x0964
    third                      0x053B            0x053A            0xFF
      (no bit clears follow)

Decoding the boot value 0x76:

    bits [2:0] = 6  -> pattern 0x06 on channel 1
    bits [5:3] = 6  -> pattern 0x06 on channel 2
    bit 6 = 1, bit 7 = 0

**Stock boots both channels to pattern 0x06.** mboxfw boots
`g_mux_state = 0x00` (`hw_init.c:175`), which is not any of the three legal
patterns and matches no stock site. `hw_init.c:184`'s `0xFF & ~0x01 & ~0x08`
= 0xF6 does match the second init site, but the 0x00 path does not.

There is also a fourth write, `MOV 0x22,A` at Rev 20 0x093F / Rev 22 0x0860 —
a computed store, not yet traced.

## Consequences for the capture measurements

The 44.1 kHz loopback on 2026-07-29 was run with mboxfw's mux word at an
illegal value, so which analog source reached the ADC was undefined. Seth
reports both front-panel LEDs indicating mic while the loop is wired to source
2 and S/PDIF. "No tone in the capture" is therefore explained by the source
routing alone and cannot be used as evidence about the audio path, the #147
mute bits, or anything else.

The 8-frame corruption is a separate matter and stands on its own: silence does
not read as +/-full-scale at Fs/8, whichever source is selected.

See `FINDING_capture_8frame_artifact.md`, whose conclusions about "no audio in
capture" are superseded on this point.

## Still open on this byte

  * Physical meaning of patterns 0x05 / 0x03 / 0x06 (mic / line / inst — which
    is which). Needs hardware or the analog schematic; not in the firmware.
  * Bit 7's meaning. Cleared at boot, set at Rev 20 0x03E6 with an immediate
    publish; only those two sites.
  * Bit 6's meaning, beyond its derivation from IRAM 0x25.4 / 0x25.5.
  * The computed store at Rev 20 0x093F / Rev 22 0x0860.
  * Which bits, if any, drive the panel LEDs versus the analog mux. The shift
    register is 8 bits and all 8 are accounted for as above, so the LEDs are
    presumably decoded from the same source fields — unconfirmed.
