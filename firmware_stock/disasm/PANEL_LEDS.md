# Mbox 1 front-panel LED map — resolved

**Date:** 2026-07-26. Supersedes every "External wiring UNKNOWN" note about
IRAM 0x22 in `rev20_ANNOTATED.md` and `rev22_ANNOTATED.md`. Those notes were
written from the disassembly alone; combining them with observed hardware
behaviour pins the mapping exactly.

## Observed behaviour (real unit, this hardware)

| Firmware | Panel state |
|---|---|
| Power-on, before firmware drives anything | **all LEDs on** — but see the correction below |
| Stock Rev 20 (and Rev 22) after init | spdif, USB, mono, inst×2, line×2 **off**; **two mic LEDs on** |
| mboxfw | reaches the **same two-mic-LED state** |
| safety_net | **all on** — never drives either latch |
| bootstrap / boot-ROM DFU | **all on** — boot ROM never drives either latch |

Peak LEDs flash on then off at power-up; that is analog/hardware, not firmware.

### Correction 2026-08-10: "all LEDs on at power-on" is not universal

That row was written from **one unit**. The second unit on the void box
(`RK1672500M`) does not do it — it comes up with the panel in a different state
before firmware drives either latch. Two units, two behaviours, so an undriven
panel is **not** guaranteed to read all-on.

The mechanism claimed below — "neither chain is driven by the boot ROM, which is
why an undriven panel reads all-on" — explains why the panel is undriven, which
is solid, but not what an undriven panel *shows*. That depends on how the
4094 outputs power up and on per-unit analog behaviour, and it evidently varies.

Consequence for anyone debugging: **the panel is not a reliable indicator of
whether firmware has run.** Use telemetry block 0 (`build id`, `stage`,
`phases`) instead, which is unambiguous and costs one EP0 read.

## The two latch chains

Both are bit-banged shift registers on Port 1. Neither is driven by the boot
ROM, which is why an undriven panel reads all-on.

| Chain | Pins | Source | Rev 20 fn | Rev 22 fn |
|---|---|---|---|---|
| 8-bit | P1.7 data, P1.5 clock, P1.6 latch | IRAM `0x22` | `0x0F0C` `shiftreg8_commit_p1_7_6_5` | `0x0EFC` `shiftreg_out8_p1hi` |
| 16-bit | P1.0 data, P1.2 clock, P1.1 latch | IRAM `0x23` then `0x25` | `0x0E62` `shiftreg16_commit_p1_0_1_2` | `0x0E56` `shiftreg_out16_p1` |

## 8-bit chain (IRAM 0x22) — input-source LEDs, ACTIVE LOW

Two one-cold groups of three. "One-cold" = exactly one bit LOW per group,
selecting that source — which is exactly an active-low LED-per-source drive.

| Bit | Bit-addr | Role |
|---|---|---|
| 0x22.0 | 0x10 | channel A — **mic** |
| 0x22.1 | 0x11 | channel A — line |
| 0x22.2 | 0x12 | channel A — inst |
| 0x22.3 | 0x13 | channel B — **mic** |
| 0x22.4 | 0x14 | channel B — line |
| 0x22.5 | 0x15 | channel B — inst |
| 0x22.6 | 0x16 | control line, **not an LED** — `= !bit0x2c && !bit0x2d` |
| 0x22.7 | 0x17 | run/stop-like line, **not an LED** |

**Bit clear = LED lit.**

### Proof

`hw_master_init` (Rev 20 `0x08CB`) at `0x095B–0x0960`:

```
0x095B  75 22 ff    mov  0x22,#0xFF     ; all source LEDs off
0x095E  c2 10       clr  0x10           ; 0x22.0 -> channel A position 0
0x0960  c2 13       clr  0x13           ; 0x22.3 -> channel B position 0
0x0964  12 0f 0c    lcall 0x0F0C        ; shift IRAM 0x22 out
```

Final byte = `0xF6` = `1111 0110`, i.e. exactly bits 0 and 3 clear → exactly
two LEDs lit → **the two mic LEDs**, matching the observed end state. Position
0 of each one-cold group is therefore mic.

The grouping itself is from the annotations: bits .0/.1/.2 are the channel-A
one-cold outputs written by `panel_state_cycle_A`, bits .3/.4/.5 the channel-B
outputs written by `panel_state_cycle_B` (`rev22_ANNOTATED.md` §2.2). Three
positions per channel = the three front-panel sources.

Corroboration: the stream-start path at `0x039B` writes `0x22 = 0xFF` (all
source LEDs off) then selectively clears, and the input-select command handlers
at `0x045C`/`0x046B` toggle bit 0x16 — the control line, never a source bit.

## 16-bit chain (IRAM 0x23:0x25) — spdif / USB / mono, plus control

The remaining three panel LEDs (spdif, USB, mono) are **not** on the 8-bit
chain — it only has six LED bits and they are all accounted for. They ride the
16-bit chain, which Rev 20 commits during `hw_master_init` at `0x096C` with
`0x23 = 0x00` (written `0x096A`) and `0x25 = 0x00` (written `0x0967`), i.e.
those LEDs go dark. Opposite polarity to the 8-bit chain.

Individual bit→LED assignment within this chain is **not yet pinned**; the byte
also carries audio-path control (`0x25.4` input-source flag, `0x25.6` hw-init-
done, `0x25.7` serial chip-select), so it is a mixed control/LED word. What is
established is that committing `0x0000` here is what extinguishes spdif/USB/mono.

## Why this matters for the silent-USB bug

mboxfw's `mux.c` is the 8-bit chain driver and `codec.c` is the 16-bit chain
driver (`codec_shift_byte` + `codec_write_word`, porting `fcn.0x0E62`). In
`main()`:

```
usb_init();  hw_init();  check_boot_dfu_button();
EA = 1;  usb_attach();          <-- D+ asserts here
cs8427_boot_init();  codec_init();   <-- 16-bit chain committed here
for (;;) buttons_poll();
```

- `hw_init()` ends with the `mux_write` calls → **two mic LEDs** ⇒ hw_init
  completed.
- The 16-bit chain is only ever committed inside `codec_init()`, which runs
  **after** `usb_attach()` and after `cs8427_boot_init()`.

So if mboxfw shows spdif/USB/mono extinguished as well, `codec_init()` ran,
which means it got through `cs8427_boot_init()` too and reached the polling
loop. Combined with D+ asserting, **mboxfw completes all of `main()` and is
still silent on USB** — no init hang anywhere. That localises the defect to the
USB service path, the same conclusion `safety_net/EP0_DIFF_vs_REV20.md` reached
from the register side.

Supporting check: neither `cs8427.c` nor `codec.c` contains an unbounded loop
(every loop is `for i<8` or `do{}while(--i)`), so there is no hang candidate in
either.

**Open, one glance to settle:** whether mboxfw leaves spdif/USB/mono on or off.
On ⇒ `codec_init()` never ran, and the 16-bit chain is untouched. Off ⇒ mboxfw
runs to completion and the bug is purely in USB servicing.
