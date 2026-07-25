#!/usr/bin/env bash
# Mutation-test the pre-flash gate suite. Introduces known bugs into
# the firmware, verifies each intended-catching gate fires. If a
# mutation slips through, the gate has a false negative — surface it
# BEFORE that class of bug hits real silicon.
#
# Each mutation is one of the bugs we've seen tonight (or a close
# cousin) turned into an sed script. We apply, build, run the specific
# gate that SHOULD catch it, then revert.
#
# Exit 0 if every mutation is caught by its intended gate.
# Exit 1 if any mutation slips through.

set -eu
cd "$(dirname "$0")/.."

RED=$'\033[31m'; GREEN=$'\033[32m'; NORMAL=$'\033[0m'

pass=0; fail=0
failures=()

# Snapshot current source files so we can restore between mutations.
SNAPSHOT=$(mktemp -d)
trap 'restore_all; rm -rf "$SNAPSHOT"' EXIT

FILES=(
    mboxfw/src/main.c
    mboxfw/src/usb.c
    mboxfw/src/hw_init.c
    mboxfw/src/eeprom.c
    mboxfw/src/descriptors.c
    mboxfw/include/usb.h
    tools/wrap_hex.py
    tools/sfr_writes.allowed
)
for f in "${FILES[@]}"; do
    # Use a flat basename in the snapshot, but tag with path hash so
    # two files with the same basename (usb.h vs usb.c-adjacent header)
    # don't collide.
    tag=$(echo "$f" | tr '/' '_')
    cp "$f" "$SNAPSHOT/$tag"
done

restore_all() {
    for f in "${FILES[@]}"; do
        tag=$(echo "$f" | tr '/' '_')
        cp "$SNAPSHOT/$tag" "$f"
    done
    rm -rf mboxfw/build safety_net/build 2>/dev/null || true
    (cd mboxfw && make -s) >/dev/null 2>&1 || true
}

# Each mutation: name | sed edit | rebuild-needed | gate command | expected FAIL substring
# Returns 0 if gate FAILED (mutation caught) or 1 if gate PASSED (miss).

test_mutation() {
    local name="$1"
    local target="$2"          # file to mutate
    local sed_expr="$3"
    local gate="$4"            # gate command that should catch it
    local expect_substr="$5"   # substring in gate output that confirms detection

    printf "  %-52s " "$name"
    # Verify the sed pattern actually changes something. A no-op sed
    # would leave the source identical and every subsequent gate would
    # spuriously PASS while looking like it caught nothing.
    local before after
    before=$(md5 -q "$target" 2>/dev/null || md5sum "$target" | awk '{print $1}')
    # Apply mutation
    sed -i.bak "$sed_expr" "$target"
    rm -f "$target.bak"
    after=$(md5 -q "$target" 2>/dev/null || md5sum "$target" | awk '{print $1}')
    if [[ "$before" == "$after" ]]; then
        printf "%sSED-NOOP%s (pattern did not match source — mutation is stale)\n" \
            "$RED" "$NORMAL"
        fail=$((fail + 1))
        failures+=("$name — sed pattern matched nothing in $target")
        restore_all
        return
    fi

    # Rebuild if the mutation touches anything the firmware image
    # depends on: C source, headers, or anything under mboxfw/.
    if [[ "$target" == mboxfw/* ]]; then
        rm -rf mboxfw/build
        (cd mboxfw && make -s) >/tmp/mut_build.log 2>&1
        rc_build=$?
        if (( rc_build != 0 )); then
            # Build itself failed — that's ALSO a catch, arguably (compile
            # error is a stronger gate than any static analyzer). Count
            # as pass since the mutation is not going to get flashed.
            printf "%sCAUGHT-BY-BUILD%s\n" "$GREEN" "$NORMAL"
            pass=$((pass + 1))
            restore_all
            return
        fi
    fi

    # Run the gate
    if out=$(eval "$gate" 2>&1); then
        # Gate PASSED but shouldn't have — mutation slipped through
        printf "%sMISSED%s\n" "$RED" "$NORMAL"
        fail=$((fail + 1))
        failures+=("$name — gate '$gate' passed on mutated firmware")
    else
        # Gate failed. Verify it's failing for the right reason.
        if [[ -z "$expect_substr" ]] || echo "$out" | grep -qiE "$expect_substr"; then
            printf "%sCAUGHT%s\n" "$GREEN" "$NORMAL"
            pass=$((pass + 1))
        else
            printf "%sCAUGHT-WRONG-REASON%s\n" "$RED" "$NORMAL"
            fail=$((fail + 1))
            failures+=("$name — gate failed but not for expected reason ('$expect_substr' not in output)")
        fi
    fi

    restore_all
}

echo "=== MUTATION TESTS ==="
echo "Each row: mutation applied, expected-catching gate run, then reverted."
echo

# --- Mutation 1: revert USBCTL to assignment (flash #2 brick pattern) ---
# Current mboxfw uses `USBCTL |= (USBCTL_CONN | USBCTL_FEN);` — mutate to
# a plain assignment that clobbers boot-ROM-owned bits.
test_mutation \
    "USBCTL |= CONN|FEN → USBCTL = 0xC0 (flash #2)" \
    mboxfw/src/usb.c \
    's/USBCTL |= (USBCTL_CONN | USBCTL_FEN);/USBCTL = 0xC0;/' \
    "python3 tools/audit_sfr_writes.py" \
    "0xfffc"

# --- Mutation 2: drop USBCTL CONN write entirely ---
test_mutation \
    "drop USBCTL |= CONN|FEN (no bus attach)" \
    mboxfw/src/usb.c \
    's#USBCTL |= (USBCTL_CONN | USBCTL_FEN);#// removed#' \
    "bash tools/sim_smoke.sh" \
    "USBCTL"

# --- Mutation 3: drop the boot-time button DFU check ---
test_mutation \
    "drop check_boot_dfu_button call (recovery path)" \
    mboxfw/src/main.c \
    's/check_boot_dfu_button();/\/\/ removed/' \
    "python3 tools/verify_setup_paths.py" \
    "button"

# --- Mutation 4: reintroduce the 8192-pad in wrap_hex (flash #1) ---
# Inject a padding step at the end of emit_records so any output
# grows to exactly 8192 bytes. Golden test's new small-payload
# cross-check catches this.
test_mutation \
    "wrap_hex pads to 8192 again (flash #1)" \
    tools/wrap_hex.py \
    's|image = bytearray(header + code)|image = bytearray(header + code); image.extend(b"\\xff" * max(0, 8192 - len(image)))|' \
    "python3 tools/test_wrap_hex_golden.py" \
    "GOLDEN FAIL|padding|wrapped to"

# --- Mutation 5: remove SDCC-DEC-DPL handling in audit (self-test) ---
# Skip — this tests the audit tool itself, not firmware.

# --- Mutation 6: corrupt Rev 20 in the golden regression source ---
# We can't easily mutate the stock file. Skip.

# --- Mutation 7: skip reply_zero_length in handle_setup default case ---
# Current source is `g_pending_address = wValueL;\n    reply_zero_length();`
# — sed the pending_address line and delete the very next line.
test_mutation \
    "SET_ADDRESS handler forgets reply_zero_length" \
    mboxfw/src/usb.c \
    '/g_pending_address = wValueL;/{N;s|reply_zero_length();|// removed|;}' \
    "python3 tools/verify_setup_paths.py" \
    "SET_ADDRESS handler ACKs"

# --- Mutation 8: change VID from 0x0DBA (mboxflash won't find us) ---
# MBOX_VID is #defined in mboxfw/include/usb.h, consumed by
# descriptors.c. Mutate the header.
test_mutation \
    "VID changed away from 0x0DBA" \
    mboxfw/include/usb.h \
    's/#define MBOX_VID.*/#define MBOX_VID 0xBEEF/' \
    "python3 tools/verify_descriptors.py" \
    "idVendor|0DBA|0x0dba"

# --- Mutation 9: bump code past size budget ---
# Adding a giant array to force >6KB. Skip in normal run — heavy.

# --- Mutation 10: swap I²C read to forget the 0xFF dummy write (BRICK LOG entry) ---
test_mutation \
    "eeprom_read forgets I2C_TX = 0xFF dummy (I²C brick)" \
    mboxfw/src/eeprom.c \
    's/I2C_TX    = 0xFF;.*dummy/\/\/ removed dummy write/' \
    "python3 tools/audit_sfr_writes.py" \
    "ffc1"

echo
echo "=== RESULTS ==="
printf "  passed:  %d\n" "$pass"
printf "  failed:  %d\n" "$fail"

if (( fail > 0 )); then
    printf "\n%sMUTATION FAIL%s: %d mutation(s) slipped past their intended gates:\n" \
        "$RED" "$NORMAL" "$fail"
    for f in "${failures[@]}"; do
        echo "  - $f"
    done
    echo
    echo "For each miss above, either the gate needs to be tightened, or"
    echo "a new gate needs to be added to catch that mutation class."
    exit 1
fi

printf "\n%sMUTATION PASS%s: all %d mutations caught by their intended gate.\n" \
    "$GREEN" "$NORMAL" "$pass"
