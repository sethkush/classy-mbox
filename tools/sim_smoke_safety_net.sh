#!/usr/bin/env bash
# safety_net twin of tools/sim_smoke.sh.
#
# Smoke-test safety_net.ihx in the SDCC / ucSim 8051 simulator.
#
# Passes if the CPU reaches the idle busy-loop at the end of main()
# within the sim timeout — proof that init runs cleanly with no trap
# or hang and USBCTL's CONN bit is set.
#
# Structural differences from mboxfw's sim_smoke:
#   1. safety_net has no phase-canary bytes at 0xFA00 — the whole
#      point of the firmware is minimality; adding canaries would
#      inflate the .ihx we're trying to keep visually eyeballable
#      under 1 KB. So the canary check is DROPPED here.
#   2. Loop entry is `sjmp .` at the tail of _main (a self-jump —
#      SDCC label like 00107$: immediately followed by `80 FE`).
#      Multiple 00NNN$ labels exist in main.rst, so we can't grep
#      by label name — resolve by locating _main: and finding the
#      first self-jump within its span.
#   3. safety_net runs a ~65k-iteration settle loop between init
#      and the CONN attach; even at s51's speed this fits within
#      a 30s budget on modern hardware.
#   4. Post-hoc USBCTL-write scan uses the same `mov dptr,#0xFFFC
#      + movx within 8 bytes` fingerprint; safety_net writes USBCTL
#      three times (disconnect at entry, VEC_RSTR handler, attach
#      at end), any of which trips the check.
#
# Usage:  tools/sim_smoke_safety_net.sh [path/to/safety_net.ihx]

set -eu

IHX="${1:-safety_net/build/safety_net.ihx}"
RST="$(dirname "$IHX")/main.rst"
if [[ ! -f "$IHX" ]]; then
    echo "not found: $IHX" >&2
    exit 2
fi
if [[ ! -f "$RST" ]]; then
    echo "not found: $RST (needed to locate the main-loop entry)" >&2
    exit 2
fi

# Locate the `for(;;) {}` idle loop at the end of _main. Structure
# in main.rst:
#     ...
#     0004D7                       1320 00107$:
#     0004D7 80 FE            [24] 1322 sjmp    00107$
#
# Multiple 00107$ labels exist across other functions; we only care
# about the one inside _main. Awk from `_main:` forward, remember the
# most-recent label address seen, and on the first `sjmp` whose target
# address (previous label address, since it's a self-jump encoded as
# `80 FE`) matches — capture it.
LOOP_ENTRY=$(python3 - "$RST" <<'PY'
import re, sys
lines = open(sys.argv[1]).read().splitlines()
in_main = False
last_addr = None
label_re = re.compile(r'^\s+([0-9A-F]{4,6})\s+\d+\s+00\d+\$:\s*$')
sjmp_re  = re.compile(r'^\s+([0-9A-F]{4,6})\s+80\s+FE\b')
for line in lines:
    if '_main:' in line and re.match(r'^\s+[0-9A-F]{4,6}\s+\d+\s+_main:', line):
        in_main = True
        continue
    if not in_main:
        continue
    m = label_re.match(line)
    if m:
        last_addr = m.group(1)
        continue
    m = sjmp_re.match(line)
    if m and m.group(1) == last_addr:
        print("0x" + m.group(1).lower())
        break
PY
)

if [[ -z "$LOOP_ENTRY" ]]; then
    echo "could not locate _main idle sjmp in $RST" >&2
    exit 2
fi

# Same run-until-breakpoint + dx script as sim_smoke.sh. safety_net's
# init has a ~65k-iter settle loop, so budget 30s instead of 20s. On
# a Mac mini M2 the actual run completes in <2s.
out=$(printf 'run 0 %s\ndx 0xfffc 0xfffc\nq\n' "$LOOP_ENTRY" \
      | timeout 30 s51 -q "$IHX" 2>&1 || true)

loop_norm=$(printf "0x%06x" "$LOOP_ENTRY")
if ! echo "$out" | grep -q "Stop at ${loop_norm}:.*Breakpoint"; then
    echo "SMOKE FAIL (safety_net): did not reach $LOOP_ENTRY within 30s" >&2
    echo "$out" | tail -20 >&2
    exit 1
fi

usbctl=$(echo "$out" | grep -iE "^0xfffc" | awk '{print $2}')
if [[ -z "$usbctl" ]]; then
    echo "SMOKE FAIL (safety_net): could not read XDATA[0xFFFC] (USBCTL)" >&2
    echo "$out" | tail -20 >&2
    exit 1
fi

usbctl_int=$((16#$usbctl))
if (( (usbctl_int & 0x80) == 0 )); then
    echo "SMOKE FAIL (safety_net): USBCTL[0xFFFC] = 0x$usbctl — CONN bit" >&2
    echo "  (0x80) NOT set. Device would come up silent on USB." >&2
    exit 1
fi

# Belt-and-suspenders post-hoc scan: at least one code path must write
# USBCTL (the sim's default XDATA[0xFFFC] = 0xA0 already has bit 7 set,
# so the byte-level check above passes even if firmware never wrote).
# safety_net writes USBCTL from three call sites; any one satisfies.
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
    echo "SMOKE FAIL (safety_net): no code path writes USBCTL[0xFFFC]." >&2
    echo "  Firmware never attaches to the USB bus — the sim's 0xA0" >&2
    echo "  default masks this. main.c:437-444 or 520 should have emitted" >&2
    echo "  the write; refactor probably dropped it." >&2
    exit 1
fi

echo "SMOKE PASS (safety_net): reached idle loop @ $LOOP_ENTRY,"
echo "                         USBCTL=0x$usbctl (CONN set),"
echo "                         USBCTL write present in code stream"
exit 0
