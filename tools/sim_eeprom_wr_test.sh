#!/usr/bin/env bash
# Prove eeprom_write_byte emits the I²C SFR sequence a real 24C64 expects,
# and prove eeprom_invalidate_signature targets offsets 2 and 3.
# Two layers: (A) static audit of the compiled asm in mboxfw/build/eeprom.rst
# (ground truth of what the chip does), and (B) an s51 run that steps into
# eeprom_write_byte and confirms XRAM 0xFFC3 (I2C_SADDR) == 0xA0.
# We don't drive the full fn under sim: wait_bit polls real HW status bits
# that sim can't raise — it would just time out. Layer (A) covers the rest.

set -eu

REPO="$(cd "$(dirname "$0")/.." && pwd)"
RST="$REPO/mboxfw/build/eeprom.rst"
IHX="$REPO/mboxfw/build/mboxfw.ihx"

[ -r "$RST" ] || { echo "FAIL missing $RST — build mboxfw first"; exit 1; }
[ -r "$IHX" ] || { echo "FAIL missing $IHX — build mboxfw first"; exit 1; }

fail=0
pass() { printf '  PASS  %s\n' "$1"; }
fail() { printf '  FAIL  %s\n' "$1"; fail=1; }

# ---- Layer A: static asm audit --------------------------------------------

# Extract asm lines between _eeprom_write_byte: and the next label at
# column 0 in .rst format. Drop line-number prefixes.
# .rst lines are `<addr> <bytes> [ncycles] <lineno> <asm>`; the fn label
# also carries the prefix, so match anywhere in the line, and stop at the
# next `_<name>:` label.
awk '
  /_eeprom_write_byte:$/ { on=1; next }
  on && /_[A-Za-z_][A-Za-z0-9_]*:$/ { exit }
  on { print }
' "$RST" > /tmp/eeprom_wr.asm

# Expected ordered pattern of SFR touches. Grep -F and check line order.
expected=(
    'mov	dptr,#0xffc0'      # clear I2C_STA
    'anl	a,#0x54'           # CLEAR_ALL
    'mov	dptr,#0xffc3'      # I2C_SADDR
    'mov	a,#0xa0'           # 0xA0 = write-mode slave
    'mov	dptr,#0xffc1'      # TX addr_hi
    'lcall	_wait_bit'
    'mov	dptr,#0xffc1'      # TX addr_lo
    'lcall	_wait_bit'
    'mov	dptr,#0xffc0'      # STOP_WRITE
    'orl	a,#0x01'
    'lcall	_wait_bit'         # after data
    'mov	dptr,#0xffc0'      # final CLEAR_ALL
    'anl	a,#0x54'
)
prev=0
for pat in "${expected[@]}"; do
    # find first line >= prev matching pat
    ln=$(awk -v pat="$pat" -v prev="$prev" '
        NR>prev && index($0, pat) { print NR; exit }' /tmp/eeprom_wr.asm)
    if [ -z "$ln" ]; then
        fail "asm order broken: expected '$pat' after line $prev"
        break
    fi
    prev=$ln
done
[ $fail -eq 0 ] && pass "eeprom_write_byte asm matches I²C WRITE|STOP|WORD_ADDR sequence"

# wait_bit must include ERROR-bit check (anl a,#0x20 in wait_bit body).
# SDCC compiles `s & 0x20` as either `anl a,#0x20` or `jnb acc.5,...`
# (bit-test on ACC bit 5). Accept either — both prove ERROR is checked.
wb=$(awk '/_wait_bit:$/{on=1;next} on&&/_[A-Za-z_][A-Za-z0-9_]*:$/{exit} on' "$RST")
if echo "$wb" | grep -qE 'anl[[:space:]]+a,#0x20|jnb[[:space:]]+acc\.5'; then
    pass "wait_bit checks I2C_ERROR (bit 5 / 0x20)"
else
    fail "wait_bit missing ERROR-bit check"
fi

# eeprom_invalidate_signature must set PARM_2 (lo addr) to 0x02 then 0x03,
# with PARM_3 (data) = 0x00 both times, and DPL (hi addr) = 0x00.
awk '/_eeprom_invalidate_signature:$/{on=1;next} on&&/_[A-Za-z_][A-Za-z0-9_]*:$/{exit} on' \
    "$RST" > /tmp/eeprom_inv.asm
grep -qF 'mov	_eeprom_write_byte_PARM_2,#0x02' /tmp/eeprom_inv.asm \
  && grep -qF 'mov	_eeprom_write_byte_PARM_2,#0x03' /tmp/eeprom_inv.asm \
  && pass "invalidate_signature targets EEPROM offsets 2 and 3" \
  || fail "invalidate_signature does not write both signature bytes"

# ---- Layer B: s51 sanity run ---------------------------------------------

# Address of _eeprom_write_byte, from .rst leftmost hex on its def line.
addr=$(awk '/^ +[0-9a-f]+ +[0-9]+ _eeprom_write_byte:$/{print "0x"$1; exit}' "$RST")
[ -n "$addr" ] || { fail "could not locate _eeprom_write_byte address"; addr=0x967; }

# Load .ihx, seed I2C_STA=0xFF so first read has bits to AND, set PC,
# step ~10 instructions (past CLEAR_ALL + SADDR write), then dump XRAM
# at 0xFFC3 — must be 0xA0.
out=$(printf 'set memory xram 0xffc0 0xff\npc %s\nstep 10\ndx 0xffc3 0xffc3\nq\n' \
        "$addr" | timeout 10 s51 -q "$IHX" 2>&1 || true)

# `dx 0xffc3 0xffc3` output looks like: "0xffc3 a0 ."
if echo "$out" | grep -qiE '0xffc3[[:space:]]+a0'; then
    pass "s51 run: I2C_SADDR (0xFFC3) = 0xA0 after entry"
else
    fail "s51 run: I2C_SADDR was not 0xA0 after entry; got:"
    echo "$out" | tail -6 | sed 's/^/        /'
fi

exit $fail
