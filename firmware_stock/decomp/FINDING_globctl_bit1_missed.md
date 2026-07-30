# Stock sets GLOBCTL bit 1 at boot; mboxfw did not, and the write had been ruled a scanner artifact

Found 2026-07-29 while building `tools/verify_init_order.py` to close the
ordering gap named in `WHAT_REMAINS_UNKNOWN.md` §5. Like the reachability gate
before it, the tool found something other than what it was built for.

## The write

Both stock images, in their boot-init function, before the codec-port block:

    Rev 20  hw_master_init @ 0x08CB
      0x08D4  MOV DPTR,#0xffb0     ; MEMCFG
      0x08D8  MOVX @DPTR,A         ; MEMCFG = 1  (SDW)
      ... 27 instructions of direct-addressed SFR writes, no DPTR touched ...
      0x08FB  INC DPTR             ; -> 0xFFB1 GLOBCTL
      0x08FC  MOV A,#0x06
      0x08FE  MOVX @DPTR,A         ; GLOBCTL = 0x06
      ... codec-port block ...
      0x0934  GLOBCTL |= 0x01      ; CPTEN, last

    Rev 22  hw_clock_codec_init @ 0x07EC -- identical shape, 0x07F5 / 0x081C /
            0x081D / 0x081F, codec block, 0x0855.

Byte-scanned both images: `a3 74 06 f0` occurs **exactly once each**, and
`90 ff b1 74 06 f0` occurs **nowhere**. DPTR is never loaded with 0xFFB1 here.

## Why every tool missed it, and why that mattered

`audit_sfr_writes`, `diff_vs_rev20` and `xdata_access_map` all find XDATA writes
by locating `MOV DPTR,#imm16` and looking a bounded number of instructions ahead
for the store. None can connect a load to a store 27 instructions later, and none
tracked `INC DPTR` at all.

That is not just a missed row. `tools/rev20_diff_justifications.md` carried:

> `assign 0x06` is a scanner artifact — no `mov a, #0x06; movx @dptr, a` to
> 0xffb1 exists in rev20_flat.asm; the 0x06 is a nearby `mov 0x06, a`
> direct-memory write.

The search was correct. The conclusion was wrong, and the reasoning is the
recognisable shape of arguing from absence in a tool's output. It also cited
`rev20_flat.asm`, which this project already knows is the bad disassembly.

Meanwhile `disasm/rev20_STARTUP_TRACE.md` step 14 recorded `0x08FE GLOBCTL =
0x06` correctly, because it was traced by hand against the good listing. **Two
documents in the same repository contradicted each other for as long as both have
existed**, and nothing noticed, because the justification file is what a gate
reads and the trace is prose.

## Consequence for mboxfw

mboxfw only ever did `GLOBCTL |= 0x01`. The boot ROM leaves GLOBCTL = 0x04
(TI RomBoot.c:33, "12Mclk, Ext int off, LPWR on, CODEC is off"), so:

    stock   0x04 -> 0x06 -> 0x07
    mboxfw  0x04 --------> 0x05

Bit 1 was clear for mboxfw's entire run where stock has it set from boot.

Fixed as `GLOBCTL |= 0x02` in `hw_init.c`, placed where stock places it (after
the ports/timers, before the codec block). RMW rather than stock's outright
`= 0x06`, per task #48, which reaches the same value from the ROM's 0x04 without
blindly clearing bits the ROM may own.

**GLOBCTL bit 1's function is still UNKNOWN.** TI's ROM documents only bit 2
(LPWR) and bit 7 (CPU speed), and `rev20_STARTUP_TRACE.md`'s open-items list has
carried "GLOBCTL bit 1 — UNKNOWN" from the start. The argument for setting it is
not that we know what it does; it is that both stock images set it at boot and
mboxfw exists to mirror stock's boot state. That distinction is written into the
code comment so it cannot be mistaken for understanding later.

## Four tools fixed

  * `xdata_access_map.py` — new pass for accesses reached by DPTR arithmetic
    (`INC DPTR` / `DEC DPL` / `MOV DPL,#imm`), tracked STRAIGHT-LINE only: the
    run ends at any branch, call, jump or return, so a tracked DPTR is never
    carried across an edge where its value is unknown. Found 5 more sites per
    image, all coherent: GLOBCTL 0x06, `USBIMSK = 0x9F` in both the resume and
    bus-reset handlers, a computed `I2C_TX`, and the `wLength` high byte next to
    the low byte the map already had.
  * `check_citation_targets.py` — consults the access map when no immediate DPTR
    load sits near the cited address. Without this it rejected the (correct)
    citation of 0x08FE, which is precisely how a real write gets recorded as an
    artifact. Mutation-tested: a wrong address still fails.
  * `rev20_diff_justifications.md` — both 0xffb1 rows retracted and replaced.
  * `verify_init_order.py` — the new gate, below.

## The ordering gate

`tools/verify_init_order.py` compares the ORDER of mboxfw's `hw_init` writes
against both stock boot-init functions. Correspondence established structurally,
not from our function names: both are the second-action writer of USBCTL 0xFFFC,
and the surrounding functions pair up (suspend 0x0526/0x0525, main
0x0A95/0x0A3F, rstr 0x0F43/0x0F64).

Result: 14 common registers, 8 inversions, all understood and listed in
`ORDER_EXEMPT` with reasons — mboxfw does timers → IE → ports → MEMCFG where
stock does MEMCFG → ports → timers → IE. Each is benign for a stated reason
(SDW already set by the ROM so the write is idempotent; EA stays clear for all of
hw_init so early IE bits cannot deliver an interrupt; no timer runs during
hw_init so reload-before-mode is equivalent). The codec block and CPTEN agree
with stock exactly.

Two mistakes made building it, both worth keeping:

  * The mboxfw parser reported five writes to CPTCNF1 because SDCC walks the
    adjacent, descending codec registers with `dec dpl`. Same trap as the stock
    side, one day apart.
  * **First-touch keying silently failed to check the property the docstring
    claims.** GLOBCTL is written twice for unrelated reasons, so its first touch
    is the bit-1 write, and a mutation that moved CPTEN to before the codec
    block passed. Now asserted explicitly in `check_cpten_last()`, which
    mutation-testing confirms names the real violation.

## New open question, deliberately not acted on

Both stock images write `USBIMSK = 0x9F` in their **bus-reset** handler
(Rev 20 0x0F6E, Rev 22 0x0F8F) — sites the map only now shows. mboxfw's
`VEC_RSTR` case deliberately does not touch USBIMSK, on the stated reasoning
that a bus reset leaves it alone and re-ORing 0xE5 would be pointless work.

Whether that reasoning holds depends on whether a bus reset clears USBIMSK,
which the listings cannot settle. Not changed, for two reasons: USBIMSK bits 1
and 3 within that 0x9F are themselves recorded unknowns, and masking SOF off in
USBIMSK has already cost this project one real bug. Needs the datasheet's
bus-reset behaviour, and then either an implementation or a justification row.
