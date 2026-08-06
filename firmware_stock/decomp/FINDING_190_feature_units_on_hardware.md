# #190 on hardware: the host parsed both Feature Units, and the mute survives a stream reopen

2026-08-06, build 0x0037, both units on 192.168.1.76.

## The host found them

`snd-usb-audio` merged the two Feature Units into one simple control carrying
both switches, which is the correct rendering of two FUs on opposite paths:

    numid=3  'PCM Playback Switch'    <- FU 8  -> codec bit 0x23.3
    numid=5  'PCM Capture Switch'     <- FU 9  -> codec bit 0x23.2
    Capabilities: pswitch pswitch-joined cswitch cswitch-joined

`joined` is master-channel-only — exactly what was declared, and exactly what
the hardware is: one bit per path, so `bmaControls(0)` carries Mute and the two
per-channel entries are zero.

## Each switch drives the gate it claims

Toggled mid-stream, reading the codec word back over EP0 after each change:

| action | 0x23 | expected |
|---|---|---|
| both on | 0x1C | 0x1C |
| capture off | **0x18** | bit 2 clear |
| both off | **0x10** | both clear |
| capture on | **0x14** | bit 2 set |
| both on | 0x1C | 0x1C |

Five states, five matches. No inference from "the request did not stall" — the
bit is read back each time.

## The mute survives a stream reopen

This is the defect #190 actually fixed, and the reason it is more than a
descriptor change. `streaming_set_rate()` used to raise the whole pair, so every
stream open cleared a host-set mute:

    mute playback, no stream running   -> 0x23 = 0x14
    OPEN a stream                      -> 0x23 = 0x14   (was: cleared to 0x1C)
    stop, reopen                       -> 0x23 = 0x14
    ALSA still reports                 -> values=off

The host's view and the device's state stay in agreement across the transition
that used to break them. #189 met this as a test artifact before it was
understood as a bug.

## The full sweep, through the class control

`test_mute_pair.sh` re-run driving `amixer` instead of the retired vendor
request — so the harness now tests what ships. Unit A, ch0 RMS, and unit B is
the same table:

| mask | 0x23 | OUTPUT arm | INPUT arm |
|---|---|---|---|
| both | 0x1C | **-29.78** | **-29.85** |
| none | 0x10 | -101.75 | **-inf** (0/240000) |
| a (0x04) | 0x14 | -101.47 | **-29.85** |
| b (0x08) | 0x18 | **-29.79** | **-inf** (0/240000) |
| both | 0x1C | **-29.79** | **-29.85** |

Against #189's vendor-request run: -101.51 vs -101.47 on the muted output arm,
`-inf` vs `-inf` on the muted input arm. Identical within noise. The shipped
class control and the retired bench alias drive the same hardware the same way,
which is what justified removing the alias.

Bracket holds — `both` first and last agree to 0.01 dB. The unfed control
channel sat at -101 to -105 dBFS in all 20 cells.

## Method note: two ways the harness could have lied

**`amixer set PCM capture mute` is rejected outright.** The simple-mixer layer
merges both Feature Units into one control named `PCM`, and that syntax is not
valid for its capture side. Worse, searching `scontrols` for something matching
/capture/ finds **`PCM Capture Source`** — the Selector Unit, a different
control entirely. A harness built on simple-mixer names would either fail or
quietly move the input selector while reporting mute results.

Element names are unambiguous, so the harness now addresses
`cset name='PCM Playback Switch'` / `'PCM Capture Switch'` and aborts if either
element is missing. That abort matters: with no mute reaching the device, every
row is the same condition measured ten times — which is exactly how the first
run of this script failed, on 2026-08-06, before the mute request existed.

The first attempt at the fixed harness also died on a shell-function ordering
mistake — the definitions were placed after their first use. It failed loudly at
`mute_ctls: not found` rather than skipping the mute, which is the behaviour the
abort-on-unconfirmed design is for.
