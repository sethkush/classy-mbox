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
     (`MOV R7,0x08` at 0x045E). mboxfw has no equivalent; it tracks a sample
     rate, not a mode. Whether anything needs the distinction is open.
  2. **Mode 4 is not implemented in stock either.** `fcn.0x0728`'s dispatch at
     0x073C tests for modes 2, 3, 5, 1 in that order and falls through to the
     common tail for anything else. Work code 0x09 passes mode 4 and no
     frequency programming happens. It also has no posting site. Dead.
  3. **Modes 1 and 5 are never invoked by mboxfw.** streaming.c uses only the
     mode-2 (44.1 kHz) and mode-3 (48 kHz) frequency words. Mode 1 is the
     S/PDIF-slave path (`ACGCTL = 0x0D`, CS8427 CLOCKSOURCE via 0x31/0x32);
     mode 5 is I2S "1 OUT and 1 IN at different frequencies", the branch that
     writes `CPTRXCNF4 = 0x01`.

## 3. Genuinely unknown, in descending order of consequence

**a. Whether the boot ROM leaves the EP0 Y counts non-zero.** Now
unrecoverable by observation in the shipping build, because `usb_ep0_setup()`
clears them before any host can ask. Block 7 answers the surviving question
(does the UBM write back into Y during a session?). See
`FINDING_ep0_y_buffer_residue.md`.

**b. What `DMABCNT0L/H` (0xFFEB/0xFFEC) are for.** Read-only playback DMA byte
counters, updated every SOF. Stock reads them; mboxfw never does. Still
untraced: *what stock does with the value*. A per-frame byte count is what an
adaptive-rate scheme needs, and mboxfw declares adaptive endpoints, so this is
the most likely remaining functional gap in the audio path.

**c. Bit 7 of the panel word (`RAM[0x22].7`).** Rests set, cleared at Rev 20
0x03A0 / Rev 22 0x03A4 in the SET_INTERFACE handler, set again at 0x03E6 /
0x03EA -- each with an immediate publish. Tracks something an alt-setting change
turns off and back on. Two sites, no name.

**d. Bit 6 of the panel word**, beyond its derivation
`22.6 = !25.4 && !25.5`. Codes 0x04 and 0x05 pair it with 0x25.4 in opposite
senses, which is consistent with an internal-vs-external clock indicator, but
that is a reading, not a determination.

**e. The vendor's name and package pin for the codec-word lines.** Every bit's
*function* is determined (see `IRAM23_IRAM25_ANNOTATION.md`); the part number
needs the board.

**f. Which bits drive panel LEDs versus the analog mux.** All 8 bits of
`RAM[0x22]` are accounted for as source fields plus two control bits, so the
LEDs are presumably decoded from the same fields. Unconfirmed.

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

## 5. What this document does not cover

Reachability. Every check described here is whole-image: "does the firmware
anywhere set this bit / write this register". None of them prove a write is
reached on the path that needs it. The `TR0` mutation test makes the limit
concrete -- with `power.c` present, deleting the `main()` site alone leaves the
gate green, because the resume path still sets it. Closing that needs a
call-graph-aware check, which does not exist yet.
