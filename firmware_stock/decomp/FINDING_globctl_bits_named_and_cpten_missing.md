# GLOBCTL bit 1 is P3PUDIS, bit 0 is CPTEN — and mboxfw sets neither

2026-07-31, from `reference/tas1020a/ti_uac_reference/` and TAS1020B §6.5.7.4.
The TI reference application had been sitting unread in the repo; §6.5.7.4 had
been read for other bits but never for bits 1 and 0.

## 1. GLOBCTL bit 1 has a name, and it is not mysterious

TAS1020B §6.5.7.4, the full bit map:

    7 MCUCLK   6 XINTEN   5 P1PUDIS   4 VREN   3 RESET   2 LPWR   1 P3PUDIS   0 CPTEN

> **P3PUDIS** — Pullup resistor disable. If set to 1, disables on-chip pullup
> resistors on P3 GPIO pins.

This project has carried "GLOBCTL bit 1 — UNKNOWN" since
`rev20_STARTUP_TRACE.md` was written; it is repeated in `hw_init.c`, in
`FINDING_globctl_bit1_missed.md`, and in `tools/rev20_diff_justifications.md`.
It is P3PUDIS. Stock's `GLOBCTL = 0x06` is `LPWR | P3PUDIS`: run normally, and
**turn off the internal pull-ups on P3**.

### This also explains the hardware result that got it reverted

Build 0x0010 (with `GLOBCTL |= 0x02`) never attached; 0x0011 (without) attached
in 7 s — bisected back to back, and recorded in `hw_init.c` with the
explanation "doing it to a live USB engine stops enumeration". That explanation
was a guess and is almost certainly wrong. The real path is in `main.c:48`:

    static void check_boot_dfu_button(void)
    {
        for (i = 0; i < 0x5000; i++) {
            if (P3 & P3_BTN_CH1_MASK) { held = 0; break; }
        }
        if (held) {
            if (eeprom_smoke_test()) (void)eeprom_invalidate_signature();
            for (;;) { }          /* never attach */
        }
    }

and in the comment right above its call site at `main.c:255`:

> `check_boot_dfu_button()` runs after `hw_init` **so P3 pull-ups are set before
> the button is sampled**

**The button read explicitly depends on the internal P3 pull-ups — and
`GLOBCTL |= 0x02` disables them.** With them off, that pin never reads high, the
loop never breaks, `held` stays 1, and the firmware **invalidates its own EEPROM
signature and spins forever without attaching.** That is exactly "silent on USB",
it needs no USB-engine theory, and it is consistent with this project's
button-hold brick history (#143).

It also means build 0x0010 very likely **invalidated the signature** on the unit
it ran on — which is the DFU trigger. Anyone reading the bisect should know the
"silent" build was not passively silent; it was actively rewriting the header.

Consequence: setting P3PUDIS is not inherently fatal. It is fatal *in
combination with* a button read that relies on the internal pull-ups. Stock sets
it and reads its buttons fine, so the board has its own pull-up arrangement.
Reinstating it means sampling the button before the write, or fixing the read —
not leaving the bit permanently unexplained.

## 1b. CORRECTION 2026-08-03 — the bit is required, not merely tolerable

Everything in §1 about what P3PUDIS *is* holds. The reading of *why build 0x0010
went silent* was half right and it mattered.

§1 says the boot-button read "explicitly depends on the internal P3 pull-ups",
which assumes the buttons are active-low. They are **active HIGH**. The board
holds P3.3/P3.4/P3.5 low at rest and a press drives them high; the pull-ups do
not enable that read, they **defeat** it by pinning the port at 1.

Proof, from the stock image rather than from the datasheet: `p3_button_scan`
fires on `prev == 0 && cur == 1`, and Keil's `?C_INITSEG` table zeroes the
previous-sample shadow at IRAM 0x20 (record `01 20 00`). If those pins idled
high, all three handlers would fire on the first scan of every boot — both
channels stepping MIC→LINE, mono toggling — before anyone touched the box. The
hardware boots to MIC and holds. Confirmed by the complementary measurement:
mboxfw with P3PUDIS clear reads P3 = 0xFA with bit 3 stuck at 1 under a held
button, while stock on the same unit cycles mic → line → inst.

So the corrected conclusion is stronger than "not inherently fatal": **P3PUDIS
is required for the front-panel buttons to work at all.** Build 0x0010 was
silenced by an active-low test meeting an active-high button — `held` was true
with nothing pressed — not by the pull-ups being needed. Both are fixed
together in build 0x0016; either alone is useless or dangerous. #169 answered.
Full write-up: `FINDING_buttons_are_active_high.md`.

## 2. mboxfw never enables the codec port — **RETRACTED 2026-08-04, the section is false**

**mboxfw sets CPTEN, and always has.** `hw_init.c:302`:

    GLOBCTL  |= 0x01;   /* enable codec port (CPTEN) */

`git log -S` puts it in the initial skeleton commit `ffa7da4`; it was never
added and never removed. It reaches the shipped image — `build/hw_init.rst`
line 400 emits `90 FF B1 / E0 / 44 01 / F0`, and that string is in
`build/mboxfw_flasher.bin` at offset **0x43F**. It is correctly placed, after
all six CPTCNF/CPTRXCNF writes, exactly as §6.5.7.4 requires and as stock does
at Rev 20 `0x0934` / Rev 22 `0x0805`.

The error was a grep, and the reason is worth keeping: the line has **two
spaces** before `|=`. A search for `GLOBCTL |= ` with one space matches
`hw_init.c:129` and `main.c:342` but not this line — and the section below was
written on the strength of a grep described as "exhaustive". This is the second
time in this project a claim of absence from a text search turned out to be a
whitespace or addressing-mode artifact (the first was `INC DPTR` reaching
0xFFB1, recorded in the same file). CLAUDE.md's rule already covers it: never
argue from absence in a tool's output.

Everything below is retained as the historical record of the wrong claim. The
"honest complication" it ends on — `CPTSTA` reading 0x70 with real audio
flowing — was the correct signal and was overruled by the grep. With CPTEN set
there is no complication: hardware setting TXE on an enabled port is ordinary.

### The original (false) section follows

§6.5.7.4, bit 0:

> **CPTEN** — Codec port enable. The codec port enable bit is set to a 1 by the
> MCU to enable the operation of the codec port interface. Note that the codec
> port interface configuration registers must be fully programmed before this
> bit is set by the MCU.

Stock writes GLOBCTL six times (`XDATA_ACCESS_MAP.md`): `0x0334` and `0x0799`
clear bit 0, `0x07A6`, `0x0934` and `0x0FF9` set bit 0, and `0x08FE` writes 0x06.
The pattern is CPTEN off → reconfigure → CPTEN on, every time.

**mboxfw contains no write to GLOBCTL at all.** Exhaustive grep of
`mboxfw/src` and `mboxfw/include`: the only executable reference is
`main.c:231`, `tlm_boot_handoff[2] = GLOBCTL;` — a *read* for telemetry. Every
other hit is a comment. And telemetry block 8 byte 2 measured the boot ROM
leaving `GLOBCTL = 0x04` on this actual part, so bit 0 arrives clear and is never
set.

mboxfw programs CPTCNF1..4, CPTCTL, CPTRXCNF2..4, DMATSH/DMATSL and then never
turns the port on.

### The justification table asserts a write that does not exist

*(2026-08-04: this subsection is the inverted one. The original rows were right.)*

`tools/rev20_diff_justifications.md` rows for 0xffb1 said, twice, "mboxfw's
single `|= 0x01` is sufficient" and "mboxfw only sets (`|= 0x01`)". **Both were
accurate**, and were overwritten on 2026-07-31 with the false claim that no such
line exists. A gate reads this table, and CLAUDE.md's rule is that a wrong row
there is worse than no row — so this edit did the exact damage the rule warns
about, for four days. Both rows are restored and annotated.

### The honest complication

Telemetry block 6 read `CPTSTA = 0x70` mid-stream, where mboxfw writes 0x50 —
so hardware set TXE (bit 5), and 288 B/ms of *varying* data reached the host.
Both are hard to square with a codec port that was never enabled. Either CPTEN is
being set by something not visible in the source, or the C-port does more than
CPTEN gates, or the "varying" capture data is not coming from the codec. This is
recorded as a tension rather than resolved, and #168 settles it by reading
GLOBCTL back live rather than at boot-ROM handoff.

## 3. TI's reference gives the canonical order, and one idiom mboxfw gets wrong

`Application/Codec.c`, `coInitCodec()`:

    GLOBCTL &= 0xFE;    // C-port is disabled
    CPTCNF1 = 0x0C;  CPTCNF2 = 0xE5;  CPTCNF3 = 0xAC;  CPTCNF4 = 0x03;
    DMACTL0 = 0x01;     // full write, DMAEN clear
    DMATSH0 = 0x80;  DMATSL0 = 0x03;
    DMACTL0 = 0x01;     // full write again
    DMACTL0 |= 0x80;    // enable
    GLOBCTL |= 0x01;    // C-port is enabled

Three things worth taking:

  * The **CPTEN bracket** around all C-port programming — which is what stock
    does and what mboxfw does not.
  * **Full-register writes to DMACTL**, not read-modify-write. TI writes the
    whole configuration byte with DMAEN clear, sets up the slots, rewrites it,
    then ORs in DMAEN.
  * `CPTCNF3 = 0xAC` is commented "**byte inverse** & 1clk delay" — TI's own name
    for BYOR = 1, useful for #161.

`Application/SoftPll.c` ends with TI's version of the Rev 22 watchdog, which
confirms that reading independently:

    if (EngAcgCap2 % DEV_NUM_BYTE_PER_SAMPLE) {
        // Alligning the UBM and DMA pointers
        DMACTL0 = 0;      // non-integral, so reset pointers
        DMACTL0 = 0x81;
    }

Note the comment: **writing DMACTL to 0 is how the UBM and DMA pointers are
reset.** TI does a full zero write, then a full re-write with DMAEN set. mboxfw
uses `DMACTL1 &= ~DMA_EN`, which preserves EPDIR/EPNUM — so if the pointer reset
is triggered by the register going to zero, **mboxfw's stream stop never resets
the DMA pointers.** Stock uses RMW here too, so this is a divergence from TI
rather than from stock, and it is only interesting because a stale pointer pair
is exactly the shape of a fixed-phase capture artifact. Recorded as a lead.

TI applies the realignment to channel 0 (playback/OUT) only, and its comment
says why — "Check if host drops part of a sample". The IN direction cannot be
misaligned by the host, so no capture-side equivalent exists in TI, in Rev 20, or
in Rev 22.

## For #147

Nothing here derives the 3-in-8 duty either. But "the codec port interface is
never enabled" is a far larger and more basic divergence than anything found in
the register *values*, and it is the first candidate that would plausibly stop
the capture path from ever having worked correctly. #168 settles whether it is
real in one byte.
