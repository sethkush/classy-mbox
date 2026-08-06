#!/bin/sh
# Run measure_drift.py against BOTH units at once — #181/#182.
#
#   run_drift_pair.sh <rate> <seconds> <cardA> <cardB> <tag>
#
# A script rather than an ssh one-liner on purpose. The one-liner version was
#   ssh 'cd /tmp && rm -f ... && nohup A ... & nohup B ... & sleep 5'
# and it silently started only ONE capture: `&&` bound the `cd` into the first
# background job, so the second launched from the wrong directory, and the
# `rm -f` then raced the second job and deleted the log that would have shown
# the failure. Both units must be measured over the same window or the pair is
# not a pair.
#
# Card indices are arguments, never constants: they are reassigned on every
# replug, and they FLIPPED between A and B across the 0x0031 reflash. Re-read
# them from sysfs each time and pass them in.
set -u

RATE=$1
SECS=$2
CARD_A=$3
CARD_B=$4
TAG=$5

cd /tmp || exit 1
rm -f "d${TAG}A.json" "d${TAG}B.json" "d${TAG}A.log" "d${TAG}B.log"

nohup python3 /tmp/measure_drift.py --card "$CARD_A" --rate "$RATE" \
      --seconds "$SECS" --label A --out "/tmp/d${TAG}A.json" \
      > "/tmp/d${TAG}A.log" 2>&1 &
nohup python3 /tmp/measure_drift.py --card "$CARD_B" --rate "$RATE" \
      --seconds "$SECS" --label B --out "/tmp/d${TAG}B.json" \
      > "/tmp/d${TAG}B.log" 2>&1 &

sleep 8
N=$(pgrep -c arecord || echo 0)
echo "tag=${TAG} rate=${RATE} secs=${SECS} cardA=${CARD_A} cardB=${CARD_B}"
echo "arecord processes running: ${N}  (expect 2)"
[ "$N" = "2" ] || echo "WARNING: expected 2 captures, see /tmp/d${TAG}*.log"
