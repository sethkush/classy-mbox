#!/usr/bin/env bash
# Fail if the installed sdcc version doesn't match tools/SDCC_VERSION.
# Different SDCC versions can emit different assembly for the same C
# source, silently invalidating the audit manifest and the wrap_hex
# golden regression.
set -eu
cd "$(dirname "$0")/.."

want=$(grep -v '^#' tools/SDCC_VERSION | grep -v '^$' | head -1)
if ! command -v sdcc >/dev/null; then
    echo "SDCC not installed. Wanted version: $want" >&2
    exit 1
fi
have=$(sdcc --version 2>&1 | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
if [[ "$have" != "$want" ]]; then
    echo "SDCC version mismatch." >&2
    echo "  installed: $have" >&2
    echo "  pinned:    $want" >&2
    echo "" >&2
    echo "The pinned version is what produced the byte-for-byte manifest" >&2
    echo "in tools/sfr_writes.allowed and the wrap_hex golden output." >&2
    echo "" >&2
    echo "Options:" >&2
    echo "  * Install SDCC $want (e.g. https://sdcc.sourceforge.net)" >&2
    echo "  * If moving to a newer SDCC intentionally, verify:" >&2
    echo "      make clean && make" >&2
    echo "      python3 tools/test_wrap_hex_golden.py    # still passes?" >&2
    echo "      python3 tools/audit_sfr_writes.py        # any drift?" >&2
    echo "    then bump tools/SDCC_VERSION and commit with the audit refresh." >&2
    exit 1
fi
echo "SDCC VERSION PASS: $have matches pinned $want"
