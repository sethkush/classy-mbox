# The feedback endpoint works — and it lied for 64 ms at every stream start

2026-08-05, builds 0x0033 (defect) and 0x0034 (fix), unit A (`RK10874600Q`)
on 192.168.1.76.

## What was built

#185 relabelled both iso endpoints `SYNC_ASYNC` — `SYNC_ADAPTIVE` was a false
claim, proved twice over the same day (#181/#182 host-side, ACGCAP device-side).
#186 stage 2 gave playback the explicit feedback endpoint that asynchronous OUT
obliges: EP2 IN, 3 bytes, 10.14 samples-per-frame, derived from a 64-frame sum
of ACGCAP deltas.

## It works

snd-usb-audio bound it exactly as declared. Its own `/proc/asound/card0/stream0`:

    Endpoint: 0x02 (2 OUT) (ASYNC)
    Sync Endpoint: 0x82 (2 IN)
    Sync EP Interface: 1     Sync EP Altset: 1
    Implicit Feedback Mode: No

and `lsusb -v` parses the pair correctly, including `bSynchAddress 130` on the
data endpoint and usage-type Feedback with `bRefresh 2` on EP 0x82.

The VALUE is right, which is the part that needed proving. usbmon over an 8 s
playback capture, dominant reading `0x0BFFFE` = 47.99988 samples/frame =
**-2.5 ppm**, against block 11's independent device-side **-2.62 ppm** for the
same unit. Two paths sharing nothing but the silicon, agreeing to 0.1 ppm.

## And the first window was garbage

Build 0x0033, first twenty polls in time order:

    0x0C0000  0x0D1FFD x16  0x0C0000 ...
     seed      52.5 samples/frame, +9.4%

Sixteen consecutive polls is exactly one window's worth of arming (a value is
re-armed every 4 frames and a window is 64), sitting immediately after the seed.
That window straddles `streaming_set_rate()` reprogramming the ACG, so it summed
a mixture of the old clock and the new.

**This was not harmless, and the reason is the interesting part.** The expected
failure mode for a bad feedback value is the host rejecting it: Linux
range-checks against roughly `[freqn - freqn/8, freqn + 50%]` and silently falls
back to nominal, which is exactly why a mis-scaled value presents as an inert
endpoint. But +9.4% is INSIDE that band. The host took it, and asked for 9% more
samples per frame for 64 ms at every single stream start.

Nothing about this is visible without a real host polling a real clock through a
real rate change. Enumeration was perfect. The simulator had no opinion. The
only reason it was found is that the test asked whether the host **adopted** the
value rather than whether the endpoint appeared.

## The fix, and what each half is for

`streaming_set_rate()` discards the window in progress and re-primes `acg_prev`.
The re-prime matters as much as the reset: MCLKO stops while the generator is
idle, so the first difference taken across a restart is against a capture from
before the gap and means nothing.

A window is then published only if it lands within ~3900 ppm (1/256) of the
nominal for the current rate. A real crystal sits tens of ppm out; anything
further is the clock having moved under the accumulator. This is the backstop
for disturbances nothing announces — including a missed-SOF wrap, where three
consecutive misses would silently lose 65536 counts. Rejections are counted and
surfaced on block 11 byte 7.

TI reached for the same idea and settled for `MclkPerMs = 11290;` hardcoded past
the counter, under the comment "debug test for capture counter malfunction".

## Verification, build 0x0034

    usbmon:  0 out-of-range polls of 1745   (was 16 of 1833)
    first 16 polls: 0x0C0000, the seed -- the contaminated window is discarded
    poll 17 onward: 0x0BFFFB..0x0C0001, clustered on the measured rate

    A  block 11  -2.62 ppm   rejected windows: 0
    B  block 11  -7.07 ppm   rejected windows: 0

The reject counter reading 0 rather than one-per-start is the useful detail: the
window reset handles the known disturbance before the plausibility guard is
needed, so the guard is a true backstop and has not had to fire.

The published values spread from -1.3 to -5.1 ppm around block 11's -2.62. That
spread is the 10.14 format's 1.27 ppm quantisation dithering about the true
rate, not instability.

## Method note

The device-side differential across the two units is now +4.45 ppm (A -2.62,
B -7.07), against +4.53 ppm measured device-side earlier and +4.263 +/- 0.989
ppm measured host-side in #181/#182. Three measurements, two independent
mechanisms, one answer.
