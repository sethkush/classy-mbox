#!/bin/bash
# #151 part 2: is pb_resyncs == 0 "never needed" or "never ran"?
#
# Two independent observables, both mid-stream:
#   sof_count (block 5)  -- the SOF ISR is executing at all
#   mux word bit 7       -- panel_update_streaming() asserts it (drives it LOW)
#                           exactly when playback_running is set, which is the
#                           one condition streaming_sof() early-returns on.
# Both true => the watchdog's body runs every SOF, and a zero count means the
# buffer really was frame-aligned.
cd ~/mboxtmp
P="sudo /home/seth/mbox-venv/bin/python3 mboxtlm.py"
A_SN=RK10874600Q; B_SN=RK1672500M
card_of(){ for d in /sys/bus/usb/devices/*/; do [ "$(cat $d/serial 2>/dev/null)" = "$1" ] || continue
  for s in $d*/sound/card*; do [ -e "$s" ] && basename "$s"|sed s/card//&&return 0; done; done; return 1; }
A=$(card_of $A_SN); B=$(card_of $B_SN)
$P reset --serial $B_SN >/dev/null 2>&1
echo "=== IDLE (nothing streaming) ==="
$P read 5 --serial $B_SN | grep -E "sof_count"
$P read 9 --serial $B_SN | grep -o "mux word.*"
aplay -D hw:$B,0 /tmp/tone48.wav >/tmp/ap.log 2>&1 &
AP=$!
sleep 2
kill -0 $AP 2>/dev/null || { echo "PLAYER DIED"; cat /tmp/ap.log; exit 1; }
echo "=== DURING PLAYBACK ==="
for t in 1 2 3; do
  echo -n "  sample $t: "
  S=$($P read 5 --serial $B_SN | grep -o "sof_count:.*")
  M=$($P read 9 --serial $B_SN | grep -o "mux word  =0x[0-9A-F]*")
  R=$($P read 7 --serial $B_SN | grep -o "pb_resyncs:[ 0-9]*")
  echo "$S | $M | $R"
  sleep 4
done
kill $AP 2>/dev/null
