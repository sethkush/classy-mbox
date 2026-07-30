# Direct-addressed SFRs (0x80–0xFF) — the ledger's fifth category

This category was **missing from the ledger entirely** until 2026-07-29. The
four original numbers covered IRAM below 0x80, XDATA reached via `MOV DPTR`,
call targets and bit addresses. Direct accesses to SFR space were in none of
them, so "100% on all four" silently excluded 16 SFRs and ~60 sites per image.
The categories were my choice, so that was a gap in the measurement, not a
technicality about it — and the missing mboxfw suspend path was hiding in it.

16 SFRs in each image, 60 sites in Rev 20, 69 in Rev 22.

## Timing and interrupts

    0x89 TMOD   = 0x11              both timers 16-bit mode 1
    0x8C TH0    = 0xCE              panel tick reload; reloaded in the ISR at
                                    Rev 20 0x1022 / Rev 22 0x101A
    0x8A TL0                        tick timer low
    0x8B TL1 / 0x8D TH1             timer 1, loaded but unused
    0x88 TCON   = 0x00              clears IT0, so INT0 is level-triggered
    0xB8 IP     = 0x00              no interrupt priorities

Interrupt enables are set bit-by-bit rather than as a byte, Rev 20
0x08ED–0x08F7: `CLR IE.7` (EA off during setup), `CLR IE.4` (serial),
`CLR IE.2` (INT1), **`SETB IE.1` (ET0)**, `CLR IE.3` (timer 1),
**`SETB IE.0` (EX0/USB)**. So exactly timer 0 and INT0 are enabled — mboxfw's
`IE = 0x03` is the same result written as a byte.

## 0x87 PCON — the divergence this category exposed

    Rev 20 0x0543  ORL PCON,#0x01

Sets IDL, putting the MCU into idle mode. It is the last instruction of the USB
suspend handler at 0x0526 (work code 0x0E — see `IRAM23_IRAM25_ANNOTATION.md`),
after the codec word is zeroed, the master clocks are disabled and the panel is
blanked.

**mboxfw never writes PCON**, and has no suspend path at all: no codec-off, no
clock-off, no idle. It also sets `USBIMSK = 0xF5`, which enables the SUSR and
RESR interrupts that Rev 20's `0x9F` masks off — so mboxfw enables
suspend/resume interrupts and then does nothing with them.

## Ports

    0x90 P1   20 sites, all writes. Two bit-banged shift chains:
              P1.0 data / P1.2 clock / P1.1 latch  -> 16-bit codec word
              P1.7 data / P1.5 clock / P1.6 latch  -> 8-bit panel word
    0xB0 P3   2 sites: `MOV P3,#0xFF` at Rev 20 0x08DC enables the
              quasi-bidirectional pull-ups; `MOV R5,P3` at 0x0ED7 is the
              button read.

## Compiler and ISR housekeeping

    0x81 SP    = 0x33 (Rev 20) / 0x32 (Rev 22). Stack starts one above.
                Rev 20 additionally uses IRAM 0x33 as an R7 spill and Rev 22
                does not — exactly consistent with SP sitting one lower there.
    0xD0 PSW   `PUSH PSW` then `MOV PSW,#0x10` at Rev 20 0x0DB4/0x0DB6:
                the ISR prologue switching to register bank 2.
    0xE0 ACC   direct-addressed for bit tests (ACC.0 in the shift loops) and
                for ISR save/restore.
    0xF0 B     ISR save/restore, and `MOV B,#3 / MUL AB` for jump-table strides.
    0x82 DPL / 0x83 DPH  loaded from the IRAM pointer pairs (0x19:0x1A table
                pointer, 0x1B:0x1C EP0 pointer) and saved across the ISR.
