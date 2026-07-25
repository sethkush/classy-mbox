#!/usr/bin/env bash
# Fire each defensive ISR stub in s51 and verify LJMP -> RETI -> return works.
#
# Why: the vector-table LJMPs at 0x000B/0x0013/0x001B/0x0023/0x002B are
# emitted by SDCC only when a matching `__interrupt(N) { }` stub exists in
# a translation unit reachable at link time. A grep of build/main.rst proves
# the LJMPs are present, but nobody has actually _executed_ the stubs to
# confirm that (a) the LJMP lands in a valid stub body, (b) the stub's sole
# instruction is RETI (0x32), and (c) RETI pops the pre-interrupt PC back
# off the stack cleanly with no SFR corruption.
#
# Method: for each vector, seed IRAM stack with a sentinel return address
# (0x7FFE, well past any real code), set SP so RETI will pop it, then
# `run <vector> 0x7FFE` in s51. If the breakpoint at 0x7FFE fires, the
# LJMP+RETI round-trip worked; if it times out or halts elsewhere, the
# stub is broken or missing.
#
# Also verifies SP returned to its pre-interrupt value (net-zero push/pop).
#
# Usage:  tools/sim_reti_stub_test.sh

set -eu

# Sentinel "return address" we seed onto the stack. RETI pops it into PC,
# s51 breakpoint at this address then halts the sim. Chosen well outside
# any real code so we don't collide with an actual instruction stream.
SENTINEL=0x7FFE
SENTINEL_LO=0xFE
SENTINEL_HI=0x7F

# Pre-interrupt SP. Anything above the register banks (0x00-0x1F) and
# bit-addressable area (0x20-0x2F) is safe. 0x30 is the same convention
# SDCC uses for __start__stack in these firmwares.
SP_BASE=0x30
# After we push 2 bytes (LO then HI), SP == SP_BASE + 2 == 0x32.
SP_LOADED=0x32

# fire_vector <ihx> <vector_addr_hex>
# Returns 0 on PASS, 1 on FAIL. Prints one status line either way.
fire_vector() {
    local ihx="$1" vec="$2" label="$3"

    # s51 command sequence:
    #   1. Seed sentinel return address in IRAM at [SP_BASE+1, SP_BASE+2].
    #   2. Load SP so it points to the top pushed byte (SENTINEL_HI).
    #   3. Set PC to the vector.
    #   4. run until PC == SENTINEL (breakpoint fires BEFORE executing
    #      the byte at SENTINEL, so it's fine if that byte is garbage).
    #   5. dump SP so we can confirm net-zero push/pop.
    local out
    out=$(printf 'set memory iram %s %s %s\nset memory sfr 0x81 %s\npc %s\nrun %s %s\ndump sfr 0x81 0x81\nq\n' \
            "$(printf '0x%x' $((SP_BASE + 1)))" "$SENTINEL_LO" "$SENTINEL_HI" \
            "$SP_LOADED" "$vec" "$vec" "$SENTINEL" \
          | timeout 10 s51 -q "$ihx" 2>&1 || true)

    # Success line: "Stop at 0x007ffe: (104) Breakpoint"
    if ! echo "$out" | grep -qiE "Stop at 0x0*7ffe:.*Breakpoint"; then
        echo "  FAIL  $label vec=$vec: RETI did not return to sentinel $SENTINEL"
        echo "$out" | tail -8 | sed 's/^/        /'
        return 1
    fi

    # SP dump: "0x81 SP:  0b00110000 0x30 '0' 48" — RETI must have popped
    # the 2 bytes we pushed, so SP is back to SP_BASE (0x30).
    local sp_after
    # Two "0x81 SP:" lines appear: the first is echoed by `set memory sfr`
    # (pre-run value 0x32), the second is our post-run `dump sfr` (should
    # be back to 0x30 if RETI popped cleanly). Take the last match.
    sp_after=$(echo "$out" | awk '/0x81 SP:/ {v=$4} END {print v}')
    if [[ "$sp_after" != "0x30" ]]; then
        echo "  FAIL  $label vec=$vec: SP=$sp_after after RETI (expected 0x30)"
        return 1
    fi

    echo "  PASS  $label vec=$vec  (LJMP -> RETI -> ret; SP restored)"
    return 0
}

run_suite() {
    local name="$1" ihx="$2"; shift 2
    echo "== $name ($ihx) =="
    if [[ ! -f "$ihx" ]]; then
        echo "  SKIP  build artifact missing: $ihx"
        return 0
    fi
    local fails=0
    while (( "$#" >= 2 )); do
        fire_vector "$ihx" "$1" "$2" || fails=$((fails + 1))
        shift 2
    done
    return "$fails"
}

total_fail=0

# safety_net: 5 stubs (usb_isr at 0x03 is the only real handler).
run_suite "safety_net" "safety_net/build/safety_net.ihx" \
    0x000B "isr_timer0" \
    0x0013 "isr_int1  " \
    0x001B "isr_timer1" \
    0x0023 "isr_uart  " \
    0x002B "isr_timer2" \
    || total_fail=$((total_fail + $?))

# mboxfw: 4 stubs (isr_int0 @ 0x03 and isr_timer0 @ 0x0B are real handlers).
run_suite "mboxfw" "mboxfw/build/mboxfw.ihx" \
    0x0013 "isr_int1  " \
    0x001B "isr_timer1" \
    0x0023 "isr_uart  " \
    0x002B "isr_timer2" \
    || total_fail=$((total_fail + $?))

if (( total_fail > 0 )); then
    echo "RETI STUB TEST: $total_fail vector(s) failed"
    exit 1
fi
echo "RETI STUB TEST: all vectors PASS"
