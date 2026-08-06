#!/bin/sh
# #184 — does the AES3 transmitter actually carry the playback stream?
#
#   test_spdif_out.sh            (run on the unit host, both units attached)
#
# WHY THIS IS NOT JUST "PLAY A TONE AND LISTEN".
#
# cs8427.c writes DATAFLOW = 0x0C: TXD = 01 (CS8427_TXDSERIAL, transmitter fed
# from the serial audio input port) with TXOFF clear. By that register alone the
# RCA digital output carries the playback side of the C-port -- not yet tested
# at the jack. That is read off the register we write, not observed on the wire,
# and #187 wants to declare an
# Output Terminal for it -- declaring a terminal that turns out to be silent
# would be worse than not declaring it.
#
# THE CONFOUND. A reaches B by BOTH paths (BENCH_WIRING.md):
#
#     A line out 1 ──TS───► B line source 1      (analog)
#     A spdif out  ──coax─► B spdif in           (digital)
#
# so a tone appearing in B's capture proves nothing on its own. It would appear
# identically if the transmitter were dead and the analog cable were doing all
# the work.
#
# THE DISCRIMINATOR IS THE CLOCK, not the audio.
#
# B is put in selector=spdif AND clock=slave (mode 1). Its master clock then
# comes from the CS8427's recovered clock -- i.e. from A's carrier. The CS8427
# has no sample-rate converter, so if A's transmitter were not emitting, B has
# no master clock at all and cannot produce a coherent capture. A coherent
# capture in that state is itself evidence of the carrier; the tone inside it is
# evidence of what the carrier is carrying. The analog cable cannot supply
# either, because the selector routes the capture path to the receiver.
#
# NEGATIVE CONTROL. Capture again with A silent. The transmitter keeps emitting
# a carrier (so B keeps its clock and the capture still runs), but the tone must
# vanish. If a "tone" survives A going quiet, it was never A's playback.
#
# RECOVERY. Everything here is a vendor request over EP0 and reversible over the
# wire: `mboxtlm.py clock 48000 --source analog` restores B. The 8051 runs from
# the crystal, not MCLKO, so even slaving to an absent carrier costs audio and
# nothing else -- no power cycle, which matters at 1 km.
set -u

SER_A=RK10874600Q
SER_B=RK1672500M
PY=${PY:-$HOME/mbox-venv/bin/python}
TLM="sudo $PY /tmp/mboxtlm.py"
RATE=48000
SECS=12

card_for_serial() {
    for c in /sys/class/sound/card*; do
        [ -e "$c/device" ] || continue
        p=$(readlink -f "$c/device"); par=$(dirname "$p")
        if [ "$(cat "$par/serial" 2>/dev/null || echo)" = "$1" ]; then
            basename "$c" | tr -d 'card'; return 0
        fi
    done
    return 1
}

CA=$(card_for_serial "$SER_A") || { echo "FAIL: no card for A ($SER_A)"; exit 1; }
CB=$(card_for_serial "$SER_B") || { echo "FAIL: no card for B ($SER_B)"; exit 1; }
echo "A=card${CA} (${SER_A})   B=card${CB} (${SER_B})"

echo
echo "=== 1. tone source ==="
python3 /tmp/mktone.py --hz 1000 --rate $RATE --seconds 40 -o /tmp/tone1k.wav

echo
echo "=== 2. put B on the digital receiver, slaved to A's carrier ==="
$TLM clock slave --source spdif --serial "$SER_B" 2>&1 | tail -20

echo
echo "=== 3. ARM: capture B while A plays ==="
aplay -D "hw:${CA},0" /tmp/tone1k.wav >/tmp/aplay_a.log 2>&1 &
APLAY_PID=$!
sleep 3     # let the stream settle before the window opens
arecord -D "hw:${CB},0" -f S24_3LE -c2 -r $RATE -d $SECS /tmp/spdif_live.wav \
        >/tmp/arec_live.log 2>&1
kill $APLAY_PID 2>/dev/null
wait $APLAY_PID 2>/dev/null
sleep 2

echo
echo "=== 4. CONTROL: capture B with A silent ==="
arecord -D "hw:${CB},0" -f S24_3LE -c2 -r $RATE -d $SECS /tmp/spdif_silent.wav \
        >/tmp/arec_silent.log 2>&1

echo
echo "=== 5. state of B at capture time (block 9) ==="
$TLM read 9 --serial "$SER_B" 2>&1 | tail -20

echo
echo "=== 6. restore B ==="
$TLM clock $RATE --source analog --serial "$SER_B" 2>&1 | tail -6

echo
echo "=== 7. results ==="
echo "--- with A playing (expect a coherent 1 kHz tone) ---"
python3 /tmp/tone_peak.py --skip=2 --take=6 /tmp/spdif_live.wav
echo "--- with A silent (expect NO coherent tone) ---"
python3 /tmp/tone_peak.py --skip=2 --take=6 /tmp/spdif_silent.wav
