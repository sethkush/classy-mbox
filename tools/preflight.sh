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

# RELEASE BUILD DETECTION. A `make MBOX_RELEASE=1` image compiles out the
# telemetry block reader, the counters, and the boot canaries, so the three
# gates that exercise that surface cannot pass and are not defects.
#
# Detected from the LINK MAP rather than an environment variable, so it reflects
# the artifact actually being gated instead of what someone meant to build. A
# flag could be passed while gating a stale diagnostic build in build/ -- which
# is a real mistake that happened while writing this, and produced a full
# 37/37 PASS on the wrong image.
#
# Skipped is printed as SKIP and counted separately. It is never counted as a
# pass: a release build genuinely has less evidence behind it than a diagnostic
# one, and the summary should say so rather than round up.
MBOXFW_MAP="mboxfw/build/mboxfw.map"
IS_RELEASE=0
# Keyed on _tlm_reset_counters, NOT _tlm_read_block. #205 put a cut-down
# tlm_read_block back into release builds so a field unit can still report its
# build id, which silently broke the older test and turned three SKIPs into
# FAILs. tlm_reset_counters has no release counterpart.
if [ -f "$MBOXFW_MAP" ] && ! grep -q "_tlm_reset_counters" "$MBOXFW_MAP"; then
    IS_RELEASE=1
fi
skipped=0
check_diag() {
    local name="$1"; shift
    if [ "$IS_RELEASE" = "1" ]; then
        printf "  %-45s %sSKIP%s (release build: no telemetry)\n" "$name" "$YELLOW" "$NORMAL"
        skipped=$((skipped+1))
        return
    fi
    check "$name" "$@"
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
    # This gate existed and ran NOWHERE. Not preflight, not ci_bisect_gates --
    # so it had been failing silently with six unjustified diffs. A gate no
    # runner invokes is a file, not a gate. Wired in 2026-08-05 alongside the
    # scanner-parity fix that gave it the same Rev 20 baseline diff_vs_rev20.py
    # uses (straight-line DPTR tracking + writes performed via a helper).
    check "Rev-20 SFR diff justified (safety_net)" python3 tools/diff_vs_rev20_safety_net.py
    check "mboxflash --validate on target"  ./mboxflash/mboxflash --validate "$IMG"
else
    check "code size within budget"             python3 tools/check_code_size.py
    check_diag "sim_smoke (main loop + CONN + canaries)" bash tools/sim_smoke.sh
    check "verify_descriptors"        python3 tools/verify_descriptors.py
    check "terminals cite a measurement"      python3 tools/check_terminal_evidence.py
    # #214: the endpoint set is load-bearing for code nowhere near the
    # descriptors. Sits beside the terminal gate because it is the same shape of
    # problem -- a declaration whose consequences live elsewhere.
    check "endpoint set matches manifest"     python3 tools/check_endpoint_manifest.py
    check "UAC1 class rulebook"               python3 tools/check_uac1_rulebook.py
    check "UAC1 rulebook selftest"            python3 tools/check_uac1_rulebook.py --selftest
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
    check_diag "EP0 answers requests (executed)"     python3 tools/sim_ep0_requests.py
    # The gate above checks mboxfw against expectations we wrote. This checks
    # it against safety_net -- the one image in this tree whose USB behaviour
    # is confirmed on the real device (it enumerated, bcdDevice 0xDEAD on the
    # bus). Same SETUP packets, both images, diffed.
    # FINDING_differential_vs_safety_net.md.
    check "EP0 vs the image that enumerated"    python3 tools/sim_ep0_diff.py
    # Every gate above executes the FIRMWARE. This is the only one that
    # executes the HOST side: it reads all 11 telemetry blocks out of the
    # running image and decodes them with mboxtlm.py, the tool that will read
    # them on the bench. Until it existed, ~570 lines of decoder had never
    # seen a byte, and a retired second reader was 6 blocks behind the
    # firmware. FINDING_telemetry_roundtrip.md.
    check_diag "telemetry round-trip (fw -> host tool)" python3 tools/sim_telemetry_roundtrip.py
    # The round-trip gate above proves the DECODER is right. This proves the
    # tool talks to the unit you named: `--serial` given before the subcommand
    # was silently discarded by argparse, and with one unit attached that read
    # the wrong device and printed a perfectly valid-looking result.
    check "mboxtlm unit selection"              python3 tools/mboxtlm.py --selftest
    # #180. A citation is a claim; an un-gated citation format is an unchecked
    # one. rev20_flat.asm disassembles the EEPROM (addresses = true + 0x12),
    # and a dozen citations quoted it in a free-form shape that
    # check_citation_targets.py does not parse, so nothing ever verified them.
    check "no citations from rev20_flat.asm"    python3 tools/check_flat_asm_citations.py
    # #158. ci_bisect_gates.sh is what makes `git bisect` usable after a brick,
    # and it ran 6 of ~30 gates while claiming to run every non-hardware one.
    # Two hand-maintained lists drift; this makes the drift an error.
    check "per-commit runner covers preflight"  python3 tools/check_gate_coverage.py
    # #154. The flasher's read-back compare, exercised with no device: the
    # transport needs hardware, the pass/fail logic does not, and the header
    # checksum rule is easy to get wrong in either direction.
    check "flasher read-back compare logic"     python3 tools/mboxflash_linux.py --selftest
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
if (( skipped > 0 )); then
    printf "%sAll %d gates PASSED, %d SKIPPED.%s\n" "$GREEN" "$pass" "$skipped" "$NORMAL"
    printf "%s  RELEASE BUILD: the skipped gates exercise telemetry and the boot\n" "$YELLOW"
    printf "  canaries, which this image compiles out. It therefore carries LESS\n"
    printf "  evidence than a diagnostic build, not the same amount. Validate the\n"
    printf "  diagnostic image first, then ship this one.%s\n" "$NORMAL"
else
    printf "%sAll %d gates PASSED.%s\n" "$GREEN" "$pass" "$NORMAL"
fi

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
# This used to `ls backups/` and print whatever was there. On 2026-08-02 that
# was three files: two byte-identical copies of firmware_stock/
# rev20_flasher_payload.bin (not device dumps at all), and one 8 KB dump taken
# after the header checksum was broken to reach DFU, which the flasher
# REJECTS. So the checklist was asking you to confirm a restore path backed by
# images that could not restore anything.
#
# The restore path is the stock payloads, which have both been written to this
# unit successfully. Every candidate below is validated here rather than
# listed, so an unflashable image can never appear as a rollback option again.
echo "     Restore images (validated now, not just present):"
found=0
for cand in firmware_stock/rev20_flasher_payload.bin \
            firmware_stock/rev22_flasher_payload.bin \
            backups/*.bin; do
    [[ -f "$cand" ]] || continue
    if ./mboxflash/mboxflash --validate "$cand" >/dev/null 2>&1; then
        printf "       %sok%s   %s (%s bytes)\n" \
            "$GREEN" "$NORMAL" "$cand" "$(wc -c < "$cand" | tr -d ' ')"
        found=$((found+1))
    else
        printf "       %sNO%s   %s — the flasher rejects this; NOT a rollback option\n" \
            "$RED" "$NORMAL" "$cand"
    fi
done
if (( found == 0 )); then
    printf "     %sNo image in this tree validates. There is no rollback path.%s\n" \
        "$RED" "$NORMAL"
    echo "     Do not flash."
    exit 3
fi
echo "     Recovery: mboxflash --enter-dfu → mboxflash --flash <image above>"
echo "     If --enter-dfu fails: hold source-1 during replug (button DFU)."
echo "       PROVEN 2026-08-03, build 0x0016, once. The device goes silent by"
echo "       design (breaks its own checksum, spins); the NEXT power cycle"
echo "       brings it up as ffff:fffe. Silence is also what a brick looks"
echo "       like -- the following power cycle is what tells them apart."
echo "       As of #172 (build 0x0017) it runs BEFORE usb_init/hw_init,"
echo "       with the two writes it needs hoisted with it, so it covers a"
echo "       hang anywhere in the boot path. It still cannot recover an"
echo "       image that never executes at all -- the oversize case, which"
echo "       the linker and check_code_size.py now reject up front."
echo "       That position is unproven on hardware; 0x0016 fired from the"
echo "       old post-hw_init position."
echo "     If both fail: physical SDA short (see recovery notes)."
read -r -p "  → Rollback plan is understood? [yes/no] " ans
[[ "$ans" == "yes" ]] || { echo "aborted."; exit 3; }

echo
echo "  4. RECOVERY PATHS ACTIVE IN THE FLASHED FIRMWARE"
grep -q "handle_digi_enter_dfu" mboxfw/src/usb.c 2>/dev/null && \
    echo "     ✓ class-request DFU trigger (mboxflash --enter-dfu)" || \
    echo "     ✗ class-request DFU trigger MISSING"
# Presence is not function. This printed a green tick for a path that could
# not fire in any build up to 0x0015 -- the test was inverted AND read through
# the internal pull-ups. Check the three things that have to hold together, so
# the tick means something: the call exists, it tests for a HIGH pin, and
# hw_init sets P3PUDIS (without which P3 reads a stuck 1 whatever is pressed).
# See firmware_stock/decomp/FINDING_buttons_are_active_high.md.
if grep -q "check_boot_dfu_button" mboxfw/src/main.c 2>/dev/null; then
    _btn_ok=1
    grep -q 'if (!(p3 & P3_BTN_CH1_MASK)) { held = 0; break; }' mboxfw/src/main.c \
        || { echo "     ✗ boot-button read is not the active-HIGH form"; _btn_ok=0; }
    grep -q 'GLOBCTL |= 0x02' mboxfw/src/hw_init.c 2>/dev/null \
        || { echo "     ✗ P3PUDIS not set — P3 reads stuck high, button dead"; _btn_ok=0; }
    [[ "$_btn_ok" == 1 ]] && \
        echo "     ✓ boot-time button-hold DFU trigger (fired on hardware" && \
        echo "       2026-08-03; #172 moved it ahead of usb_init/hw_init)"
else
    echo "     ✗ boot-time button DFU trigger MISSING"
fi
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
