#!/bin/bash
# #46, mid-stream arm. The rate MUST be changed after the host has opened the
# stream: opening sends SET_CUR(48000), which restores the /4 divider. Same
# trap that voided the first #179 run.
cd ~/mboxtmp
PY=~/mbox-venv/bin/python3
BADDR="2:$(cat /sys/bus/usb/devices/2-1.4/devnum)"
echo "B at $BADDR"

$PY - > /tmp/tone12s.wav <<'PYEOF'
import math, sys, wave, io
sr, f, n = 48000, 1000.0, 48000*12
buf = io.BytesIO()
w = wave.open(buf,'wb'); w.setnchannels(2); w.setsampwidth(3); w.setframerate(sr)
d = bytearray()
for i in range(n):
    b = int(0.5*8388607*math.sin(2*math.pi*f*i/sr)).to_bytes(3,'little',signed=True)
    d += b + b
w.writeframes(bytes(d)); w.close(); sys.stdout.buffer.write(buf.getvalue())
PYEOF

sudo $PY mboxtlm.py --addr $BADDR setmux line line >/dev/null 2>&1
sudo $PY mboxtlm.py --serial RK10874600Q setmux line line >/dev/null 2>&1

echo "=== start capture on A (12 s) and playback on B ==="
arecord -D hw:2,0 -f S24_3LE -c 2 -r 48000 -d 12 /tmp/midstream.wav >/dev/null 2>&1 &
REC=$!
sleep 0.5
aplay -D hw:1,0 /tmp/tone12s.wav >/dev/null 2>&1 &
PLAY=$!
sleep 4
echo "--- t=4s: switching B to 96 kHz MID-STREAM ---"
sudo $PY mboxtlm.py --addr $BADDR clock 96000 2>&1 | grep -E "selector|requested"
sleep 4
echo "--- t=8s: back to 48 kHz ---"
sudo $PY mboxtlm.py --addr $BADDR clock 48000 2>&1 | grep -E "selector|requested"
wait $PLAY; wait $REC
ls -l /tmp/midstream.wav

echo
echo "=== window 1: t=1.0-3.5s (48 kHz, before the switch) ==="
$PY tone_peak.py --skip=1.0 --take=2.5 /tmp/midstream.wav
echo "=== window 2: t=5.0-7.5s (96 kHz, after the switch) ==="
$PY tone_peak.py --skip=5.0 --take=2.5 /tmp/midstream.wav
echo "=== window 3: t=9.0-11.0s (back at 48 kHz) ==="
$PY tone_peak.py --skip=9.0 --take=2.0 /tmp/midstream.wav
