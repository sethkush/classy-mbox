#!/usr/bin/env bash
#
# Capture the USB enumeration of the Mbox on macOS so we can diff a
# working device against a broken one without a hardware USB analyser.
#
# macOS exposes USB bus traffic as pcap-able network interfaces named
# `XHCN` (XHCI controller N, one per physical port group). tcpdump can
# record from them if given the raw name. This script picks the right
# interface, records for a configurable duration, and — if `tshark` is
# installed — pretty-prints the SETUP transactions to stdout so you can
# eyeball the failing request without opening Wireshark.
#
# Typical usage before first flash:
#
#   1. Plug a known-working UAC1 device (e.g. any USB-C interface) into
#      the same port group and capture its enumeration as `baseline`:
#         sudo tools/capture_enum.sh baseline 8
#      Unplug + replug the device during those 8 seconds.
#
#   2. Flash mboxfw; capture its enumeration attempt:
#         sudo tools/capture_enum.sh mboxfw 8
#
#   3. Diff the two summaries (each *.setup.txt file is one SETUP
#      per line, tokenised so `diff` and `grep` work well):
#         diff -u baseline.setup.txt mboxfw.setup.txt
#
# The diff usually points straight at the SETUP that the mboxfw code
# gets wrong (bad wLength, missing string index, stall vs. data phase, …)
#
# Args:
#   $1 = label (used for output filename)
#   $2 = duration in seconds (default 8)

set -eu

LABEL="${1:-mbox}"
SECS="${2:-8}"
OUT_DIR="${OUT_DIR:-$(pwd)/captures}"
mkdir -p "$OUT_DIR"

TS="$(date +%Y%m%d-%H%M%S)"
PCAP="$OUT_DIR/${LABEL}-${TS}.pcap"
SETUP_TXT="$OUT_DIR/${LABEL}-${TS}.setup.txt"

# tcpdump needs root to sniff XHC*, but Wireshark's `ChmodBPF` also
# grants pcap access to the admin group — try without sudo first.
TCPDUMP="tcpdump"
if [[ $EUID -ne 0 ]] && ! groups | tr ' ' '\n' | grep -qx access_bpf; then
    echo "note: not root and not in access_bpf group — re-invoking with sudo" >&2
    TCPDUMP="sudo tcpdump"
fi

# Enumerate XHC interfaces the kernel currently exposes. On Apple
# Silicon there are usually XHC20 (rear USB-C hub) and XHC0/XHC1.
IFACES=($(ifconfig -l | tr ' ' '\n' | grep -E '^XHC' || true))
if [[ ${#IFACES[@]} -eq 0 ]]; then
    echo "no XHC* interfaces found — is System Integrity Protection blocking? Try:" >&2
    echo "  sudo /usr/sbin/rvictl -s <UDID>   (for iOS devices)" >&2
    echo "  or: install Wireshark's ChmodBPF helper to expose XHC to tcpdump" >&2
    exit 1
fi
echo "found USB interfaces: ${IFACES[*]}"

# Capture from all of them in parallel — the Mbox may be on any bus.
# Bus name doesn't matter for the analysis; we filter by VID:PID after.
IFACE_ARGS=""
for i in "${IFACES[@]}"; do IFACE_ARGS+=" -i $i"; done

echo "capturing to $PCAP for $SECS s — unplug then replug the Mbox NOW"
# `-U` = unbuffered; `-s0` = full packets (needed for descriptor payloads).
${TCPDUMP} $IFACE_ARGS -s0 -U -w "$PCAP" -G "$SECS" -W 1 2>&1 | tail -5

# Post-process with tshark if present — one line per SETUP transaction,
# annotated with bmRequestType / bRequest / wValue / wIndex / wLength
# and (for GET_DESCRIPTOR) the descriptor type + index.
if command -v tshark >/dev/null 2>&1; then
    echo "extracting SETUP transactions → $SETUP_TXT"
    tshark -r "$PCAP" -Y 'usb.transfer_type == 2 && usb.setup_data' \
        -T fields -E separator=' ' \
        -e frame.number \
        -e usb.device_address \
        -e usb.bmRequestType \
        -e usb.setup.bRequest \
        -e usb.setup.wValue \
        -e usb.setup.wIndex \
        -e usb.setup.wLength \
        2>/dev/null \
        | awk '{printf "frame=%s dev=%s bmReq=%s bReq=%s wVal=%s wIdx=%s wLen=%s\n",
                       $1,$2,$3,$4,$5,$6,$7}' \
        > "$SETUP_TXT"
    echo "wrote $(wc -l <"$SETUP_TXT" | tr -d ' ') SETUP transactions to $SETUP_TXT"
    echo
    echo "-- first 20 SETUPs --"
    head -20 "$SETUP_TXT"
else
    echo "note: tshark not installed (brew install wireshark) — pcap saved,"
    echo "      analyse with: tshark -r $PCAP -O usb"
fi

echo
echo "next: run again with a different label, then diff:"
echo "  diff -u <baseline>.setup.txt <mboxfw>.setup.txt"
