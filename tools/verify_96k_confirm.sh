#!/bin/bash
# Confirmation arm: 2 kHz source. If the switch is a RATE doubling the output
# goes to 4 kHz; if the 2 kHz result was a resonance or an artifact fixed at
# that frequency, it stays at 2 kHz.
cd ~/mboxtmp
PY=~/mbox-venv/bin/python3
BADDR="2:$(cat /sys/bus/usb/devices/2-1.4/devnum)"
$PY - > /tmp/tone2k12s.wav <<'PYEOF'
import math, sys, wave, io
sr, f, n = 48000, 2000.0, 48000*12
buf=io.BytesIO(); w=wave.open(buf,'wb'); w.setnchannels(2); w.setsampwidth(3); w.setframerate(sr)
d=bytearray()
for i in range(n):
    b=int(0.5*8388607*math.sin(2*math.pi*f*i/sr)).to_bytes(3,'little',signed=True); d+=b+b
w.writeframes(bytes(d)); w.close(); sys.stdout.buffer.write(buf.getvalue())
PYEOF
arecord -D hw:2,0 -f S24_3LE -c 2 -r 48000 -d 12 /tmp/mid2k.wav >/dev/null 2>&1 &
REC=$!
sleep 0.5; aplay -D hw:1,0 /tmp/tone2k12s.wav >/dev/null 2>&1 & PLAY=$!
sleep 4; sudo $PY mboxtlm.py --addr $BADDR clock 96000 >/dev/null 2>&1
sleep 4; sudo $PY mboxtlm.py --addr $BADDR clock 48000 >/dev/null 2>&1
wait $PLAY; wait $REC
$PY - <<'PYEOF'
import sys
sys.path.insert(0,'/home/seth/mboxtmp')
from tone_peak import read_wav_mono, goertzel
import math
for label, skip in (("48 kHz  t=1.0-3.5", 1.0), ("96 kHz  t=5.0-7.5", 5.0), ("48 kHz  t=9.0-11.0", 9.0)):
    x, sr = read_wav_mono('/tmp/mid2k.wav', 0, skip, 2.5 if skip<9 else 2.0)
    m=sum(x)/len(x); y=[v-m for v in x]
    rms=math.sqrt(sum(v*v for v in y)/len(y))
    cr=sum(1 for i in range(1,len(y)) if (y[i-1]<0)!=(y[i]<0))
    zcr=(cr*sr)/(2.0*(len(y)-1))
    g={f: goertzel(y,sr,f) for f in (1000.,2000.,4000.,8000.)}
    print("  %-20s  %.2f dBFS  ZCR %7.1f Hz   1k %.5f  2k %.5f  4k %.5f  8k %.5f"
          % (label, 20*math.log10(rms), zcr, g[1000.],g[2000.],g[4000.],g[8000.]))
PYEOF
