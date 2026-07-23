#!/usr/bin/env bash
# Post-flash hardware verification. Run after every successful flash
# to confirm the firmware is behaving as expected on real silicon.
# Mix of automated USB probes and human-eyeball physical checks.
#
# Usage:
#   tools/postflash_verify.sh <expected_bcd_hex>
#     e.g. tools/postflash_verify.sh 0100     for mboxfw
#          tools/postflash_verify.sh 0020     for Rev 20 restore
#          tools/postflash_verify.sh dead     for safety_net
#
# Any physical check the user can't complete gets skipped with
# explicit "SKIPPED" rather than passing silently.

set -eu
cd "$(dirname "$0")/.."

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; NORMAL=$'\033[0m'

EXPECT_BCD="${1:?usage: $0 <expected_bcd_hex — e.g. 0100 or 0020 or dead>}"

pass=0; fail=0; skip=0
findings=()

record() {
    local name="$1"; local status="$2"; local detail="${3:-}"
    case "$status" in
        PASS) pass=$((pass + 1));  color="$GREEN" ;;
        FAIL) fail=$((fail + 1));  color="$RED"; findings+=("$name — $detail") ;;
        SKIP) skip=$((skip + 1));  color="$YELLOW" ;;
    esac
    printf "  %-52s %s%-4s%s %s\n" "$name" "$color" "$status" "$NORMAL" "$detail"
}

ask_yn() {
    local prompt="$1"
    while :; do
        printf "    %s [y/n/skip]: " "$prompt"
        read -r ans
        case "$ans" in
            [yY]*) return 0 ;;
            [nN]*) return 1 ;;
            [sS]*) return 2 ;;
        esac
    done
}

echo "=== Automated USB probes ==="

# 1. Device enumerates at all
if out=$(./mboxflash/mboxflash --probe 2>&1); then
    record "device enumerates on USB" PASS "$out"
else
    record "device enumerates on USB" FAIL "mboxflash --probe returned:
$out"
    echo "  Cannot proceed without USB enumeration. Aborting."
    exit 1
fi

# 2. bcdDevice matches expected
if echo "$out" | grep -iq "0x$EXPECT_BCD"; then
    record "bcdDevice == 0x$EXPECT_BCD" PASS
else
    got=$(echo "$out" | grep -oE "bcdDevice = 0x[0-9a-fA-F]+" | head -1)
    record "bcdDevice == 0x$EXPECT_BCD" FAIL "$got"
fi

# 3. bDeviceClass sanity
class=$(ioreg -c IOUSBHostDevice -r -l -w 0 2>/dev/null \
    | awk '/idVendor.*3514/{f=1} f && /bDeviceClass/{print $NF; exit}')
if [[ -z "$class" ]]; then
    record "bDeviceClass readable via ioreg" SKIP "(device not visible to ioreg)"
elif [[ "$EXPECT_BCD" == "dead" ]]; then
    # safety_net advertises DFU class 0xFE
    if [[ "$class" == "254" ]]; then
        record "bDeviceClass = 0xFE (DFU) for safety_net" PASS
    else
        record "bDeviceClass = 0xFE (DFU) for safety_net" FAIL "got $class"
    fi
else
    # mboxfw / Rev 20 should be 0 (class-per-interface for audio)
    if [[ "$class" == "0" ]]; then
        record "bDeviceClass = 0 (class-per-interface)" PASS
    else
        record "bDeviceClass = 0 (class-per-interface)" FAIL "got $class"
    fi
fi

# 4. --enter-dfu round-trips
echo
echo "=== Recovery-path smoke test ==="
echo "  (About to trigger DFU via --enter-dfu, then verify device"
echo "   re-enumerates. This is disruptive — if the device is mid-audio"
echo "   playback, stop it now.)"
printf "  Proceed? [yes/skip]: "
read -r yn
case "$yn" in
    [yY]*)
        if ./mboxflash/mboxflash --enter-dfu >/tmp/pf_dfu.txt 2>&1; then
            record "--enter-dfu accepted" PASS
            sleep 2
            if ./mboxflash/mboxflash --dfu-status >/tmp/pf_st.txt 2>&1 \
                    && grep -q "dfuIDLE\|dfuMANIFEST" /tmp/pf_st.txt; then
                record "post-DFU state = dfuIDLE (recovery worked)" PASS
            else
                record "post-DFU state = dfuIDLE (recovery worked)" FAIL \
                    "$(cat /tmp/pf_st.txt)"
            fi
        else
            record "--enter-dfu accepted" FAIL "$(tail -5 /tmp/pf_dfu.txt)"
        fi
        ;;
    *)  record "--enter-dfu round-trip" SKIP "(user declined)" ;;
esac

echo
echo "=== Physical checks (human required) ==="

if ask_yn "Front-panel LEDs on? (any at all)"; then
    record "front-panel LEDs on" PASS
elif [[ $? == 2 ]]; then record "front-panel LEDs on" SKIP
else                     record "front-panel LEDs on" FAIL "user reported LEDs off"
fi

if ask_yn "Any LED lit corresponds to a channel source (mic/line/inst)?"; then
    record "LED state = expected source-selector default" PASS
elif [[ $? == 2 ]]; then record "LED state = expected source-selector default" SKIP
else                     record "LED state = expected source-selector default" FAIL "user reported unexpected LED pattern"
fi

if [[ "$EXPECT_BCD" != "dead" ]]; then
    if ask_yn "Press source-1 button. Does the LED for ch1 source change?"; then
        record "source-1 button changes ch1 LED" PASS
    elif [[ $? == 2 ]]; then record "source-1 button changes ch1 LED" SKIP
    else                     record "source-1 button changes ch1 LED" FAIL "no visible change"
    fi
fi

echo
echo "=== Summary ==="
printf "  passed:  %d\n" "$pass"
printf "  failed:  %d\n" "$fail"
printf "  skipped: %d\n" "$skip"

if (( fail > 0 )); then
    printf "\n%sPOSTFLASH FAIL%s: %d check(s) failed:\n" "$RED" "$NORMAL" "$fail"
    for f in "${findings[@]}"; do echo "  - $f"; done
    exit 1
fi
if (( skip > 0 )); then
    printf "\n%sPOSTFLASH PARTIAL%s: %d passed, %d skipped.\n" \
        "$YELLOW" "$NORMAL" "$pass" "$skip"
    exit 0
fi
printf "\n%sPOSTFLASH FULL PASS%s: %d checks confirmed working.\n" \
    "$GREEN" "$NORMAL" "$pass"
