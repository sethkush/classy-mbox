#!/usr/bin/env bash
# safety_net twin of tools/dfu_timing_profile.sh.
#
# Static analysis of the DFU-response code path in safety_net. Counts
# the MAXIMUM number of 8051 machine cycles from `_handle_setup` entry
# to `_reply_zlp` (which stages the ACK via IEPBCTX0=0) along the
# Digi-DFU-recognition branch.
#
# Why this matters for safety_net specifically: the entire justification
# for shipping this firmware is that it can answer the Digi enter-DFU
# request fast enough that a host doesn't time out. If our SETUP-to-ACK
# path is slow, the recovery firmware is useless — mboxflash --enter-dfu
# would fail and you'd be back to physical EEPROM shorts.
#
# Structural differences from the mboxfw twin:
#   - safety_net inlines the whole thing in main.rst (single .c file).
#   - The recognition + dispatch happens inside _handle_setup itself
#     (no separate _handle_digi_enter_dfu wrapper); instead there is
#     a `_handle_dfu_trigger` that runs AFTER reply_zlp (delay, EEPROM
#     invalidate, boot-ROM re-entry). The response deadline is
#     _handle_setup entry → _reply_zlp entry → IEPBCTX0=0 write.
#
# Same threshold: 4000 cycles ≈ 333 µs @ 12 MHz. 150× under Windows'
# fastest observed SET_CUR timeout.

set -eu
cd "$(dirname "$0")/.."

RST="safety_net/build/main.rst"
[[ -f "$RST" ]] || { echo "run \`make -C safety_net\` first" >&2; exit 2; }

# Sum the [NN]-tagged cycle counts of every instruction between two
# label patterns in the .rst, up to and including the LCALL/LJMP that
# leaves the region.
#
# `handle_setup` in safety_net tail-calls `reply_zlp` via LJMP when
# SET_ADDRESS / SET_CONFIGURATION / SET_INTERFACE ACK, and via
# `handle_dfu_trigger()` for the Digi request. The critical path we
# time is the Digi branch: handle_setup → handle_dfu_trigger →
# reply_zlp → IEPBCTX0 write. Same shape as the mboxfw twin, one
# level of indirection deeper (the C-visible name is different).

CYC_SETUP=$(awk '
    /_handle_setup:/                { seen=1 }
    seen && /_handle_dfu_trigger/   {
        cycles += 24        # ljmp/lcall itself
        print cycles; exit
    }
    seen && match($0, /\[[0-9]+\]/) {
        cycles += substr($0, RSTART+1, RLENGTH-2)
    }
' "$RST")

CYC_DFU=$(awk '
    /_handle_dfu_trigger:/          { seen=1 }
    seen && /_reply_zlp/            {
        cycles += 24
        print cycles; exit
    }
    seen && match($0, /\[[0-9]+\]/) {
        cycles += substr($0, RSTART+1, RLENGTH-2)
    }
' "$RST")

CYC_RZL=$(awk '
    /_reply_zlp:/                   { seen=1 }
    seen && /movx.*@dptr,a/         {
        cycles += 24
        print cycles; exit
    }
    seen && match($0, /\[[0-9]+\]/) {
        cycles += substr($0, RSTART+1, RLENGTH-2)
    }
' "$RST")

# The DFU path in safety_net actually replies BEFORE the invalidate /
# boot-ROM handoff, so what matters is time-to-ACK: everything after
# the IEPBCTX0 write (delay loop, eeprom_wr, ljmp 0x8000) happens off
# the wire deadline. This matches the mboxfw twin's rationale.

TOTAL=$((CYC_SETUP + CYC_DFU + CYC_RZL))

printf "  handle_setup     → handle_dfu_trigger    : %5d cycles\n" "$CYC_SETUP"
printf "  handle_dfu_trig  → reply_zlp             : %5d cycles\n" "$CYC_DFU"
printf "  reply_zlp        → IEPBCTX0 write        : %5d cycles\n" "$CYC_RZL"
printf "  TOTAL SETUP → ACK stage                  : %5d cycles\n" "$TOTAL"

US=$((TOTAL / 12))
printf "  at 12 MHz                                : %5d µs\n" "$US"

if (( TOTAL < 4000 )); then
    echo
    echo "TIMING PASS (safety_net): DFU response path under 4000-cycle"
    echo "                          budget. 150× headroom vs. host"
    echo "                          SET_CUR timeouts."
    exit 0
fi
echo
echo "TIMING FAIL (safety_net): DFU response path exceeds 4000 cycles." >&2
echo "                          Host may time out enter-DFU before we ACK." >&2
exit 1
