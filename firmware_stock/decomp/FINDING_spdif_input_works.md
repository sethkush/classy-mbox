# S/PDIF input works, and RMCK→MCLKI is confirmed

Measured 2026-08-04 on the void box, build 0x0020 (#177) on unit B
(`2-1.4`, card 2), unit A (`RK10874600Q`, `2-1.3`, card 0) untouched on its
internal clock as the master. Only one unit may slave — the crossed S/PDIF
topology is circular otherwise.

## The chain

    A USB playback -> A TAS serial port -> A CS8427 (TXD=01, fed from the
      serial input port) -> S/PDIF out -> B S/PDIF in -> B CS8427 (SPD=10,
      AES3 receiver -> serial output port) -> B TAS C-port -> DMA -> B capture

1 kHz sine, amplitude 0.5 FS, 24-bit, played to A while both units captured.
B was switched to the slaved clock **mid-stream at t≈3 s**, because opening the
capture stream sends `SET_CUR(48000)` and would otherwise put B straight back on
the internal clock.

## The numbers

    A capture (analog self-loop, the control)
      R  -29.2 dBFS steady       <- the tone through DAC -> ADC
      L  -92.5 dBFS              <- that channel's loop is not wired (BENCH_WIRING)

    B capture (S/PDIF from A; slaved from t=4 s onward)
      t=0..3  -9.0 dBFS   rms 2965819.7
      t=4..11 -9.0 dBFS   rms 2965819.7   <- byte-identical across the switch

**-9.0 dBFS is exactly right for a bit-exact transfer.** A sine of amplitude
0.5 FS has RMS 0.5/√2 = 0.354, which is -9.03 dBFS. B is not receiving A's
analog loop (that is the -29 dBFS figure); it is receiving **A's playback
samples digitally, unmodified**.

The zero counts confirm it independently: 2000 zero samples per second, every
second, and a 1 kHz sine at 48 kHz has exactly 2000 mathematical zero crossings
per second. An analog path would not produce exact zeros at all. This is a
bit-exact digital link.

## What this settles: RMCK is wired to MCLKI

This was the open board question — `ACGCTL = 0x0D` sources both codec master
clocks from MCLKI, which is only useful if MCLKI carries the CS8427's recovered
clock on RMCK. No schematic has been read, and the inference rested on stock's
mode 1 being incoherent otherwise.

It is now settled by the data path rather than by inference. `SERIALINPUT`/
`SERIALOUTPUT = 0x05` sets `SOMS = 0`, so the CS8427 is the **slave**: OSCLK and
OLRCK are inputs, driven by the TAS. Those clocks derive from MCLKO, and in mode
1 MCLKO derives from MCLKI. So if MCLKI carried nothing while slaved, the C-port
would stop clocking and the DMA would deliver zero-length packets.

It delivered 576000 frames in 12.00 s, at full rate, with bit-stable content,
for eight seconds after the switch. **MCLKI is alive in mode 1, so RMCK feeds
it.** The safety caveat in `streaming_set_rate()` is discharged.

## What this run did NOT show

**That slaving is *necessary*.** B received the tone cleanly at t=0..3 as well,
while still on its internal 48 kHz clock with `CLOCKSOURCE = 0x40`. Both units
are nominally at 48 kHz from the same family of synthesizer settings, and 12 s is
far too short for the drift between two free-running crystals to accumulate into
an audible glitch. The failure mode that slaving prevents — periodic sample slips
as the receiver's read pointer laps the writer's — needs a long run, or two units
deliberately clocked apart, to demonstrate.

So: slaving works and is stable. "Without it, S/PDIF input breaks" is correct in
principle (the CS8427 has no sample-rate converter — that is the CS8420) and is
*not* something this measurement proves.

**Persistence across a power cycle** is also untested. B's EEPROM holds a valid
0x0020 image, so a power cycle boots it; nothing here depended on that.

## Incidental: a P3.0 transient that was not a signal

The first read after switching to the slaved clock returned `P3 = 0xC3` where
every prior sample was `0xC2` — P3.0, which is RXD, apparently going high in
response to a clock change. That would have been a fourth P3 story.

It does not reproduce. Fifteen samples across all four combinations of
{internal 48 k, slaved} × {analog, S/PDIF routing} all read `0xC2`. The one
`0xC3` was taken immediately after a burst of CS8427 register writes, in the
same command as the switch.

What is refuted is a **persistent level tied to the clock mode or the routing**.
A transient during the SPI burst is not refuted and was not tested. Recorded
because the previous three P3 stories were each built on one unchecked sample,
and this one was caught only by re-sampling instead of writing it up.
