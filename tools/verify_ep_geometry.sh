#!/bin/bash
# NB: never `pkill -f aplay` from an ssh one-liner -- the pattern matches the
# remote shell's own command line and kills the session (exit 255).
# Post-flash verification for #162 + #163 (build 0x0023).
cd ~/mboxtmp
P="sudo /home/seth/mbox-venv/bin/python3 mboxtlm.py"
B_SN=RK1672500M
A_SN=RK10874600Q
card_of(){ for d in /sys/bus/usb/devices/*/; do [ "$(cat $d/serial 2>/dev/null)" = "$1" ] || continue
  for s in $d*/sound/card*; do [ -e "$s" ] && basename "$s"|sed s/card//&&return 0; done; done; return 1; }
B=$(card_of $B_SN); A=$(card_of $A_SN)
echo "A card $A   B card $B"
echo "=== 1. which build ==="
$P read 0 --serial $B_SN | grep -E "build id|bus resets"
echo "=== 2. buffer geometry as the DEVICE reports it (block 6) ==="
$P read 6 --serial $B_SN | grep -E "IEPBSIZ1|CPTCTL|IEPCNF1|IEPDCNTX1|DMACTL"
echo "=== 3. analog capture, 12 s, A -> B over line ==="
$P clock 48000 --source analog --serial $A_SN >/dev/null 2>&1
$P setmux line line --serial $A_SN >/dev/null 2>&1
$P clock 48000 --source analog --serial $B_SN >/dev/null 2>&1
$P setmux line line --serial $B_SN 2>&1 | grep "mux word"
# Start the tone ONCE and leave it up for both captures. An earlier version
# killed and immediately restarted aplay between the analog and S/PDIF arms;
# the second player never got the card, and B faithfully captured 12 s of
# digital silence -- which reads exactly like an S/PDIF regression. Player
# stderr is captured, not discarded, for the same reason.
aplay -D hw:$A,0 /tmp/tone48.wav >/tmp/ap.log 2>&1 &
AP=$!
sleep 3
kill -0 $AP 2>/dev/null || { echo "TONE PLAYER DIED -- results below are meaningless"; cat /tmp/ap.log; }
arecord -D hw:$B,0 -f S24_3LE -c 2 -r 48000 -d 12 /tmp/v23_analog.wav >/dev/null 2>&1
echo "=== 4. S/PDIF capture, 12 s (also re-checks #179 holds) ==="
amixer -c $B cset numid=3 1 >/dev/null 2>&1
arecord -D hw:$B,0 -f S24_3LE -c 2 -r 48000 -d 12 /tmp/v23_spdif.wav >/dev/null 2>&1 &
AR=$!
sleep 5
echo -n "   MID-STREAM: "; $P read 9 --serial $B_SN | grep -o "selector.*"
wait $AR; kill $AP 2>/dev/null
amixer -c $B cset numid=3 0 >/dev/null 2>&1
echo "=== 5. analysis ==="
~/mbox-venv/bin/python3 - <<'PY'
import math,struct
def load(p):
    d=open(p,'rb').read()[44:]; n=len(d)//6; L=[];R=[]
    for i in range(n):
        for ch,out in ((0,L),(1,R)):
            b=d[i*6+ch*3:i*6+ch*3+3]; v=b[0]|b[1]<<8|b[2]<<16
            out.append(v-(1<<24) if v&0x800000 else v)
    return L,R
for p in ("/tmp/v23_analog.wav","/tmp/v23_spdif.wav"):
    try: L,R=load(p)
    except Exception as e: print(p,"->",e); continue
    n=len(L); pk=max(abs(v) for v in L) or 1
    rms=math.sqrt(sum(v*v for v in L)/n)
    print("%-22s frames=%d peak=%d rms=%.0f  peak/rms=%.3f (sine=1.414)"
          % (p.split('/')[-1],n,pk,rms,pk/rms if rms else 0))
    # dead-region scan: longest run of consecutive IDENTICAL sample values.
    # #163 predicts a constant region the DMA never refills.
    best=cur=1; bi=0
    for i in range(1,n):
        if L[i]==L[i-1]: cur+=1
        else:
            if cur>best: best,bi=cur,i-cur
            cur=1
    if cur>best: best,bi=cur,n-cur
    print("   longest constant run: %d samples (at %d, %.3f s)" % (best,bi,bi/48000.0))
    # zero-crossing count sanity for a 1 kHz tone
    zc=sum(1 for i in range(1,n) if (L[i-1]<0)!=(L[i]<0))
    print("   zero crossings: %d  -> %.1f Hz" % (zc, zc/2.0/(n/48000.0)))
PY
