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

## Slaving does real work — measured, 2×180 s

The first run showed slaving was *stable* but not that it was *necessary*: B
received the tone cleanly on its own clock too, because both units sit at
nominally 48 kHz and 12 s is far too short for two crystals to drift apart by a
whole sample. Repeated at 180 s per arm, after the host reboot.

Detection is a linear-prediction residual with no tuned threshold. For a pure
sinusoid, `x[n] - 2cos(w)x[n-1] + x[n-2] == 0` exactly; a dropped or repeated
sample breaks the recurrence. Threshold = 2 % of the signal's own peak, and the
observed residuals reach 99 % of peak, so the margin is three orders of
magnitude, not a judgement call.

    unslaved   40 slip events in 180 s
               mean gap 4.65 s, and the sequence is a metronome:
               16.0 20.5 25.0 29.6 34.1 38.6 43.2 47.7 52.2 56.8 61.3 65.8 ...

    slaved      3 events, two of them at t=3.2 s -- the switch itself --
               and one at 17.6 s. Then NOTHING for the remaining 162 s.

**One sample every 4.53 s at 48 kHz is a 4.6 ppm offset between the two
crystals**, which is an entirely ordinary figure for two uncompensated
oscillators. That is the mechanism, quantified: the receiver's read pointer laps
the writer's at the drift rate, and each lap drops or repeats a sample.

So the claim is now measured rather than argued from the datasheet: without
clock slaving, S/PDIF input glitches **every few seconds, indefinitely**. With
it, the glitches stop. The CS8427 has no sample-rate converter (that is the
CS8420) and nothing else in the path can reconcile two independent clocks.

The residual event at 17.6 s is unexplained and is one event in 162 s against 40
in 180 s. It is as likely to be a USB or DMA hiccup as a clock slip; nothing
here distinguishes them.

## Persistence across a power cycle: confirmed

The host was rebooted and both units physically replugged on 2026-08-04. B came
back at `0dba:2000` running **build 0x0020**, with `bus resets: 3` — a genuine
cold-boot count, not the saturated value a warm device shows — and
`selector = analog, clock = internal 48 kHz (mode 3)`.

Two things follow. The `ffff:fffe` + bus-reset app-switch path **does write
EEPROM**; it is not a RAM-only load, despite the flasher's warning at manifest
time being about the pre-switch window. And the boot default holds across a real
power cycle: mode 1 is never entered unless a host asks.

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

## The control is discoverable with no quirk — verified 2026-08-04, build 0x0021

#177 made the S/PDIF path work; #160 made a host able to find it. Until 0x0021
mboxfw answered Selector Unit 5 but declared no Selector Unit, so only a host
that already knew the unit ID could reach it. Adding an S/PDIF input terminal
(ID 6, type 0x0605), a Selector Unit (ID 5, pins [2, 6]) and repointing the
capture output terminal's `bSourceID` to the unit changed that.

`snd-usb-audio` on the void box, at `MBOX_PID=0x2000` where the `mbox1` quirk
does **not** apply, now enumerates the control from the descriptors alone:

    card 1 (B, build 0x0021)   Simple mixer control 'PCM Capture Source'
                               Items: 'Line' 'IEC958 In'
    card 2 (A, build 0x001F)   no such control

The kernel derived both item names from the terminal types it parsed — 0x0603
"line connector" and 0x0605 "S/PDIF interface". Nothing in the firmware supplies
those strings.

Driving it moves the hardware, end to end:

    amixer -c 1 cset numid=3 1   ->  codec word 0x1CC0 -> 0x1CD0  (bit 0x25.4)
                                     selector = S/PDIF
                                     clock    = slaved to S/PDIF (mode 1)

    amixer -c 1 cset numid=3 0   ->  codec word 0x1CD0 -> 0x1CC0
                                     selector = analog
                                     clock    = internal 48 kHz (mode 3)

Two things to notice in that trace.

**Selecting S/PDIF forced the slaved clock by itself**, from a plain ALSA
control, because the handler ports stock's cmd5 side effect. The kernel quirk
documents that behaviour ("Setting the input source to S/PDIF resets the clock
source to S/PDIF") and here it happens with no quirk loaded at all.

**Returning to analog restored 48 kHz**, which is where the deliberate
divergence from stock earns itself. Stock's cmd4 reloads `RAM[0x08]`, and mode 1
writes `RAM[0x08] = 1`, so stock would have come back from this excursion still
slaved — see the correction in §2 of `FINDING_spdif_path_rev20_rev22.md`.
mboxfw keeps `g_internal_rate` separately and returns to a real clock. The bug
is only invisible on stock because its host always follows a source change with
an explicit set-clock-source request.

Still not measured on macOS. That one selector is the shape Apple's driver
models ("items are PATHS, not pins") is read from its source, not observed.
