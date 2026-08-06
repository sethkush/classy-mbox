# The S/PDIF output is live, bit-transparent, and was undeclared — #184/#187

2026-08-05, build 0x0031, units A (`RK10874600Q`) and B (`RK1672500M`) on
192.168.1.76.

## The claim being tested

`cs8427.c:221` writes `DATAFLOW = 0x0C`: `TXD = 01` (`CS8427_TXDSERIAL`, AES3
transmitter fed from the serial audio input port) with `TXOFF` clear. By that
register the RCA digital output carries the playback side of the C-port, in
parallel with the analog line out — and the descriptor set declared no Output
Terminal for it, so no host could see a digital output the device was already
producing.

That is a register we write, not an observation at the jack. #187 wanted to
declare a terminal on the strength of it, and a declared terminal that turns
out to be silent is worse than no terminal: it tells every host the device has
an output it does not have.

## Why the obvious test cannot answer it

Play a tone on A, capture on B, look for the tone. That fails, and it fails
*positively*, which is the dangerous kind. `BENCH_WIRING.md`:

    A line out 1 ──TS───► B line source 1      (analog)
    A spdif out  ──coax─► B spdif in           (digital)

A reaches B by both paths. A dead transmitter yields the same clean 1 kHz tone
as a working one, sent the long way round through the analog cable. The
measurement cannot distinguish the thing it exists to measure.

## The discriminator is the clock, not the audio

B was put in `selector=spdif` **and** `clock=slave` (mode 1), verified live in
telemetry block 9 at capture time:

    selector  =S/PDIF   clock=slaved to S/PDIF (mode 1)

In that state B's master clock is the CS8427's recovered clock — derived from
A's carrier. The CS8427 has no sample-rate converter (that is the CS8420), so
with no carrier present B has no master clock at all and cannot produce a
coherent capture. Coherence is therefore evidence of the carrier; the tone
inside it is evidence of what the carrier carries; and the analog cable can
supply neither, because the selector routes the capture path to the receiver.

## Result

With A playing a 1 kHz tone:

    zero-crossing   1000.0 Hz
    level           -9.03 dBFS rms, peak 0.5000
    goertzel        1 kHz 0.250000   2 kHz 0.000000   (-228 dB)

Negative control, identical setup, A silent:

    level           -999.00 dBFS rms, peak 0.0000
    VERDICT         silent -- nothing to read a pitch from

**Three independent confirmations that this is the digital path:**

1. **Peak returned exactly 0.5000**, bit-identical to the source amplitude the
   generator wrote. An analog round trip through a DAC, a cable and an ADC
   cannot return a bit-exact value. The path is digital and bit-transparent.
2. **The silent control is exact zeros, not a noise floor.** An analog input
   would have shown roughly -80 dBFS of hiss; -999 dBFS means the receiver
   delivered literal zero samples.
3. **The analog mux was on MIC** (`mux word =0xB6 ch1=mic ch2=mic`) while the
   bench analog loopback is wired to LINE. The analog cross-cable was not even
   selected. This one was not planned — `mboxtlm.py`'s "a channel not on line
   is not carrying the test signal" warning became a control.

## Consequence

`TERM_SPDIF_OUT` (0x07), terminal type 0x0605, `bSourceID = TERM_USB_OUT_STREAM`
— the same source as the analog line out, which is UAC1 §3.2's shape for one
signal split to two physical outputs. 9 descriptor bytes, no code.

## Method note worth keeping

The confound here was found by reading `BENCH_WIRING.md` while designing the
test, not by the test failing. The naive procedure would have produced a clean,
confident, meaningless 1 kHz reading, and the terminal would have shipped on
it. Whenever two paths connect the same two units, a signal arriving proves
only that *some* path works — the design has to make the paths distinguishable
before the measurement is worth taking.

Everything here was reversible over EP0 (`mboxtlm.py clock 48000 --source
analog`), so it cost no power cycle. B was restored and verified:

    selector  =analog   clock=internal 48 kHz (mode 3)
