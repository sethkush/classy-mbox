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
started driving. It currently reports 12 known gaps and no unexplained ones.

Note the `MOV bit,C` opcode in that scan. A `SETB`-only search reported IRAM
0x21's low bits as never set and made stock's whole stream-start path look like
dead code; the bits are set by `MOV bit,C` from `std_set_interface`. Any future
"is this bit ever set" question has to cover D2, 92 and B2, and this gate does.
