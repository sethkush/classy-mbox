#!/usr/bin/env bash
# Pre-flash checklist. Runs every gate + prompts for the four
# checklist items from POLICY.md §6. Refuses to say "ready to flash"
# without explicit sign-off on each.
#
# Usage:
#   tools/preflight.sh <image.bin>
#
# On success, prints the exact mboxflash command to run + a summary of
# what was verified.

set -eu
cd "$(dirname "$0")/.."

IMG="${1:?usage: $0 <image.bin>}"
[[ -f "$IMG" ]] || { echo "not found: $IMG" >&2; exit 2; }

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; NORMAL=$'\033[0m'

pass=0; fail=0
declare -a failures

check() {
    local name="$1"; shift
    printf "  %-45s " "$name"
    if "$@" >/dev/null 2>&1; then
        printf "%sPASS%s\n" "$GREEN" "$NORMAL"
        pass=$((pass+1))
    else
        printf "%sFAIL%s\n" "$RED" "$NORMAL"
        failures+=("$name")
        fail=$((fail+1))
    fi
}

echo "=== GATE RUN ==="

# Target detection: safety_net-flavored images run the safety_net gate
# set. All others (mboxfw + any future targets) run the mboxfw set.
# The wrap_hex + validate + code-size gates apply to both.
case "$(basename "$IMG")" in
    safety_net_*.bin) TARGET=safety_net ;;
    *)                TARGET=mboxfw ;;
esac
echo "  (target: $TARGET)"

# Gates that apply to the compiled image (not the flasher bin).
# preflight is called on a wrapped .bin, but the gates run against the
# build artifacts that produced it. We assume the working tree matches.
check "SDCC version matches pin"            bash    tools/check_sdcc_version.sh
check "wrap_hex golden regression"          python3 tools/test_wrap_hex_golden.py
check "SFR register names consistent"       python3 tools/check_sfr_names.py
check "quoted bytes match the images"       python3 tools/check_byte_quotes.py
check "citations land on target (both revs)" python3 tools/check_citation_targets.py
# Every bit of IRAM 0x22/0x23/0x25 is a physical control line on one of the two
# P1 shift chains. This asks, per bit, "stock drives it -- does mboxfw?" It
# exists because that question was asked by hand exactly once, about 0x23.4,
# and the answer was a defect (#166). Nothing was checking the other 23.
check "latch-word bit coverage vs stock"    python3 tools/latch_word_bit_diff.py
# The access map's direction classifier got 26 entries wrong once; these are the
# hand-verified sites that pinned each failure mode.
check "access-map classifier self-test"     python3 tools/xdata_access_map.py --selftest
# The decompilation is only a claim about the stock image if it still
# reproduces it. match51 checks each candidate standalone; link51 places them
# all at their stock addresses and resolves every inter-function call for real,
# which is what catches wrong call targets and functions that grew past their
# stock extent.
check "decomp candidates match stock"      python3 tools/match51.py firmware_stock/decomp/cand/*.c
# cand/ must be exact. A declared partial belongs in cand/partial/, where it is
# still checked -- but allowing one to sit in cand/ would quietly weaken the
# gate above into "matches, or has an excuse".
check "no declared partials in cand/"      sh -c '! grep -hE "^// MATCH:.*partial=" firmware_stock/decomp/cand/*.c 2>/dev/null | grep -q .'
# An empty cand/partial/ is the normal state now that both images are exact;
# the mechanism stays for the next stubborn function. Only run the check when
# there is something to check, rather than failing on an unexpanded glob.
check "declared partials hold exactly"     sh -c 'set -- firmware_stock/decomp/cand/partial/*.c; [ -e "$1" ] || exit 0; python3 tools/match51.py "$@"' 
check "decomp links at stock addresses"    python3 tools/link51.py rev20
check "decomp links at stock addresses (v22)" python3 tools/link51.py rev22
# The strongest statement the decompilation can make, and the cheapest to check:
# build the whole ROM from source and diff it against the part's contents.
check "rebuilt image == stock rev20"       sh -c 'python3 tools/link51.py rev20 --emit-image "$(mktemp -t r20)" | grep -q "IMAGE IDENTICAL"'
check "rebuilt image == stock rev22"       sh -c 'python3 tools/link51.py rev22 --emit-image "$(mktemp -t r22)" | grep -q "IMAGE IDENTICAL"' 

if [[ "$TARGET" == "safety_net" ]]; then
    # Safety-net path: minimal, single-file image whose sole job is
    # to enumerate on USB and accept the Digi DFU class trigger.
    check "verify_safety_net init writes"   python3 tools/verify_safety_net.py
    check "mboxflash --validate on target"  ./mboxflash/mboxflash --validate "$IMG"
else
    check "code size within budget"             python3 tools/check_code_size.py
    check "sim_smoke (main loop + CONN + canaries)" bash tools/sim_smoke.sh
    check "verify_descriptors"        python3 tools/verify_descriptors.py
    check "verify_usb_init"           python3 tools/verify_usb_init.py
    check "verify_cs8427"             python3 tools/verify_cs8427.py
    # verify_cs8427 checks the ten register VALUES. This runs the image and
    # decodes the P1 waveform, so it checks the framing, the chip select, the
    # RESET release and the latch chain that carries them -- the things #157,
    # #166 and #167 were each wrong about, none of which a static gate can see.
    # Validated by decoding Rev 20 and Rev 22 the same way and requiring the
    # same answer. FINDING_bringup_waveform.md.
    check "P1 waveform vs stock (executed)"     python3 tools/sim_p1_waveform.py
    # Everything above drives the firmware with NO INPUT. This delivers a
    # SETUP packet to XDATA 0xFF28, sets VECINT, and reads back what gets
    # staged -- descriptors, GET_STATUS, the telemetry protocol, and the
    # STALL on an unsupported request. FINDING_ep0_request_harness.md.
    check "EP0 answers requests (executed)"     python3 tools/sim_ep0_requests.py
    check "verify_setup_paths"        python3 tools/verify_setup_paths.py
    check "usb_init unconditionally reached"    python3 tools/verify_conn_reachable.py
    check "SFR writes match manifest"           python3 tools/audit_sfr_writes.py
    check "Rev-20 SFR diff justified"           python3 tools/diff_vs_rev20.py
    # Core-SFR diff. The two gates above are MOVX-only, so the 8051 direct SFR
    # space (TCON/TMOD/IE/IP/PCON/P1/P3) and every bit-addressable SFR bit were
    # never compared against stock. That is how TR0 stayed unset for weeks with
    # every gate green -- Timer 0 never ran. Mutation-verified.
    check "Core-SFR + bit diff vs stock"        python3 tools/sfr_direct_diff.py
    # Call-graph gate. Every check above is whole-image, so none can tell
    # "the firmware sets TR0" from "TR0 is set on a path that runs at boot".
    # Also catches emitted-but-uncalled function bodies, which is how three
    # busy-wait delays came to be deleted at every call site.
    check "boot-path reachability + orphans"     python3 tools/verify_reachability.py
    # Ordering gate. Every check above is a set/value question; this is the only
    # one that asks about SEQUENCE, including "CPTEN is set only after the
    # codec-port registers are configured".
    check "hw_init write order vs stock"         python3 tools/verify_init_order.py
    check "DFU response timing"                 bash    tools/dfu_timing_profile.sh
    check "mboxflash --validate on target"      ./mboxflash/mboxflash --validate "$IMG"
fi

echo
if (( fail > 0 )); then
    printf "%sGATES FAILED (%d):%s\n" "$RED" "$fail" "$NORMAL"
    for f in "${failures[@]}"; do
        echo "  - $f"
    done
    echo
    echo "Do not flash. Re-run individual failed gates for details:"
    for f in "${failures[@]}"; do
        echo "  # $f"
    done
    exit 1
fi
printf "%sAll %d gates PASSED.%s\n" "$GREEN" "$pass" "$NORMAL"

echo
echo "=== CHECKLIST (POLICY.md §6) ==="
echo
echo "  1. CHANGED SINCE LAST FLASH"
echo "     Recent commits touching firmware:"
git log --oneline -10 -- mboxfw/ safety_net/ tools/wrap_hex.py 2>/dev/null | sed 's/^/       /'
echo
read -r -p "  → Everything above is intentional and reviewed? [yes/no] " ans
[[ "$ans" == "yes" ]] || { echo "aborted."; exit 3; }

echo
echo "  2. UNKNOWNS"
echo "     What have you NOT verified end-to-end on real hardware since last flash?"
read -r -p "  → List each (or 'none'): " unknowns
[[ -n "$unknowns" ]] || { echo "must list at least 'none'; aborted."; exit 3; }

echo
echo "  3. ROLLBACK PLAN"
echo "     Available restore images in backups/:"
ls -la backups/*.bin 2>/dev/null | awk '{print "       " $NF, "(" $5 " bytes)"}' | head -5
echo "     Recovery: mboxflash --enter-dfu → mboxflash --flash <backup>"
echo "     If --enter-dfu fails: hold source-1 during replug (button DFU)."
echo "     If both fail: physical SDA short (see recovery notes)."
read -r -p "  → Rollback plan is understood? [yes/no] " ans
[[ "$ans" == "yes" ]] || { echo "aborted."; exit 3; }

echo
echo "  4. RECOVERY PATHS ACTIVE IN THE FLASHED FIRMWARE"
grep -q "handle_digi_enter_dfu" mboxfw/src/usb.c 2>/dev/null && \
    echo "     ✓ class-request DFU trigger (mboxflash --enter-dfu)" || \
    echo "     ✗ class-request DFU trigger MISSING"
grep -q "check_boot_dfu_button" mboxfw/src/main.c 2>/dev/null && \
    echo "     ✓ boot-time button-hold DFU trigger" || \
    echo "     ✗ boot-time button DFU trigger MISSING"
[[ "$IMG" == *safety_net* ]] && \
    echo "     ✓ this IS the safety-net firmware" || \
    echo "     Consider flashing safety_net first if not recently done"
read -r -p "  → Recovery paths acceptable for this flash? [yes/no] " ans
[[ "$ans" == "yes" ]] || { echo "aborted."; exit 3; }

echo
echo "=== READY TO FLASH ==="
echo
echo "Command:"
echo "  echo yes | ./mboxflash/mboxflash --flash $IMG"
echo
echo "Post-flash: run tools/e2e_flash_loop.sh $IMG to verify round-trip."
