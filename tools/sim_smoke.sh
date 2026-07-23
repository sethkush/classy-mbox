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
# us and we fail. Reaching STOP means usb_init + hw_init + cs8427_boot_init
# + codec_init all returned cleanly.
#
# After reaching the loop, also dump XDATA[0xFFFC] (USBCTL) to verify the
# CONN bit (0x80) is set — proof that usb_init actually attached us to
# the bus, not just that the code path returned. Assignment-vs-OR bugs
# (like 2026-07-22 flash #2 where `USBCTL = 0xC0` clobbered SDW) would
# have shown up here as a missing or wrong-value read.
out=$(timeout 20 s51 -q \
    -e "run 0 $LOOP_ENTRY" \
    -e "dx 0xfffc 0xfffc" \
    -e "q" "$IHX" 2>&1 || true)

# `Stop at 0xNNNN: (104) Breakpoint` is the success-reaching-main line.
loop_norm=$(printf "0x%06x" $((LOOP_ENTRY)))
if ! echo "$out" | grep -q "Stop at ${loop_norm}:.*Breakpoint"; then
    echo "SMOKE FAIL: did not reach $LOOP_ENTRY within 20s" >&2
    echo "$out" | tail -20 >&2
    exit 1
fi

# `dx 0xfffc 0xfffc` prints one line like:  "0xfffc a0 ."
# Extract the byte value.
usbctl=$(echo "$out" | grep -iE "^0xfffc" | awk '{print $2}')
if [[ -z "$usbctl" ]]; then
    echo "SMOKE FAIL: could not read XDATA[0xFFFC] (USBCTL) from sim output" >&2
    echo "$out" | tail -20 >&2
    exit 1
fi

# CONN is bit 7 = 0x80. Any value with bit 7 set is a pass; we
# specifically want to catch USBCTL == 0 which is what a missing
# `USBCTL |= CONN` would leave.
usbctl_int=$((16#$usbctl))
if (( (usbctl_int & 0x80) == 0 )); then
    echo "SMOKE FAIL: USBCTL[0xFFFC] = 0x$usbctl — CONN bit (0x80) NOT set." >&2
    echo "  device would come up silent on USB; usb_init did not attach to bus." >&2
    exit 1
fi

ticks=$(echo "$out" | grep -oE "stepped [0-9]+ ticks" | tail -1)
echo "SMOKE PASS: reached main loop @ $LOOP_ENTRY, USBCTL=0x$usbctl (CONN set) ($ticks)"
exit 0
