# 0xFFDC is control AND status, and that closes the 0x70 question

**Task #164, resolved 2026-08-04 — but not the way the task framed it.**

#164 read: *"Rename CPTSTA to CPTCTL — 0xFFDC is a control register, not a
status register."* The rename is right. The reason given is wrong, and the
wrong reason mattered, because the register being *both* is what explains a
reading that four separate investigations filed as unexplained.

## What the sources actually say

TI's `Reg_stc1.h` defines **both names at the same address** — this is not a
correction of one by the other, both lines are live:

```c
/* reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h */
#define CPTSTA      stc_sfr(0xFFDC)      /* line 49 */
#define CPTCTL      stc_sfr(0xFFDC)      /* line 50 */
```

The datasheet names it once, and the full title is the point:

> **6.5.4.5 Codec Port Interface Control *and Status* Register (CPTCTL —
> Address FFDCh)**
> "The codec port interface control and status register contains various
> control and status bits used for the codec port interface operation."

The register-map table (§6.5.4, datasheet p. 5062 of the extracted text) lists
the address as `CPTCTL`. So `CPTCTL` is canonical and `CPTSTA` is TI's alias
for the same address; there is one register, with per-bit types:

| bit | mnemonic | type | meaning |
|---|---|---|---|
| 7 | RXF | **R** | receive data register full — hardware sets |
| 6 | RXIE | R/W | receive interrupt enable |
| 5 | TXE | **R** | transmit data register empty — hardware sets |
| 4 | TXIE | R/W | transmit interrupt enable |
| 3 | — | R | reserved |
| 2:1 | CID(1:0) | R/W | codec ID, AC'97 primary/secondary select |
| 0 | CRST | R/W | codec reset → the CRESET output pin |

## The 0x70 reading was never a discrepancy

`hw_init` writes `0x50`; telemetry block 6 reads `0x70` back. That gap is
flagged as odd, unresolved, or evidence of hardware misbehaviour in **four**
documents:

- `FINDING_147_cport_and_ep_buffer_divergences.md`
- `FINDING_170_audio_works.md` (under #168)
- `FINDING_capture_works_anyway.md`
- `FINDING_globctl_bits_named_and_cpten_missing.md` — which calls it the
  "honest complication" the document ends on

Decode it against the table:

```
write 0x50 = 0101 0000 = RXIE | TXIE            both R/W, both control
read  0x70 = 0111 0000 = RXIE | TXE | TXIE      the same two, plus TXE
```

**Every writable bit reads back exactly as written.** The single extra bit is
TXE — bit 5, read-only, set by hardware when the transmit data register has
been sent to the codec. Hardware reporting its own state through the same
address it takes control bits on is the entire definition of a control-and-
status register.

Nothing was ever wrong with the hardware. The *name* was wrong, in a way that
made a normal reading look pathological: `CPTSTA` said the whole byte was
status, `hw_init` treated the whole byte as control, and the truth is per-bit.
Each investigation that hit `0x70` reached for "the part is not holding the
value written" because the name offered no other reading.

This is the same failure mode as the `DMACTL1`/`ACGCTL` mix-up recorded in
`telemetry.c` and the retired `CPTCTL`/`CPTBRRX`/`CPTCNF1-4` labels in
`regs.h`: *a register name is a claim, and an unverified one propagates into
every conclusion drawn downstream of it.*

## The clear-on-read caution was invented by the wrong name

`telemetry.c` carried:

> "CAUTION: if CPTSTA has clear-on-read bits, reading it here consumes them."

The datasheet refutes it directly. RXF "is cleared to a 0 by hardware when the
MCU reads the new value from the **receive data register**" (CPTDATH/CPTDATL,
0xFFD9/0xFFDA); TXE "is cleared to a 0 by hardware when a new data byte is
written to the **transmit data register**". Neither is cleared by reading
0xFFDC. It also notes that writing the interrupt vector register clears the
*interrupt* but explicitly **not** these status bits.

Reading CPTCTL for telemetry is side-effect free. The caution was a guess
dressed as a hazard, derived from nothing but the letters `STA`.

## Loose end, deliberately not alarmed about

`0x50` leaves **CRST = 0**, and per the table that holds the CRESET output pin
active low. Stock writes the same `0x50` (Rev 20 `fcn.0x08CB`, Rev 22
@0x0844), and stock demonstrably drives a working codec — so CRESET is not
how this board resets the codec. That is consistent with #166: the codec's
reset is IRAM `0x23.4` (RESET_N) delivered through shift chain B, not through
the TAS's CRESET pin. Recorded here so the next reader who decodes CRST=0 does
not re-open it as a bug.

Not independently verified: no schematic has been read, so "CRESET is unused
on this board" remains an inference from stock's behaviour, not a measurement.

## What changed

- `regs.h` — `CPTSTA` → `CPTCTL`, with the bit table and this reasoning inline
- `hw_init.c`, `telemetry.c` — renamed; the clear-on-read caution replaced
  with the datasheet's actual clearing rules
- `tools/mboxtlm.py` — `dec_cptctl()` decodes the bits at the point of
  reading, instead of printing a raw byte and a guess. Printing `0x70` bare is
  what made it look anomalous four times
- `tools/verify_init_order.py`, `TELEMETRY.md` — renamed
- `XDATA_ACCESS_MAP.md`, `ANNOTATION_CLAIMS.tsv` — regenerated

The other four documents are **not** rewritten. They are dated records of what
was believed when written, and editing them would erase the evidence that this
name misled four investigations in a row. This document is the correction they
point forward to.
