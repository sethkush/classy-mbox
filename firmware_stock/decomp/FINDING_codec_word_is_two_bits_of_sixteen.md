# mboxfw drives 2 bits of the 16-bit codec control word; stock drives 12

2026-07-31, in answer to "is there anything else that can be figured out
without the mbox plugged in?" Yes — by generalising the last defect instead of
looking for a new one.

#166 was found by hand: IRAM 0x23.4 is the external-chip RESET, stock sets it
once at boot, mboxfw never does. That is a *class* of bug — "stock drives this
control line, mboxfw never does" — and it had never been checked systematically.
`tools/latch_word_bit_diff.py` now checks all 24 bits, and is wired into
preflight.

## The result

    IRAM 0x22  g_mux_state         mboxfw can set mask 0xFF   — fully driven
    IRAM 0x23  g_codec_state_23    mboxfw can set mask 0x0C
    IRAM 0x25  g_codec_state_25    mboxfw can set mask 0x00

`g_codec_state_25`'s only writes anywhere in mboxfw are three `= 0` sites
(`codec.c:27`, `codec.c:96`, `power.c:74`). **The codec control word's entire low
byte is never driven.** The word mboxfw shifts out is always `0x0000` or
`0x0C00` — two bits out of sixteen, both from `streaming.c:179`.

Stock drives twelve. Ten of those are real gaps:

| bit | function | status |
|---|---|---|
| 0x23.0, 0x23.1 | mode-5 branch | **not a gap** — reachable only from work code 0x0A, which nothing posts in either image. Dead in stock too. |
| 0x23.2, 0x23.3 | mute / audio-path enable | mboxfw sets these (`\|= 0x0C`) |
| 0x23.4 | external-chip RESET, active low | **#166** — never released |
| 0x23.6 | mono | mboxfw keeps it in a separate `__bit g_mono`, which `mux_write()` presents as the **panel** chain's 9th bit. That half works. The **codec** word's bit 6 is always 0 where stock mirrors the live state. |
| 0x25.0–0x25.3 | per-channel source pattern | never driven |
| 0x25.4 | UAC Selector Unit position | **#159** — read by `codec_source_changed()`, set by nothing |
| 0x25.5 | S/PDIF receiver engaged | never driven |
| 0x25.6 | bring-up-has-run guard | never driven |
| 0x25.7 | CS8427 chip select, active low | **#167** — never driven, so the part never sees the high→low transition that selects SPI mode |

## A knock-on defect the sweep exposes

`codec.c:43`:

    if ((g_codec_state_25 & 0x10) || (g_codec_state_25 & 0x20)) {
        g_mux_state &= ~0x40;
    } else {
        g_mux_state |= 0x40;
    }

Both tested bits are permanently zero, so the condition is **constant false** and
the `else` always runs. `g_mux_state` bit 6 is set unconditionally. Stock's
derivation is meaningful; mboxfw's is degenerate. It happens to land on the same
value stock uses at stream start (mux `0x76`, bit 6 set), so it is not obviously
harmful — but it is a live branch that cannot branch.

## Why this matters for #147

`FINDING_147_the_capture_stream_is_noise.md` established that the capture stream
is noise at -3.5 dBFS, ~90 dB hotter than a working ADC on a quiet input, and
that the right question is why nothing drives CDATI. This is the most direct
answer available: **the codec is held in reset with every one of its control
lines at 0.** It has never been told to do anything.

Three previously separate items — #166 (reset), #159 (selector), #167 (chip
select) — are all instances of the same omission, and #170 covers the rest.

## The gate

`tools/latch_word_bit_diff.py` scans both stock images for every opcode that can
set a bit (`SETB` D2, `MOV bit,C` 92, `CPL` B2) at each bit address, and parses
mboxfw's assignments to the three mirrors to over-estimate what it can set — so
a reported gap is a real gap. Each known gap carries a reason in `EXPECTED_GAPS`;
an unlisted gap fails, and so does a stale entry for a bit mboxfw has since
started driving. As of this writing it reported 12 known gaps and no
unexplained ones; see the 2026-08-03 section below, after which it reports 4.

Note the `MOV bit,C` opcode in that scan. A `SETB`-only search reported IRAM
0x21's low bits as never set and made stock's whole stream-start path look like
dead code; the bits are set by `MOV bit,C` from `std_set_interface`. Any future
"is this bit ever set" question has to cover D2, 92 and B2, and this gate does.

---

## RESOLVED 2026-08-03 (build 0x001B) — the source nibble is now driven

`codec_source_changed()` derives IRAM 0x25.0-.3 from `g_mux_state` on every
publish, so the codec chain now agrees with the panel/relay chain. The map,
read off the state machines in both images (identical instruction for
instruction — Rev 20 `button_a_cycle_3state` @0x0E27 / `button_b_cycle_3state`
@0x0E9D, Rev 22 `panel_state_cycle_A` @0x0E1B / `panel_state_cycle_B` @0x0E8F):

    pattern 0x06 MIC  (boot) -> (lo,hi) = (0,0)
    pattern 0x05 LINE        -> (1,1)
    pattern 0x03 INST        -> (1,0)

ch1 = (0x25.0 lo, 0x25.2 hi), ch2 = (0x25.1 lo, 0x25.3 hi).

Stock stores the 2-bit state and derives the 3-bit panel pattern; mboxfw stores
the pattern and derives the state. Equivalent, because the pattern is only ever
one of the three legal one-cold values.

**Why this is the #147/#170 candidate.** Until this build the low nibble was
write-zero-only, so the codec's own input select read MIC on both channels no
matter what the relays did. `FINDING_147_capture_works_analog_path_does_not.md`
measured a working ADC (a clean first-order settling transient, ~-100 dBFS
floor), both DMA paths live mid-stream, and no tone through a line loopback.
An ADC converting the mic input while the relay routes line in produces exactly
that. Not yet confirmed on hardware — see the measurement note below.

Boot is unaffected: MIC maps to (0,0), which is what `codec_init()` already
published, so this changes nothing until a source actually changes.

### Remaining gaps in the word, and why they are not omissions

  * **0x23.0 / 0x23.1** — dead in stock too (mode-5 branch, work code 0x0A,
    which nothing posts in either image).
  * **0x25.4** — Selector Unit position. mboxfw has no Selector Unit (removed
    in 0x001A), and 0 *is* the analog position, so clear is correct.
  * **0x25.5** — "S/PDIF receiver engaged". Only stock's P3.1-fall handler sets
    it. Clear is correct for the analog path.

Both remaining bits being 0 also means `codec_source_changed()`'s derivation of
0x22.6 is still constant-false-with-the-else-taken — degenerate, but it lands on
the correct value for the analog case, which is the case that matters here.

### Gate bug this exposed

`tools/latch_word_bit_diff.py` scanned line-by-line with the RHS terminator
`[^;\n]*` — "semicolon **or end of line**". Any statement wrapped across lines
was truncated to its first line; the first version of this fix captured only
`= (unsigned char)`, which has no literals and no letters after cast-stripping,
so it passed `_is_pure_literal` and contributed **zero** to the settable mask.
The function's contract is "over-estimates, never under", and that path
under-estimated — the one direction that turns a real gap into a silent pass.

Fixed by splitting the scan into two passes over disjoint text: statements
(';'-terminated, newlines spanned) and `#define` bodies (line-terminated,
because a macro body carries no semicolon of its own and a ';'-terminated scan
would run past the end of the define into the next statement). Verified by
mutation: deleting one of the four bit writes takes the gate from exit 0 to
exit 1 with `IRAM 0x25.3 ... UNEXPLAINED`.

The C is written as an explicit clear-then-set with literal masks rather than
one compound `x = (x & ~0x0F) | <expr>`, for the same reason: the compound form
drives the gate onto its conservative branch, masking the byte to 0xFF and
reporting 0x25.4/0x25.5 as driven when nothing drives them. Over-estimating is
"safe" for the gap check but destroys the gate as a signal. It also happens to
be the shape stock uses — per-bit SETB/CLR, never a byte store.
