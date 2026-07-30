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

## HARDWARE RESULT 2026-07-29: implementing it makes the device SILENT ON USB

Everything above is correct about the stock images and about the scanners. It was
wrong about what to do next. The write was implemented as `GLOBCTL |= 0x02` in
build 0x000F, and on hardware the device **never enumerates** -- no VID/PID
appears at all, the app never attaches.

Bisected between two images differing in ONLY this line, same flasher, same
procedure, same host, back to back:

    build 0x0010  (GLOBCTL |= 0x02 present)  ->  silent, never attaches
    build 0x0011  (line removed)             ->  attaches in 7 s

**Why stock can and mboxfw cannot.** Stock runs its hardware init BEFORE bringing
USB up. mboxfw deliberately calls `usb_init()` first (task #47), so the write
lands AFTER the USB engine is configured rather than before it. Whatever bit 1
does, doing it to a live USB engine stops enumeration.

**The arithmetic was never the problem.** Telemetry block 8 byte 2 -- added in the
same session for a different purpose -- reads `GLOBCTL = 0x04` at boot-ROM
handoff on this actual part, confirming TI RomBoot.c's documented value. So
`|= 0x02` did reach stock's 0x06 exactly as intended. The value was right and the
timing was fatal.

**The lesson worth keeping.** The argument for shipping this was explicitly "we
do not know what bit 1 does, but both stock images set it at boot and mboxfw
mirrors stock's boot state." That argument is a reason to investigate and is
never on its own a reason to ship. Mirroring a write without mirroring its
ORDERING mirrors nothing. The new ordering gate compared first-touch positions
and passed this build, because both sides wrote GLOBCTL before the codec block --
the gate cannot see that mboxfw's "before the codec block" is also "after
usb_init", which stock's is not.

Reinstating it requires moving the write before `usb_init()` AND a hardware test.
The removal is recorded in `hw_init.c`, `tools/sfr_writes.allowed`,
`tools/rev20_diff_justifications.md`, and as eight ORDER_EXEMPT entries in
`verify_init_order.py`.

## The USBIMSK question it raised — RESOLVED from the datasheet, no change needed

Both stock images write `USBIMSK = 0x9F` in their **bus-reset** handler
(Rev 20 0x0F6E, Rev 22 0x0F8F) — sites the map only shows since it learned DPTR
arithmetic. mboxfw's `VEC_RSTR` deliberately does not touch USBIMSK, on the
stated reasoning that a bus reset leaves it alone. Read the datasheet (task
#152) rather than guess, and **mboxfw is right**.

Datasheet §2.1.9 Reset, on the FRSTE-clear branch:

> if the MCU has cleared FRSTE, incoming USB resets is treated as interrupts to
> the MCU (via INT0) if the corresponding function reset bit RSTR in the USB
> interrupt mask register USBMSK has been set by the MCU. If neither FRSTE or
> RSTR has been set by the MCU, **USB resets have no effect on the TAS1020B,
> other than resetting the USB serial interface engine (SIE) and the USB buffer
> manager (UBM)**.

USBIMSK is neither SIE nor UBM state, so it survives a bus reset. Stock's write
is redundant re-assertion. The precondition holds for mboxfw: it never sets
FRSTE — every `USBCTL` write is an RMW of CONN/FEN only — and FRSTE's default is
0 and is itself "not affected by USB reset".

The same paragraph confirms the writes mboxfw *does* perform there are the
required ones: `FEN` is documented per-bit as "cleared by a USB reset", and the
UBM owns the endpoint configuration, hence re-arming `OEPCNF0`/`IEPCNF0` and
`USBFADR`. `CONT` is "not affected by USB reset", so OR-ing it back is harmless.

## Two more open items closed by the same read

`rev20_STARTUP_TRACE.md` carried "**USBIMSK bits 1 and 3** in the 0x9F written
at 0x09F1 — **UNKNOWN**". The register table (§6.5.1.3) answers both:

    bit 7 RSTR   6 SUSR   5 RESR   4 SOF   3 PSOF   2 SETUP   1 — (R)   0 STPOW

  * **bit 1 is Reserved, type R — read-only.** Stock setting it in 0x9F is inert,
    which is why the value looked odd.
  * **bit 3 is PSOF**, pseudo start-of-frame.

Stock's `0x9F` therefore enables RSTR + SOF + PSOF + SETUP + STPOW and masks
SUSR/RESR off. mboxfw's `0xF5` enables RSTR + SUSR + RESR + SOF + SETUP + STPOW:
it takes suspend/resume (deliberate — it has a suspend path, and stock's cannot
suspend twice) and leaves **PSOF masked**.

PSOF looked like it might matter, because the datasheet ties it directly to audio:

> a counter ... included in the TAS1020B to generate pseudo start-of-frame
> interrupt in case the SOF packet on the USB bus is corrupted. This is done to
> maintain synchronization to the USB bus and maintain the fidelity any on going
> streaming audio application.

It does not, and the reason is worth recording rather than acting on the
paragraph above: **the PSOF vector is a bare `RET` in both images** — VECINT 0x13
points at Rev 20 0x1033 and Rev 22 0x102B, both single-byte `RET` stubs. So even
Rev 22's SOF watchdog does not run on a PSOF frame; stock enables an interrupt
and then discards it. Leaving PSOF masked in mboxfw is behaviourally equivalent
to stock's handling of it, and costs one fewer ISR entry per corrupted frame.

If a future change makes `streaming_sof()` work that must happen on every frame
including corrupted ones, PSOF becomes worth enabling — and it would then need a
`VEC_PSOF` case, since the datasheet requires the interrupt and the status bit be
cleared by writing VECINT.
