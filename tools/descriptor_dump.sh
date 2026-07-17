#!/usr/bin/env bash
# Phase 0 reconnaissance dump for the Mbox 1.
#
# Grabs every bit of USB descriptor / driver-binding state we care
# about into reference/phase0/<timestamp>/. Safe to run repeatedly.
# Requires the Mbox 1 to be plugged in. No sudo needed.

set -euo pipefail

VID=0x0dba
PID=0x1000
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="$REPO_ROOT/reference/phase0/$STAMP"
mkdir -p "$OUT_DIR"

echo "==> Mbox 1 Phase 0 dump — $STAMP"
echo "    output: $OUT_DIR"

if ! system_profiler SPUSBDataType 2>/dev/null | grep -qiE 'mbox|digidesign|0x0dba'; then
    echo "!! Mbox not found on USB bus. Plug it in and re-run." >&2
    exit 1
fi

echo "[1/4] system_profiler (Mbox subtree)"
system_profiler SPUSBDataType 2>/dev/null \
    | awk '/[Mm][Bb]ox|[Dd]igidesign|0x0dba/{flag=1; blank=0}
           flag && /^$/{blank++; if(blank>=2){flag=0}}
           flag' \
    > "$OUT_DIR/system_profiler.txt"

echo "[2/4] system_profiler (full tree, for context)"
system_profiler SPUSBDataType > "$OUT_DIR/system_profiler_full.txt" 2>&1

echo "[3/4] ioreg -p IOUSB (full)"
ioreg -p IOUSB -w0 -l > "$OUT_DIR/ioreg_full.txt" 2>&1

echo "[4/4] Driver-binding check"
{
    echo "# Classes / kexts near the Mbox entry in ioreg:"
    awk '/[Mm][Bb]ox|[Dd]igidesign/{flag=NR+40} NR<=flag' "$OUT_DIR/ioreg_full.txt" \
        | grep -iE 'IOClass|IOProviderClass|AppleUSBAudio|IOUSBHost|IOUserClient|CFBundleIdentifier' \
        | sort -u \
        || echo "(no driver-binding matches — device is unclaimed)"
} > "$OUT_DIR/driver_binding.txt"

echo
echo "==> Done. Next steps:"
echo "    - Confirm VID/PID = 0x0dba:0x1000:  grep -iE 'product id|vendor id' $OUT_DIR/system_profiler.txt"
echo "    - Check bound driver:               cat $OUT_DIR/driver_binding.txt"
echo "    - Endpoint list requires IOUSBHost probing — deferred to Phase 1 skeleton."
