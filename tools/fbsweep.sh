#!/bin/sh
# #215/#211 -- sweep the feedback endpoint's armed byte count and read back what
# the device actually emits at each setting.
#
# THE QUESTION. EP 0x82 emits NINE bytes per packet while IEPDCNTX2 says 3, and
# the 10.14 value in the first three bytes is correct. So the payload is right
# and the length is wrong. What is unknown is whether that register governs the
# length at all.
#
#   emitted length TRACKS the armed count  -> the register governs; the fix is
#       arithmetic, and the 9 has an off-by-six to find.
#   emitted length stays 9 regardless      -> the count is ignored entirely, the
#       length follows the buffer, and since EP_BSIZE() cannot express a buffer
#       smaller than 8 the fix cannot come from either register. The buffer has
#       to move, or the endpoint has to be rethought.
#
# Needs a 0x0056+ image built with MBOX_FB_TUNE=1; older builds stall the
# request, which shows up below as "poke FAILED" rather than as a silent no-op.
#
# Usage:  sudo fbsweep.sh <busnum> <devnum> [counts...]
#         sudo fbsweep.sh 2 10 1 2 3 4 5 6 7 8

set -u
BUS="${1:?usage: fbsweep.sh <busnum> <devnum> [counts...]}"
DEV="${2:?usage: fbsweep.sh <busnum> <devnum> [counts...]}"
shift 2
COUNTS="${*:-1 2 3 4 6 8}"

# NOT $HOME/mbox-venv: this script runs under sudo (insmod needs it), and sudo
# resets HOME to /root, so $HOME/mbox-venv resolves to a venv that does not
# exist. Every poke then failed with the script's own "pre-0x0056 image"
# message -- which pointed at the firmware for what was a host-side path bug.
# Prefer the invoking user's venv, fall back to a system python.
_owner="${SUDO_USER:-$(id -un)}"
_home=$(getent passwd "$_owner" 2>/dev/null | cut -d: -f6)
PY="${PY:-${_home:-$HOME}/mbox-venv/bin/python}"
[ -x "$PY" ] || PY=$(command -v python3)
MOD="${MOD:-/tmp/ch9mod/fbmax.ko}"
TLM_REQ_FB_TUNE=0x18

printf '%-7s %-9s %s\n' "armed" "emitted" "first bytes / status"
printf -- '---------------------------------------------------------------\n'

for n in $COUNTS; do
    # Poke the count. bmRequestType 0x40 = vendor, host-to-device, DEVICE
    # recipient -- deliberately not interface, so it still lands with
    # snd-usb-audio bound. See telemetry.h.
    if ! "$PY" - "$BUS" "$DEV" "$n" <<'PYEOF' 2>/dev/null
import sys, usb.core
bus, dev, n = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
d = next((x for x in usb.core.find(find_all=True, idVendor=0x0dba)
          if x.bus == bus and x.address == dev), None)
if d is None:
    sys.exit(1)
d.ctrl_transfer(0x40, 0x18, n, 0, None, 2000)
PYEOF
    then
        printf '%-7s %-9s %s\n' "$n" "--" "poke FAILED -- device stalled it, or $PY is wrong"
        continue
    fi

    # Read back. Schedule 32 bytes so nothing is rejected as babble no matter
    # what the device decides to send -- the point is to observe the length, not
    # to constrain it.
    dmesg -C 2>/dev/null
    insmod "$MOD" busnum="$BUS" devnum="$DEV" pktsize=32 npkts=4 2>/dev/null
    line=$(dmesg | grep -m1 'fbmax:  pkt' | sed 's/.*fbmax:  //')
    [ -z "$line" ] && line="(no packet carried data)"
    emitted=$(printf '%s' "$line" | sed -n 's/.*: \([0-9]*\) bytes.*/\1/p')
    printf '%-7s %-9s %s\n' "$n" "${emitted:-0}" "$line"
done

printf -- '---------------------------------------------------------------\n'
echo "If every row emits 9 regardless of the armed count, IEPDCNTX2 does not"
echo "govern the length and the fix is not in that register."
echo
echo "Restore the shipping value when done:  sudo fbsweep.sh $BUS $DEV 3"
