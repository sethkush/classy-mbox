# P3.1 is TXD. It is not an S/PDIF input, and stock's automatic clock switch never fires

Settles task #145's blocking question, measured on hardware 2026-08-04 with a
real external S/PDIF carrier and a control unit.

The short version: **P3.1 is the 8051 serial port's TXD pin.** It does not track
S/PDIF carrier presence, it reads high permanently, and because stock's edge
machine requires P3.1 to *fall* before anything can happen, work codes 0x0B and
0x0C are unreachable on this board. Stock has no automatic clock switching. It
never did.

## 1. The measurement

Both units run build 0x001F at `MBOX_PID=0x2000`, addressed by serial. The two
S/PDIF ports were crossed on 2026-08-04 (`BENCH_WIRING.md`) specifically so each
receiver watches an independent transmitter — the old A-to-A self-loop was
circular for exactly this question.

Both transmitters are live, which matters because a dead transmitter would make
this whole test a false negative. `cs8427_boot_init()` writes `DATAFLOW = 0x0C`
= `TXOFF=0`, `TXD=01` (`CS8427_TXDSERIAL`, transmitter fed from the serial audio
input port) per `reference/cs8427/alsa_cs8427.h`, and both codecs are clocked
(`codec word = 0x1CCF`, audio measured at -28 dBFS the same day).

The coax was pulled from **A's S/PDIF IN** and re-seated. B was left cabled
throughout as a control.

| | A (input pulled) | B (control) |
|---|---|---|
| seated | `P3 = 0xC2` | `0xC2` |
| **A's S/PDIF IN unplugged** | `P3 = 0xC2` | `0xC2` |
| re-seated | `P3 = 0xC2` | `0xC2` |

Three samples per state, all identical. **P3.1 did not move.**

## 2. Why the level is not merely "idle high"

This is the part that makes the null result informative rather than
inconclusive, and it turns on P3PUDIS.

`hw_init` sets `GLOBCTL` bit 1 = P3PUDIS, disabling the on-chip P3 pull-ups —
stock at Rev 20 `0x08FE` / Rev 22 `0x081F`, mboxfw at `hw_init.c`. With the
pull-ups off, the board's own network decides each pin, and the board pulls the
button pins **low**: that is the established active-high button model, and P3.3
/ P3.4 / P3.5 duly read 0 in the same samples above.

So a pin reading 1 with the pull-ups disabled is not passively floating high.
Look at the whole byte rather than bit 1 alone:

    P3 = 0xC2 = 1100 0010
                 ││    └── P3.1  HIGH
                 │└─────── P3.6  HIGH
                 └──────── P3.7  HIGH

Three bits high, and they are precisely the three pins that are not GPIO.

## 3. What P3.1 actually is — from TI's own source for this part

    reference/tas1020a/ti_uac_reference/ROM/Utils.SRC:67
        TXD   BIT   0B0H.1        ; P3 is at 0xB0, so P3.1 = TXD
        RXD   BIT   0B0H.0        ;                  P3.0 = RXD

This is TI's reference code for the TAS1020B, not a generic 8051 assumption.
P3.1 is TXD; P3.6 / P3.7 are the external-bus strobes, which are exercised
constantly because every SFR at `0xFFxx` is reached by MOVX.

**The serial port is never configured.** Byte-scanned both Ghidra listings for
any access to `SCON` (0x98) or `SBUF` (0x99), case-insensitively, with a control
pattern proving the scan matches real instructions (`0xB0` returns 2 hits, the
known `MOV 0xb0,#0xff` in hw_init):

    rev20_ghidra.txt   SCON/SBUF accesses: 0
    rev22_ghidra.txt   SCON/SBUF accesses: 0
    mboxfw             SCON/SBUF references: 0

`SCON` therefore rests at its reset value 0x00 — serial mode 0, `REN` clear —
and `SBUF` is never written, so nothing is ever transmitted.

**Confidence split.** That P3.1 is TXD is *determined* (TI's source). That it
reads high *because* the unused TXD alternate function holds it there is a
**reading** — the leading one, and consistent with P3.6/P3.7 behaving the same
way, but it is not directly probed and this document does not need it. The
measurement in §1 stands on its own.

## 4. Consequence: work codes 0x0B and 0x0C are dead in stock

`FINDING_clock_modes_and_p31.md` already established the guards from the
listings (Rev 20 `0x0AEC`, Rev 22 `0x0A96`, identical):

    code 0x0B  fires only when  P3.1 == 0 AND 0x27 == 0   (then 0x27 <- 1)
    code 0x0C  fires only when  P3.1 == 1 AND 0x27 == 1   (then 0x27 <- 0)

`main` initialises `0x27 = 0` as its first action, and **only code 0x0B sets the
latch**. So the machine is strictly ordered: P3.1 must fall first, then rise.

That document reached "at boot, with P3.1 idling high, neither block fires" from
the bytes, but left open whether P3.1 would later fall on some S/PDIF event. It
does not. P3.1 is an output pin belonging to an unconfigured UART, held high,
and the pull test confirms nothing about the S/PDIF input moves it.

**Therefore neither handler is reachable.** Stock stays on whatever
`hw_init` seeded — mode 3, internal 48 kHz, `RAM[0x08] = 3` — until the *host*
asks for something else.

This also explains the detail that never fit the CS8427-lock reading: code 0x0B
sets the "slaved" latch `0x25.5` while selecting the *internal* clock. Dead code
does not have to be coherent.

## 5. What this means for #145

**mboxfw is not missing a behaviour the original had.** The automatic half of
S/PDIF clock slaving does not exist on this hardware in either stock image, so
porting it would be reproducing a mechanism that has never once executed on this
board. Nothing to mirror.

The **host-driven** path is untouched by this and remains fully specified in
`FINDING_host_control_protocol.md` — all four vendor requests, clock mode 1, and
the all-zeroes sample rate that means "S/PDIF-synced" and that the kernel quirk
already expects. That is the only route to slaving, and it is the one a
class-compliant device should have anyway.

Implementation status: `#159` added Selector Unit and clock-source control, and
`dd56159` ("Remove software source control; keep setmux", 5994 -> 5228 bytes)
removed it again for code size. `usb.c` today handles only the endpoint
sampling-frequency control. So the work is genuinely absent, not already done.

**Unresolved before any implementation, and it is a board question:** clock mode
1 sets `ACGCTL = 0x0D`, sourcing both codec master clocks from **MCLKI**. That
is only useful if MCLKI is wired to the CS8427's RMCK. `cs8427_boot_init` writes
`CONTROL1 = 0x01` with `SWCLK = 0` so RMCK carries the *recovered* clock, and
stock's mode 1 and that register write only make sense together on that
assumption — but the trace is circumstantial, not a board reading. If the
assumption is wrong, selecting mode 1 leaves the codec with no clock at all: a
silent device recoverable only by reflashing, at 2 km per power cycle.

## 6. Method note

This is the third P3.1 story built on an unchecked physical assumption. The
first was "S/PDIF presence, active low" (backwards in both directions). The
second was a normally-closed switched jack, retracted the same day it was
written because the S/PDIF connector is RCA and has no switch contact. Both were
internally consistent.

What broke the pattern was not more reasoning about the handlers — it was
reading the pin map out of the vendor's own source and then pulling a cable.

Two near-misses inside this investigation, both caught by controls rather than
by care:

  * I suspected #165's CDOUT probe had run on a chip still held in reset, which
    would have invalidated its null result and made P3.1-is-CDOUT live again.
    The git order says otherwise: `5a23353` (the probe) landed *after*
    `fe49e48` (RESET release + SPI select), both on 2026-07-31. #165 stands.
  * My first `SCON` scan returned 0 hits from a lowercase-only regex against an
    uppercase listing. The null was an artifact of the pattern. It only surfaced
    because the pattern was re-run against a write known to exist. **A scan with
    no positive control is not evidence of absence** — the same failure mode
    that filed a real GLOBCTL write as a scanner artifact, and that missed
    CPTEN over two spaces in a grep.
