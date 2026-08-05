#!/bin/bash
# #151: verify the Rev 22 playback SOF watchdog on hardware.
#
# B PLAYS (this is the path never confirmed working), A CAPTURES.
# BENCH_WIRING.md: B out1 -> A src1, so the tone lands on A channel 1 and A
# must be on LINE. Telemetry is sampled MID-STREAM, because a count read after
# teardown cannot distinguish "never fired" from "fired and the stream ended".
cd ~/mboxtmp
P="sudo /home/seth/mbox-venv/bin/python3 mboxtlm.py"
A_SN=RK10874600Q; B_SN=RK1672500M
card_of(){ for d in /sys/bus/usb/devices/*/; do [ "$(cat $d/serial 2>/dev/null)" = "$1" ] || continue
  for s in $d*/sound/card*; do [ -e "$s" ] && basename "$s"|sed s/card//&&return 0; done; done; return 1; }
A=$(card_of $A_SN); B=$(card_of $B_SN)
echo "A(capture) card $A    B(playback) card $B"

echo "=== 0. both units internal 48k analog; A listening on LINE ==="
$P clock 48000 --source analog --serial $A_SN >/dev/null 2>&1
$P clock 48000 --source analog --serial $B_SN >/dev/null 2>&1
$P setmux line line --serial $A_SN 2>&1 | grep "mux word"
$P reset --serial $B_SN >/dev/null 2>&1

echo "=== 1. BASELINE (nothing streaming) ==="
$P read 7 --serial $B_SN | grep -E "pb_resyncs|suspends"

echo "=== 2. B plays 20 s, A records; telemetry sampled mid-stream ==="
arecord -D hw:$A,0 -f S24_3LE -c 2 -r 48000 -d 18 /tmp/pb151_A.wav >/tmp/ar151.log 2>&1 &
AR=$!
sleep 1
aplay -D hw:$B,0 /tmp/tone48.wav >/tmp/ap151.log 2>&1 &
AP=$!
sleep 3
kill -0 $AP 2>/dev/null && echo "   player alive" || { echo "   PLAYER DIED:"; cat /tmp/ap151.log; }
for t in 3 8 13; do
    echo -n "   t=${t}s  "; $P read 7 --serial $B_SN | grep -o "pb_resyncs.*" | head -1
    sleep 5
done
wait $AR
kill $AP 2>/dev/null
sleep 1
echo "=== 3. AFTER teardown ==="
$P read 7 --serial $B_SN | grep -E "pb_resyncs"
echo "=== 4. what A actually heard ==="
~/mbox-venv/bin/python3 - <<'PY'
import math
d=open("/tmp/pb151_A.wav","rb").read()[44:]; n=len(d)//6
L=[];R=[]
for i in range(n):
    for ch,o in ((0,L),(1,R)):
        b=d[i*6+ch*3:i*6+ch*3+3]; v=b[0]|b[1]<<8|b[2]<<16
        o.append(v-(1<<24) if v&0x800000 else v)
for name,x in (("A ch1 (B out1)",L),("A ch2",R)):
    pk=max(abs(v) for v in x) or 1
    rms=math.sqrt(sum(v*v for v in x)/n)
    zc=sum(1 for i in range(1,n) if (x[i-1]<0)!=(x[i]<0))
    print("%-16s peak=%7d rms=%8.0f  %6.1f dBFS  %6.1f Hz  crest=%.3f"
          % (name,pk,rms,20*math.log10(rms/8388607.0) if rms>0 else -999,
             zc/2.0/(n/48000.0), pk/rms if rms else 0))
PY
