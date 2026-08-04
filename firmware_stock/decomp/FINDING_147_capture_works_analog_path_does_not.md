# Capture works. Playback DMA works. The analog path between them does not.

2026-08-03, Mbox A on 192.168.1.76, build 0x001A, `MBOX_PID=0x2000`, mux set
to `line line` before every measurement (0xED).

This supersedes the premise of #147 ("the capture stream is 8 frames of noise,
3 of which saturate"). That description no longer matches the hardware.

## 1. Capture delivers a full-rate, correctly-ordered, quiet stream

`arecord -D hw:1,0 -f S24_3LE -c 2 -r 48000 -d 4` produced 1,152,044 bytes =
4 x 48000 x 6 + 44. Not one short packet, not one zero-length frame.

Byte order is CORRECT, and this settles **#161** by measurement rather than by
the chain of inference the task was opened for. Decoding the same bytes both
ways:

    order=le  median|x| = 95        p99 = 1,196,191   max = 3,167,994
    order=be  median|x| = 3,407,873 p99 = 8,257,537   max = 8,388,608

Quiet audio decoded with the wrong byte order turns into large pseudo-random
values, because the LSB becomes the MSB. That signature appears ONLY in the
big-endian column. The wire data is little-endian exactly as the descriptors
declare, so mboxfw's current `CPTRXCNF3 = 0xA8` (BYOR clear) is right for the
capture direction. This says nothing about the playback direction.

## 2. The startup burst is an analog settling transient, not corruption

Every stream start produces a burst of large samples that decays smoothly to
the noise floor over ~345 ms (16,576 frames at 48 kHz):

    slice  0  median|x| = 1,419,306
    slice  1  median|x| = 1,251,256      ratio 0.8816
    slice  2  median|x| = 1,102,485      ratio 0.8811
    slice  3  median|x| =   971,687      ratio 0.8814
    ...
    slice 15  median|x| =   213,021

A constant ratio per slice is a first-order exponential; tau is about 8,200
frames, ~171 ms. Digital corruption does not decay smoothly. This is a DC step
settling through a DC-blocking high-pass — the standard behaviour of an audio
codec when its ADC is enabled, which is why real interfaces mute their first
few hundred milliseconds.

Reproducible and re-armed on every stream start (three consecutive 2 s
recordings):

    pass 1: loud=16581  first-500 median=1,467,146  last-500 median=51
    pass 2: loud=16567  first-500 median=1,465,850  last-500 median=46
    pass 3: loud=16569  first-500 median=1,466,375  last-500 median=38

Identical each time because alt 0 tears the path down and alt 1 re-enables it.
After the burst the floor is a median of 38-79 counts out of 8,388,607, about
-100 dBFS. That is a working ADC, not noise.

## 3. The loopback carries no signal

A 1 kHz tone at -9 dBFS played to `hw:1,0` while recording, with A's line out 2
wired to A's line source 2 (BENCH_WIRING.md), measured well past the startup
transient (frames 40,000-90,000), Goertzel at 1 kHz:

    L (src1, NOT looped)   median|x| = 553  max = 11,622   1 kHz mag =  41.5
    R (src2, LOOPED)       median|x| = 540  max = 11,571   1 kHz mag =  71.8

No tone. A -9 dBFS 1 kHz tone would read in the hundreds of thousands. The two
channels are also near-identical in median and max, which is common-mode noise
picked up by both, not two independent inputs — the looped channel is not
carrying anything the unlooped one is not.

The floor does rise from ~79 (idle) to ~550 during playback, so playback is
doing something electrically. It is not reaching the input.

## 4. Both digital paths are live, so the gap is analog

Sampled MID-STREAM, per the project rule, not after teardown.

During capture:

    DMACTL1 (capture, EP1 IN) = 0x89   DMAEN=1
    IEPDCNTX1 = 0xB0                   176 bytes in the capture EP buffer
    alt_seen = 0x03, last SET_INTERFACE iface 2 alt 1

During playback:

    DMACTL0 (playback, EP2 OUT) = 0x82  DMAEN=1
    OEPDCNTX2 = 0xB0                    176 bytes in the playback EP buffer
    alt_seen = 0x03, last SET_INTERFACE iface 1 alt 1

So the host's audio reaches the playback DMA, the DMA is armed, the capture DMA
is armed and filling, and the ADC is demonstrably converting (§2). Every
digital stage on both directions is running.

**The signal dies in the analog stage between them.**

## 5. That makes #170 the blocker, not #147

mboxfw drives **2 bits of the 16-bit codec control word** (#170). The panel
word (RAM 0x22, shift-register chain A) sets the source relays and the LEDs and
demonstrably works — the LEDs follow it and `setmux`/the buttons both move it.
The codec word (RAM 0x23:0x25, chain B) is the codec's own control register and
is almost entirely unimplemented; mboxfw publishes a static `0x1CC0`.

Output routing to the line outputs, input routing into the ADC, and mute are
all plausible occupants of the 14 bits never written. Nothing here proves which
bit does what — that is exactly what #170 is for, and it is RE work on the
stock images that needs no hardware.

## 6. Consequences for the task list

  * **#147** — its premise is retired. Capture is not 8 frames of noise; it is
    a full-rate quiet stream with a normal startup transient. Reopen as "the
    analog path carries no signal", which is #170.
  * **#161** — settled for the capture direction by §1. Playback direction
    still unmeasured, and needs a working analog path first.
  * **#170** — promoted. It is now the thing standing between this firmware and
    audible audio.
  * **#168** (CPTEN never set) — still unresolved and still odd: `CPTSTA` reads
    0x70 and audio bytes move, with no code setting CPTEN. Worth settling, but
    it is not what is blocking the analog path.

## 7. What was NOT tested, and why

Mbox B was not connected. Its value is as a signal source that is not the unit
under test, and that requires B to run **stock** (Rev 22 — Rev 20's playback is
the buggy one), not mboxfw, whose playback is the thing in question. With A
alone every loopback puts mboxfw's playback in series with its capture, so §3
alone cannot say which side is at fault. §4 narrows it — both digital paths run
— but a stock B feeding A's source 1 would separate them outright and is the
right next hardware step if #170 does not resolve it.
