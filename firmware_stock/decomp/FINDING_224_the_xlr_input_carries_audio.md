# #224 — the XLR input carries audio, and the old null was about the cabling

2026-08-16. The last undeclared input on this device, and the last row
`check_terminal_evidence.py` was waiting for.

## Why this took so long, and it was never the analog

LINE and INSTRUMENT share one physical 1/4" jack — the 74HC157 muxes pick which
front end feeds the shared gain stage. So both were measurable with the TS
cabling already on the bench: same cable, different mux pattern. That is how
#196 measured INST as 18.9 dB hotter than LINE at an identical dial position.

**The microphone is a separate XLR connector**, and nothing on the bench drove
it. Not a mysterious signal path — to the firmware it is one more three-bit mux
pattern, and 48V is a mechanical switch that never involves firmware at all. It
simply had no source.

## The old null was correct, and it was evidence about the cabling

`descriptors.c` carried this, as the reason microphone stayed undeclared:

> interleaved line/mic, MIC read the analysis floor at every source level while
> LINE read normally, which proves the mux switches but not that the preamp
> carries audio

That measurement was sound and its conclusion was sound. What it actually
established is that **the 1/4" jack does not reach the mic front end** — which
is exactly what you would expect, because the mic front end is the XLR. It was
never evidence against the path.

## Two arms first, with nothing plugged into the XLR

Bracketed LINE / MIC / LINE on unit A, using the committed `noisefloor.py`:

| arm | ch1 200Hz-2k | ch2 200Hz-2k |
|---|---|---|
| LINE | **-141.3** | **-113.0** |
| MIC | -152.4 | -151.6 |
| LINE (repeat) | **-141.3** | **-112.9** |

The two LINE arms reproduce to 0.1 dB, so the mux commands demonstrably took
effect and nothing drifted. MIC is a different input: ch2 drops 38 dB. Both mic
channels are symmetric (-151.6 / -152.4), which is what two identical preamps
with open inputs should look like.

Direction is the opposite of the naive prediction, and `BENCH_WIRING.md`
explains it: on LINE, unit A's inputs are **driven by unit B's outputs**, so
that arm measures unit B's output stage. On MIC the jacks are open. Quieter,
because nothing is feeding it.

This proved the position was live and distinct. It did **not** prove the jack
passes audio — an open input with a plausible floor is consistent with a working
preamp and with a preamp whose connector is disconnected downstream.

## Then an SM58 and a tone

An SM58 in source 1 of unit A, gain cranked, 1234 Hz played into the room from
the void box's speakers. **1234 Hz was chosen so nothing ambient could fake it**
— not a mains harmonic, and no room noise puts a peak in those bins.

| arm | ch1 (miked) | ch2 (no mic — the control) |
|---|---|---|
| tone OFF | -93.0 dBFS, SNR **2.2 dB** | -148.7, SNR 2.2 dB |
| tone ON | **-23.1 dBFS, SNR 56.2 dB** | -138.0, SNR 12.7 dB |

**Channel 1 lifts 69.9 dB at exactly 1234 Hz**, from indistinguishable-from-noise
to a 56 dB SNR peak. Channel 2 — same unit, same ADC, same capture, no
microphone — moves 10.7 dB, which is acoustic bleed into an open input and sits
45 dB below the miked channel. That channel is what makes the number mean
something rather than being a bare assertion about one capture.

Also note the tone-OFF floors: with the gain up, the miked channel sits 55 dB
above the un-miked one on room noise alone.

## The first run was VOID, and it looked exactly like a refuted hypothesis

The first attempt reported no tone on either channel. That was **not** a result:
the tone file was mono, `aplay` rejected it with `set_params:1404: Channels
count non available`, and the failure was hidden because the command was
backgrounded with output redirected to `/dev/null`. The stimulus never fired.

Had that been reported, it would have read as "the XLR path does not carry
audio" — a confident, wrong, and very plausible conclusion, since it agreed with
the prior null. It was caught only by checking the stimulus rather than the
result, and the re-run used a stereo file with playback confirmed audible.

This is the 2026-08-10 lesson for the sixth time: **a null from an instrument
that was never connected looks exactly like a null from a refuted hypothesis.**
The rule that saved it is the ordinary one — put a known-answer arm in every run,
and verify the stimulus fired before believing the measurement.

## What is now declared

- `TERM_MIC_IN` (0x0B), Input Terminal type **0x0201 (UAC_TT_MIC)** — the only
  input on this device whose terminal type actually names it; LINE and INST both
  share 0x0603 because the Terminal Types document has nothing for Hi-Z.
- Selector Unit position **4**, appended. Positions 1-3 keep their meaning; pin
  order is the protocol and inserting would silently renumber every host.
- String 9, "Microphone", so the position is legible rather than numbered.

**Only one input can be live at a time, by construction.** A UAC1 Selector Unit
has N pins and one output, and SET_CUR carries a single 1-based index, so "line
and mic together" is not expressible and no host can request it. The construct
that would allow simultaneous inputs is a Mixer Unit, which this firmware
deliberately does not declare.

## Still open

The mux **boots to MIC** while position 1 is LINE, so at power-on the hardware
sits in a state the Selector now can describe but does not claim. Whether the
boot default should change, or the Selector should report position 4 at boot, is
a separate decision and is not made here.
