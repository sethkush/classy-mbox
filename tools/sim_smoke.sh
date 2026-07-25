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
# s51's -e commands seem to execute BEFORE `run` returns — dx probes
# the initial memory state instead of post-run. Feed commands via a
# scripted config file so they execute in order. break/g/dx/q semantics
# guarantee the memory reads happen after the sim halts at LOOP_ENTRY.
# s51's -e flag executes commands NON-blocking against the simulator
# thread — dx reads run BEFORE the run command finishes. Feeding
# commands via stdin (one per line) gives sequential execution: run
# blocks until the breakpoint hits, THEN dx reads the post-run memory.
out=$(printf 'run 0 %s\ndx 0xfffc 0xfffc\ndx 0xfa00 0xfa05\nq\n' "$LOOP_ENTRY" \
      | timeout 20 s51 -q "$IHX" 2>&1 || true)

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

# The sim's default XDATA[0xFFFC] on reset is 0xA0 (bit 7 already set),
# so the byte-level check above passes even if the firmware NEVER
# wrote USBCTL. Belt-and-suspenders: parse the emitted .ihx for at
# least one `mov dptr,#0xFFFC` (`90 FF FC`) followed by any movx
# write within 8 bytes. If missing, no code ever touches USBCTL.
usbctl_write_present=$(python3 -c "
import sys
data = bytearray()
for line in open('$IHX'):
    line = line.strip()
    if not line.startswith(':'): continue
    n = int(line[1:3],16); a = int(line[3:7],16); t = int(line[7:9],16)
    if t != 0: continue
    b = bytes.fromhex(line[9:9+n*2])
    if a + n > len(data):
        data.extend([0] * (a + n - len(data)))
    for i, x in enumerate(b): data[a+i] = x
tgt = bytes([0x90, 0xFF, 0xFC])
i = 0
while True:
    j = data.find(tgt, i)
    if j < 0:
        print('MISSING'); sys.exit(0)
    if 0xF0 in data[j+3:j+3+8]:
        print('OK'); sys.exit(0)
    i = j + 1
" 2>&1 || echo "PYERR")
if [[ "$usbctl_write_present" != "OK" ]]; then
    echo "SMOKE FAIL: no code path writes USBCTL[0xFFFC]. Firmware never" >&2
    echo "  attaches to the USB bus — the sim's 0xA0 default masks this." >&2
    exit 1
fi

# Read phase canaries at 0xFA00..0xFA05. `dx 0xfa00 0xfa05` prints
# a single line like: "0xfa00 a1 a2 a3 a4 a5 a6 ......"
canary_line=$(echo "$out" | grep -iE "^0xfa00")
if [[ -z "$canary_line" ]]; then
    echo "SMOKE FAIL: could not read canary bytes at 0xFA00" >&2
    echo "$out" | tail -20 >&2
    exit 1
fi
# Expected canary values (must match mboxfw/src/main.c CANARY_*).
# Kept as positional indexes because macOS ships bash 3.2 (no assoc arrays).
EXPECT=(a1 a2 a3 a4 a5 a6)
PHASE_NAMES=(main usb_init hw_init cs8427_init codec_init main_loop_entry)
# canary_line looks like "0xfa00 a1 a2 a3 a4 a5 a6 ......"
read -r _addr b0 b1 b2 b3 b4 b5 _rest <<< "$canary_line"
got=("$b0" "$b1" "$b2" "$b3" "$b4" "$b5")
missed_phase=""
for i in 0 1 2 3 4 5; do
    if [[ "${got[$i]}" != "${EXPECT[$i]}" ]]; then
        missed_phase="${PHASE_NAMES[$i]}"
        break
    fi
done
if [[ -n "$missed_phase" ]]; then
    echo "SMOKE FAIL: canary for phase '$missed_phase' not set." >&2
    echo "  expected canary bytes: a1 a2 a3 a4 a5 a6" >&2
    echo "  got:                   ${got[*]}" >&2
    echo "  → firmware hung during or before '$missed_phase'" >&2
    exit 1
fi

ticks=$(echo "$out" | grep -oE "stepped [0-9]+ ticks" | tail -1)
echo "SMOKE PASS: reached main loop @ $LOOP_ENTRY, USBCTL=0x$usbctl (CONN set),"
echo "            all 6 phase canaries set (main → usb → hw → cs8427 → codec → loop)"
exit 0
