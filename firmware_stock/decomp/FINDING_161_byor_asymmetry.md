# The BYOR asymmetry: what the datasheet and the images actually settle

2026-08-04. Written because "mboxfw needs BYOR SET on transmit and CLEAR on
receive, and stock runs it symmetric" is the kind of claim that gets explained
away rather than investigated, and three plausible explanations died here.

## The measured facts

Build 0x001B (`CPTCNF3 = 0xAC`, `CPTRXCNF3 = 0xA8`) carries clean audio: 1 kHz
through the analog loopback, amplitude/rms = sqrt(2) to five digits, harmonics
~100 dB down, constant 20.20 dB loss across a 36 dB input sweep.

Build 0x001C (`CPTCNF3 = 0xA8`) destroyed playback:

    in  -9 dBFS -> out -31.62    A/rms 0.04484   h2  -6.8 dB
    in -21 dBFS -> out -32.18    A/rms 0.04173   h2 -12.1 dB
    in -33 dBFS -> out -30.95    A/rms 0.04913   h2  +0.2 dB
    in -45 dBFS -> out -33.49    A/rms 0.03608   h2  +3.5 dB

Three independent byte-swap signatures: A/rms collapsed from 1.414 to ~0.045
(no tone, just broadband energy), harmonics at or above the fundamental, and
output pinned near -31 dBFS regardless of input across 36 dB. Promoting a
fast-varying LSB into the MSB gives near-constant-power noise that does not
track input level. **So BYOR SET is uniquely correct on transmit.**

Capture is separately pinned: a clean sine out of the loop requires BOTH
directions correct simultaneously, and capture ran BYOR CLEAR throughout.

## Register identities — settled from the datasheet, not reconstructed

  * **§6.5.4.3 — `CPTCNF3`, address FFDEh.** BYOR is bit 2.
  * **§6.5.4.12 — `CPTRXCNF3`, address FFD5h.** BYOR is bit 2.

The BYOR text is **word-for-word identical** in both: "the byte order for the
data moved by the DMA between the USB endpoint buffer and the codec port
interface... when set to a 1, the byte order of each audio sample is reversed."
Neither description is direction-qualified, and neither hints at a polarity
difference.

**This corrects `rev20_boot_rom_audit.md` #12**, which says "TI defines
CPTVSLH/.../CPTCTL at 0xFFD7-0xFFDC. Nothing at 0xFFD4-0xFFD6 or
0xFFDD-0xFFDF" and files our names under "grounded in Rev 20 disasm not TI
reference". The *header* `Reg_stc1.h` omits them; the *datasheet* defines every
one — §6.5.4.11 (FFD6h), §6.5.4.12 (FFD5h), §6.5.4.13 (FFD4h), plus the
register table. `regs.h` is correct and that audit item can be closed.

## Three explanations, all dead

**1. "CPTRXCNF3 is inert, so only CPTCNF3 matters and stock is symmetric."**
§6.5.4.12 opens with "The codec port receive interface configuration register 3
is **only used in I2S Mode 5**", which would have been the escape. It is not
inert: `CPTCNF1 = 0x0D` = `00001_101`, and MODE(2:0) = **101b = I2S mode 5**
(§6.5.4.1). The register is live by the datasheet's own condition.

**2. "The two paths have different slot alignment, shifting byte lanes."**
They do not. `CPTCNF2 = 0xE5` is TSL0L=11b (32 CSCLK for slot 0), BPTSL=100b
(**24 data bits**), TSLL=101b (32 cycles). `CPTRXCNF2 = 0x25` is TSL0L=00b
("same as other time slots", i.e. also 32), BPTSL=100b (**24 bits**), TSLL=101b
(32). Different encodings, identical geometry: 24-in-32 both ways.

**3. "Stock byte-swaps in software for one direction."** It does not. A scan of
Rev 20 for `MOV DPTR,#0xFAxx` / `#0xFCxx` — the audio endpoint buffer region —
returns **zero** hits. The 8051 never touches sample bytes; the DMA moves all
of it. At 48 kHz x 6 bytes there was never CPU budget for it anyway.

## What stock actually does, and why it is not a contradiction

Stock is symmetric by construction: the helper at Rev 20 `0x0FF4` /
Rev 22 `0x0FE2` writes ONE accumulator to BOTH FFDEh and FFD5h and then raises
CPTEN. Boot is 0xAC/0xAC (Rev 20 @0x090B, @0x0923); running is 0xA8/0xA8
(Rev 20 @0x0358, the only reachable call site — the 0xAC site @0x034A is gated
on IRAM 0x21.2, which nothing in either image sets). mboxfw never runs that
helper, so it is the only one of the two that CAN be asymmetric.

Crucially, **stock and mboxfw declare different wire formats**:
`sound/usb/quirks-table.h` declares the stock Mbox (0x0dba:0x1000, iface 1
alt 1) as **S24_3BE**; mboxfw declares **S24_3LE**. So stock running BYOR=0/0
and calling it big-endian is internally consistent and says nothing about what
little-endian requires. There is no contradiction between the two firmwares —
only an unexplained asymmetry *within* mboxfw.

## RESOLVED — there was no asymmetry. CPTRXCNF3's BYOR is inert.

Build 0x001D set `CPTRXCNF3 = 0xAC` (BYOR SET on receive) with `CPTCNF3` held
at its proven-good 0xAC — a single-variable test of the receive side. The
result was **identical to 0x001B to five significant digits**:

    in  -9 dBFS -> out -29.20    A/rms 1.41421   h2 -101.8 dB
    in -21 dBFS -> out -41.20    A/rms 1.41421   h2  -80.5 dB
    in -33 dBFS -> out -53.20    A/rms 1.41414   h2  -74.8 dB
    in -45 dBFS -> out -65.20    A/rms 1.41308   h2  -57.8 dB

Same output levels, same sqrt(2) purity, same constant 20.20 dB loss. Flipping
the receive BYOR bit changed **nothing**, so candidate (a) is selected:

**The capture path does not take its byte order from CPTRXCNF3**, even though
the part is in I2S mode 5 and §6.5.4.12's stated condition is therefore met.
`CPTCNF3`'s BYOR governs BOTH directions — which is exactly what its own text
says, "the byte order for the data moved by the DMA between the USB endpoint
buffer and the codec port interface", with no direction qualifier anywhere in
it. The identical bit description in §6.5.4.12 is best read as documentation
copy-paste rather than a second, independent control.

The `0xA8` mboxfw carried at FFD5h was therefore **inert** — residue from the
#147 work that was never doing anything. It is left at 0xAC so both codec-port
configuration registers match stock's boot init and nobody has to re-derive
why they differed.

### Everything now follows from one bit

    stock   CPTCNF3 = 0xA8 -> BYOR 0 -> big-endian    both ways; declares S24_3BE
    mboxfw  CPTCNF3 = 0xAC -> BYOR 1 -> little-endian both ways; declares S24_3LE
    0x001C  CPTCNF3 = 0xA8 -> BYOR 0 -> big-endian    both ways, while declaring
                              S24_3LE -> both directions wrong. Playback showed
                              it; capture had no signal left to show it with.

No special pleading, no silicon asymmetry, no contradiction between the two
firmwares. The whole puzzle was one register being assumed to do something it
does not do.

### What this cost, and what it bought

Two flash cycles. 0x001C proved by falsification that BYOR SET is *uniquely*
correct on transmit — not merely correct, which is all the original sweep had
shown. 0x001D proved the receive register is inert. Before this, BOTH values
rested on "it works and we never varied it", and one of the two arguments
propping up the transmit value (the Linux-quirk endianness chain) was already
known to be broken. Now each rests on a measurement that could have come out
the other way.

## A separate thread this turned up — **RETRACTED 2026-08-04, it was an error**

The paragraph that stood here claimed:

> The part is in I2S mode 5 ... so the receive path runs on SCLK2/LRCK2 derived
> from ACG synthesizer 2 ... The mode-5 software branch that programs
> synthesizer 2 is reachable only from work code 0x0A, which nothing in either
> image ever posts — it is dead in stock too ... the second synthesizer is never
> programmed at runtime by anyone.

**It is wrong, and it spliced two unrelated numbering schemes.** `CPTCNF1`'s
MODE field (codec-port mode 5) and `audio_clock_mode_apply`'s R7 argument
(Digidesign clock-mode index 5) are different things that share a digit. The
part being in codec-port mode 5 implies nothing about whether the dispatcher's
mode-5 branch runs.

Synthesizer 2 **is** programmed, on the ordinary rate path, by both firmwares:
`acg_set_freq_48k_family` @ Rev 20 `0x0DEC` writes `ACG2FRQ0/1/2` at
`0x0DFE/0x0E04/0x0E0A` alongside synthesizer 1, and `0x0E18` writes `ACG2DCTL`.
mboxfw's `streaming_set_rate()` mirrors all of it. And `0x0A` is an index into
the **host** vendor-command table at `0x0300`, not an internal work code — the
branch is host-reachable, not dead.

Full analysis, including the CPTEN half of the same mistake:
`FINDING_capture_works_anyway.md`. Nothing in the BYOR conclusion above depends
on this paragraph.
