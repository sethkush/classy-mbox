"""Find runs of exact digital zeros in a capture and report where and how wide.

The capture gate emits exact zeros when low (#189), and an idle analog input
never does -- its noise floor sits around 1e-5 -- so a run of zeros is
unambiguous evidence that the codec ACCEPTED a gate-low word. The width of the
run identifies which arm of the #197 gate probe produced it. FINDING_197,
"2026-08-07".
"""
import sys
import numpy as np

path = sys.argv[1]
raw = open(path, "rb").read()[44:]
n = len(raw) // 6
a = np.frombuffer(raw[:n*6], dtype=np.uint8).reshape(n, 6).astype(np.int32)


def ch(o):
    v = a[:, o] | (a[:, o+1] << 8) | (a[:, o+2] << 16)
    return np.where(v & 0x800000, v - (1 << 24), v)


# Both channels, because the gate is one bit for the whole capture path: a run
# on one channel only would mean something other than the gate.
z = (ch(0) == 0) & (ch(3) == 0)
d = np.diff(np.concatenate(([0], z.view(np.int8), [0])))
starts, ends = np.where(d == 1)[0], np.where(d == -1)[0]
wide = [(s, e) for s, e in zip(starts, ends) if e - s >= 8]
print("%d samples (%.2f s), %d zero runs >= 8 frames" % (n, n / 48000.0, len(wide)))
for s, e in wide:
    print("  t = %7.3f s   width = %5d frames = %6.2f ms"
          % (s / 48000.0, e - s, (e - s) / 48.0))
