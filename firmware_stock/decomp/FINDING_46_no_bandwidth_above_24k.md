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
