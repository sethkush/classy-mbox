# 96 kHz buys no bandwidth: everything folds about 24 kHz

Measured 2026-08-05. A (RK10874600Q) on 192.168.1.86 playing at 96 kHz,
B (RK1672500M) on 192.168.1.76 capturing at 96 kHz, both on build 0x002C,
analog loopback A out1 -> B src1, both units LINE.

**One unit per machine, deliberately.** Two Mboxes both at 96 kHz is
582 + 582 = 1164 B/frame, which exceeds a single host controller's full-speed
periodic budget -- measured as denied on 1.76's EHCI hub AND on 1.86's xHCI
root port. Splitting them across two machines gives each a full bus.

## The sweep

Strongest bins in the capture, relative to its own rms:

    played    observed                          48000 - f
    ------    --------------------------------  ---------
      1 kHz    1 kHz  +3.0 dB   (clean)
     10 kHz   10 kHz  +2.5 dB   (clean)
     20 kHz   20 kHz  +0.7 dB   AND 28 kHz -1.6    28
     23 kHz   23 kHz  -0.1 dB   AND 25 kHz -0.7    25
     26 kHz   22 kHz  +0.2 dB   AND 26 kHz -0.9    22
     30 kHz   18 kHz  +1.2 dB   nothing else       18
     40 kHz    8 kHz  +2.7 dB   nothing else        8

Every observation is `48000 - f`. Above 24 kHz the tone FOLDS and nothing
remains at the played frequency: 30 kHz comes back entirely as 18 kHz with the
next-strongest bin 60 dB down. Below 24 kHz the tone survives but gains a
mirror IMAGE at 48000 - f, strong enough at 20 kHz (-1.6 dB) to rival the
fundamental.

Both are the signature of a 48 kHz sampling process in the chain. **96 kHz
carries no information a 48 kHz path could not.**

The 1 kHz arm initially read smeared (900/1200 Hz at -34 dB) and was clean
(+3.0 dB) when repeated after settling -- it was the first capture after a
48 -> 96 kHz change. Rate-change transients corrupt the first capture; every
number above is from a settled measurement.

## 88.2 kHz folds the same way, about 22.05 kHz -- predicted, then measured

The 96 kHz sweep alone could not say WHAT the converter was tracking. 88.2 kHz
discriminates, because it belongs to the 44.1 kHz family: if the converter
follows the doubled frame clock it should resolve to 44.1 kHz, and if it
follows the unchanged ACG word it should fold about 22.05 kHz at `44100 - f`.

The prediction was written down first and hit every bin exactly:

    played    observed              44100 - f
    ------    --------------------  ---------
      1 kHz    1 kHz  +2.9  clean
     10 kHz   10 kHz  +2.2  clean
     20 kHz   20 kHz  -0.1  AND 24100 Hz -1.4     24100
     25 kHz   19100 Hz +0.2 (strongest), 25 kHz -1.6  19100
     30 kHz   14100 Hz +0.9                       14100
     40 kHz    4100 Hz +1.9                        4100

So the converter runs at the BASE rate of each family -- 44.1 for the 44.1/88.2
pair, 48 for the 48/96 pair -- and the doubled rate is a fiction it never
participates in.

## Mechanism: the codec follows MCLK, and MCLK cannot be doubled

This is the constraint from the very start of #46 coming back around.
streaming_set_rate() reaches 88.2/96 by halving the C-port divider and leaving
the ACG frequency word ALONE, because there is no doubled word to write:
datasheet 2.2.6.1 caps the synthesizer at 25 MHz and 48 kHz already runs it at
24.576 MHz = 512 fs, so 96 kHz would need 49.152 MHz.

The result is that the frame clock doubles while MCLK stays put, presenting the
codec with 256 fs where it has only ever seen 512 fs. A codec with no register
interface -- and this one has none, only the 16-bit discrete word in codec.c --
cannot be told about the change, so it keeps converting at the rate MCLK
implies. The TAS1020B faithfully moves 96 (or 88) samples per frame; the analog
side is still 48 (or 44.1) kHz. Hence duplication, folding, and the mirror
images.

**No firmware change reaches this.** The one thing that would fix it, a doubled
master clock, is outside the synthesizer's range by construction.

## Which converter folds is NOT established

The tone must survive A's reconstruction filter and B's anti-alias filter, and
USB cannot see between them. Two readings fit equally:

* A's DAC converts at 48 kHz, so the 96 kHz stream aliases BEFORE reaching
  analog and A emits a real 18 kHz tone, which B captures faithfully.
* A emits a real 30 kHz tone and B's ADC samples at 48 kHz, folding it.

Distinguishing them needs an analog observer (scope or analyser) on A's output,
which the bench does not have. Either way at least one converter is a 48 kHz
part, and the end-to-end answer is the same.

## Tension with FINDING_46_codec_converts_at_96k.md

That entry concluded the codec converts at 96 kHz from a 2 kHz tone returning
at 4 kHz, "exactly double, reversibly". A doubled pitch is what you get when a
96 kHz stream is consumed by a converter running at 48 kHz -- which is evidence
AGAINST its own conclusion, and consistent with everything here. That entry
should be re-read against this one before either is relied on.

## What this means for #46

88.2/96 kHz remain correct as a CLASS FEATURE: the rates are declared, the
host negotiates them, the clock programs, the streams run clean, and playback
at 96 kHz measures equal to 48 kHz on a 1 kHz tone. What they do not do is
carry more audio bandwidth. Combined with the duplex limit -- 96 kHz cannot run
both directions on any host tested -- the honest description is "the device
accepts and correctly handles 88.2/96 kHz, simplex, with no bandwidth benefit."

## Appendix: the full-speed budget, bracketed at 48 kHz (2026-08-05)

Both units on one EHCI controller, 48 kHz throughout, counting bandwidth
denials in dmesg:

     588 B   A plays + B captures (the loopback rig)   OK
     588 B   A duplex                                  OK
     882 B   A duplex + B captures                     OK
    1068 B   88.2 duplex, one unit                     DENIED
    1176 B   A duplex + B duplex                       DENIED

So one host controller's full-speed periodic budget lies between **882 and
1068 bytes per frame**, and the same bracket held on the xHCI box. Everything
the bench actually does at 44.1/48 fits comfortably; only both units in full
duplex at once does not.

METHOD WARNING, learned three times in one day: `dmesg | grep -c` returns 0
when dmesg ITSELF fails, which is indistinguishable from a clean run. On a host
with dmesg_restrict set, an unprivileged read fails and every configuration
reports as passing. The detector must be verified before it is trusted -- and
the grep must be case-insensitive, because EHCI logs "not enough bandwidth"
while xHCI logs "Not enough bandwidth for altsetting 2".
