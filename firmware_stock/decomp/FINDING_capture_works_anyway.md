# "Capture works anyway" — there was nothing to explain

2026-08-04. Static RE only; no flash cycle, no hardware time.

Two claims had been carried as unexplained anomalies, both phrased as "capture
works despite X":

1. **"Capture is clocked from ACG synthesizer 2, which no runtime code
   configures."** Recorded in `FINDING_161_byor_asymmetry.md`, in commit
   `65786a9`, and stated to Seth as "not a thing I can currently explain".
2. **"mboxfw never sets CPTEN, so the codec port interface is never enabled."**
   `FINDING_globctl_bits_named_and_cpten_missing.md` §2, and task #168.

**Both are false.** Capture works because nothing is wrong with it. Neither
claim survives contact with the disassembly or with mboxfw's own build output.

---

## 1. Two different things were both called "mode 5"

This is the root error, and it produced claim 1 by splicing two unrelated
numbering schemes into one sentence.

**Mode 5 (a):** the MODE field of `CPTCNF1`. Stock boot writes `CPTCNF1 = 0x0D`
= `00001_101` at Rev 20 `0x0902`, so MODE = `101b` = codec-port mode 5, which
the datasheet (§2.4) calls *"1 OUT and 1 IN at different frequencies"*. mboxfw
writes the same 0x0D at `hw_init.c:137`. This is a **codec port** mode.

**Mode 5 (b):** the argument in R7 to `audio_clock_mode_apply` @ Rev 20
`0x0728`, a Digidesign clock-mode index running 1..5. This is a **firmware
dispatcher** argument with its own unrelated numbering.

The dispatch at `0x073C`:

    073c  MOV A,0x2e          ; the R7 argument
    073e  ADD A,#0xfe         ; A = mode - 2
    0740  JZ  0x075f          ;   mode 2 -> 44.1 kHz ACG words
    0742  DEC A               ; A = mode - 3
    0743  JZ  0x078e          ;   mode 3 -> LCALL 0x0dec, 48 kHz ACG words
    0745  ADD A,#0xfe         ; A = mode - 5
    0747  JZ  0x0799          ;   mode 5 -> the CPTEN/CPTRXCNF4 bracket
    0749  ADD A,#0x4          ; A = mode - 1
    074b  JNZ 0x07c5          ;   mode 1 falls through, else straight to tail

The sentence "capture runs off synthesizer 2 because the part is in mode 5, and
mode 5 is reachable only from 0x0A" takes the *codec-port* mode from `CPTCNF1`
and then reasons about the reachability of the *dispatcher's* mode-5 branch.
They are not the same 5. The part being in codec-port mode 5 says nothing about
whether the dispatcher's mode-5 branch ever runs.

## 2. ACG synthesizer 2 is programmed, by both firmwares, on every rate change

Claim 1 asserted that nothing configures it. Every synthesizer-2 register is
written in straight-line code on the ordinary rate path.

`acg_set_freq_48k_family` @ Rev 20 `0x0DEC` — called from `0x078E` (dispatcher
mode 3, the stream-start path) and from `0x081B` (`audio_path_reconfig_ext_chips`):

    0dec  ACG1FRQ1 (0xffe6) = 0xa8      0dfe  ACG2FRQ1 (0xfff8) = 0xa8
    0df2  ACG1FRQ2 (0xffe5) = 0x61      0e04  ACG2FRQ2 (0xfff7) = 0x61
    0df8  ACG1FRQ0 (0xffe7) = 0x0f      0e0a  ACG2FRQ0 (0xfff9) = 0x0f

Both synthesizers, same word, one function. The 44.1 kHz arm at `0x075F` does
the identical thing with `0x6A/0x4B/0x20`, writing `0xFFF8/F7/F9` at
`0x0771/0x0777/0x077D`. `acg_set_both_dctl_10` @ `0x0E18` writes `0x10` to
`ACGDCTL` **and** `ACG2DCTL` (`0xFFF6`) in one call.

mboxfw's `streaming_set_rate()` mirrors all of it — `ACG2FRQ0/1/2` for both
rates and `ACG2DCTL = 0x10` — and has since the ACGCTL/DIVEN fix. Synthesizer 2
is configured on every `SET_INTERFACE` and every `SET_CUR(rate)`.

The only thing that made synthesizer 2 look neglected is that it is written via
`INC DPTR`/`DEC DPL` chains and through shared tail helpers, which is the exact
scanner-blindness trap CLAUDE.md already warns about.

## 3. mboxfw sets CPTEN, and has since its first commit

`hw_init.c:302`:

    GLOBCTL  |= 0x01;   /* enable codec port (CPTEN) */

`git log -S` dates it to the initial skeleton commit `ffa7da4` — never added,
never removed. It is in the shipped image, not merely the source:
`build/hw_init.rst` line 400 emits `90 FF B1 / E0 / 44 01 / F0`, and that byte
string appears in `build/mboxfw_flasher.bin` at offset **0x43F**.

Placement is correct per §6.5.7.4 ("the codec port interface configuration
registers must be fully programmed before this bit is set"): it follows all six
`CPTCNF*`/`CPTRXCNF*` writes, matching stock's boot bracket at Rev 20 `0x0934` /
Rev 22 `0x0805` and TI's `coInitCodec()`.

All three GLOBCTL writes in mboxfw are read-modify-write — verified in the
listings, `E0 / 44 xx / F0` or `E0 / FF / 43 07 xx / EF / F0` — so none clobbers
CPTEN. Ordering is safe too: `main.c:342`'s P3PUDIS write runs *before*
`hw_init`.

### Why the 2026-07-31 grep missed it

The line has **two spaces** before `|=`. A search for `GLOBCTL |= ` with one
space matches `hw_init.c:129` and `main.c:342` and misses this one. That search
was described as "exhaustive" and a whole finding, a task, and two rows of the
machine-read justification table were built on its null result.

This is the second time in this project a claim of absence from a text search
turned out to be an artifact of how the write was spelled — the first being
`INC DPTR` reaching `0xFFB1` from the `MEMCFG` write 27 instructions earlier,
recorded in the very same file. CLAUDE.md's rule already covers it: **never
argue from absence in a tool's output.**

### The signal that was overruled

The same finding ended on what it called an "honest complication": telemetry
read `CPTSTA = 0x70` mid-stream where mboxfw writes `0x50`, meaning hardware had
set TXE, with 288 B/ms of varying data reaching the host. It offered three
explanations, the first of which was "CPTEN is being set by something not
visible in the source."

That was right, modulo "not visible" — it was visible, and one line long. A
hardware observation was subordinated to a text search, which inverts this
project's own stated precedence. With CPTEN set there is no complication at
all: an enabled port setting TXE is ordinary.

## 4. What host command 10 actually does

The dispatcher's mode-5 branch at `0x0799` is the only code in either image
that brackets CPTEN at runtime:

    0799  GLOBCTL   &= 0xfe        ; CPTEN off
    07a0  CPTRXCNF4  = 0x01        ; receive divider: boot uses 0x03
    07a6  GLOBCTL   |= 0x01        ; CPTEN back on
    07b2  ACG2DCTL   = 0x10
    07b8  SETB 0x18 / SETB 0x19    ; IRAM 0x23.0, 0x23.1
    07bc  LCALL 0x0e62             ; publish the codec word

It is reached only from `cmd10_set_cpt_mode5` @ `0x04BC`, whose sole XREF is
`0x031B` — entry 10 of `event_jump_table` @ `0x0300`, indexed by *work code
minus one* (`0x0300 + 3*9 = 0x031B`).

> **CORRECTION, same day.** This section first claimed "0x0A is a host command
> index, not an internal work code ... nothing in the firmware posts it because
> the Digidesign driver does". **That was wrong**, and it withdrew a correct
> claim in `FINDING_codec_word_bits_resolved.md`. The table is dispatched from
> an internal work code held in **IRAM 0x0A**, and work codes are posted by
> request handlers (`0x005B` posts 0x0D, `0x0006` posts 0x0E) — so the host
> reaches the table *indirectly*, but only through a handler that posts a
> specific code. No handler posts 0x0A.
>
> The reachability is now settled by a scan over **every addressing mode** that
> can write IRAM 0x0A in both images, not just `MOV direct,#imm`:
>
>   - 13 immediates: `0x01`–`0x08` and `0x0B`–`0x0E`. **0x09 and 0x0A absent.**
>   - `MOV 0x0a,A` @ `0x0565` — `evt_dispatch_epilogue`, preceded by `CLR A`.
>     Writes 0.
>   - `MOV 0x0a,A` @ `0x0A06` — A cleared at `0x09F5`, not reloaded. Writes 0.
>
> **No site can post 0x0A. The branch is dead in stock**, as originally
> recorded. (0x09 absent likewise makes clock mode 4 dead, which independently
> matches the dispatcher not implementing it.)
>
> A first pass at this scan also reported `INC 0x0a` at `0x0EE1`. That was an
> artifact: the bytes are `20 05 0a` = `JB 0x05,0x0eed`, and the scanner matched
> `05 0a` *inside* a three-byte instruction. Decode from instruction
> boundaries — the same lesson four earlier tools learned on DPTR arithmetic.

What it does is change `CPTRXCNF4` from `0x03` to `0x01` — the divider from
MCLKO2 to SCLK2 — i.e. it re-clocks the capture path to a rate independent of
playback. That is precisely the capability `CPTCNF1`'s codec-port mode 5
advertises, and it is the one place where the two "mode 5"s genuinely touch.
No class-compliant host sends it, so mboxfw stays at the boot divider with both
directions on one rate, which is what UAC1 wants.

Corollary: **IRAM 0x23.0 and 0x23.1 are dead in stock after all.** Each has
exactly one setter, in this unreachable branch, in both images (Rev 20
`0x07B8/0x07BA`, Rev 22 `0x0796/0x0798`). `latch_word_bit_diff.py`'s
`EXPECTED_GAPS` said "dead in stock too" and was right; it was briefly reworded
on the strength of the erroneous host-command reading above and is now restored,
with the complete-addressing-mode scan as its backing rather than the
immediate-only one.

These are a *different* pair from the `0x23.2`/`0x23.3` of #171, which are set
unconditionally in straight-line code at `0x07EE/0x07F0` and which mboxfw does
mirror. Adjacent bits, adjacent addresses, unrelated reachability — worth not
confusing.

## Consequences

- **#168 is closed as a false premise.** There is no missing CPTEN write.
- Claim 1 is retracted. `FINDING_161_byor_asymmetry.md` and commit `65786a9`
  both carry the "synthesizer nothing configures" wording and are wrong on it.
- `FINDING_globctl_bits_named_and_cpten_missing.md` §2 is retracted in place.
  §1/§1b (P3PUDIS, active-high buttons) are unaffected and still stand.
- Both `0xffb1` rows in `tools/rev20_diff_justifications.md` are restored to
  what they said before 2026-07-31 and annotated. A gate reads that table; the
  2026-07-31 edit put a false row into it for four days, which is exactly the
  failure CLAUDE.md names as "worse than no row".
- No firmware change. No flash cycle. Build `0x001D` is unaffected.

## The transferable lesson

Three artifacts — a finding, a task, and two rows of a machine-read table —
were generated from one grep whose null result was an artifact of whitespace.
None of them was ever checked against the build output, which would have
falsified all three in one command:

    grep -n "ffb1" mboxfw/build/*.rst

An absence claim about a firmware image is cheap to test against the image.
Testing it against the *source text* tests the spelling, not the firmware.
