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
    check "verify_setup_paths"        python3 tools/verify_setup_paths.py
    check "usb_init unconditionally reached"    python3 tools/verify_conn_reachable.py
    check "SFR writes match manifest"           python3 tools/audit_sfr_writes.py
    check "Rev-20 SFR diff justified"           python3 tools/diff_vs_rev20.py
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
