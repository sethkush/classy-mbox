#!/bin/sh
# Wait out the in-flight 48 kHz drift pair, then run the 44.1 kHz pair — #181/#182.
#
#   nohup chain_drift.sh <seconds> &
#
# Runs ON the unit host and detached, so neither run depends on an ssh session
# staying up.
#
# 44.1 kHz is a separate measurement, not a formality. It drives the OTHER ACG
# frequency word (mode 2, 0x204B6A, against mode 3's 0x0FA861). A generator
# locked to the USB SOF is locked at both rates; a free-running one can sit at a
# different ppm at each, and that difference is itself a discriminator. So the
# 48 kHz result does not transfer, and assuming it did would be assuming the
# answer.
#
# Card indices are re-derived from the serial here rather than passed through
# from the 48 kHz run: they are reassigned on every replug and they already
# flipped once across the 0x0031 reflash. If a unit is replugged mid-chain the
# pair is void anyway, but a stale index would produce a plausible number from
# the wrong device, which is worse than an error.
set -u

SECS=${1:-1800}
SER_A=RK10874600Q
SER_B=RK1672500M

card_for_serial() {
    want=$1
    for c in /sys/class/sound/card*; do
        [ -e "$c/device" ] || continue
        p=$(readlink -f "$c/device")
        par=$(dirname "$p")
        ser=$(cat "$par/serial" 2>/dev/null || echo "")
        if [ "$ser" = "$want" ]; then
            basename "$c" | tr -d 'card'
            return 0
        fi
    done
    return 1
}

echo "waiting for the 48 kHz pair to land..."
while [ ! -f /tmp/d48A.json ] || [ ! -f /tmp/d48B.json ]; do
    sleep 20
done
echo "48 kHz pair complete."
python3 /tmp/measure_drift.py --compare /tmp/d48A.json /tmp/d48B.json \
    > /tmp/result48.txt 2>&1
cat /tmp/result48.txt

CARD_A=$(card_for_serial "$SER_A") || { echo "FAIL: no card for $SER_A"; exit 1; }
CARD_B=$(card_for_serial "$SER_B") || { echo "FAIL: no card for $SER_B"; exit 1; }
echo "44.1 kHz: A=card${CARD_A} B=card${CARD_B}"

/tmp/run_drift_pair.sh 44100 "$SECS" "$CARD_A" "$CARD_B" 441

while [ ! -f /tmp/d441A.json ] || [ ! -f /tmp/d441B.json ]; do
    sleep 20
done
echo "44.1 kHz pair complete."
python3 /tmp/measure_drift.py --compare /tmp/d441A.json /tmp/d441B.json \
    > /tmp/result441.txt 2>&1
cat /tmp/result441.txt
touch /tmp/chain_done
