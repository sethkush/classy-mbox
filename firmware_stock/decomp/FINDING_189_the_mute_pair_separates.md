# #189: the mute pair IS two gates — 0x23.2 is capture, 0x23.3 is playback

2026-08-06, build 0x0035, both units on 192.168.1.76. One flash, one power
cycle, twenty measurements.

## The answer

**They separate, cleanly, on both units.**

| bit | mask | gates |
|---|---|---|
| `0x23.2` (`CODEC23_MODE5_A`) | 0x04 | **capture** |
| `0x23.3` (`CODEC23_MODE5_B`) | 0x08 | **playback** |

Each bit kills exactly one direction and leaves the other untouched, and the two
bits kill opposite directions. That is precisely the outcome #46 named as the
one that unlocks a UAC1 Feature Unit.

Unit A, ch0 RMS (the fed channel); unit B is the same table within 6 dB on the
muted cells and 0.06 dB on the live ones:

| mask | 0x23 | OUTPUT arm | INPUT arm |
|---|---|---|---|
| both | 0x1C | **-29.77** | **-29.83** |
| none | 0x10 | -101.43 | **-inf** (0/240000) |
| a (0x04) | 0x14 | -101.51 | **-29.83** |
| b (0x08) | 0x18 | **-29.77** | **-inf** (0/240000) |
| both | 0x1C | **-29.77** | **-29.83** |

The bracket holds: `both` measured first and last on each unit agrees to 0.00 dB.
The unfed control channel sat at -101 to -105 dBFS in every one of the 20 cells,
so ~72 dB of channel separation stands behind every "silent" claim above.

## The two mutes are not the same kind of mute

Muting **capture** produces **exact digital zeros** — 0 of 240000 samples
non-zero, on both channels, `-inf` rather than a low number. Muting **playback**
leaves -95 to -101 dBFS at the far end, which is the *reference unit's own input
noise floor*: its ADC is still running and still capturing, there is simply
nothing on the cable.

That asymmetry is independent confirmation of the assignment. A single global
gate could not produce a digitally exact zero in one direction and an analog
noise floor in the other, and the direction that goes exactly-zero is the one
whose samples this device generates.

## Two prior attempts got this wrong, in two different ways

**#171 could not have found it.** Its `MBOX_NO_MUTE_PAIR` build removed
`orl _g_codec_state_23,#0x0c` — both bits at once — because the question then
was whether the pair mattered at all. It does, and both directions died, which
is exactly what one global gate looks like from the outside. The conclusion
"a global audio-path enable" was the correct reading of that experiment; it was
the experiment that could not distinguish one gate from two.

**The first run of the new harness produced a complete, clean, meaningless
table.** The staged copy of `mboxtlm.py` predated the `mute` subcommand,
argparse rejected all ten requests, and the confirmation step was a `grep` for
the success text — which matched nothing and continued. Ten identical rows is
what a working mute looks like when nothing is being muted. The tell was `none`
reading identical to `both` when #171 had measured 71 dB between them.

**The second run failed for a real and much more interesting reason**, and it is
the trap worth carrying forward:

> `streaming_set_rate()` ends with `g_codec_state_23 |= CODEC23_MUTE_PAIR` and
> publishes. **Every stream open re-raises the pair.**

So setting the mask and *then* starting `aplay`/`arecord` undoes the mask before
a single sample is captured. The fix is to bring both streams up first and apply
the mask mid-stream, which is only possible because the control is a runtime
request. **The two compile-time images this experiment replaced could not have
been rescued this way** — a boot-time constant is re-raised by the first stream
open on every image, so the two-flash design would have produced the same null
result and cost two round trips to do it.

The harness now reads the codec word back **after** each capture and prints it
on every row, so a re-raise mid-capture is visible rather than inferred.

## What this unblocks, and the one thing #190 must handle

#190 is unblocked: two Feature Units, one per path, each with a Mute control,
are now an honest declaration rather than a control that lies. `0x23.2` belongs
to the capture Feature Unit and `0x23.3` to the playback one.

**But the re-raise above is a defect for #190, not just a test artifact.** A
host that mutes a stream and then reopens it — which is ordinary, `SET_INTERFACE`
plus the sampling-frequency `SET_CUR` — would find the mute silently cleared.
`streaming_set_rate()` must raise only the bits the host has not muted, rather
than the whole `CODEC23_MUTE_PAIR` mask. That is a condition on a single `|=`,
and it is the reason this finding matters beyond naming two bits.

## Method note

The units were identified by **crystal offset**, not by USB path. In DFU neither
serves a serial, so the flash targets bus:addr, and the sysfs paths had already
moved once today — `BENCH_WIRING.md`'s table says A is at `2-1.3`, while A
answered at `2-1.4` and B at `2-1.2`. Block 11 settled it independently: -2.70
ppm and -6.99 ppm against A's -2.54 and B's -6.76 measured before the flash, the
4.3 ppm separation intact. The silicon fingerprints itself, which is a stronger
identity check than either the port or the serial string the image was built
with.
