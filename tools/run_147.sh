#!/bin/sh
# #147 pass 1 capture runner. Runs ON the bench host (the void box), one arm
# per invocation.
#
#   run_147.sh <arm> <capture-card> <playback-card> [rate] [seconds]
#   run_147.sh tone  hw:1,0 hw:2,0 48000
#   run_147.sh quiet hw:1,0 hw:2,0 48000
#   run_147.sh mic   hw:1,0 hw:2,0 48000
#
# ONE CABLE: B line out 1 -> A line source 1. <capture-card> is A, the unit
# under test; <playback-card> is B, the generator. A's own playback stays out
# of the circuit entirely, which is the whole point -- every self-loop puts
# mboxfw's DAC in series with its own ADC and a null implicates both.
#
# THE ARMS ARE NOT INTERCHANGEABLE AND THE ORDER MATTERS:
#
#   tone   A on LINE, B generating. THE KNOWN-ANSWER ARM. If this shows no
#          tone, every other arm in the run is VOID -- not interesting, void.
#          CLAUDE.md: a null from an instrument that was never connected looks
#          exactly like a null from a refuted hypothesis, and five measurements
#          died of it in one session.
#   quiet  A on LINE, B silent. THE ACTUAL MEASUREMENT. The artifact appeared
#          regardless of whether anything was playing, so this is where it has
#          to show up if it still exists.
#   mic    A on MIC, B generating. Proves the button press moved the mux.
#          Expect the tone to VANISH. Skipping this is how 2026-07-29 was
#          voided: the mux sat on mic while the loopback fed a line input.
#
# SOURCE SELECT IS PANEL-ONLY. 0x0061 is a release build; check_release_surface
# confirms it answers TLM_REQ_ENTER_DFU and nothing else, so mboxtlm.py setmux
# is gone. Both units BOOT TO MIC while both wired inputs are LINE, so reaching
# LINE is one button press per channel: 0x06 MIC -> 0x05 LINE -> 0x03 INST.
#
# Nothing here flashes, enters DFU, or power-cycles anything.
set -eu

ARM=${1:?arm: tone|quiet|mic}
CAP=${2:?capture card, e.g. hw:1,0}
PLAY=${3:?playback card, e.g. hw:2,0}
RATE=${4:-48000}
SECS=${5:-12}

# THESE TWO NAMES MUST NOT COLLIDE, AND ONCE DID. With OUT=147_${ARM}_${RATE}
# and TONE=147_tone_${RATE}, the tone arm's capture and the generator's source
# file were THE SAME PATH -- so the next arm's mktone silently overwrote the
# capture with the pristine 0.5 FS source, and the analyser read the generator
# instead of the device. It reported -9.03 dBFS, which is exactly a 0.5 FS sine
# and about 45 dB hotter than the real analog round trip. Caught only because
# the number was too good; a null would have sailed through. Hence "cap" and
# "gen" rather than a shared stem.
OUT=/tmp/147_cap_${ARM}_${RATE}.wav
GEN=/tmp/147_gen_${RATE}.wav
HZ=1000

# 0.5 FS leaves headroom so a hot input stage cannot clip the tone into
# harmonics, and -- load-bearing for the analysis -- keeps it clear of the
# 0.98 FS rail threshold, so the tone can never fake the artifact.
python3 "$(dirname "$0")/mktone.py" --hz $HZ --rate "$RATE" \
        --seconds "$(( SECS + 4 ))" --amplitude 0.5 -o "$GEN" >/dev/null

case "$ARM" in
  tone|mic) echo "arm=$ARM: B generates ${HZ}Hz, A captures" ;;
  quiet)    echo "arm=quiet: B SILENT, A captures" ;;
  *) echo "unknown arm: $ARM" >&2; exit 2 ;;
esac
echo "  capture=$CAP  playback=$PLAY  rate=$RATE  ${SECS}s -> $OUT"

if [ "$ARM" = quiet ]; then
  arecord -D "$CAP" -f S24_3LE -c 2 -r "$RATE" -d "$SECS" "$OUT"
else
  # Start the generator FIRST and give it a moment, so the capture window does
  # not open onto a still-silent output and read as a dead path.
  aplay -D "$PLAY" "$GEN" >/dev/null 2>&1 &
  APID=$!
  sleep 1
  arecord -D "$CAP" -f S24_3LE -c 2 -r "$RATE" -d "$SECS" "$OUT"
  kill $APID 2>/dev/null || true
  wait $APID 2>/dev/null || true
fi

ls -l "$OUT"
echo "expected size: $(( SECS * RATE * 6 + 44 )) bytes for ${SECS}s stereo S24_3LE"
