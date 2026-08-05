# rev20_flat.asm is a disassembly of the EEPROM, so every address is +0x12

**Measured 2026-08-05.** CLAUDE.md has said for months that
`firmware_stock/disasm/rev20_flat.asm` is "a bad disassembly". That is true in
effect and wrong in detail, and the detail is the useful part.

## What it actually is

It is a linear-sweep disassembly of **`rev20_eeprom.bin`** — the full 8192-byte
EEPROM image *including its 18-byte header* — not of
`rev20_firmware_code.bin`.

Tested against every binary in `firmware_stock/`, counting instructions whose
bytes match the file at the address the listing claims:

| file | size | instructions matching at claimed address |
|---|---|---|
| **rev20_eeprom.bin** | 8192 | **4096 / 4096** |
| rev20_firmware_code.bin | 8174 | 2084 / 4096 |
| rev22_flasher_payload_raw.bin | 8192 | 2114 / 4096 |
| rev22_firmware_code.bin | 8174 | 2003 / 4096 |
| rev20_flasher_payload.bin | 11264 | 374 / 4096 |

And `eeprom[18:30] == code[0:12]`. So:

> **Every address in `rev20_flat.asm` is the true code address + 0x12 (18).**

Confirmed exhaustively: of the instructions at listing address ≥ 18,
**4086 / 4086** match the code image at `(address − 0x12)`.

## Why the instruction decoding is nevertheless correct

A linear sweep that starts inside a header normally desynchronises and emits
garbage until it happens to re-align. This one does not, by luck: the 18
header bytes decode as exactly 18 bytes of instructions —

```
60 12 | 12 34 0d | ba 10 01 | 01 01 | 04 | fa | 02 20 01 | 00 | 1f | ee
 jz      lcall      cjne       ajmp   inc  mov   ljmp      nop  dec  mov
```

2+3+3+2+1+1+3+1+1+1 = 18. The sweep lands byte-aligned on the reset vector
(`02 0a 09  LJMP 0x0a09`) and stays aligned for the whole image.

So the listing's *decoding* of the real code is sound. Only its *addresses*
are wrong, uniformly, by 18.

## Consequence 1: citations sourced from it point at unrelated code

`safety_net/src/main.c:64` cites `rev20_flat.asm 0x0ADE` for the USBCTL CONN
write. Against the real image:

```
0x0ADE:  f4 12 0e d5 ...           CPL A ; LCALL ...      <- unrelated
0x0ACC:  90 ff fc  e0  44 80  f0   MOV DPTR,#0xFFFC(USBCTL)
         (= 0x0ADE - 0x12)         MOVX A,@DPTR ; ORL A,#0x80(CONN) ; MOVX @DPTR,A
```

The citation is right only under the −0x12 correction. Read at face value it
lands on code that has nothing to do with USBCTL — which is exactly how a real
write gets written off as a scanner artifact, the incident CLAUDE.md records
for GLOBCTL.

Roughly ten citations across `mboxfw/`, `safety_net/` and `tools/` quote
addresses from this listing. They use the free-form `rev20_flat.asm @ 0xNNNN`
shape rather than the `Rev 20 fcn.0xXXXX @ 0xYYYY` shape that
`check_citation_targets.py` validates, which is why none of them were ever
checked. Filed as its own task; not fixed here, because each needs
re-deriving rather than blind arithmetic — some may have been written from the
Ghidra listing and merely *labelled* rev20_flat.asm.

## Consequence 2: the gate's phantom Rev 20 writes were its own bug

`diff_vs_rev20.py` consumes only `(SFR address, pattern, immediate)` and never
an instruction address, so the +0x12 offset does not affect its output at all.
The suspicion that its stock-side data was unreliable was correct; the cause
was not.

Compared against `XDATA_ACCESS_MAP.md` (built from the binary plus the Ghidra
recursive listing), the gate's Rev 20 write set was:

- **0 addresses missed**
- **3 addresses invented**: 0xFF29, 0xFF2B, 0xFF2C

Those three are the setup data packet buffer — `bRequest`, `wValue`, `wIndex`.
The UBM writes them; the MCU only ever reads them. Stock writing them is
impossible.

Cause: the scanner carried `last_dptr` across `ret` and branch instructions,
so a `MOVX @DPTR,A` in a *different function* was attributed to whatever
address was loaded last. Example — `0x0067` loads `#0xff29`, reads it, branches,
returns; the DPTR stayed live past all of that.

This is the rule CLAUDE.md already states — *"Track straight-line only; end
the run at any control-flow edge"* — and the gate violated it. Four earlier
tools missed real writes for the mirror-image reason. Restoring the rule
removed all three phantoms.

**A phantom stock write is the dangerous direction of error.** It surfaces as
`REV20_ONLY`, which reads as *"stock does this and we don't"* — an invitation
to copy a write that does not exist. `GLOBCTL |= 0x02` was shipped on exactly
that reasoning and made the device silent on USB.

## Consequence 3: calls, and what the fix nearly broke

Ending the run at `lcall` lost a **real** write: `ACGDCTL` (0xFFE2). Stock
loads DPTR and calls a shared helper which issues the MOVX against the DPTR it
inherited — `0x0736` loads `#0xFFE2` and `LCALL 0x0E18`, whose body is
`74 10 f0 90 ff f6 f0 22` (write 0x10 to the caller's address, then to
0xFFF6). A linear listing cannot see this: the instructions after the LCALL
are the caller's continuation, not the helper's.

The pre-fix scanner reported `('ffe2','assign','0x10')`, which is *correct* —
but arrived at by carrying a stale DPTR across branches until an unrelated
`MOV A,#0x10 / MOVX` turned up. Right answer, no method. Trading three
phantoms for one missed real write would have been a bad trade, and getting
the right value by accident is not a reason to keep a broken state machine.

`load_rev20_helper_writes()` now recovers these from the access map's explicit
`write-via-helper <target> [<value>]` classification, decoding the immediate
from the helper's own body where the map does not record one.

## Result

The gate's Rev 20 write set now equals the access map exactly: **52 addresses,
0 missed, 0 phantom.**

```
before:  matches 69   rev20-only 9   changed 25
after :  matches 74   rev20-only 0   changed 21
```

**All nine `REV20_ONLY` entries were phantoms.** There was never a stock write
mboxfw was failing to make. One genuine diff became newly visible — `0xFFDE`
`assign 0xA8`, stock's *running* CPTCNF3 value reached through helper 0x0FF4,
which the linear scan could not see. It is the same deliberate #161 divergence
already justified for 0xAC, now recorded in its own row.

## Not done

- The ~10 flat-asm citations are still off by 18. Task filed.
- `diff_vs_rev20_safety_net.py` reads the same listing and has the same
  scanner; not audited here.
- Repointing the gate's stock side at the Ghidra listing outright would remove
  the dependency on a listing whose addresses are wrong. The union with the
  access map gets the same write set today, so this is cleanup, not a
  correctness gap.

---

## #180 RESOLVED 2026-08-05 — all citations re-derived, and two were false

Every flat-asm-sourced citation was re-derived against
`rev20_firmware_code.bin` and `rev22_firmware_code.bin` using the access map,
never by subtracting 0x12 blindly. That caution was justified: **the offset
was not the whole story.**

**Correct as written, merely mislabelled (1).** `usb.c` cited
`rev20_flat.asm @ 0x099e` for the EP0 enable, and 0x099E *is* the real
`IEPCNF0` write site. It was derived from a correct source and labelled with
the wrong one. Blind arithmetic would have broken it. It did carry a separate
error: the two register names were paired with each other's addresses
(0xFFA8 is OEPCNF0 and 0xFF68 is IEPCNF0, per TI Reg_stc1.h 109/170).

**Off by exactly 0x12 (most of them).** USBCTL CONN, the USBIMSK 0x9F sites,
the DMA channel/timeslot block, both ACGCTL sites, the settle loop, the
USBCTL-zero site. Each now cites both images.

**Wrong in BOTH directions — i.e. simply false (2).**

1. `safety_net/src/main.c` cited `rev20_flat.asm 0x0910` for `USBFADR = 0`.
   True 0x0910 is a `CPTCNF4` write; 0x0910 − 0x12 is `GLOBCTL`. Neither is
   USBFADR, which stock writes at Rev 20 0x09F2 / Rev 22 0x0913.

2. `rev20_diff_justifications_safety_net.md` claimed *"TI UsbEng.c:640, Rev 20
   rev20_flat.asm 0x0917 both use 0xE5"* for `USBIMSK`. **Rev 20 never writes
   USBIMSK = 0xE5.** Byte-scanned: its only USBIMSK values are 0xFF (0x03F1),
   0x9F (0x09EC, 0x0550, 0x0F6E) and 0x00 (0x0AA6). The 0xE5 seen at flat
   0x0917 is `CPTCNF2` at true 0x0905 — a codec-port register. A value
   collision plus a wrong-register attribution, in a table a gate reads.
   Only the TI reference ever supported 0xE5; the Rev 20 half is withdrawn.

Also corrected: the function label `fcn.0x0982` in `regs.h` and
`verify_usb_init.py` was itself flat-relative — 0x0982 − 0x12 = 0x0970, the
real function. The offset had propagated into function *names*, not just
addresses.

### The structural fix

`tools/check_flat_asm_citations.py`, wired into preflight (34 gates now).
Quoting an address next to `rev20_flat.asm` is an error; `flat-asm-ok` on the
line annotates a known-wrong citation being retired. Verified to fail on an
injected citation.

These survived for months because the free-form `rev20_flat.asm @ 0xNNNN`
shape is not what `check_citation_targets.py` parses. **An un-gated citation
format is an unverified claim** — the gate did not miss them, it was never
looking. That is the reusable lesson, and it is why the fix is a gate rather
than ten edits.

Two other gates caught errors in the corrections themselves while this was
being written: `check_sfr_names.py` flagged explanatory text that put a
register name beside a different register's address, and `check_byte_quotes.py`
had earlier flagged stock bytes cited against the `MOVX` address when the run
starts at the `MOV DPTR` three bytes earlier.

The mboxfw image is byte-identical across this change (`ef1ab9ab…`, the 0x0023
image running on unit B): comments and tables only.

### Still open

`tools/diff_vs_rev20_safety_net.py` reads the same listing with its own copy of
the DPTR scanner and did not get the straight-line fix. Its stock-side data has
the phantom-write flaw described above.
