#!/bin/bash
# #179: does holding the slaved clock actually stop the sample slips?
#
# A free-runs at 48 kHz and transmits a 1 kHz sine over S/PDIF. B receives it.
# Unslaved, B's C-port runs on its own crystal and must drop or repeat a sample
# each time the two drift apart by one sample period. Slaved, B's C-port IS A's
# clock and no slip is possible.
#
#   control  arecord first, THEN force mode 3 with `clock 48000 --source spdif`.
#            The ordering is load-bearing and the first version of this script
#            got it wrong: forcing BEFORE the stream opens does not survive,
#            because opening the stream sends SET_CUR(48000) and on 0x0022 that
#            re-slaves. Both arms then came out slaved and the A/B had no
#            contrast at all. Forcing mid-stream sticks, since nothing sends a
#            further SET_CUR -- which is exactly why TLM_REQ_SET_CLOCK is kept
#            able to express the mismatched combination.
#   fixed    amixer cset numid=3 1, then arecord -- the class-compliant path a
#            real host takes. This is the arm #179 changes.
#
# Detection: for a pure sinusoid, x[n] - 2cos(w)x[n-1] + x[n-2] == 0 exactly.
# A dropped or repeated sample breaks the recurrence and shows up as a spike.
# A linear-prediction residual, not a threshold anyone tuned.
cd ~/mboxtmp
P="sudo /home/seth/mbox-venv/bin/python3 mboxtlm.py"
DUR=${1:-180}
A_SN=RK10874600Q
B_SN=RK1672500M

card_of() {
    for d in /sys/bus/usb/devices/*/; do
        [ "$(cat $d/serial 2>/dev/null)" = "$1" ] || continue
        for s in $d*/sound/card*; do
            [ -e "$s" ] && basename "$s" | sed 's/card//' && return 0
        done
    done
    return 1
}
A_CARD=$(card_of $A_SN) || { echo "no ALSA card for A"; exit 1; }
B_CARD=$(card_of $B_SN) || { echo "no ALSA card for B"; exit 1; }
echo "A=$A_SN card $A_CARD    B=$B_SN card $B_CARD"

~/mbox-venv/bin/python3 - "$DUR" <<'PY'
import math, struct, wave, sys
dur = int(sys.argv[1]) + 20
w = wave.open("/tmp/tone48.wav","wb"); w.setnchannels(2); w.setsampwidth(3); w.setframerate(48000)
amp = int(0.5*(2**23-1))
w.writeframes(b"".join(struct.pack("<i", int(amp*math.sin(2*math.pi*1000*i/48000)))[:3]*2
                      for i in range(48000*dur)))
w.close()
PY

echo "--- A: internal 48k, the free-running master ---"
$P clock 48000 --source analog --serial $A_SN >/dev/null 2>&1
$P setmux line line --serial $A_SN 2>&1 | grep "mux word"

aplay -D hw:$A_CARD,0 /tmp/tone48.wav >/tmp/aplay.log 2>&1 &
APLAY=$!
sleep 2

for MODE in control fixed; do
    echo "=== $MODE, ${DUR}s ==="
    # Put B on S/PDIF the class-compliant way in BOTH arms, so the only
    # difference between them is the clock, not the routing.
    amixer -c $B_CARD cset numid=3 1 >/dev/null 2>&1
    arecord -D hw:$B_CARD,0 -f S24_3LE -c 2 -r 48000 -d $DUR /tmp/b_$MODE.wav >/tmp/arec_$MODE.log 2>&1 &
    AREC=$!
    sleep 5
    if [ "$MODE" = "control" ]; then
        echo "   forcing internal clock MID-STREAM (reproduces pre-#179 state)"
        $P clock 48000 --source spdif --serial $B_SN >/dev/null 2>&1
    fi
    sleep 3
    echo -n "   MID-STREAM: "; $P read 9 --serial $B_SN 2>&1 | grep -o "selector.*"
    wait $AREC
    echo -n "   AT END:     "; $P read 9 --serial $B_SN 2>&1 | grep -o "selector.*"
done
kill $APLAY 2>/dev/null
echo "--- analysing ---"
~/mbox-venv/bin/python3 slips.py /tmp/b_control.wav /tmp/b_fixed.wav
