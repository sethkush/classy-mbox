#!/usr/bin/env bash
# One-shot setup for a fresh clone or worktree:
#   * install the git pre-commit hook (SFR citation + name-consistency gates)
#   * verify all required toolchain versions are present
#
# Run: tools/setup.sh
#
# Idempotent — re-running is safe.

set -eu
cd "$(dirname "$0")/.."

RED=$'\033[31m'; GREEN=$'\033[32m'; NORMAL=$'\033[0m'
ok=0; fail=0

check_cmd() {
    local name="$1"; local want="${2:-}"
    printf "  %-30s " "$name"
    if command -v "$name" >/dev/null 2>&1; then
        local got
        got=$($name --version 2>&1 | head -1 | tr -d '\n')
        if [[ -n "$want" && "$got" != *"$want"* ]]; then
            printf "%sfound but version mismatch%s\n" "$RED" "$NORMAL"
            printf "    %s\n" "$got"
            printf "    wanted contains: %s\n" "$want"
            fail=$((fail + 1))
            return
        fi
        printf "%sOK%s  (%s)\n" "$GREEN" "$NORMAL" "${got:0:60}"
        ok=$((ok + 1))
    else
        printf "%sMISSING%s\n" "$RED" "$NORMAL"
        fail=$((fail + 1))
    fi
}

echo "=== Toolchain check ==="
SDCC_WANT=$(grep -v '^#' tools/SDCC_VERSION | grep -v '^$' | head -1)
check_cmd sdcc "$SDCC_WANT"
check_cmd s51
check_cmd python3
check_cmd git
check_cmd make
check_cmd clang

echo
echo "=== Git hook install ==="
HOOK_DIR=$(git rev-parse --git-path hooks 2>/dev/null || echo "")
if [[ -z "$HOOK_DIR" ]]; then
    echo "  (not in a git repository — skipping)"
else
    mkdir -p "$HOOK_DIR"
    cat > "$HOOK_DIR/pre-commit" << 'HOOK'
#!/usr/bin/env bash
# Three gates, all cheap enough to run on every commit:
#   1. SFR-touching changes must carry a reference citation.
#   2. Register names must agree with the datasheet and with each other.
#   3. Bytes quoted from a firmware image must be at the address cited.
# Installed by tools/setup.sh — see tools/check_sfr_citations.py,
# tools/check_sfr_names.py and tools/check_byte_quotes.py.
set -e
ROOT="$(git rev-parse --show-toplevel)"
python3 "$ROOT/tools/check_sfr_citations.py"
python3 "$ROOT/tools/check_sfr_names.py"
python3 "$ROOT/tools/check_byte_quotes.py"
HOOK
    chmod +x "$HOOK_DIR/pre-commit"
    echo "  installed $HOOK_DIR/pre-commit"
    ok=$((ok + 1))
fi

echo
if (( fail > 0 )); then
    printf "%sSETUP INCOMPLETE (%d issue(s)):%s\n" "$RED" "$fail" "$NORMAL"
    echo "Install the missing tools then re-run tools/setup.sh."
    echo
    echo "Installation hints:"
    echo "  * sdcc $SDCC_WANT   : https://sdcc.sourceforge.net (needs the mcs51 backend)"
    echo "  * s51 (ucsim)       : bundled with SDCC in newer builds; else brew install sdcc"
    echo "  * python3           : brew install python@3.12 or newer"
    exit 1
fi
printf "%sSETUP OK%s — %d checks passed.\n" "$GREEN" "$NORMAL" "$ok"
echo
echo "Next steps:"
echo "  cd mboxfw && make"
echo "  bash tools/preflight.sh mboxfw/build/mboxfw_flasher.bin"
