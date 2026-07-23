#!/usr/bin/env bash
# Static analysis of the DFU-response code path. Counts the MAXIMUM
# number of 8051 machine cycles from `handle_setup` entry to
# `reply_zero_length` (which stages the ACK) along the
# Digi-DFU-recognition branch.
#
# Why static: s51 doesn't accurately model TAS1020A's VECINT auto-clear
# / interrupt-source register semantics, so a dynamic trigger-and-time
# test gives wrong numbers. Static path analysis via .rst pass is
# deterministic and matches real silicon at 12 MHz.
#
# Goal: total path < 4000 cycles ≈ 333 µs @ 12 MHz.
# Windows' fastest observed SET_CUR timeout is ~50 ms; 333 µs is 150x
# under that, so any host will see our ACK land in time.

set -eu
cd "$(dirname "$0")/.."

RST="mboxfw/build/usb.rst"
[[ -f "$RST" ]] || { echo "run make first" >&2; exit 2; }

# The relevant chain:
#   handle_setup                → reads bmReq/bReq/wValueL/wValueH
#     if recipient=interface + Digi match → handle_digi_enter_dfu
#       reply_zero_length()      ← this write is the "responded" point
#       ~48000-cycle delay       ← we do this AFTER the ACK
#       eeprom_smoke_test        ← after ACK
#       ljmp 0                   ← after ACK
#
# So the response deadline is between handle_setup entry and the
# IEPBCTX0=0 write inside reply_zero_length.

count_cycles_between() {
    local from="$1"
    local to="$2"
    awk -v from="$from" -v to="$to" '
        # Lines look like:   00042A E5 82 [12]   nnn instr...
        # The [12] or [24] is the cycle count.
        $0 ~ from        { seen=1 }
        seen             {
            if (match($0, /\[[0-9]+\]/)) {
                cycles += substr($0, RSTART+1, RLENGTH-2)
            }
        }
        $0 ~ to && seen  { print cycles; exit }
    ' "$RST"
}

# handle_setup starts at label _handle_setup:. reply_zero_length is
# labeled _reply_zero_length: and its first act is writing IEPBCTX0 = 0
# (a mov+movx or clr a + movx). We measure from _handle_setup to the
# first `mov dptr,#0xff6b` on the Digi branch.
#
# Simpler: count cycles in _handle_setup up to its `lcall
# _handle_digi_enter_dfu`, plus cycles from that entry to its
# `lcall _reply_zero_length`, plus cycles inside _reply_zero_length
# to the write. Each is a bounded static span.

CYC_SETUP=$(awk '
    /_handle_setup:/            { seen=1 }
    seen && /_handle_digi_enter_dfu/  {
        # Include the lcall itself (24 cyc)
        cycles += 24
        print cycles; exit
    }
    seen && match($0, /\[[0-9]+\]/)   {
        cycles += substr($0, RSTART+1, RLENGTH-2)
    }
' "$RST")

CYC_DFU=$(awk '
    /_handle_digi_enter_dfu:/   { seen=1 }
    seen && /_reply_zero_length/      {
        cycles += 24
        print cycles; exit
    }
    seen && match($0, /\[[0-9]+\]/)   {
        cycles += substr($0, RSTART+1, RLENGTH-2)
    }
' "$RST")

CYC_RZL=$(awk '
    /_reply_zero_length:/       { seen=1 }
    seen && /movx.*@dptr,a/           {
        cycles += 24
        print cycles; exit
    }
    seen && match($0, /\[[0-9]+\]/)   {
        cycles += substr($0, RSTART+1, RLENGTH-2)
    }
' "$RST")

TOTAL=$((CYC_SETUP + CYC_DFU + CYC_RZL))

printf "  handle_setup     → handle_digi_enter_dfu : %5d cycles\n" "$CYC_SETUP"
printf "  handle_digi_dfu  → reply_zero_length     : %5d cycles\n" "$CYC_DFU"
printf "  reply_zero_len   → IEPBCTX0 write        : %5d cycles\n" "$CYC_RZL"
printf "  TOTAL SETUP → ACK stage                  : %5d cycles\n" "$TOTAL"

# 12 MHz TAS1020A: 12 machine cycles per µs → cycles / 12 = µs.
US=$((TOTAL / 12))
printf "  at 12 MHz                                : %5d µs\n" "$US"

# Threshold: 4000 cycles (~333 µs) is 150× under Windows' fastest
# observed SET_CUR timeout of ~50 ms.
if (( TOTAL < 4000 )); then
    echo
    echo "TIMING PASS: DFU response path under 4000-cycle budget."
    echo "             150× headroom vs. host SET_CUR timeouts."
    exit 0
fi
echo
echo "TIMING FAIL: DFU response path exceeds 4000 cycles." >&2
echo "             Host may time out the class request before we ACK." >&2
exit 1
