# The codec converts at 96 kHz. Measured 2026-08-05, build 0x0024.

The open question in #46 was whether the CONVERTERS follow when the C-port
frame rate is doubled. Nothing in the firmware could answer it — the codec has
no register interface, so there is nothing to ask. It is answered now, on
hardware, and the answer is yes.

## Method

Build 0x0024 reaches 96 kHz by halving the C-port serial-clock dividers
(`CPTCNF4`/`CPTRXCNF4`, /4 -> /2) with the ACG frequency word unchanged. The
synthesizer cannot do it any other way: its range is 12-25 MHz (§2.2.6) and 48
kHz already runs it at 24.576 MHz.

Only the PLAYBACK direction is measurable without descriptor changes. At 96 kHz
the capture endpoint would want 96 x 6 = 576 B per frame against a
`wMaxPacketSize` of 294, so the host sees a babble rather than audio. Playback
has no such limit — the host keeps sending 48 samples per frame and the DAC
simply consumes them twice as fast — so the signature is PITCH.

B plays a tone, A listens at 48 kHz. `B out1 -> A src1`, both `src1` on LINE.

**THE SWITCH MUST HAPPEN MID-STREAM.** The first run of this experiment set 96
kHz and then started `aplay`, and both arms read 1000.0 Hz with no contrast at
all. Opening the stream sends `SET_CUR(48000)`, and 0x0024 restores the /4
divider on that request — so the setting was undone before a single sample
flowed. This is the same trap that voided the first #179 A/B run, recorded at
the top of `sliptest179.sh`, and it was walked into again. The rate is changed
four seconds INTO a twelve-second stream, and one capture is read out of three
windows.

## Result, 1 kHz source

| window | B's clock | level | zero-crossing | 2k vs 1k |
|---|---|---|---|---|
| t = 1.0-3.5 s | 48 kHz | -29.85 dBFS | 1000.0 Hz | -70.7 dB |
| t = 5.0-7.5 s | **96 kHz** | -32.86 dBFS | 3721.2 Hz | **+2.8 dB** |
| t = 9.0-11.0 s | 48 kHz | -29.95 dBFS | 1043.5 Hz | -33.2 dB |

The ratio swings 73.5 dB across the switch and returns.

## Confirmation, 2 kHz source — this is the one that settles it

A 1 kHz tone appearing at 2 kHz is suggestive; it is not proof of a *rate*
change, because a fixed artifact at 2 kHz would look identical. Repeating with
a 2 kHz source distinguishes them — a rate doubling puts it at 4 kHz, an
artifact leaves it at 2 kHz:

| window | B's clock | 1 kHz | 2 kHz | 4 kHz | 8 kHz |
|---|---|---|---|---|---|
| 48 kHz | mode 3 | 0.00000 | **0.02273** | 0.00000 | 0.00000 |
| **96 kHz** | mode 7 | 0.00334 | **0.00000** | **0.01136** | 0.00000 |
| 48 kHz | mode 3 | 0.00017 | **0.02160** | 0.00056 | 0.00000 |

The 2 kHz bin goes to **exactly zero** and its entire content reappears at 4
kHz. Two source frequencies, both doubling exactly, both reversing.

**And the amplitude is exactly half** — 0.01136 against 0.02273. That is not
noise, it is the predicted consequence of the method: the DAC consumes 96
samples per frame while the host supplies 48, so half of what it converts is
stale buffer content. A doubling that also halved the amplitude by exactly the
right factor is a stronger result than the pitch alone, because the starvation
was predicted before the measurement rather than explained after it.

The high zero-crossing counts in the 96 kHz windows (3721 Hz, 9394 Hz) are the
same artifact — discontinuities at each buffer wrap — and are why the ZCR and
Goertzel readings disagree there. The Goertzel bins are the trustworthy half of
that pair under starvation, which is why both are reported rather than one.

## What this establishes, and what it does not

**Established:** the analog output stage tracks the doubled C-port frame rate.
The codec is being clocked at MCLK = 256fs instead of 512fs and keeps
converting. 88.2/96 kHz are real on this hardware, not just a register setting.

**Not established:** clean, unstarved 96 kHz audio, in either direction. Every
sample above came through a deliberately starved buffer, and CAPTURE was not
measured at all — it cannot be, until the descriptors advertise the rate and
`wMaxPacketSize` grows to hold a 576 B frame. Nothing here says 96 kHz sounds
good; it says the converters do not refuse it.

## What it costs to make it class-compliant

The clock side is done and cheap. The exposure is not:

- `wMaxPacketSize` 294 -> ~582, which on a single alt setting would reserve 96
  kHz bandwidth even while running at 48. The fix is a second alt setting
  (alt 1 = 44.1/48, alt 2 = 88.2/96) — roughly 92 descriptor bytes plus
  `SET_INTERFACE` handling.
- Full-speed duplex at 96 kHz is 1152 kB/s of payload, about 9.2 Mbit/s on a
  12 Mbit/s bus. Against the 90% periodic cap it fits, but only just, and
  nothing else on that bus will. Expect single-direction 96 kHz to be
  comfortable and simultaneous both-ways to be marginal. That is a bus limit,
  not a firmware one.
- The endpoint buffer is 640 B against a 576 B frame: 64 B of slack versus
  352 B at 48 kHz, single circular buffer, no double buffering. There is no
  room to enlarge — the two buffers already use 1280 of the 1288 B available.
- Build 0x0024 is 5998 of 6016 bytes. 18 bytes free against ~100+ needed.

So the follow-up is real work with a real risk of not fitting, but it is now
work worth doing rather than a guess. Before this measurement the honest move
would have been to revert the 128 bytes.
