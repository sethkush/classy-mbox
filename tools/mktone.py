#!/usr/bin/env python3
"""Generate a continuous stereo test tone as a WAV — bench signal source.

Written because `speaker-test` is the wrong instrument and produced two
retracted measurements. It plays front-left, THEN front-right, alternating, so
a capture window can land entirely inside a half-period where the channel under
test is silent -- which looks exactly like a broken signal path. This writes a
tone present on BOTH channels for the whole file, continuously.

Defaults match the bench: 24-bit in 3-byte subframes (S24_3LE), stereo, the
format the device actually streams, so `aplay` needs no conversion and no
plughw rate/format converter sits in the path pretending things work.

    mktone.py --hz 1000 --rate 48000 --seconds 60 -o /tmp/tone.wav
"""
import argparse
import math
import struct
import wave


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hz", type=float, default=1000.0)
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--amplitude", type=float, default=0.5,
                    help="0..1 of full scale; 0.5 leaves headroom so a hot "
                         "input stage cannot clip the tone into harmonics "
                         "that would confuse a pitch reading")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()

    n = int(a.rate * a.seconds)
    amp = int(a.amplitude * (2 ** 23 - 1))
    w = wave.open(a.out, "wb")
    w.setnchannels(2)
    w.setsampwidth(3)          # 24-bit packed, matching the device
    w.setframerate(a.rate)

    frames = bytearray()
    two_pi_f_over_sr = 2.0 * math.pi * a.hz / a.rate
    for i in range(n):
        v = int(amp * math.sin(two_pi_f_over_sr * i))
        b = struct.pack("<i", v)[:3]      # little-endian, drop the high byte
        frames += b                        # left
        frames += b                        # right — both channels, always
    w.writeframes(bytes(frames))
    w.close()
    print("wrote %s: %.1f s of %.0f Hz, %d Hz stereo 24-bit, amplitude %.2f"
          % (a.out, a.seconds, a.hz, a.rate, a.amplitude))


if __name__ == "__main__":
    main()
