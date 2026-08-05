#!/usr/bin/env bash
# #46 — does the CODEC follow the doubled C-port clock?
#
# Build 0x0024 can put the TAS's codec port at 96 kHz: the frequency word is
# unchanged and CPTCNF4/CPTRXCNF4 go /4 -> /2, so MCLKO stays 24.576 MHz and
# the frame rate doubles. Whether the CONVERTERS convert at that rate is a
# separate question the firmware cannot answer, because the codec has no
# register interface to ask.
#
# WHY THIS TESTS PLAYBACK AND NOT CAPTURE. At 96 kHz the capture endpoint wants
# to send 96 samples x 6 B = 576 B per frame, and wMaxPacketSize is still 294 —
# the descriptors deliberately do not advertise these rates. The host would see
# a babble/overrun, not audio. Playback has no such limit: the host keeps
# sending 48 samples per frame and the DAC simply consumes them twice as fast.
#
# So the signature is PITCH. Play a 1 kHz tone from B at a host rate of 48 kHz
# with B's codec port at 96 kHz, and listen on A at 48 kHz:
#
#   codec converts at 96 kHz  ->  peak near 2000 Hz (plus starvation artifacts,
#                                 because the DAC drains the circular buffer
#                                 twice as fast as the host fills it)
#   codec does not follow     ->  no coherent 2 kHz peak: silence, noise, or
#                                 something that is not a tone
#
# The starvation is expected and is not the measurement. A buffer read twice as
# fast as it is written repeats or skips, which spreads energy around the peak;
# what distinguishes the two outcomes is whether there IS a peak at 2 kHz.
#
# CONTROL ARM FIRST. The same tone at 48 kHz on both ends must land at 1000 Hz
# before the 96 kHz arm means anything — that is what separates "the codec does
# not do 96 kHz" from "the cable moved".
#
# Bench: BENCH_WIRING.md. B out1 -> A src1. Both src1 are LINE; mboxfw boots to
# MIC, so the mux is set explicitly below.
set -u

HOST=${HOST:-192.168.1.76}
VENV=${VENV:-\$HOME/mbox-venv}
A_SERIAL=${A_SERIAL:-RK10874600Q}
B_SERIAL=${B_SERIAL:-RK1672500M}
A_CARD=${A_CARD:-2}
SECS=${SECS:-6}
OUT=${OUT:-/tmp/mbox96k}

run() { ssh "$HOST" "$@"; }
tlm() { run "$VENV/bin/python3 ~/mboxtmp/mboxtlm.py --serial $1 ${*:2}"; }

echo "=== identity ==="
tlm "$B_SERIAL" read 0        # expect build 0x0024
tlm "$A_SERIAL" read 0

echo "=== mux: both units to LINE ==="
# 0x2D = ch1 LINE (0x05) | ch2 LINE (0x05) << 3
tlm "$B_SERIAL" raw 0x13 0x2D 0
tlm "$A_SERIAL" raw 0x13 0x2D 0

run "mkdir -p $OUT"
run "$VENV/bin/python3 - <<'PY' > $OUT/tone.wav
import math, struct, sys
sr, f, n = 48000, 1000.0, 48000*$SECS
d=b''.join(struct.pack('<i', int(0.5*2147483647*math.sin(2*math.pi*f*i/sr)))[1:] for i in range(n) for _ in (0,1))
hdr=b'RIFF'+struct.pack('<I',36+len(d))+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,2,sr,sr*6,6,24)+b'data'+struct.pack('<I',len(d))
sys.stdout.buffer.write(hdr+d)
PY"

arm() {   # $1 = label, $2 = clock wValue (2 = 48 kHz, 4 = 96 kHz)
    echo "=== arm $1 (clock wValue $2) ==="
    tlm "$B_SERIAL" raw 0x14 "$2" 0xFF     # wIndex 0xFF: leave the Selector alone
    tlm "$B_SERIAL" read 9                 # byte 7 = clock mode: 3 or 7
    run "arecord -D hw:$A_CARD,0 -f S24_3LE -c 2 -r 48000 -d $SECS $OUT/$1.wav &
         sleep 0.5
         aplay -D hw:\$(cat $OUT/bcard),0 $OUT/tone.wav >/dev/null 2>&1
         wait"
    run "$VENV/bin/python3 ~/mboxtmp/iso_loopback.py --analyse $OUT/$1.wav"
}

# B's ALSA card number, resolved by serial rather than assumed.
run "for c in /proc/asound/card*; do
       [ -e \$c/usbid ] || continue
       n=\${c##*card}
       grep -qs $B_SERIAL \$c/usbbus 2>/dev/null || true
     done
     aplay -l | grep -i mbox" | tee /dev/stderr >/dev/null
echo "NOTE: set $OUT/bcard to B's card number before running the arms," \
     "e.g. ssh $HOST 'echo 1 > $OUT/bcard'"

arm control_48k 2
arm doubled_96k 4

echo "=== restore B to 48 kHz ==="
tlm "$B_SERIAL" raw 0x14 2 0xFF
tlm "$B_SERIAL" read 9

echo
echo "READ IT AS: control_48k peak ~1000 Hz is the precondition. Then"
echo "doubled_96k peak ~2000 Hz => the codec converts at 96 kHz and the"
echo "descriptor work in the follow-up is worth doing. No coherent peak =>"
echo "the converters do not follow, and 88.2/96 stays a vendor-request-only"
echo "curiosity or gets reverted for the 128 bytes."
