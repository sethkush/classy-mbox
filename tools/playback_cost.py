"""Does a capture open gate playback? Clean re-run.

Two measurements currently contradict each other. The bracket demonstrably runs
at capture open (8825 leading zero frames = tRTV), it clears BOTH pair bits, and
driving 0x23.3 low by hand mutes the output within ~15 ms. Yet scanning B's copy
of A's tone across three capture opens found no dropout at all.

One of those is wrong. This times the A-side capture open precisely and reports
the deepest dip anywhere in B's recording, so there is nowhere for a 187 ms hole
to hide.
"""
import subprocess, time, wave
import numpy as np

ap = subprocess.Popen(["aplay", "-D", "hw:0", "/tmp/tone20.wav"],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(1.0)
rec = subprocess.Popen(["arecord", "-D", "hw:2", "-f", "S24_3LE", "-c", "2",
                        "-r", "48000", "-d", "10", "/tmp/xopen2.wav"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
t0 = time.time()
starts = []
for at in (3.0, 6.0):
    time.sleep(max(0, t0 + at - time.time()))
    s = time.time() - t0
    p = subprocess.Popen(["arecord", "-D", "hw:0", "-f", "S24_3LE", "-c", "2",
                          "-r", "48000", "-d", "1", "/tmp/xa_%d.wav" % int(at)],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p.wait()
    starts.append((s, time.time() - t0))
    print("A capture: launched t=%.3f, finished t=%.3f" % starts[-1])
rec.wait(); ap.terminate()

w = wave.open("/tmp/xopen2.wav", "rb"); fs = w.getframerate(); n = w.getnframes()
a = np.frombuffer(w.readframes(n), dtype=np.uint8).reshape(n, 6).astype(np.int32); w.close()
v = a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)
x = np.where(v & 0x800000, v - (1 << 24), v).astype(np.float64)
b = int(fs * 0.005); m = n // b
env = np.abs(x[:m*b].reshape(m, b)).mean(axis=1); med = np.median(env)
after = env[int(0.6/0.005):]
j = int(np.argmin(after)) + int(0.6/0.005)
print("median %.0f LSB; deepest dip after 0.6 s = %.2f %% of median at t=%.3f s"
      % (med, 100*env[j]/med, j*0.005))
for s, e in starts:
    k0, k1 = int((s-0.3)/0.005), int((e+0.3)/0.005)
    seg = env[k0:k1]
    print("  around A-capture %.2f-%.2f s: min %.2f %% of median" % (s, e, 100*seg.min()/med))
# confirm the bracket ran in each A-side capture
for at in (3, 6):
    w = wave.open("/tmp/xa_%d.wav" % at, "rb"); nn = w.getnframes()
    aa = np.frombuffer(w.readframes(nn), dtype=np.uint8).reshape(nn, 6).astype(np.int32); w.close()
    vv = aa[:, 0] | (aa[:, 1] << 8) | (aa[:, 2] << 16)
    xx = np.where(vv & 0x800000, vv - (1 << 24), vv)
    nz = np.nonzero(xx)[0]
    print("  A capture at t=%d: leading zeros %d frames (%.1f ms) -- bracket %s"
          % (at, nz[0], 1000.0*nz[0]/fs, "RAN" if nz[0] > 5000 else "DID NOT RUN"))
