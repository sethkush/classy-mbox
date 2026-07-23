#!/usr/bin/env bash
# End-to-end flash-loop gate. Post-flash regression check that the
# flash toolchain still works round-trip:
#
#   1. Device must be in DFU (bulletproof 0xFFFF:0xFFFE or app-DFU
#      0x0DBA:0x1001). If not, exit and prompt user.
#   2. Dump current EEPROM contents.
#   3. Flash the target image.
#   4. Wait for manifest completion.
#   5. Re-enter DFU (from the flashed firmware if it works, else via
#      button-hold prompt).
#   6. Dump EEPROM contents again.
#   7. Verify dump #2 matches the flashed image (proves the flash
#      pipeline is bit-perfect end-to-end).
#   8. If image was mboxfw/safety_net: verify it enumerated with the
#      expected VID/PID/bcdDevice after step 4.
#
# Failure at any step surfaces exactly which link in the chain broke —
# either the flash tool, the firmware's DFU trigger, or the EEPROM
# read-back — instead of "device silent, no idea why".
#
# Usage:  tools/e2e_flash_loop.sh <image.bin> [expected_bcd]
#
# Where <image.bin> is a wrap_hex output and expected_bcd is the
# bcdDevice we expect after boot (0x0020 for Rev 20 restore,
# 0x0100 for mboxfw, 0xDEAD for safety_net).

set -eu
cd "$(dirname "$0")/.."

IMG="${1:?usage: $0 <image.bin> [expected_bcd_hex]}"
EXPECT_BCD="${2:-}"
MBOXFLASH="./mboxflash/mboxflash"

step() { printf "\n=== %s ===\n" "$1"; }
die()  { printf "FAIL: %s\n" "$*" >&2; exit 1; }

step "1. Check DFU state"
if ! $MBOXFLASH --dfu-status > /tmp/e2e_status.txt 2>&1; then
    cat /tmp/e2e_status.txt >&2
    die "device not in DFU. Enter DFU first (button-hold plug-in or"
    die "  SDA short if firmware is stuck) then re-run."
fi
grep -E "bState|bStatus" /tmp/e2e_status.txt

step "2. Dump current EEPROM"
DUMP_BEFORE="$(mktemp).bin"
$MBOXFLASH --dump "$DUMP_BEFORE" > /tmp/e2e_dump1.txt 2>&1 || \
    die "dump failed. Contents of /tmp/e2e_dump1.txt:
$(cat /tmp/e2e_dump1.txt)"
echo "  captured $(wc -c < "$DUMP_BEFORE") bytes → $DUMP_BEFORE"

step "3. Flash target image: $IMG"
echo "yes" | $MBOXFLASH --flash "$IMG" > /tmp/e2e_flash.txt 2>&1 || {
    tail -20 /tmp/e2e_flash.txt >&2
    die "flash reported failure"
}
tail -3 /tmp/e2e_flash.txt

step "4. Wait for post-flash re-enumeration (up to 10s)"
found=""
for _ in $(seq 1 20); do
    sleep 0.5
    if out=$($MBOXFLASH --probe 2>&1); then
        found="$out"
        break
    fi
done
if [[ -z "$found" ]]; then
    die "device did not re-enumerate within 10s of flash completion"
fi
echo "$found"

if [[ -n "$EXPECT_BCD" ]]; then
    if ! echo "$found" | grep -qi "bcddevice = 0x${EXPECT_BCD}"; then
        die "expected bcdDevice=0x${EXPECT_BCD}, probe reported:
$found"
    fi
    echo "  bcdDevice matches expected 0x${EXPECT_BCD}"
fi

step "5. Re-enter DFU (via --enter-dfu on the freshly-flashed firmware)"
if ! $MBOXFLASH --enter-dfu > /tmp/e2e_dfu.txt 2>&1; then
    tail -10 /tmp/e2e_dfu.txt >&2
    die "--enter-dfu failed against freshly-flashed image.
    Recovery paths are BROKEN in the just-flashed firmware.
    Fix the firmware's handle_digi_enter_dfu before shipping."
fi
sleep 1
echo "  DFU trigger accepted"

step "6. Dump EEPROM back"
DUMP_AFTER="$(mktemp).bin"
$MBOXFLASH --dump "$DUMP_AFTER" > /tmp/e2e_dump2.txt 2>&1 || \
    die "post-flash dump failed"
echo "  captured $(wc -c < "$DUMP_AFTER") bytes → $DUMP_AFTER"

step "7. Verify dump #2 == flashed image"
if cmp -s "$IMG" "$DUMP_AFTER"; then
    echo "  bit-perfect match ($(wc -c < "$IMG") bytes)"
else
    # Find first diff byte for the failure message
    diff_off=$(cmp "$IMG" "$DUMP_AFTER" 2>&1 | head -1)
    die "flashed bytes do not match EEPROM contents.
        $diff_off
        This means either: (a) mboxflash --flash lost some bytes,
        (b) mboxflash --dump lost some bytes, or
        (c) the boot ROM's DFU_UPLOAD returns a modified image.
        Investigate before trusting any subsequent flash."
fi

step "E2E PASS"
echo "  target:       $IMG"
echo "  dump-before:  $DUMP_BEFORE"
echo "  dump-after:   $DUMP_AFTER"
echo "  flash + recovery pipeline verified end-to-end."
