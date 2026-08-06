#!/bin/sh
# #189 -- are IRAM 0x23.2 and 0x23.3 one gate or two?
#
#   test_mute_pair.sh            (run on the void box, both units attached)
#
# THE QUESTION. #171 settled that the PAIR gates the audio path in BOTH
# directions -- 71 dB on the output side, 0 of 95232 non-zero samples on the
# input side -- but it only ever moved the two bits TOGETHER, because the
# question then was whether they mattered at all. Whether they are one gate or
# two is the whole question for a UAC1 Feature Unit: a class Mute sits on ONE
# path, so declaring one is honest only if muting a direction leaves the other
# alone. If both bits gate both directions, #190 closes as not-implementable on
# this hardware and no descriptor change can rescue it.
#
# WHY THIS IS NOT TWO FLASHED IMAGES. It was, until build 0x0035: two
# MBOX_MUTE_PAIR_MASK variants, two power cycles, two 2 km round trips, and each
# mask value getting exactly one attempt with a reflash between the halves of
# the comparison. TLM_REQ_SET_MUTE moves the bits over EP0, so all four states
# land on one power cycle, in any order, repeatably. That is what lets this
# script bracket the run -- `both` is measured FIRST and LAST, and if those two
# disagree the run is void and nothing between them means anything.
#
# THE MEASUREMENT. Each unit is tested with the OTHER as the instrument, over
# the crossed TS pair (BENCH_WIRING.md):
#
#     A out1 ──TS──► B src1        B out1 ──TS──► A src1
#
#   OUTPUT arm:  UUT plays a tone, REF captures it.  UUT's input is out of the
#                circuit entirely.
#   INPUT  arm:  REF plays the tone, UUT captures it. UUT's output is out of
#                the circuit entirely.
#
# A self-loop cannot do this: out2 -> src2 puts one unit's DAC and ADC in
# series, so a null result implicates both and names neither. That is exactly
# why #171 could not finish the job.
#
# BOTH UNITS ARE TESTED, each as UUT with the other as REF. Two independent
# replications on different silicon; a per-bit behaviour that appears on one
# unit and not the other is a defect in the experiment, not a finding.
#
# THE CONTROLS.
#   * ch2 is never fed. It is quoted with every reading -- BENCH_WIRING.md
#     measures ~66 dB between a fed and an unfed channel, and a "silent"
#     result that does not clear the unfed channel's floor is not silence.
#   * `both` is measured at the start AND the end of each unit's sweep.
#   * The selector stays on ANALOG. The units are also crossed over coax, so a
#     tone could arrive by S/PDIF; the analog selector keeps that path out of
#     the capture chain.
#   * setmux line line on BOTH units, after every replug. The mux boots to MIC
#     while both loopbacks feed LINE inputs -- the trap that voided 2026-07-29.
#
# RECOVERY. Nothing here can wedge anything. `none` mutes the audio path and
# `both` restores it over the same EP0 that issued the mute, and the script
# restores `both` on exit however it exits.
set -u

SER_A=RK10874600Q
SER_B=RK1672500M
PY=${PY:-$HOME/mbox-venv/bin/python}
TLM="sudo $PY $HOME/mboxtlm_cur.py"
# #190. The mute now lives in the two UAC1 Feature Units, so ALSA carries it as
# an ordinary mixer switch and `amixer` reaches it WITH the driver bound. That
# is why TLM_REQ_SET_MUTE could go: the vendor alias existed only because there
# was no class control to reach.
#
# The control NAMES are discovered rather than assumed -- snd-usb-audio derives
# them from the terminal types, and guessing one that does not exist would make
# every amixer call fail in a way this script would then have to notice.
LEVEL="$PY $HOME/ch_level.py"
RATE=48000
SECS=8
TONE=/tmp/mute_tone.wav
OUT=${OUT:-/tmp/mute189}

mkdir -p "$OUT"

# Resolve ALSA card -> unit by SYSFS PATH, not by card number. The indices are
# assigned in enumeration order and swapped when the host was rebooted on
# 2026-08-04; the socket is what identifies the unit.
card_for() {
    _ser=$1
    for d in /sys/bus/usb/devices/*; do
        [ -f "$d/serial" ] || continue
        [ "$(cat "$d/serial" 2>/dev/null)" = "$_ser" ] || continue
        _path=$(basename "$d")
        # /proc/asound/cards names each card's sysfs path, e.g.
        #   "Digidesign Mbox (classc at usb-0000:00:1d.0-1.4, full speed"
        # so the socket suffix of the device that answered to this SERIAL is
        # what picks the card index.
        awk -v p="-${_path#*-}" '
            /^ *[0-9]+ \[/ { idx = $1 }
            $0 ~ p        { print idx; exit }' /proc/asound/cards
        return
    done
}

CARD_A=$(card_for $SER_A)
CARD_B=$(card_for $SER_B)
if [ -z "$CARD_A" ] || [ -z "$CARD_B" ]; then
    echo "FATAL: could not resolve both units to ALSA cards (A='$CARD_A' B='$CARD_B')."
    echo "Both must be attached and running an image that serves a serial."
    exit 1
fi
echo "unit A ($SER_A) = card $CARD_A"
echo "unit B ($SER_B) = card $CARD_B"

restore() {
    echo
    echo "--- restoring both units to mask=both"
    apply_mask "$CARD_A" both >/dev/null 2>&1
    apply_mask "$CARD_B" both >/dev/null 2>&1
}
trap restore EXIT INT TERM

# The two switches, addressed by ELEMENT NAME rather than by simple-mixer name.
#
# snd-usb-audio merges both Feature Units into ONE simple control ("PCM", with
# pswitch and cswitch), so `amixer set PCM ...` cannot name the capture side --
# `set PCM capture mute` is rejected outright. Worse, a scontrols-based search
# for something matching /capture/ finds "PCM Capture Source", the Selector
# Unit, which is a different control entirely. Element names are unambiguous:
#
#   PCM Playback Switch -> FU 8 -> codec bit 0x23.3
#   PCM Capture Switch  -> FU 9 -> codec bit 0x23.2
PB_ELEM="PCM Playback Switch"
CAP_ELEM="PCM Capture Switch"

mute_ctls() {
    amixer -c "$1" controls 2>/dev/null | sed -n "s/.*name='\(.*\)'/\1/p"
}

# Apply one mask value through the class control. `a` leaves 0x23.2 up (capture
# audible, playback muted); `b` leaves 0x23.3 up (playback audible, capture
# muted) -- #189's naming, preserved so the two runs are comparable.
apply_mask() {
    _card=$1; _m=$2
    case "$_m" in
        both) _pb=on;  _cap=on  ;;
        none) _pb=off; _cap=off ;;
        a)    _pb=off; _cap=on  ;;
        b)    _pb=on;  _cap=off ;;
        *)    echo "FATAL: unknown mask $_m"; return 1 ;;
    esac
    amixer -c "$_card" -q cset name="$PB_ELEM" $_pb >/dev/null 2>&1 || {
        echo "FATAL: cset '$PB_ELEM' $_pb on card $_card failed"; return 1; }
    amixer -c "$_card" -q cset name="$CAP_ELEM" $_cap >/dev/null 2>&1 || {
        echo "FATAL: cset '$CAP_ELEM' $_cap on card $_card failed"; return 1; }
    return 0
}


# LEFT ONLY. ch2 is the control and must stay unfed -- and a tone on both
# channels would also drive A's out2 -> src2 self-loop, putting A's own DAC and
# ADC back in series with each other, which is the very thing the crossed
# cabling exists to avoid.
$PY "$HOME/mktone.py" --hz 1000 --rate $RATE --seconds $((SECS + 4)) \
    --channels left -o $TONE

for ser in $SER_A $SER_B; do
    $TLM setmux line line --serial $ser >/dev/null || exit 1
    $TLM clock $RATE --source analog --serial $ser >/dev/null || exit 1
done

# Name the two switches once, from card A, and ABORT if either is missing --
# #190 is not testable at all if the host did not parse the Feature Units, and
# a script that quietly skipped the mute would produce ten identical rows,
# which is exactly the failure mode the first run of this script had.
echo "--- mixer controls on card $CARD_A:"
mute_ctls "$CARD_A" | sed 's/^/    /'
for _e in "$PB_ELEM" "$CAP_ELEM"; do
    mute_ctls "$CARD_A" | grep -qxF "$_e" || {
        echo "FATAL: card $CARD_A has no element '$_e'. The host did not parse"
        echo "       the Feature Units -- read the list above. Do NOT let the"
        echo "       sweep run: with no mute reaching the device every row is"
        echo "       the same condition measured ten times."
        exit 1; }
done
echo "    -> '$PB_ELEM' and '$CAP_ELEM' both present"

# One cell: UUT at $mask, measured in one direction.
#   arm=out -> play on UUT, record on REF
#   arm=in  -> play on REF, record on UUT
# THE MASK IS APPLIED MID-STREAM, AND THAT IS THE WHOLE DESIGN.
#
# streaming_set_rate() ends with `g_codec_state_23 |= CODEC23_MUTE_PAIR` and
# publishes -- so EVERY stream open re-raises the pair. The first working
# version of this script set the mask and then started aplay/arecord, which
# undid it before a single sample was captured, and produced a complete table
# in which nothing was ever muted. The tell was `none` reading identical to
# `both` while #171 had measured 71 dB between them.
#
# So: bring both streams up first, let them settle, THEN apply the mask, and
# analyse only the window after it landed. The pair is read back at the END of
# the capture as well -- if something re-raised it mid-capture the row is
# marked rather than reported as a level.
cell() {
    _uut_card=$1; _ref_card=$2; _arm=$3; _tag=$4; _uut_ser=$5; _mask=$6
    if [ "$_arm" = out ]; then
        _play=$_uut_card; _rec=$_ref_card
    else
        _play=$_ref_card; _rec=$_uut_card
    fi
    aplay -D hw:$_play -q $TONE >/dev/null 2>&1 &
    _ap=$!
    arecord -D hw:$_rec -f S24_3LE -c2 -r$RATE -d $SECS "$OUT/$_tag.wav" \
        >/dev/null 2>&1 &
    _ar=$!
    sleep 2                     # both streams live and past their set_rate
    apply_mask "$_uut_card" "$_mask" || { kill $_ap $_ar 2>/dev/null; exit 1; }
    wait $_ar 2>/dev/null
    _after=$($TLM read 9 --serial $_uut_ser 2>&1 | sed -n 's/.*codec word=0x\(..\).*/\1/p')
    kill $_ap 2>/dev/null; wait $_ap 2>/dev/null
    printf '  %-22s ' "$_tag"
    # --skip=3 so the analysed window starts a full second after the mask
    # landed at t=2, with the stream already running before that.
    $LEVEL --skip=3.0 "$OUT/$_tag.wav" | sed -n 's/^  ch/ch/p' | tr '\n' '|'
    printf ' 0x23=0x%s' "$_after"
    echo
}

sweep() {
    _uut_name=$1; _uut_ser=$2; _uut_card=$3; _ref_card=$4
    echo
    echo "================ UUT = unit $_uut_name  (REF = the other) ================"
    # The mask is applied inside cell(), mid-stream. ABORTING when the device
    # does not confirm it is cell()'s job for the same reason: the first
    # version of this script grep'd for the success text and printed nothing
    # when the request failed outright, turning a hard failure into ten clean
    # identical rows. Never let the confirmation step be the thing that can be
    # silent.
    for mask in both none a b both; do
        cell $_uut_card $_ref_card out "${_uut_name}_${mask}_out" $_uut_ser $mask
        cell $_uut_card $_ref_card in  "${_uut_name}_${mask}_in"  $_uut_ser $mask
    done
}

sweep A $SER_A "$CARD_A" "$CARD_B"
sweep B $SER_B "$CARD_B" "$CARD_A"

echo
echo "READ IT LIKE THIS:"
echo "  The two 'both' rows for a unit must agree. If they do not, the run is"
echo "  void -- something drifted and no row between them can be trusted."
echo "  ch0 is the FED channel (src1, the crossed TS leg). ch1 is unfed and is"
echo "  the control -- BENCH_WIRING.md measures ~66 dB between them, so a row"
echo "  claiming silence has to clear ch1's floor to mean anything."
echo "  A Feature Unit is possible ONLY if 'a' and 'b' kill opposite arms."
echo "  0x23= is the codec word high byte read back AFTER the capture. If its"
echo "  low nibble is not the requested mask, something re-raised the pair"
echo "  mid-capture and that row measures nothing."
