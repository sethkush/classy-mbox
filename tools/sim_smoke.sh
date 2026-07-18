#!/usr/bin/env bash
# Smoke-test mboxfw.ihx in the SDCC / ucSim 8051 simulator.
#
# Passes if the CPU reaches the main polling loop (0x00E4 or the second
# instruction 0x00E7) within 100 000 steps — proof that hw_init(),
# cs8427_boot_init(), and usb_init() all return cleanly with no trap or
# infinite loop.
#
# The simulator doesn't model the TAS1020A's USB SFRs or the CS8427 on
# P1.3/P1.4, so this only catches control-flow / stack / calling-convention
# bugs. That's still worth catching before we spend a physical flash cycle.
#
# Usage:  tools/sim_smoke.sh [path/to/mboxfw.ihx]

set -eu

IHX="${1:-mboxfw/build/mboxfw.ihx}"
RST="${IHX%.ihx}"
RST="$(dirname "$IHX")/main.rst"
if [[ ! -f "$IHX" ]]; then
    echo "not found: $IHX" >&2
    exit 2
fi
if [[ ! -f "$RST" ]]; then
    echo "not found: $RST (needed to locate the main-loop entry symbol)" >&2
    exit 2
fi

# Extract the address of the post-init main loop entry (SDCC labels it
# 00102$: — the sjmp target at the end of main).  This shifts every time
# we add/remove init calls, so we always resolve it from the .rst.
LOOP_ENTRY=$(grep -m1 '00102[$]:' "$RST" | awk '{print "0x"$1}')

if [[ -z "$LOOP_ENTRY" ]]; then
    echo "could not locate 00102\$ (loop entry) in $RST" >&2
    exit 2
fi

# `run 0 STOP` runs from address 0 until PC hits STOP (a breakpoint), so
# if the CPU is stuck in an earlier trap or infinite loop the timeout kills
# us and we fail. Reaching STOP means hw_init + cs8427_boot_init + usb_init
# all returned cleanly.
out=$(timeout 20 s51 -q -e "run 0 $LOOP_ENTRY" -e "pc" -e "q" "$IHX" 2>&1 || true)

# `Stop at 0xNNNN: (104) Breakpoint` is the success line.
loop_norm=$(printf "0x%06x" $((LOOP_ENTRY)))
if echo "$out" | grep -q "Stop at ${loop_norm}:.*Breakpoint"; then
    ticks=$(echo "$out" | grep -oE "stepped [0-9]+ ticks" | tail -1)
    echo "SMOKE PASS: reached main loop @ $LOOP_ENTRY ($ticks)"
    exit 0
fi

echo "SMOKE FAIL: did not reach $LOOP_ENTRY within 20s" >&2
echo "$out" | tail -20 >&2
exit 1
