#!/bin/sh
# Run the 48 kHz drift pair, then the 44.1 kHz pair — #181/#182.
#
#   setsid nohup chain_drift.sh <seconds> &
#
# Runs ON the unit host and detached, so neither pair depends on an ssh session.
#
# This script owns BOTH pairs rather than waiting for one someone else started.
# The earlier version waited for /tmp/d48*.json to appear and then ran 44.1 --
# which races the launcher that deletes those same files at startup, and on a
# re-run would see the PREVIOUS run's results already sitting there and skip
# straight to 44.1 with stale numbers.
#
# 44.1 kHz is a separate measurement, not a formality. It drives the OTHER ACG
# frequency word (mode 2, 0x204B6A, against mode 3's 0x0FA861). A generator
# locked to the USB SOF is locked at both rates; a free-running one can sit at a
# different ppm at each. The 48 kHz answer does not transfer.
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

run_pair() {
    rate=$1
    tag=$2
    # Card indices are re-derived per pair, never carried forward: they are
    # reassigned on every replug and they already flipped between A and B
    # across the 0x0031 reflash. A stale index produces a plausible number
    # from the wrong device, which is worse than an error.
    ca=$(card_for_serial "$SER_A") || { echo "FAIL: no card for $SER_A"; return 1; }
    cb=$(card_for_serial "$SER_B") || { echo "FAIL: no card for $SER_B"; return 1; }
    echo "=== ${rate} Hz: A=card${ca} B=card${cb}, ${SECS}s ==="
    /tmp/run_drift_pair.sh "$rate" "$SECS" "$ca" "$cb" "$tag" || return 1
    while [ ! -f "/tmp/d${tag}A.json" ] || [ ! -f "/tmp/d${tag}B.json" ]; do
        sleep 20
    done
    python3 /tmp/measure_drift.py --compare "/tmp/d${tag}A.json" \
        "/tmp/d${tag}B.json" > "/tmp/result${tag}.txt" 2>&1
    cat "/tmp/result${tag}.txt"
}

rm -f /tmp/chain_done /tmp/result48.txt /tmp/result441.txt
rm -f /tmp/d48A.json /tmp/d48B.json /tmp/d441A.json /tmp/d441B.json

run_pair 48000 48   || { echo "48 kHz pair failed"; exit 1; }
run_pair 44100 441  || { echo "44.1 kHz pair failed"; exit 1; }

touch /tmp/chain_done
echo "chain complete"
