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

    # Build + fast gates only (per-commit CI shouldn't run the slow
    # hardware-dependent stuff). Sim smoke + descriptor + wrap_hex +
    # audit + size — every non-hardware gate we have.
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
        exit 0
    )
    rc=$?

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
