# What remains unknown for mboxfw — inventory, 2026-07-29

Written after the defect-fix pass, in answer to "is there anything else that
needs to be known that we don't know?" asked a second time. The first answer to
that question was a bidirectional XDATA diff, and it was incomplete: the fix
pass that followed it turned up two defects the diff could not have seen. This
document starts from *why* it was incomplete, because a blind spot in the method
is worth more than another list of facts.

## 1. The blind spot that hid TR0 — closed

`audit_sfr_writes.py` and `diff_vs_rev20.py` find writes by looking for
`MOV DPTR,#0xFFxx` followed by `MOVX @DPTR,A`. That is the TAS1020B's
memory-mapped UIFR register space and nothing else.

The 8051 core SFR space at 0x80-0xFF is **direct**-addressed: `TCON`, `TMOD`,
`IE`, `IP`, `PCON`, `TH0`/`TL0`, `P1`, `P3`, `PSW`, `SP`. Every bit-addressable
SFR bit -- `TR0`, `EA`, `EX0`, `IT0` -- is reached with `SETB`/`CLR`/`CPL`. None
of those instructions contains a `MOVX`. **None of them had ever been compared
against stock.** Six gates were green for the entire period in which Timer 0
never ran.

Closed by `tools/sfr_direct_diff.py`. Two design points that took three
attempts to get right, recorded because both failure modes are easy to
reintroduce:

  * **Byte assignment does not cover a bit.** The first version treated any
    byte-wide write as covering every bit of that SFR, so `TCON = 0x00` counted
    as satisfying stock's `SETB TR0`. It does the opposite. That version passed
    its own mutation test with `TR0` deleted -- a gate that cannot detect the
    bug it was written for.
  * **The two sides need opposite treatment of unprovable writes.** A computed
    write (`MOV TCON,A`, `POP IP`) proves nothing about which bits stock sets,
    so on the stock side it must contribute no requirement; on the mboxfw side
    it must count as capability, or the gate fails on ordinary codegen. Getting
    this symmetric produced 15 bogus FAILs on TCON and IP.

Current state: **no missing SETs**, mutation-verified (deleting either `TR0 = 1`
site is not enough -- both must go, since `power.c` sets it on resume; the gate
is reachability-blind and only proves "somewhere in the image").

Residual difference, benign and now visible: stock sets `SP = 0x33` (stack from
0x34, 204 bytes); SDCC's crt0 sets `SP = 0x6E` (stack from 0x6F, 145 bytes),
because mboxfw has far more IRAM globals than stock's 0x00-0x33. No collision.

## 2. Stock's work-code surface, and how much of it mboxfw lacks

Stock defers work from interrupt context to the main loop through `RAM[0x0A]`,
dispatched by `fcn.0x02EE` via a 14-entry `LJMP` table at `0x0300`
(index = code - 1). Full table, with every posting site found by scanning for
`MOV 0x0A,#imm`:

| code | handler | posted from | what it does | mboxfw |
|---|---|---|---|---|
| 0x01 | 0x032A | 0x0293 SET_CONFIGURATION | apply clock mode | inline |
| 0x02 | 0x0386 | 0x02C5 SET_INTERFACE | apply iface-1 alt | inline |
| 0x03 | 0x03FD | 0x02D9 SET_INTERFACE | apply other iface | inline |
| 0x04 | 0x0454 | 0x0D51 EP0-OUT done | clear 0x25.4, set 0x22.6, publish both, re-apply mode from `RAM[0x08]` | **no** |
| 0x05 | 0x0466 | 0x0D56 EP0-OUT done | set 0x25.4, clear 0x22.6, publish both, mode 1 | **no** |
| 0x06 | 0x0478 | 0x0D42 EP0-OUT done | mode 1 | **no** |
| 0x07 | 0x0480 | 0x0D35 EP0-OUT done | mode 2 + CS8427 reg 0x23 | **no** |
| 0x08 | 0x049A | 0x0D3C EP0-OUT done | mode 3 + CS8427 reg 0x23/0x40 | **no** |
| 0x09 | 0x04B4 | *none* | mode 4 | **no** |
| 0x0A | 0x04BC | *none* | mode 5 | **no** |
| 0x0B | 0x04C4 | 0x0AF6 main loop, P3.1 | S/PDIF present -> CS8427 init | **no** (#145) |
| 0x0C | 0x0511 | 0x0B07 main loop, P3.1 | S/PDIF absent -> mode 1 | **no** (#145) |
| 0x0D | 0x0518 | 0x005B class request | zero a 3-arg call, clear OEPDCNTX0 | **no** |
| 0x0E | 0x0526 | 0x0006 SUSR vector | suspend / resume | **yes**, new |

Codes 0x04-0x0A are all `MOV R7,#mode; LCALL 0x0728` -- they are stock's
**vendor control surface for selecting clock mode**, driven by Digidesign's
class-request protocol over EP0. mboxfw not having them is mostly *correct*:
class compliance means the host drives rate selection through standard UAC
requests, not a vendor protocol. Three things in that block are still worth
knowing:

  1. **`RAM[0x08]` is stock's persisted "current mode"** -- set to 3 in hw_init
     (0x093B), to 1 in the mode-1 branch (0x0753), and read back by code 0x04
     (`MOV R7,0x08` at 0x045E). **RESOLVED 2026-07-29**: the distinction that
     matters is internal-vs-external clock, not sample rate. Mode 1 sources both
     codec clocks from the EXTERNAL clock input; modes 2 and 3 source them from
     the two internal synthesizers at 44.1 and 48 kHz. Datasheet-confirmed from
     ACGCTL's MCLKO1S/MCLKO2S select fields -- see
     `decomp/FINDING_clock_modes_and_p31.md`. mboxfw is always internally
     clocked, which for a class-compliant device is a legitimate simplification
     rather than a missing feature.
  2. **Mode 4 is not implemented in stock either.** `fcn.0x0728`'s dispatch at
     0x073C tests for modes 2, 3, 5, 1 in that order and falls through to the
     common tail for anything else. Work code 0x09 passes mode 4 and no
     frequency programming happens. It also has no posting site. Dead.
  3. **Modes 1 and 5 are never invoked by mboxfw.** streaming.c uses only the
     mode-2 (44.1 kHz) and mode-3 (48 kHz) frequency words. Mode 1 is the
     slave-to-external-clock path (`ACGCTL = 0x0D` -> both MCLKO from `mclki`,
     no frequency word programmed at all, CS8427 CLOCKSOURCE RUN set); mode 5 is
     I2S "1 OUT and 1 IN at different frequencies", the branch that writes
     `CPTRXCNF4 = 0x01`. Mode 4 is dead in stock too.

**NEW UNKNOWN, promoted out of §2 -- what P3.1 actually is.** Tracing the two
handlers that select between the clock modes (work codes 0x0B and 0x0C) showed
that the meaning recorded in project notes, "P3.1 = S/PDIF clock presence, active
low", **cannot be right**: it maps present -> internal 48 kHz and absent ->
external clock, and the second of those would leave the codec with no clock at
all. P3 also has pull-ups and hw_init writes `P3 = 0xFF`, so idle reads 1 -- the
state that selects the external clock. A CS8427 lock/error reading is coherent
and is written up as a candidate, but it is not established, and one detail
(code 0x0B setting the "slaved" latch while selecting the internal clock) does
not fit it cleanly. **Task #145 is blocked on this**, deliberately: the modes are
understood well enough to implement, but a backwards trigger switches the codec
clock at exactly the wrong moments, which produces intermittent audio that
depends on what is plugged in -- the worst possible failure mode at 2 km per power
cycle. See `decomp/FINDING_clock_modes_and_p31.md`.

## 3. Genuinely unknown, in descending order of consequence

**a. Whether the boot ROM leaves the EP0 Y counts non-zero.** Now
unrecoverable by observation in the shipping build, because `usb_ep0_setup()`
clears them before any host can ask. Block 7 answers the surviving question
(does the UBM write back into Y during a session?). See
`FINDING_ep0_y_buffer_residue.md`.

**b. ~~What `DMABCNT0L/H` (0xFFEB/0xFFEC) are for.~~ RESOLVED 2026-07-29** --
and it was the right thing to rank first. See
`FINDING_rev22_playback_sof_watchdog.md`. Summary:

  * The premise here was wrong: "stock reads them" implied both images. **Only
    Rev 22 reads them** (0x0D58/0x0D5D); Rev 20 reads neither.
  * They are the playback circular buffer's FILL LEVEL, and the datasheet names
    USB audio synchronisation as their purpose.
  * Rev 22 uses them in a real SOF handler that Rev 20 lacks entirely (Rev 20's
    VECINT SOF entry is a bare RET): once per frame it checks whether the buffer
    holds a whole number of 6-byte sample frames, and if not, restarts the
    playback DMA and endpoint.
  * Corroborated structurally: Rev 22's EP0 pointer moved from 0x1B:0x1C to
    0x1D:0x1E precisely because the watchdog needed those two bytes for its
    saved count -- the one low-IRAM difference between the images that had no
    recorded reason.
  * This is a playback-only fix present in Rev 22 and absent from Rev 20, and
    Rev 20 is the firmware documented as needing a v22 flash before playback
    works. Strongest candidate yet for what that bug is.
  * Ported to `streaming_sof()`, which was an empty function whose comment
    reasoned from Rev 20's *Timer 0* stub -- a different interrupt entirely.

**c+d. ~~Bits 7 and 6 of the panel word.~~ RESOLVED 2026-07-29**, together,
because they turned out to be the same question: **the whole byte is
ACTIVE-LOW.** Full write-up in `disasm/MUX_IRAM22_ANNOTATION.md`, section "The
byte is ACTIVE-LOW". What settles it:

  * The three source patterns are **one-cold** -- 0x06/0x05/0x03 each have
    exactly one bit clear (b0/b1/b2 respectively). They are three per-source
    lines, not a binary code.
  * Under that convention all four immediates the byte ever receives read
    cleanly: 0x00 = all eight asserted (a lamp-test flash at boot), 0xF6 = b0+b3
    (mic on both channels), 0x76 = mic/mic plus b7, 0xFF = nothing asserted
    (suspend). The 0x00 and 0xFF values were the two that resisted an
    active-high reading.
  * **Bit 7 = streaming active.** Branch analysis of `fcn.0x0386`: 0x0389's
    `JNB 0x21.6` sends the no-alt-selected case to the STOP branch, so the
    fall-through at 0x0397 is stream START -- and it CLEARS b7, while the STOP
    branch at 0x03E6 SETs it. Cleared = asserted = streaming.
  * **Bit 6 = external (S/PDIF) clock in use.** Its derivation
    `22.6 = !(25.4) && !(25.5)` means asserted-low exactly when either is set,
    and work codes 0x04/0x05 confirm the sense: code 0x05 sets 0x25.4, clears
    22.6, and selects mode 1 (the S/PDIF-slave clock path); code 0x04 does the
    inverse and restores the mode persisted in `RAM[0x08]`.

This also resolved item **f** below, and produced a defect: **mboxfw never
touched bit 7.** `g_mux_state` starts at 0xF6 with b7 high and nothing lowered
it, so whatever the line drives sat in its not-streaming state permanently. If it
gates the analog output, that alone would make mboxfw silent on playback with
every other measurement correct. Now driven from
`panel_update_streaming()` in streaming.c under stock's condition.

What is left is a board question, not a firmware one: what bits 6 and 7 are
physically wired to. Every condition under which each is asserted is determined.

**e. The vendor's name and package pin for the codec-word lines.** Every bit's
*function* is determined (see `IRAM23_IRAM25_ANNOTATION.md`); the part number
needs the board.

**f. ~~Which bits drive panel LEDs versus the analog mux.~~ PARTLY RESOLVED
2026-07-29** by the one-cold finding in c+d above. The six source bits map
one-to-one onto six per-source lines with no bit left over:

    b0 ch1 mic   b1 ch1 line   b2 ch1 inst
    b3 ch2 mic   b4 ch2 line   b5 ch2 inst

Whether each line drives an LED, a mux enable, or both in parallel needs the
board. Which bit belongs to which source is no longer in question.

## 4. Determined but never verified on silicon

Not unknowns, but the honest list of what rests on static analysis alone. Each
has a task:

  * The button behaviour after the mapping/order/edge fix (#150), including the
    first-ever confirmation that Timer 0 now ticks.
  * Suspend/resume, including the deliberate divergence that keeps SUSR
    unmasked so a second suspend works where stock's cannot (#149).
  * The EP0 Y-count read (#148).
  * The 8-frame capture artifact (#147) -- still undiagnosed, and the one
    measurement that voided the last loopback was a source-routing mistake, not
    a firmware fault.
  * S/PDIF clock slaving (#145) -- work codes 0x0B and 0x0C above.

## 4a. The read-direction blind spot — probed 2026-07-29, and it is empty

Asked a third time. §1 closed the direct-SFR blind spot; the obvious next
question is what class of difference the gates *still* cannot see, and there was
a clean answer: **every gate in `preflight.sh` is a write-scanner.**
`audit_sfr_writes`, `diff_vs_rev20` and `sfr_direct_diff` all answer "does the
firmware write this register / set this bit". None of them answers "does the
firmware READ a register that stock reads to make a decision". That is precisely
the class `DMABCNT0L/H` (§3b) lived in, and it was found by hand -- no gate
would ever have flagged it.

So the class was probed directly: for every address either stock image reads,
does mboxfw read it too? **The answer is that the class is empty.** After the
classifier fixes below there is no register stock reads that mboxfw fails to
read. The one asymmetry in the other direction is benign: mboxfw reads
`SETPACK_WIDX_H` (0xFF2D) and neither stock image ever does, because mboxfw
records the full wIndex in telemetry block 2 and stock has nothing to record it
into. Nothing functional depends on it.

Two false alarms on the way to that answer, both worth recording because both
were tool bugs rather than firmware bugs:

  * `DMABCNT0L` and `SETPACK_WLEN_L` first appeared as "stock reads, mboxfw
    never touches". Both were wrong: SDCC reaches a neighbouring register with
    `dec dpl` / `inc dptr` rather than a fresh `MOV DPTR,#imm`, keeping DPH. A
    scanner that only recognises full DPTR loads misses the second register of
    any adjacent pair. `streaming_sof()` does read both halves of DMABCNT0 --
    `dec dpl` at 0x0CBD.
  * `OEPCNF0` appeared as "stock reads it, mboxfw only writes it". It is not a
    read at all; see below.

### The classifier was wrong 26 times

Chasing that last false alarm found a real defect in
`tools/xdata_access_map.py`, which generates `disasm/XDATA_ACCESS_MAP.md` and
whose docstring claimed it "is complete, so it cannot flatter". Completeness of
*sites* is guaranteed by construction; correctness of *direction* is not, and 26
entries were wrong:

  * **23 pure reads labelled as writes.** The idiom `LCALL helper` after a read
    was taken to mean the helper performs the caller's store. Often it does
    (Rev 20 0x0FF4 is a bare `MOVX @DPTR,A`), but Rev 20 0x0B5F opens with
    `MOV DPTR,#0xff6b` -- it is an EP0 ack tail and says nothing about the
    caller's register. Fixed by requiring a helper to reach `MOVX @DPTR,A`
    *without reloading DPTR* before it counts. All 23 corrections land in the
    SETUP-packet buffer 0xFF28-0xFF2C plus `I2C_RX`, i.e. exactly the registers
    the UBM writes and the firmware only reads. The corrected map is the first
    one that is internally coherent about the SETUP buffer.
  * **3 writes labelled as reads**, including Rev 22 0x0F9D = `OEPCNF0 |= 0x20`
    (stall EP0-OUT). The walk broke on `LCALL`/`LJMP` to catch store-via-helper
    but ran straight past `SJMP`, which Keil also uses to reach a store tail --
    and those hops chain (0x0F9D SJMPs to 0x0FB6, which LCALLs 0x0B2C).
  * **2 wrong helper attributions** and **8 dropped RMW masks**: sites reported
    as an opaque `write-via-helper` are now `set-bits 0x20` / `0x08` / `0x01`,
    which distinguishes two different `OEPCNF0` operations the old map rendered
    identically.

Following `SJMP` naively introduced a *new* false positive first -- Rev 22
0x0177 reads `SETPACK_WVAL_H`, `CJNE`s on the descriptor type and jumps to the
matching arm, which is control flow, not a store tail. Guarded by refusing to
follow an `SJMP` that sits behind a conditional branch. Regression-checked
against the three genuine helper cases the docstring itself cites.

Guarded by `xdata_access_map.py --selftest`, now a preflight gate: 8 sites read
by hand, one per failure mode. Mutation-testing it produced a result worth
keeping -- the first two mutations did not trip it, because the SJMP guard and
the helper-validation each masked the other. That is why the self-test has a
site (Rev 20 0x0173, a plain `LCALL` with no `SJMP` in between) chosen
specifically to pin the helper check on its own. It also showed the
conditional-branch guard is **redundant**: deleting it changes no entry in the
generated map, and it is labelled as such in the source rather than left looking
load-bearing.

None of this reaches the gates -- no gate consumes the map, and all 15 still
pass. The blast radius is documentation, plus one open investigation: #147's
Addendum 4 diffs the 0xFFC0-0xFFFF range against this map, and its conclusion
survives, because the only correction in that range is `I2C_RX` (a read in both
stock and mboxfw).

## 5. What this document does not cover

Reachability. Every check described here is whole-image: "does the firmware
anywhere set this bit / write this register". None of them prove a write is
reached on the path that needs it. The `TR0` mutation test makes the limit
concrete -- with `power.c` present, deleting the `main()` site alone leaves the
gate green, because the resume path still sets it. Closing that needs a
call-graph-aware check, which does not exist yet.
