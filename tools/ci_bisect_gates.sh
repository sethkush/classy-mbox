#!/usr/bin/env bash
# Walk git log across a range and run the full pre-flash gate suite on
# each commit. Fails on the first commit where any gate fails.
#
# Purpose: enables `git bisect` when a future flash bricks. If every
# commit builds AND passes the gates, bisecting finds the regression
# in log(N) steps. If any commit doesn't build cleanly, bisect derails.
#
# Usage:
#   tools/ci_bisect_gates.sh                    # last 10 commits
#   tools/ci_bisect_gates.sh HEAD~30..HEAD      # explicit range
#   tools/ci_bisect_gates.sh --pre-push         # every commit since main
#
# The script uses `git worktree` to check out each commit into a temp
# worktree so it doesn't mess up your working directory.

set -eu
cd "$(dirname "$0")/.."

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; NORMAL=$'\033[0m'

RANGE="${1:-HEAD~10..HEAD}"
if [[ "$RANGE" == "--pre-push" ]]; then
    RANGE="main..HEAD"
fi

commits=$(git rev-list --reverse "$RANGE")
[[ -n "$commits" ]] || { echo "no commits in range $RANGE"; exit 0; }

echo "Range:     $RANGE"
echo "Commits:   $(echo "$commits" | wc -l | tr -d ' ')"
echo

REPO_ROOT=$(pwd)
TMPWT=$(mktemp -d)
trap 'git worktree remove --force "$TMPWT" 2>/dev/null || true; rm -rf "$TMPWT"' EXIT

pass=0; fail=0; failing_commit=""

for sha in $commits; do
    short=${sha:0:8}
    subj=$(git log -1 --format=%s "$sha")
    printf "%s %s ... " "$short" "${subj:0:60}"

    # Fresh worktree per commit so state is clean.
    git worktree remove --force "$TMPWT" 2>/dev/null || true
    git worktree add --detach "$TMPWT" "$sha" >/dev/null 2>&1

    # Overlay the reference tree from the working copy. TI's headers and the
    # datasheet extracts are INPUTS to the gates, not code under test, and a
    # large part of reference/ is untracked -- Reg_stc1.h among it. Without
    # this, check_sfr_names.py cannot find the canonical SFR names in a fresh
    # worktree and fails for a reason that has nothing to do with the commit.
    # Using the current tree is also the correct semantics: a vendor header
    # does not have a per-commit version.
    if [[ -d "$REPO_ROOT/reference" ]]; then
        rm -rf "$TMPWT/reference"
        ln -s "$REPO_ROOT/reference" "$TMPWT/reference"
    fi

    # #158, 2026-08-05. The comment here used to read "every non-hardware
    # gate we have". It ran SIX gates out of about thirty, and the excuse did
    # not survive contact: sim_smoke.sh drives s51 too, so "hardware-dependent"
    # never distinguished what was left out. Speed did — and then not even
    # that, because the eleven omitted static gates cost 0.6 SECONDS in total.
    #
    # This script exists so a future brick can be bisected. A gate absent from
    # it is a regression bisect cannot find, and the ones missing included
    # diff_vs_rev20, audit_sfr_writes, the citation gates and the CS8427
    # framing waveform -- i.e. most of what has actually caught defects.
    #
    # Tiers:
    #   static  ~0.6 s/commit  -- always run
    #   sim     ~73  s/commit  -- s51-driven; set CI_SKIP_SIM=1 to omit, which
    #                             is an explicit choice rather than a silent
    #                             gap. sim_p1_waveform alone is 54 s of that
    #                             and is the ONLY check of CS8427 SPI framing.
    # tools/check_gate_coverage.py enforces that this list keeps up with
    # preflight's.
    (
        cd "$TMPWT" || exit 1
        rm -rf mboxfw/build safety_net/build 2>/dev/null || true
        (cd mboxfw && make -s) > /tmp/ci_build.log 2>&1 || exit 20
        [[ -d safety_net ]] && (cd safety_net && make -s) >> /tmp/ci_build.log 2>&1
        bash tools/sim_smoke.sh              > /tmp/ci_gate.log 2>&1 || exit 21
        python3 tools/verify_descriptors.py >> /tmp/ci_gate.log 2>&1 || exit 22
        python3 tools/verify_usb_init.py    >> /tmp/ci_gate.log 2>&1 || exit 23
        python3 tools/verify_cs8427.py      >> /tmp/ci_gate.log 2>&1 || exit 24
        python3 tools/verify_setup_paths.py >> /tmp/ci_gate.log 2>&1 || exit 25
        python3 tools/test_wrap_hex_golden.py >> /tmp/ci_gate.log 2>&1 || exit 26
        [[ -f tools/check_code_size.py ]] && \
            python3 tools/check_code_size.py >> /tmp/ci_gate.log 2>&1 || true

        # --- static tier: ~0.6 s for all of it ---
        for g in audit_sfr_writes diff_vs_rev20 check_sfr_names \
                 check_byte_quotes check_citation_targets \
                 check_flat_asm_citations latch_word_bit_diff \
                 verify_init_order verify_conn_reachable verify_reachability \
                 sfr_direct_diff; do
            [[ -f "tools/$g.py" ]] || continue
            python3 "tools/$g.py" >> /tmp/ci_gate.log 2>&1 || exit 27
        done
        [[ -f tools/xdata_access_map.py ]] && \
            { python3 tools/xdata_access_map.py --selftest >> /tmp/ci_gate.log 2>&1 || exit 27; }
        [[ -f tools/mboxtlm.py ]] && \
            { python3 tools/mboxtlm.py --selftest >> /tmp/ci_gate.log 2>&1 || exit 27; }
        [[ -f tools/mboxflash_linux.py ]] && \
            { python3 tools/mboxflash_linux.py --selftest >> /tmp/ci_gate.log 2>&1 || exit 27; }

        # --- simulator tier: ~73 s, opt out with CI_SKIP_SIM=1 ---
        if [[ -z "${CI_SKIP_SIM:-}" ]]; then
            for g in sim_ep0_requests sim_telemetry_roundtrip sim_ep0_diff \
                     sim_p1_waveform; do
                [[ -f "tools/$g.py" ]] || continue
                python3 "tools/$g.py" >> /tmp/ci_gate.log 2>&1 || exit 28
            done
        fi
        exit 0
    ) && rc=0 || rc=$?
    # `set -e` means a failing subshell terminates the SCRIPT unless its status
    # is consumed by a conditional. Written as `( ... ); rc=$?` this script has
    # never been able to report a FAIL: it died on the first failing gate,
    # before the reporting below, while its own comment promised "Continue
    # walking -- we want a full report, not first-fail bail". Pre-existing;
    # found 2026-08-05 when a newly-added gate failed for the first time.

    if (( rc == 0 )); then
        printf "%sPASS%s\n" "$GREEN" "$NORMAL"
        pass=$((pass + 1))
    else
        printf "%sFAIL%s (exit %d)\n" "$RED" "$NORMAL" "$rc"
        fail=$((fail + 1))
        failing_commit=$sha
        case $rc in
            20) reason="build failure — see /tmp/ci_build.log" ;;
            21) reason="sim_smoke failure — see /tmp/ci_gate.log" ;;
            22) reason="verify_descriptors failure" ;;
            23) reason="verify_usb_init failure" ;;
            24) reason="verify_cs8427 failure" ;;
            25) reason="verify_setup_paths failure" ;;
            26) reason="wrap_hex golden failure" ;;
            27) reason="static gate failure (SFR/citation/reachability) — see /tmp/ci_gate.log" ;;
            28) reason="simulator gate failure (EP0 / telemetry / CS8427 framing) — see /tmp/ci_gate.log" ;;
            *)  reason="unknown failure (rc=$rc)" ;;
        esac
        printf "    → %s\n" "$reason"
        # Continue walking — we want a full report, not first-fail bail.
    fi
done

echo
echo "Summary: $pass passed / $fail failed"
if (( fail > 0 )); then
    printf "%sCI FAIL%s: %d commit(s) don't pass gates. Bisect over this range would be unreliable.\n" \
        "$RED" "$NORMAL" "$fail"
    exit 1
fi
printf "%sCI PASS%s: every commit in range builds and passes gates.\n" "$GREEN" "$NORMAL"
