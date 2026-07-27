# Digidesign Mbox 1 — Firmware Rev 20 — Master Annotated Disassembly

## 1. What this file is

This is the consolidated, instruction-level annotated disassembly of the **stock Digidesign
Mbox 1 firmware revision 20** for the TI **TAS1020B** (8051-core USB audio streaming
controller).

| | |
|---|---|
| Source image | `/Users/seth/projects/mbox/firmware_stock/rev20_firmware_code.bin` |
| Image size | 8174 bytes (0x1FEE) |
| Load address | 0x0000 (CPU code address == file offset throughout; the image is executed from the TAS1020B shadow program RAM, `MEMCFG.SDW=1` set at 0x08D8) |
| Device identity | idVendor 0x0DBA (Digidesign), idProduct 0x1000, bcdDevice 0x0020 = "Rev 20" (device descriptor at 0x0596) |
| Reset vector | 0x0000 → `LJMP 0x0A09` (Keil C51 startup) |

### How it was produced

Twelve non-overlapping address ranges were annotated independently, each by one annotator
working from: the raw image (`xxd`), a Ghidra listing (`rev20_ghidra.txt`), the TI TAS1020B
datasheet (SLES025B), and the TI USB-audio reference sources in
`reference/tas1020a/ti_uac_reference/` (`ROM/Reg_stc1.h`, `ROM/hwMacro.h`, `ROM/UsbEng.c`,
`ROM/i2c.h`, `ROM/I2c.c`, `ROM/Mmap.h`, `ROM/Usbaudio.h`, `Application/SoftPll.c`).
This document merges those twelve outputs: their listings are reproduced **verbatim and in
full** in §5 (no instruction line was dropped or summarized), and their headline claims were
cross-checked against each other. Where two annotators disagreed, §3.1 records the conflict
and the resolution.

Only two facts in this document were derived during assembly rather than taken from a range
annotation (both are flagged in place): the 0xFF-fill extent past 0x1400 (§2.1) and the
parse of the C51 initializer table at 0x0F9C (§3.1, item R1, and §6.5).

### Coverage stats

| Region | Bytes | Status |
|---|---|---|
| 0x0000-0x13FF | 5120 | Annotated instruction-by-instruction by the 12 ranges |
| 0x0000-0x103E | 4159 | All real code + data (last non-0xFF byte of the image is at 0x103E) |
| 0x103F-0x1FED | 4015 | 0xFF erase fill — verified: **every** byte from 0x103F to end-of-file is 0xFF |
| 0x1400-0x1FED | 3054 | Outside all annotated ranges; verified 0xFF fill only |

Range boundaries as assigned: 0x0000-0x0117, 0x0118-0x0329, 0x032A-0x0517, 0x0518-0x0727,
0x0728-0x080A, 0x080B-0x0A08, 0x0A09-0x0B1D, 0x0B1E-0x0C44, 0x0C45-0x0DEB, 0x0DEC-0x0F6F,
0x0F70-0x1027, 0x1028-0x13FF. The ranges are contiguous and non-overlapping.

---

## 2. Memory map

### 2.1 Code space layout

| Range | Subsystem |
|---|---|
| 0x0000-0x0025 | 8051 interrupt vectors, interleaved with single-`RET` VECINT no-op handler stubs (OEP1-7, IEP1-6) |
| 0x0026-0x0117 | EP0 SETUP dispatcher (VECINT 0x12) and the four USB-audio class-request fast paths |
| 0x0118-0x011E | Standard-request dispatch entry (`fcn_0f70` casejump) |
| 0x011F-0x0143 | **DATA**: bRequest dispatch table (11 entries + terminator + default) |
| 0x0144-0x02ED | USB standard-request handlers (GET/SET STATUS, DESCRIPTOR, CONFIGURATION, INTERFACE, ADDRESS; stalls for the unsupported ones) |
| 0x02EE-0x02FF | Deferred-event dispatcher (`device_event_dispatch`) |
| 0x0300-0x0329 | 14-entry `LJMP` trampoline table (executed via `JMP @A+DPTR`) |
| 0x032A-0x0563 | Deferred-event/command handler bodies, events 1-14 (clock mode, interface alt apply, codec-port modes, external-chip programming, EEPROM self-test, boot-EEPROM invalidate, suspend/resume) |
| 0x0564-0x0595 | Event-dispatcher epilogue + two serial-control helper routines |
| 0x0596-0x0727 | **DATA**: complete USB descriptor set (device, two configuration sets, three strings) |
| 0x0728-0x080A | `audio_clock_mode_apply` — ACG synthesizer / MCLK routing / iso-EP re-arm per clock mode |
| 0x080B-0x08CA | External audio-path reprogram + small external-chip write helpers |
| 0x08CB-0x096F | `hw_master_init` — timers, ports, GLOBCTL, full codec-port (I²S mode 5) configuration, ACG bring-up |
| 0x0970-0x0A08 | `usb_ep_dma_init` — endpoint buffers/sizes/configs, DMA channels, USBIMSK, USBFADR |
| 0x0A09-0x0A94 | Keil C51 startup + static-initializer interpreter (incl. an 8-byte bit-mask table at 0x0A48) |
| 0x0A95-0x0B10 | `main` — init sequence, USB attach, forever loop (event drain + 1 ms tick work) |
| 0x0B11-0x0C44 | EP0 primitive library (pointer setup, stall/toggle/arm/NACK, IN chunk transmitter) + I²C EEPROM byte write |
| 0x0C45-0x0C92 | `cs8427_ctl_write` — 3-byte bit-banged serial control write |
| 0x0C93-0x0CDC | **DATA**: USB interrupt vector dispatch table (37 big-endian entries, indexed by VECINT) |
| 0x0CDD-0x0DAB | I²C EEPROM byte read, EP0 OUT data-stage handler, EP0 length clamp |
| 0x0DAC-0x0DEB | USB INT0 ISR (VECINT read → table dispatch → ack) + one-instruction SFR-store tail |
| 0x0DEC-0x0E26 | ACG frequency/divider programming helpers, pending serial-write queue helper |
| 0x0E27-0x0ED4 | Front-panel button state machines (channels A and B) and the 16-bit latch shift-out |
| 0x0ED5-0x0F42 | P3 input scan / edge dispatch and the 8-bit mux shift-out |
| 0x0F43-0x0F6F | USB bus-reset (RSTR) handler |
| 0x0F70-0x0F9B | Compiler casejump helper + indirect-jump trampoline |
| 0x0F9C-0x0FC3 | **DATA**: C51 static-initializer record table (see §3.1 R1 — one annotator called this dead data) |
| 0x0FC4-0x101D | EP0 IN-done handler, EP0 arm helpers, codec-port cfg commit, DMA0 disable, EP0 stall |
| 0x101E-0x1027 | Timer 0 ISR (1 ms system tick) |
| 0x1028-0x1030 | `toggle_flag_bit1e` (P3.5 front-panel toggle) |
| 0x1031-0x103E | 0x22 fill (decodes as 14 unreferenced `RET`s) |
| 0x103F-0x1FED | 0xFF erase fill |

### 2.2 IRAM state-variable table (merged across all ranges)

8051 bit addresses 0x00-0x7F map onto IRAM bytes 0x20-0x2F (bit addr = (byte−0x20)*8 + bit).
Several numbers exist **both** as a direct byte address and as a bit address (e.g. byte 0x0A =
event code vs bit 0x0A = a flag in byte 0x21); they are different cells and both are used.

#### Direct bytes

| Byte | Name | Meaning / evidence |
|---|---|---|
| 0x05 | (bank-0 R5 alias) | Read by direct address in `fcn_0bee` (0x0BEE), `fcn_0c45` (0x0C47), `fcn_0e62`, `fcn_0f0c` — compiler idiom for copying R5; forces those routines to run in bank 0 |
| 0x06 | scan event flags | Bit0 = "settings changed"; set by `p3_button_scan` (0x0EEA/0x0EF7/0x0F04), returned in R7, tested by main at 0x0AE3. Also used as bank-0 R6 inside `i2c_eeprom_read_byte` (0x0D03 `ORL 0x06,#1` turns 0xA0 into 0xA1) |
| 0x08 | clock/codec-port mode state | Written 1/2/3/5 by `fcn_0728` (0x0753/0x0785/0x0791/0x07BF), 3 by `hw_master_init` (0x093B) and 0x0821; initialized to 3 by the C51 init table; read back by `cmd4` (0x045E) and by GET sample-frequency (0x0098: 1→0 Hz, 2→44100, 3→48000, else stall) |
| 0x09 | EP0 IN remaining length, low | Zeroed on every SETUP (0x003B); loaded at 0x0183/0x01A7/0x01E1; down-counted in `ep0_in_fill_chunk` (0x0BB0) |
| 0x0A | pending event/command code | 1..14; written 1 (0x0293), 2 (0x02C5), 3 (0x02D9), 4/5/6/7/8 (0x0D51/0x0D56/0x0D42/0x0D35/0x0D3C), 0x0B (0x0AF6), 0x0C (0x0B07), 0x0D (0x005B), 0x0E (0x0006); consumed by `device_event_dispatch` (0x02EE), cleared at 0x0564 |
| 0x0B | EP0 IN remaining length, high | Companion of 0x09; borrow handling at 0x0BBA-0x0BBD |
| 0x0C | UNKNOWN | Set to 0xFE at 0x0A03 (`usb_ep_dma_init`); no consumer identified by any annotator |
| 0x0D | pending deferred request | 1 = class SET to endpoint (sample rate) awaiting OUT data, 2 = class SET to interface awaiting OUT data, 5 = SET_ADDRESS pending. Set at 0x006B/0x0063/0x024D; consumed at 0x0D28/0x0D45 (OUT data handler) and 0x0FD8 (IN-done handler, address commit) |
| 0x0E | pending USB device address | wValueL captured at 0x0254, written to USBFADR at 0x0FE2 after the status stage |
| 0x10-0x17 | register bank 2 | Selected by the USB ISR (`MOV PSW,#0x10` at 0x0DB6); 0x16 = bank-2 R6 = handler address high byte |
| 0x18 | EP0 IN packet fill counter | 0..8, `ep0_in_fill_chunk` (0x0B8D, 0x0BBF, 0x0BC1) |
| 0x19:0x1A | CODE source pointer (hi:lo) | Descriptor/response source; loaded with 0x0596/0x0670/0x06A6/0x06AA/0x06C8 by GET_DESCRIPTOR; read via `fcn_0b6e`, walked at 0x0BA8-0x0BAE |
| 0x1B:0x1C | XDATA destination pointer (hi:lo) | 0xFA18 (EP0 IN buffer) via `fcn_0b3e`; 0xFA10 (EP0 OUT buffer) via `fcn_0b11`; dereferenced by `fcn_0b17` |
| 0x20 | previous P3 snapshot | Written at 0x0F07 (`MOV 0x20,R5`); its bits are read as bit addresses 0x00-0x07 (so bit 0x01 = last-sampled P3.1, bit 0x03/0x04/0x05 = P3.3/P3.4/P3.5) |
| 0x21 | EP0/USB flag byte | Bits 0x08-0x0F (see below) |
| 0x22 | 8-bit mux/control shift image | Shifted out on P1.7/P1.5/P1.6 by `fcn_0f0c`; bits 0x10-0x17 |
| 0x23 | latch chain byte A | First byte shifted out on P1.0/P1.2/P1.1 by `fcn_0e62`; bits 0x18-0x1F |
| 0x24 | tick/misc flag byte | Bits 0x20-0x27 |
| 0x25 | latch chain byte B | Second byte shifted by `fcn_0e62`; bits 0x28-0x2F |
| 0x26 | shift-loop state | Bit 0x30 = "second byte still pending" inside `fcn_0e62` (set 0x0E66, cleared 0x0E8E) |
| 0x27 | P3.1 edge state | 0 = event 0x0B not yet posted, 1 = posted; main loop 0x0AEF-0x0B05 |
| 0x28:0x29 | startup delay counter | 0xFFFF down-count in `main` (0x0A9A-0x0AC6) |
| 0x2A | UNKNOWN | Written 0x00 once at 0x0A9E; no other reference anywhere |
| 0x2B | UNKNOWN | Written 0x10 once at 0x0AA1; no other reference anywhere |
| 0x2C, 0x2D | scratch (register, value) pair | Argument staging for `fcn_0c45` in the event handlers (0x048E, 0x04A8, 0x04D1, 0x0568, 0x0582) — distinct cells from bit addresses 0x2C/0x2D |
| 0x2E, 0x2F | scratch | Busy-delay counters and (register,value) staging in `fcn_080b`/`hw_master_init`; in `fcn_0728`, 0x2E holds the saved mode argument and 0x2F:0x30 the settle counter — again distinct from bit addresses 0x2E/0x2F |
| 0x30 | settle-delay counter low | `fcn_0728` (0x072D, 0x07F8-0x0807) |
| 0x31, 0x32 | pending serial-write (reg, value) | Set by `fcn_0e20` (reg 4, val 0x40) and by 0x0756/0x0759 (reg 4, val 0x41); consumed at 0x07C5 |
| 0x33 | saved R7 in `fcn_0c45` | 0x0C45; also the last byte below the stack (SP = 0x33, so pushes start at 0x34) |
| 0x34.. | stack | `MOV SP,#0x33` at 0x0A0F |

#### Bit addresses

| Bit | Byte.bit | Meaning / evidence |
|---|---|---|
| 0x01 | 0x20.1 | Latched P3.1 level (byte 0x20 is the P3 snapshot); drives events 0x0B/0x0C in main |
| 0x03/0x04/0x05 | 0x20.3/.4/.5 | Latched P3.3 / P3.4 / P3.5 (previous-state half of the edge detector) |
| 0x08 | 0x21.0 | Interface 1 alternate setting != 0 (set 0x02C3, read 0x0207/0x0341/0x038C) |
| 0x09 | 0x21.1 | Interface 2 alternate setting != 0 (set 0x02D7, read 0x0219/0x0344/0x0413) |
| 0x0A | 0x21.2 | Gate that also permits GET/SET_INTERFACE when unconfigured — **never set anywhere in the image** (see §3.1 R2); all `JB 0x0a` paths are dead |
| 0x0B | 0x21.3 | EP0 OUT data phase expected / EP0 IN transfer has more data |
| 0x0C | 0x21.4 | EP0 IN reply armed / final phase |
| 0x0D | 0x21.5 | "Transfer shorter than wLength" / terminating-ZLP-required flag: cleared 0x0039 & 0x0D8F, set 0x0DA9, consumed 0x0BE1 |
| 0x0E | 0x21.6 | Device configured (SET_CONFIGURATION 1 sets, 0 clears; cleared on bus reset 0x0F65 and at 0x09F9) |
| 0x10-0x12 | 0x22.0-.2 | Channel-A 3-way select pattern (101 / 110 / 011) written by `button_a_cycle_3state` |
| 0x13-0x15 | 0x22.3-.5 | Channel-B 3-way select pattern written by `button_b_cycle_3state` |
| 0x16 | 0x22.6 | Shared tail flag: set only when bits 0x2C and 0x2D are both clear (0x0E52-0x0E61 / 0x0EC5-0x0ED4); also set by cmd4 (0x0456), cleared by cmd5 (0x0468) and on an EEPROM-self-test pass (0x04FD). Hardware net UNKNOWN |
| 0x17 | 0x22.7 | Cleared on stream start (0x03A0), set on stream stop (0x03E6). Hardware net UNKNOWN |
| 0x18/0x19 | 0x23.0/.1 | Set only in codec-port mode 5 (0x07B8/0x07BA). Hardware net UNKNOWN |
| 0x1A/0x1B | 0x23.2/.3 | Cleared at the start of a clock-mode change (0x072F/0x0731), re-asserted after it (0x07EE/0x07F0) and by `fcn_080b` (0x0831/0x0833) — mute-during-reclock pattern; net UNKNOWN |
| 0x1C | 0x23.4 | Set at 0x0840 in `fcn_080b`. Hardware net UNKNOWN |
| 0x1E | 0x23.6 | Toggled by `toggle_flag_bit1e` on a P3.5 rising edge; consumed at 0x0F32 to select `ORL P1,#0xC0` (P1.7+P1.6 held high) vs the normal P1.7-low/P1.6-pulse ending. Set at 0x0941, cleared at 0x039E/0x053E/0x0962 |
| 0x20 | 0x24.0 | 1 ms tick flag, set by the Timer 0 ISR (0x1020), consumed by main (0x0AD3, cleared 0x0B0D) |
| 0x22 | 0x24.2 | Cleared once at 0x0AAB; no other reference — UNKNOWN |
| 0x28/0x2A | 0x25.0/.2 | Channel-A button state pair (3-position cycle) |
| 0x29/0x2B | 0x25.1/.3 | Channel-B button state pair |
| 0x2C | 0x25.4 | Input-source / mode-variant flag: selects the 0x02-vs-0x01 GET reply (0x0076), which external-chip register set cmd7/cmd8 program (0x0485/0x049F), and forces bit 0x16 low (0x0E57/0x0ECA). Set by cmd5 (0x0466), cleared by cmd4 (0x0454) and on stream start (0x03AD) |
| 0x2D | 0x25.5 | Set only by cmd11 (0x04CA); cleared on stream start (0x0395/0x041C); consumed at 0x0E5C/0x0ECF (forces bit 0x16 low). Meaning UNKNOWN |
| 0x2E | 0x25.6 | "Hardware init done": set at 0x0810 inside `fcn_080b`, every caller guards with `JB 0x2e`; cleared at 0x037B when the clock mode is deselected. Note it physically also rides in latch byte 0x25 (see §3.1 R6) |
| 0x2F | 0x25.7 | Chip-select for the `fcn_0c45` serial target: cleared before (0x0C4F) and set after (0x0C8D) each 3-byte transaction, applied by shifting the latch chain |
| 0x30 | 0x26.0 | `fcn_0e62` internal "second byte pending" flag |

### 2.3 XDATA usage

MOVX space on the TAS1020B is split between the USB buffer RAM and the memory-mapped SFR
block; this firmware touches nothing else.

| Address | Use |
|---|---|
| 0xFA10-0xFA17 | EP0 **OUT** X buffer (8 bytes) — `OEPBBAX0 = 0x42`, 0xF800 + 0x42*8 |
| 0xFA18-0xFA1F | EP0 **IN** X buffer (8 bytes) — `IEPBBAX0 = 0x43` |
| 0xFA20-0xFC9F | OUT EP2 X buffer, 640 bytes (`OEPBBAX2 = 0x44`, `OEPBSIZ2 = 0x50`) — isochronous playback |
| 0xFCA0-0xFF1F | IN EP1 X buffer, 640 bytes (`IEPBBAX1 = 0x94`, `IEPBSIZ1 = 0x50`) — isochronous capture |
| 0xFF28-0xFF2F | SETPACK: bmRequestType, bRequest, wValueL/H, wIndexL/H, wLengthL/H |
| 0xFF60-0xFF6F | IN endpoint 0/1 config, buffer base, size, data counts |
| 0xFF98-0xFFAF | OUT endpoint 0/2 config, buffer base, size, data counts |
| 0xFFB0-0xFFB2 | MEMCFG, GLOBCTL, VECINT |
| 0xFFC0-0xFFC3 | I²C: I2CSTA/I2CCTL, I2CDATO, I2CDATI, I2CADR |
| 0xFFD4-0xFFE0 | Codec-port config (CPTRXCNF4/3/2, CPTCTL, CPTCNF4/3/2/1) |
| 0xFFE1-0xFFE7, 0xFFF6-0xFFF9 | ACG: ACGCTL, ACG1DCTL, ACG1FRQ2/1/0, ACG2DCTL, ACG2FRQ2/1/0 |
| 0xFFE8-0xFFF0 | DMA channel 0/1 control and time-slot registers |
| 0xFFFC-0xFFFF | USBCTL, USBIMSK, (0xFFFE unused), USBFADR |

No XDATA outside 0xFA10-0xFF1F (buffers) and 0xFF28-0xFFFF (SFRs) is written by this
firmware. The C51 initializer interpreter (0x0A72-0x0A93) contains a general XDATA
block-init executor, but the actual init table at 0x0F9C contains only IDATA records, so
that path never runs in this image.

---

## 3. Function index (sorted by address)

Confidence values are as given by the annotator who owned the address, except where §3.1
records a revision. "Callers" lists direct code references found in the listings; `table`
means the routine is reached only through a dispatch table.

| Addr | Name | Purpose | Conf. | Callers |
|---|---|---|---|---|
| 0x0000 | `reset_vector` | `LJMP 0x0A09` (C51 startup) | certain | hardware reset |
| 0x0003 | `int0_vector` | `LJMP 0x0DAC` (USB ISR) | certain | INT0 |
| 0x0006 | `usb_ev_suspend` | VECINT 0x16 SUSR handler: `IRAM 0x0A = 0x0E`, RET | certain | VECINT table (0x0CBF) |
| 0x000A | `int1_body_reti` | bare RETI (INT1 unused) | certain | 0x0013 |
| 0x000B | `timer0_vector` | `LJMP 0x101E` | certain | Timer 0 |
| 0x000E | `timer1_body_reti` | bare RETI (Timer 1 unused) | certain | 0x001B |
| 0x000F | `uart_body_reti` | bare RETI (UART unused) | certain | 0x0023 |
| 0x0010-0x0012, 0x0016-0x001A, 0x001E-0x0022 | `usb_ev_noop_stubs` | single-`RET` handlers for OEP1-7 / IEP1-6 interrupts | certain | VECINT table |
| 0x0013 | `int1_vector` | `LJMP 0x000A` | certain | INT1 |
| 0x001B | `timer1_vector` | `LJMP 0x000E` | certain | Timer 1 |
| 0x0023 | `uart_vector` | `LJMP 0x000F` | certain | UART |
| 0x0026 | `usb_ev_setup` | VECINT 0x12 SETUP dispatcher: clear stalls, set toggles, flush counts, branch on bmRequestType | certain | VECINT table (0x0CB7) |
| 0x0055 | `setup_class_out_interface` | bmReq 0x21: bRequest 0 → event 0x0D; else pending OUT cmd 2 | certain | 0x0050 |
| 0x006B | `setup_class_out_endpoint` | bmReq 0x22 (UAC SET_CUR sample rate): pending OUT cmd 1 | certain | 0x0045 |
| 0x0073 | `setup_get_input_source` | bmReq 0xA1: 1-byte reply 0x02/0x01 from bit 0x2C | certain | 0x0049 |
| 0x008A | `setup_get_sample_freq` | bmReq 0xA2: 3-byte LE rate from IRAM 0x08, else stall | certain | 0x004C |
| 0x010D | `send_3byte_ep0_reply` | `IEPDCNTX0 = 3`, clear 0x0B, set 0x0C | certain | 0x00B8, 0x00E0, 0x0108 |
| 0x0118 | `std_request_dispatch` | A = bRequest, `LCALL 0x0F70` with inline table at 0x011F | certain | 0x0052 |
| 0x0144 | `std_clear_feature` | bRequest 1; only bmReq 0x02 + wIndexL 0 accepted → un-stall EP0 | certain | table 0x011F |
| 0x015D | `std_get_configuration` | bRequest 8; replies 1 or 0 from bit 0x0E | certain | table |
| 0x0173 | `std_get_descriptor` | bRequest 6; device/config/string selection, clamp, start IN transfer | certain | table |
| 0x01F1 | `std_get_interface` | bRequest 0x0A; 1-byte alt setting reply | certain | table |
| 0x022F | `std_get_status` | bRequest 0; always replies 0x0000 | certain | table |
| 0x024D | `std_set_address` | bRequest 5; defers address via IRAM 0x0D/0x0E | certain | table |
| 0x025B | `std_set_configuration` | bRequest 9; sets/clears bit 0x0E, queues event 1 | certain | table |
| 0x0299 | `std_set_descriptor_stall` | bRequest 7 → stall | certain | table |
| 0x029C | `std_set_feature_stall` | bRequest 3 → stall | certain | table |
| 0x029F | `std_set_interface` | bRequest 0x0B; sets bit 0x08/0x09, queues event 2/3 | certain | table |
| 0x02E7 | `std_synch_frame_stall` | bRequest 0x0C → stall | certain | table |
| 0x02EA | `std_request_unknown_default` | table default: stall + RET (no boot-ROM delegation) | certain | table default |
| 0x02EE | `device_event_dispatch` | Dispatch IRAM 0x0A (1..14) through the table at 0x0300 | certain | 0x0ADA, 0x0AF9, 0x0B0A |
| 0x0300 | `event_jump_table` | 14 × `LJMP`, executed via `JMP @A+DPTR` | certain | 0x02FF |
| 0x032A | `cmd1_apply_clock_mode` | Event 1: quiesce DMA + codec port, reprogram CPTCNF3/CPTRXCNF3, re-enable, hw init | likely | table slot 0x0300 |
| 0x0386 | `cmd2_apply_iface1_alt` | Event 2: start/stop the IN-EP1 capture stream (and OUT EP2 when bit 0x0E) | likely | slot 0x0303 |
| 0x03FD | `cmd3_apply_iface2_alt` | Event 3: start/stop the OUT-EP2 playback stream | likely | slot 0x0306 |
| 0x0454 | `cmd4_variantA_reapply_mode` | Event 4: clear bit 0x2C, set bit 0x16, re-apply current mode | guess | slot 0x0309 |
| 0x0466 | `cmd5_variantB_set_mode1` | Event 5: set bit 0x2C, clear bit 0x16, mode 1 | guess | slot 0x030C |
| 0x0478 | `cmd6_set_cpt_mode1` | Event 6: mode 1 | likely | slot 0x030F |
| 0x0480 | `cmd7_set_cpt_mode2_progchip` | Event 7: mode 2 + external-chip register set | guess | slot 0x0312 |
| 0x049A | `cmd8_set_cpt_mode3_progchip` | Event 8: mode 3 + external-chip register set | guess | slot 0x0315 |
| 0x04B4 | `cmd9_set_cpt_mode4` | Event 9: mode 4 | likely | slot 0x0318 |
| 0x04BC | `cmd10_set_cpt_mode5` | Event 10: mode 5 | likely | slot 0x031B |
| 0x04C4 | `cmd11_eeprom_selftest` | Event 11: complement/verify EEPROM byte 0x1FFF, report on bit 0x16 | likely | slot 0x031E |
| 0x0511 | `cmd12_set_cpt_mode1` | Event 12: mode 1 (duplicate of cmd6) | likely | slot 0x0321 |
| 0x0518 | `evt0d_invalidate_boot_eeprom` | Event 13: write 0x00 to boot-EEPROM offset 0, re-arm EP0 OUT | likely | slot 0x0324 |
| 0x0526 | `evt0e_usb_suspend_enter_and_resume` | Event 14: clocks off, latches idle, `PCON.IDL`; on resume disconnect/re-init/reconnect | certain | slot 0x0327 |
| 0x0564 | `evt_dispatch_epilogue` | `CLR A; MOV 0x0A,A; RET` | certain | 0x02F6, 0x0383, 0x03FA, 0x0451, 0x0463, 0x0475, 0x047D, 0x048B, 0x0497, 0x04A5, 0x04B1, 0x04B9, 0x04C1, 0x050F, 0x0516, 0x0524 |
| 0x0568 | `serial_ctl_write_04_41_then_12_00` | Ext-chip reg 0x04=0x41 then 0x12=0x00 | certain | 0x0488, 0x04A2 |
| 0x0582 | `serial_ctl_write_caller_pair_then_24_80` | Ext-chip write (IRAM 0x2C,0x2D) then 0x24=0x80 | certain | 0x0494, 0x04AE |
| 0x0728 | `audio_clock_mode_apply` | Mode 1/2/3/5: ACG synth + MCLK routing, ext-chip reg 4, iso EP re-arm, settle delay | certain | 0x03BA, 0x0426, 0x0460, 0x0472, 0x047A, 0x0482, 0x049C, 0x04B6, 0x04BE, 0x04CE, 0x0513 |
| 0x080B | `audio_path_reconfig_ext_chips` | Latch reset, MCLK enable, CS pulse, 10 external-chip register writes; sets bit 0x2E | likely | 0x0360, 0x0392, 0x0419, 0x04C7 |
| 0x08A6 | `extchip_write_reg4_zero` | reg 0x04 = 0x00 | certain | 0x0855, 0x0861 |
| 0x08B3 | `extchip_write_val05` | reg IRAM[0x2E] = 0x05 | certain | 0x088F, 0x0895 |
| 0x08BD | `extchip_write_2e_2f` | reg IRAM[0x2E] = IRAM[0x2F] | certain | 0x085E, 0x086A |
| 0x08C4 | `extchip_write_2e_2f_dup` | byte-identical duplicate of 0x08BD | certain | 0x0873, 0x087C |
| 0x08CB | `hw_master_init` | USBCTL=0, MEMCFG.SDW, ports, timers, IE/IP, GLOBCTL, full codec-port I²S mode 5, ACG, latches, settle | certain | 0x0AAD, 0x0551 |
| 0x0970 | `usb_ep_dma_init` | EP0/EP1/EP2 buffers + configs, DMA ch0/ch1, USBIMSK=0x9F, USBFADR=0, clear EP0 state | certain | 0x0AB0, 0x0554 |
| 0x0A09 | `c51_startup` | Clear IRAM 0x01-0x7F, SP=0x33, jump to init interpreter | certain | reset vector |
| 0x0A15 | (trampoline) | `LJMP 0x0A95` when the init table terminator is reached | certain | 0x0A57 |
| 0x0A18 | `c51_init_interpreter` | Walks the record table at 0x0F9C (IDATA / PDATA / XDATA / bit records) | certain | 0x0A12 (entry 0x0A50) |
| 0x0A95 | `main` | Init, USB attach, forever loop (event drain + 1 ms tick work) | certain | 0x0A15 |
| 0x0B11 | `ep0_ptr_set_out_buf` (was `ep0in_ptr_load_fa10`) | Sets 0x1B:0x1C = 0xFA10 = EP0 **OUT** buffer, falls into 0x0B17 — see §3.1 R3 | certain | 0x0D2D, 0x0D4A |
| 0x0B17 | `dptr_from_ep0_ptr` | DPTR = 0x1B:0x1C | certain | 0x0079, 0x0081, 0x009D, 0x00BF, 0x00E7, 0x0163, 0x016B, 0x020A, 0x021C, 0x0224, 0x0232, 0x0B9C, fall-through from 0x0B11 |
| 0x0B1E | `ep0_clear_stall_toggle_and_arm` | IEPCNF0/OEPCNF0 &= 0xD7, then falls into 0x0B2B | certain | 0x0D67, 0x0FE6 |
| 0x0B2B | `ep0_store_cnf_and_arm_both` | Store caller's A to caller's DPTR; IEPDCNTX0=0, OEPDCNTX0=0 | certain | 0x0036, 0x0FD5, 0x1016, fall-through |
| 0x0B36 | `ep0_buf_clear_byte` | XDATA[0x1B:A] = 0 | certain | 0x00AA, 0x00B5, 0x00DD, 0x0105, 0x023F |
| 0x0B3E | `ep0_ptr_set_in_buf` | 0x1B:0x1C = 0xFA18 (EP0 IN buffer) | certain | 0x0073, 0x0095, 0x015D, 0x0200, 0x022F, 0x0B8F |
| 0x0B45 | `ep0_send_1byte` | `IEPDCNTX0 = 1`; clear 0x0B, set 0x0C | certain | 0x0087, 0x0170, 0x0229 |
| 0x0B50 | `ep0_clear_stall_both` | IEPCNF0/OEPCNF0 &= 0xF7 | certain | 0x0026, 0x0152 |
| 0x0B5F | `ep0_nack_both` | IEPDCNTX0 = OEPDCNTX0 = 0x80; clear 0x0B/0x0C | certain | 0x0296, 0x02E1 |
| 0x0B6E | `code_read_byte_at_srcptr` | A = CODE[0x19:0x1A] | certain | 0x0180, 0x01DE, 0x0B99 |
| 0x0B77 | `ep0_in_start_transfer` | Fill chunk then clear the NACK bit to release the packet | certain | 0x01EE, 0x0FC7 |
| 0x0B82 | `ep0_arm_zlp_and_out` | OEPDCNTX0 = 0, IEPDCNTX0 = 0 | certain | 0x0D64, 0x0F43 |
| 0x0B8C | `ep0_in_fill_chunk` | Copy ≤8 bytes CODE→EP0 IN buffer, update counts and state bits | certain | 0x0B77 |
| 0x0BEE | `eeprom_write_byte` / `i2c_write_byte` | I²C write to slave 0xA0: addr hi (R7), addr lo (R5), data (R3), STOP, settle delay | certain | 0x04EE, 0x051C |
| 0x0C45 | `cs8427_ctl_write` | Bit-banged 3-byte serial write {0x20, R7, R5} on P1.4/P1.3, CS via bit 0x2F | likely (mechanics certain, chip id likely) | 0x04DB, 0x050C, 0x0572, 0x0586, 0x07C9, 0x0889, 0x08A2, 0x08B0, 0x08BA, 0x08C8 |
| 0x0CDD | `i2c_eeprom_read_byte` | Random read of one byte from slave 0xA0 at 16-bit address R7:R5 | certain | 0x04E2, 0x04F3 |
| 0x0D25 | `ep0_out_data_handler` | VECINT 0x00: decode the EP0 OUT payload into events 4-8, arm status stage | certain | VECINT table (0x0C93) |
| 0x0D6B | `ep0_clamp_len_to_wlength` | Clamp 0x09/0x0B to wLength; set bit 0x0D if shorter | certain | 0x01EB |
| 0x0DAC | `usb_int0_isr` | Save context, bank 2, EA=0, read VECINT, dispatch via table, VECINT=0, restore, RETI | certain | INT0 vector |
| 0x0DEB | `sfr_store_then_cpt_cfg_tail` | One `MOVX @DPTR,A` then falls into 0x0DEC | certain | 0x07AC, 0x092E |
| 0x0DEC | `acg_set_freq_48k_family` | Both synthesizers ← 0x61A80F (≈24.576 MHz) | certain | 0x078E, 0x081B |
| 0x0E0F | `acg_commit_and_ctl` | Caller's `MOVX` (ACG2FRQ0 latch) then `ACGCTL = 0x06` | certain | 0x0782, fall-through |
| 0x0E17 / 0x0E18 | `acg_dividers_div2` | ACG1DCTL = ACG2DCTL = 0x10 (÷2) | certain | 0x0931, 0x081E (0x0E17); 0x0739 (0x0E18) |
| 0x0E20 | `queue_cs8427_reg4_val40` | IRAM 0x31 = 4, 0x32 = 0x40 | likely | 0x0788, 0x0794, 0x07C2 |
| 0x0E27 | `button_a_cycle_3state` | P3.3 rising edge: cycle channel-A 3-position selector | likely | 0x0EF4 |
| 0x0E62 | `shiftreg16_commit_p1_0_1_2` | Shift IRAM 0x23 then 0x25 out P1.0/P1.2, latch P1.1 | certain | 0x0733, 0x07BC, 0x07F2, 0x0818, 0x0835, 0x0842, 0x084D, 0x0852, 0x096C, 0x037D, 0x03AF, 0x0458, 0x046A, 0x0538, 0x0AE9, 0x0C51, 0x0C8F |
| 0x0E9D | `button_b_cycle_3state` | P3.4 rising edge: cycle channel-B selector | likely | 0x0F01 |
| 0x0ED5 | `p3_button_scan` | Snapshot P3, edge-dispatch P3.5/P3.3/P3.4, return change flags | certain | 0x0ADF |
| 0x0F0C | `shiftreg8_commit_p1_7_6_5` | Shift IRAM 0x22 out P1.7/P1.5, latch P1.6 (or hold both high per bit 0x1E) | certain | 0x03A2, 0x03E8, 0x045B, 0x046D, 0x04FF, 0x0540, 0x0943, 0x0964, 0x0AE6 |
| 0x0F43 | `usb_rstr_handler` | VECINT 0x17: clear EP counts, USBFADR=0, EP0 CNF=0x84, USBCTL |= 0xC0, clear flags, USBIMSK=0x9F | certain | VECINT table (0x0CC1) |
| 0x0F70 | `switch_case_dispatch` | Keil casejump: pops return address as {hi,lo,key} table pointer | certain | 0x011C |
| 0x0F96 | `jmp_r2r1_trampoline` | DPTR = R2:R1, `JMP @A+DPTR` | certain | 0x0DD6 |
| 0x0FC4 | `usb_iep0_done_handler` | VECINT 0x08: continue IN transfer / arm status stage / commit SET_ADDRESS | certain | VECINT table (0x0CA3) |
| 0x0FEA | `ep0_arm_zlp_in_and_out` | IEPDCNTX0 = 0, OEPDCNTX0 = 0 | likely | 0x0380, 0x03F7, 0x044E |
| 0x0FF4 | `codec_port_cfg3_commit` | CPTCNF3 = CPTRXCNF3 = A; GLOBCTL |= CPTEN | likely | 0x034F, 0x035A |
| 0x1001 | `dma0_disable` | DMACTL0 &= 0x7F | certain | 0x032A, 0x03EE, 0x044B |
| 0x1009 | `ep0_stall_both` | STALL both EP0 directions, zero counts, clear 0x0B/0x0C | certain | 0x0092, 0x010A, 0x015A, 0x01E8, 0x022C, 0x0264, 0x0299, 0x029C, 0x02DE, 0x02E4, 0x02E7, 0x02EA |
| 0x101E | `timer0_isr_tick` | EA=0, set bit 0x20, TH0 = 0xCE, EA=1, RETI | certain | Timer 0 vector |
| 0x1028 | `toggle_flag_bit1e` | Toggle bit 0x1E | certain | 0x0EE7 |

### 3.1 Reconciliation notes

**R1 — 0x0F9C: C51 initializer table vs "dead stub sled".**
The 0x0A09 annotator read `MOV DPTR,#0x0F9C` at 0x0A50 and identified 0x0F9C as the Keil
static-initializer record table consumed by the interpreter at 0x0A18-0x0A93. The 0x0F70
annotator, seeing no Ghidra XREF (there is none — the reference is a `MOV DPTR` immediate,
not a branch), called 0x0F9C-0x0FC3 an unreferenced "stub sled" of `AJMP`s and marked it
UNKNOWN. **Resolved in favour of the initializer table**, and confirmed during assembly by
parsing the bytes against the record format the 0x0A09 annotator decoded from the
interpreter's own instructions: thirteen type-00 (IDATA) records of length 1 —
`01 22 00`, `01 20 00`, `01 25 00`, `01 23 00`, `01 24 00`, `01 21 00`, `01 09 00`,
`01 0C 00`, `01 0B 00`, `01 0E 00`, `01 0A 00`, `01 0D 00`, `01 08 03` — followed by the
`00` terminator at 0x0FC3, exactly where real code (`fcn_0fc4`) resumes. That is: zero the
flag/latch/state bytes 0x20-0x25 and the EP0 state bytes 0x09-0x0E, and set mode state
IRAM 0x08 = 3, which agrees with `hw_master_init` writing 0x08 = 3 at 0x093B. The 0x0F70
annotator's "AJMP target" reading is a mis-decode; its observation that several such
targets land mid-instruction is itself evidence the region is not code. Full decode in §6.5.

**R2 — bit 0x0A (IRAM 0x21.2): never set, or set once somewhere?**
The 0x0118 annotator wrote that "the setter is elsewhere (one `SETB 0x0A` in the whole
image, outside this range)". The 0x032A and 0x0518 annotators each independently grepped
the image (`SETB 0x0a` / opcodes `d20a`,`920a`, plus direct writes to byte 0x21) and found
**no** setter, only five clears (0x026D, 0x027C, 0x028B, 0x09F7, 0x0F63). **Resolved: the
bit is never set**; two independent negative searches beat one unsupported assertion. All
`JB 0x0A` guards (0x01F1, 0x02B0, 0x033B, 0x0341-region, 0x0365, 0x0386, 0x0406, 0x043B,
0x0526) are therefore dead at runtime, and `MOV C,0x0E / ORL C,0x0A` reduces to
"configured?". Both readings are preserved here; if a setter is ever found the affected
paths come back to life.

**R3 — EP0 buffer direction (0xFA10 vs 0xFA18).**
`ANNOTATION_BRIEF.md` states "EP0 IN buffer 0xFA10, EP0 OUT 0xFA18", and the 0x0A09
annotator carried that into the name `ep0in_ptr_load_fa10` for `fcn_0b11`. Three annotators
(0x0000, 0x080B, 0x0B1E) independently verified the programming bytes at 0x0970-0x097A:
`OEPBBAX0 (0xFFA9) = 0x42` and `IEPBBAX0 (0xFF69) = 0x43`, which with the TI `Mmap.h`
formula 0xF800 + reg*8 give **EP0 OUT = 0xFA10, EP0 IN = 0xFA18**. The 0x0C45 annotator
independently confirms the direction by use: `fcn_0b11` (0xFA10) is called from the EP0
**OUT** data handler to read the received payload. **Resolved: the brief is wrong and
`fcn_0b11` is renamed `ep0_ptr_set_out_buf`** in §3; its original name is left in place in
the §5.7 listing to keep that listing verbatim.

**R4 — Size of the VECINT dispatch table at 0x0C93.**
The 0x0DEC annotator described it in passing as "24 big-endian entries indexed by VECINT
0x00-0x17". The 0x0C45 annotator dumped the raw bytes and decoded **37 entries**
(0x0C93-0x0CDC, indices 0x00-0x24, ending exactly where `fcn_0cdd` begins), matching TI's
`Reg_stc1.h` source codes up to `NO_INT = 0x24`. **Resolved: 37 entries.** The 0x0DEC
annotator's actual conclusion (RSTR_INT 0x17 → 0x0F43 at table offset 0x0CC1) is
unaffected and agrees with the 37-entry decode.

**R5 — `fcn_080b`: "audio path reprogram" vs "one-time hardware init".**
The 0x080B annotator named it `audio_path_reconfig_ext_chips` (latch sequencing + ten
external-chip register writes); the 0x032A annotator, looking only at call sites, described
it as "one-time hardware init" guarded by bit 0x2E. **Both are correct and describe the
same routine**: it *is* the external-audio-path programming sequence, and it is invoked
once, guarded by the bit 0x2E "init done" flag that it sets at 0x0810. Preferred name:
`audio_path_reconfig_ext_chips`, with "one-time hw init (sets bit 0x2E)" as its role at the
call sites.

**R6 — bit 0x2E: "hw init done" flag or latch output line?**
`SETB 0x2E` at 0x0810 sets bit 6 of latch shadow byte 0x25, which `fcn_0e62` physically
shifts out; the 0x080B annotator therefore read it as an external latch output, while the
0x032A annotator read it as the software "hardware initialized" flag because every caller
guards `JB 0x2E` before calling `fcn_080b`, and 0x037B clears it when the clock mode is
deselected. **Resolved: both — the cell serves double duty.** The control-flow evidence for
the "init done" role is direct; the fact that it also rides out on the latch chain is a
consequence of the firmware storing the flag inside a shadow byte, and means the external
latch bit 6 is asserted for as long as the hardware is considered initialized. Which board
net latch bit 6 drives remains UNKNOWN.

**R7 — `fcn_0c45`'s target chip.**
Named `cs8427_ctl_write` by the 0x0C45 annotator (confidence "likely": the leading 0x20
byte matches the CS8427 chip-address/write byte, and project `NOTES.md` says CS8427); the
0x032A, 0x0518, 0x0728 and 0x080B annotators all refused the identification because no
CS8427 datasheet exists in the repo. **Resolved: the transport is certain** (3 bytes,
{0x20, register, value}, MSB-first on P1.4 data / P1.3 clock, chip-select through latch bit
0x2F), **the chip identity is "likely, unverified", and no register/field meaning of that
device is asserted anywhere in this document.**

**R8 — SET_CONFIGURATION accepting wValue 2.**
The 0x0518 annotator wrote that bit 0x0E is set "for wValue 1 or 2 (0x027E, 0x028D)". The
0x0118 annotator's line-by-line decode shows the wValue-2 block at 0x0288-0x0291 is
**unreachable**, because 0x0260-0x0264 stalls anything ≥ 2. **Resolved: config 2 is
rejected; the 0x0288 block is dead code.** (It is preserved in the listing, marked as such.)

**R9 — bit 0x2C: "input source" or "mode variant"?**
0x0000 calls it `input_src` (GET reply 0x02 vs 0x01; `NOTES.md` maps 0x02 = S/PDIF);
0x032A calls it the cmd4/cmd5 "mode-variant" flag selecting which external-chip register
set is written; 0x0DEC sees it only as a gate forcing mux image bit 0x16 low. **Resolved:
one flag, three consumers** — the host-visible input-source report, the external-chip
program selection, and the front-panel mux gating are all driven by the same bit, which is
consistent with it being an input-source (analog vs digital) selector. The "0x02 = S/PDIF"
mapping remains from `NOTES.md` and is not independently verified.

**R10 — byte 0x25 bits 0x28-0x2B: "status flags" or button state pairs?**
0x032A described bits 0x28-0x2B as status flags cleared on stream start and "set in the
monitor code 0x0E27/0x0E9D". The 0x0DEC annotator decoded 0x0E27/0x0E9D fully as two
three-position **button state machines** using (0x28,0x2A) and (0x29,0x2B) as state pairs.
**Resolved in favour of the button state machines** (full decode beats call-site
inference); clearing them on stream start resets both selectors to position 0.

**R11 — alternate entry points into 0x080B / 0x0902.**
The 0x080B annotator listed alternate entries at 0x080C, 0x080E, 0x0821 "from the 0x0FA9
stub table" and at 0x0902/0x0904 "from the 0x0596-0x0717 dynamic-reconfig region". Both
source regions were later shown to be **data**: 0x0F9C-0x0FC3 is the C51 initializer table
(R1) and 0x0596-0x0727 is the descriptor block (the 0x0518 annotator verified it
byte-for-byte). **Resolved: those XREFs are force-disassembly artifacts; 0x080C, 0x080E,
0x0821, 0x0902 and 0x0904 are not real entry points.** The mid-instruction "entries" at
0x0809/0x080D flagged by the same annotator fall away with them. The same applies to the two
"incoming jumps" the 0x0728 annotator noted into 0x0802 and 0x080A: their sources are 0x0A48
(the bit-mask table decoded in §5.7) and 0x0FBA (inside the C51 initializer table, R1), both
data. `fcn_0728` has exactly one entry point, 0x0728.

**R12 — naming of IRAM 0x0A's contents: "event" vs "command".**
0x0118 and 0x0518 call the codes in IRAM 0x0A *events*; 0x032A calls them *deferred
commands*. Same mechanism, no factual disagreement. This document uses "event/command code"
interchangeably and numbers them 1-14 (0x01-0x0E).

**R13 — `fcn_0728` naming.**
Called `audio_clock_mode_apply` by its owner (0x0728) and "codec-port mode configure"
(`fcn_0728(R7=mode)`) by 0x032A. The owner's full decode shows it programs the ACG
synthesizers, MCLK routing and dividers *and* touches codec-port registers in mode 5, so
the broader name is kept; "codec-port mode N" in the 0x032A listing means the same thing as
"clock mode N".

**R14 — bit 0x0D (IRAM 0x21.5).**
Marked UNKNOWN by the 0x0000 annotator ("cleared on every SETUP, role unresolved"). The
0x0C45 annotator (`ep0_clamp_len_to_wlength`, sets it at 0x0DA9 when the response is
shorter than wLength) and the 0x0B1E annotator (consumes it at 0x0BE1 to decide whether an
exactly-full final packet needs a terminating ZLP) together **resolve it**: it is the
short-transfer / ZLP-required flag. No open question remains.

**R15 — `fcn_0e62`'s second byte.**
The 0x0728 annotator described it as conditional ("if bit 0x30 is set it continues with
0x25"); the 0x0DEC listing shows bit 0x30 is set unconditionally at function entry
(0x0E66) and cleared only to end the second pass. **Resolved: both bytes are always
shifted** — 16 bits per call.

---

## 4. Execution narrative

### 4.1 Reset → static init

Power-on/reset vectors to 0x0000, which is `LJMP 0x0A09`. `c51_startup` (0x0A09) clears
IRAM 0x01-0x7F with a `DJNZ R0` loop, sets `SP = 0x33` (so the stack occupies 0x34 upward),
and jumps to 0x0A50, the entry of the Keil static-initializer interpreter. That interpreter
(0x0A18-0x0A93) walks the record table at 0x0F9C: thirteen 1-byte IDATA records that zero
IRAM 0x20-0x25 and 0x09-0x0E and set the clock-mode state byte IRAM 0x08 = 3 (48 kHz
family). The `00` terminator at 0x0FC3 makes the interpreter take `JZ 0x0A15` → `LJMP
0x0A95` into `main`.

### 4.2 main init (0x0A95-0x0AD2)

`main` clears the P3.1 edge-state byte 0x27, preloads the 16-bit startup delay counter
0x28:0x29 = 0xFFFF, writes the two never-read bytes 0x2A = 0x00 / 0x2B = 0x10, then disables
interrupts (`CLR EA`) and masks every USB interrupt source (`USBIMSK = 0` at 0x0AAA) before
touching hardware.

`hw_master_init` (0x08CB) then establishes the base hardware state: `USBCTL = 0` (D+ pull-up
Hi-Z — the device is *not* on the bus yet), `MEMCFG = 0x01` (code fetched from shadow
program RAM), `P1 = 0x00` (all bit-bang lines low), `P3 = 0xFF` (inputs released),
Timer 0/1 into mode 1 with `TH0 = 0xCE`, `TCON = 0`, and an interrupt enable set of ET0
(timer tick) + EX0 (USB) with EA still off and `IP = 0`. It then writes `GLOBCTL = 0x06`
(normal power, P3 pull-ups off, codec port still disabled) and programs the codec port
completely for **I²S mode 5** — `CPTCNF1 = 0x0D` (2 time slots, mode 5), `CPTCNF2 = 0xE5`
(24 data bits in 32-clock slots), `CPTCNF3 = 0xAC` (1-clock data delay, LRCK-style sync,
byte-order reversed), `CPTCNF4 = 0x03` (CSCLK = MCLKO/4), `CPTCTL = 0x50` (RX/TX interrupts
enabled), plus the mode-5 receive-side mirrors `CPTRXCNF2 = 0x25`, `CPTRXCNF3 = 0xAC` and
`CPTRXCNF4 = 0x03` (that last write is performed by the shared tail at 0x0DEB, which then
falls through and programs both ACG synthesizers to 0x61A80F ≈ 24.576 MHz and sets
`ACGCTL = 0x06`; `fcn_0e17` then sets both output dividers to ÷2, giving a 12.288 MHz
MCLK = 256·fs at 48 kHz). Only after all config registers are loaded does it set
`GLOBCTL |= 0x01` (CPTEN). It finishes by setting the mode state IRAM 0x08 = 3, clearing the
serial-device-B image and both latch-chain shadows, shifting them out, and burning a
~4096-iteration settle delay.

`usb_ep_dma_init` (0x0970) then programs the USB engine: EP0 OUT buffer at 0xFA10 and EP0 IN
at 0xFA18 (8 bytes each, both `CNF = 0x84` = enabled + interrupt), the isochronous playback
buffer OUT EP2 at 0xFA20 and capture buffer IN EP1 at 0xFCA0 (640 bytes each,
`CNF = 0xC5` = enabled, isochronous, 6 bytes per sample frame = 24-bit stereo), DMA channel
0 bound to OUT EP2 and channel 1 to IN EP1 (both with `DMAEN = 0`, time slots 0-1, 3 bytes
per slot), `USBIMSK = 0x9F` (RSTR + SOF + PSOF + SETUP + STPOW), `USBFADR = 0`, and finally
clears the EP0/main-loop state (bits 0x08/0x09/0x0A/0x0E, bytes 0x09/0x0A/0x0B, byte
0x0C = 0xFE).

Back in `main`, a pure software delay counts 0x28:0x29 down from 0xFFFF; then `TR0` starts
the tick timer, `EA` is set, and `USBCTL |= 0x80` (CONT) connects the D+ pull-up at 0x0AD2.
The device becomes visible to the host at that instruction.

### 4.3 Steady state — the forever loop (0x0AD3-0x0B0F)

The loop has two halves.

*Fast path*: while the 1 ms tick flag (bit 0x20) is clear, it polls the pending event byte
IRAM 0x0A; whenever it is nonzero it calls `device_event_dispatch` (0x02EE), which decrements
the code, bounds-checks it against 14, and jumps through the `LJMP` trampoline table at
0x0300 into one of the fourteen handler bodies. Every handler ends at 0x0564, which zeroes
IRAM 0x0A. All heavyweight reconfiguration (clock changes, DMA start/stop, external-chip
programming, EEPROM access, suspend) therefore happens here, in task context, never inside an
interrupt.

*Tick path*: once per Timer 0 tick, `p3_button_scan` (0x0ED5) reads P3, compares it with the
snapshot in IRAM 0x20 and, on a rising edge, calls `toggle_flag_bit1e` (P3.5),
`button_a_cycle_3state` (P3.3) or `button_b_cycle_3state` (P3.4), setting bit 0 of IRAM 0x06
each time. If that bit came back set, main re-shifts both control chains — `fcn_0f0c` (the
8-bit mux image IRAM 0x22 on P1.7/P1.5/P1.6) and `fcn_0e62` (the 16-bit latch word
IRAM 0x23:0x25 on P1.0/P1.2/P1.1). Then it edge-detects the latched P3.1 level (bit 0x01)
against the state byte 0x27 and posts event 0x0B on the falling side and event 0x0C on the
returning side, dispatching each immediately. Finally it clears the tick flag and loops.

### 4.4 USB enumeration and streaming flow

The host's SETUP packets arrive as VECINT 0x12 → `usb_ev_setup` (0x0026), which clears both
EP0 stalls, forces both EP0 toggles to DATA1, flushes both EP0 byte counts, clears the
short-transfer flag and the pending IN length, then branches on bmRequestType. Standard
requests fall through to 0x0118 and are dispatched by key through the inline table at
0x011F; unknown bRequests reach the default entry 0x02EA and are stalled locally (this
firmware never delegates to the boot ROM at 0x2F00 — see §5.2). GET_DESCRIPTOR serves the
device descriptor at 0x0596, the **vendor-class** 54-byte configuration set at 0x0670, and
strings 0/1/2; the transfer length is clamped to wLength by `ep0_clamp_len_to_wlength`
(0x0D6B) and the first ≤8-byte chunk is loaded and released by `ep0_in_start_transfer`
(0x0B77). Each completed IN packet raises VECINT 0x08 → `usb_iep0_done_handler` (0x0FC4),
which either sends the next chunk, arms the OUT status stage, or — if IRAM 0x0D holds 5 —
commits the deferred SET_ADDRESS by writing `USBFADR` *after* the status stage, as USB
requires.

SET_CONFIGURATION(1) sets the configured bit and queues event 1; SET_INTERFACE on interface 1
or 2 records the alt setting in bit 0x08/0x09 and queues event 2 or 3. The corresponding
handlers (0x032A, 0x0386, 0x03FD) do the real work: event 2 starts capture by writing
`IEPCNF1 = 0xC5`, calling `audio_clock_mode_apply(3)` and setting `DMACTL1.DMAEN`, and (when
bit 0x0E is set) also enables OUT EP2 and `DMACTL0.DMAEN`; event 3 does the playback side
alone; alt 0 tears the same paths down. Class requests take the fast paths: bmReq 0xA2 GET
returns the current rate from IRAM 0x08 as a 3-byte little-endian value, bmReq 0xA1 GET
returns 0x02/0x01 from bit 0x2C, and the SET_CUR forms defer to their OUT data stage.

### 4.5 Interrupt paths

**INT0 / USB (vector 0x0003 → 0x0DAC).** Pushes ACC/B/DPH/DPL/PSW, selects register bank 2,
clears EA, reads `VECINT (0xFFB2)`, computes `0x0C93 + 2*VECINT`, fetches the big-endian
handler address into bank-2 R6:R1, and `LCALL`s the trampoline at 0x0F96 (`DPH=R2, DPL=R1,
JMP @A+DPTR`) so the handler's `RET` comes back into the ISR. It then writes `VECINT = 0` to
acknowledge, sets EA, pops everything and `RETI`s. Live table entries:

- **0x00 OEP0_INT → 0x0D25** `ep0_out_data_handler`: if no OUT data stage is pending it just
  clears EP0 stall/toggle; otherwise it reads the payload byte at 0xFA10 and, per IRAM 0x0D,
  posts event 7 (0x44 = 44100 LSB), 8 (0x80 = 48000 LSB) or 6 (0x00) for the sample-rate
  request, or event 4/5 for the boolean interface control, then sets the EP0 IN toggle to
  DATA1 and arms the status stage.
- **0x08 IEP0_INT → 0x0FC4** `usb_iep0_done_handler` (above).
- **0x12 SETUP_INT → 0x0026** the SETUP dispatcher (above).
- **0x16 SUSR_INT → 0x0006**: posts event 0x0E and returns — the IDL write must not happen
  inside an ISR.
- **0x17 RSTR_INT → 0x0F43** `usb_rstr_handler`: zeroes both EP0 counts, clears
  `OEPDCNTX2`/`IEPDCNTX1`, `USBFADR = 0`, `IEPCNF0 = OEPCNF0 = 0x84`, re-asserts
  `USBCTL |= 0xC0` (CONT + FEN, since FEN is cleared by a bus reset), clears the four EP0
  state bits and re-writes `USBIMSK = 0x9F`.
- Everything else (OEP1-7, IEP1-7, STPOW, PSOF, SOF, RESR, CPRX/CPTX, DPRX/DPTX, I2CRX/I2CTX,
  XINT, reserved, NO_INT) maps to a bare `RET` stub — the firmware ignores SOF, start-of-frame
  pseudo-interrupts, codec-port and I²C interrupts entirely and does all audio transfer through
  the DMA engine.

**Timer 0 (vector 0x000B → 0x101E).** Clears EA, sets the tick flag bit 0x20, reloads
`TH0 = 0xCE`, sets EA, `RETI`. This is the only periodic timebase; the main loop's input
scanning and edge detection run off it.

**INT1 (0x0013), Timer 1 (0x001B), UART (0x0023).** Each vectors to a bare `RETI` in the dead
space between vectors; none of these sources is enabled.

**Suspend/resume.** VECINT 0x16 posts event 0x0E; the handler at 0x0526 runs only when the
device is configured. It turns off both ACG master-clock outputs (`ACGCTL &= 0x3F`), zeroes
and shifts out the 16-bit latch word, drives the 8-bit chain to 0xFF, and sets `PCON.IDL`,
stopping all TAS1020B clocks. The resume interrupt clears IDL automatically and execution
continues at 0x0546: `USBCTL &= 0x7F` (disconnect), `USBIMSK = 0x9F`, re-run `hw_master_init`
and `usb_ep_dma_init`, restart Timer 0, re-enable EX0 and EA, then `USBCTL |= 0x80`. The
device therefore returns from suspend by **disconnecting and re-enumerating**, not by
resuming the existing session.

---

## 5. Full annotated listing

The twelve range annotations follow in address order, reproduced verbatim — every
instruction line, table and note as written by the annotator who owned that range. Where a
later range corrected an earlier one, the correction is in §3.1; the listings themselves are
left untouched so that each range's evidence chain stays intact.

### 5.1 Range 0x0000-0x0117

# Rev 20 annotation — 0x0000-0x0117

Verification sources actually read for this range: raw bytes (`rev20_firmware_code.bin` 0x0000-0x0150 and 0x0c80-0x0ce0 via xxd), Ghidra listing, `ROM/Reg_stc1.h` (SFR names + VECINT vector values), `ROM/hwMacro.h` (EPCNF bit macros), `ROM/UsbEng.c` (engEp0SetupDone, SETPACK field order), `ROM/Usbaudio.h` (AUD_SET_CUR/AUD_SAMP_FREQ_CTL), `ROM/Mmap.h` (buffer-offset formula).

**Key ground truth established for this range:**

1. **VECINT dispatch table.** The USB ISR at 0x0dac (target of vector 0x0003) reads VECINT (0xFFB2), computes table entry = 0x0c93 + 2*VECINT, loads a big-endian handler address, and LCALLs it (via JMP @A+DPTR at 0x0f96). I dumped the raw table at 0x0c93 and decoded every entry. This proves: 0x0026 is the SETUP_INT (0x12) handler; 0x0006 is the SUSR_INT (0x16) handler; and the lone RETs at 0x0010-0x0012, 0x0016-0x001a, 0x001e-0x0022 are no-op handlers for OEP1-7/IEP1-6 endpoint interrupts. Vector values from Reg_stc1.h:234-270.
2. **EP0 buffers.** At 0x0970 the firmware writes OEPBBAX0(0xFFA9)=0x42 and IEPBBAX0(0xFF69)=0x43; with buffer base 0xF800 and offset<<3 (Mmap.h:56,72) that gives **EP0 OUT = 0xFA10, EP0 IN = 0xFA18**. (ANNOTATION_BRIEF.md has these two swapped — the brief is wrong on this point.) Helpers: fcn_0b3e sets the RAM pointer pair 0x1b:0x1c to 0xFA18 (IN), fcn_0b11 to 0xFA10 (OUT).
3. **Ghidra listing defect.** The listing omits the instruction at **0x0029** (raw bytes `90 ff 68` = MOV DPTR,#0xFF68; Ghidra shows a bogus `XRL A,R0` at 0x002b instead and lists 0x0029-0x002a as a GAP). Annotated below from the raw bytes.
4. **Spurious XREFs into this range** (from force-disassembled data): XREFs onto 0x0000 from 0x0599/0x05ad/0x0604/0x0611/0x063b/0x0684 and onto 0x0009 from 0x05a7 come from the descriptor data region; the "LCALL 0x0016" at 0x0c9a is the mis-decoded VECINT table itself; "AJMP 0x0073"/"AJMP 0x00f1" at 0x012b/0x0137 are mis-decoded bytes of an (address,key) table at 0x0122+ whose big-endian entries are 0x0173/0x01f1 etc. — none of those table entries point into this range.

**IRAM state used in this range** (direct bytes and bit addresses; bits 0x08-0x0f live in byte 0x21, bit 0x2c in byte 0x25):

| loc | name | evidence |
|---|---|---|
| byte 0x0a | main_action_req — main-loop action/request code | written 0x0e by SUSR handler (0x0006), 0x0d at 0x005b; written 1,2,3,4,6,7,8,0xb,0xc elsewhere (0x0293, 0x02c5, 0x0d35...) |
| byte 0x0d | ep0_out_cmd — pending class-OUT command | 1 (0x006b), 2 (0x0063), 5 (0x024d); consumed by EP0-OUT data handler (0x0d28/0x0d45) and IEP0 handler (0x0fd8) |
| byte 0x08 | rate_state — current sample-clock state | read-only here: 1→report 0 Hz, 2→44100, 3→48000; written 1/2/3/5 at 0x0753/0x0785/0x0791/0x07bf/0x0821/0x093b (outside range) |
| bytes 0x09/0x0b | ep0_in_len lo/hi — remaining EP0 IN transfer length | zeroed on every SETUP (0x003b/0x003d); loaded with reply lengths at 0x0183/0x01a7/0x01e1; consumed as 16-bit down-counter by the EP0 IN loader fcn_0b8c (DJNZ 0x09 / DEC 0x0b) |
| bytes 0x1b:0x1c | xptr hi:lo — XDATA write pointer for composing EP0 replies | set by fcn_0b3e (=0xFA18), incremented with the INC-lo/JNZ/INC-hi idiom |
| bit 0x0b (0x21.3) | ep0_out_phase — set on SET_CUR legs (host→device data/status flow follows) | tested by IEP0 handler 0x0fc4 (JB → 0x0b77 continue-transfer path) |
| bit 0x0c (0x21.4) | ep0_in_armed — set when an IN reply was loaded | tested by IEP0 handler 0x0fca: when set, next EP0-IN completion runs status-stage cleanup (clear it, OEPCNF0\|=0x20, flush counts) |
| bit 0x0d (0x21.5) | UNKNOWN flag — cleared on every SETUP here | set at 0x0da9, cleared at 0x0d8f, tested at 0x0be1 (all outside range); role unresolved from this range |
| bit 0x2c (0x25.4) | input_src flag — set → GET-input-source replies 0x02, clear → 0x01 | also tested at 0x0485/0x049f/0x0e52/0x0ec5 (outside range). NOTES.md identifies 0x02 = S/PDIF; plausible but not independently verified here |

## Interrupt vectors and VECINT no-op stubs (0x0000-0x0025)

The 8051 vector slots double as storage for tiny USB-event handler stubs — the firmware packs one-byte RET handlers into the dead space between vectors, and the VECINT table at 0x0c93 points at them.

```
0x0000  02 0a 09     ljmp 0x0a09           ; RESET vector -> main init (outside range). XREFs from 0x0599+ are data artifacts (descriptor region force-disassembled)
0x0003  02 0d ac     ljmp 0x0dac           ; INT0 vector = USB interrupt -> USB ISR (reads VECINT 0xFFB2, dispatches via table 0x0c93)
0x0006  75 0a 0e     mov 0x0a,#0x0e        ; usb_ev_suspend (VECINT 0x16 SUSR_INT, table entry @0x0cbf): request main-loop action 0x0e
0x0009  22           ret                   ; end usb_ev_suspend. XREF from 0x05a7 is a data artifact
0x000a  32           reti                  ; INT1 body: no-op (reached via ljmp at 0x0013)
0x000b  02 10 1e     ljmp 0x101e           ; TIMER0 vector -> Timer0 ISR (0x101e: SETB bit 0x20 tick flag, TH0=0xCE reload — outside range)
0x000e  32           reti                  ; TIMER1 body: no-op (reached via ljmp at 0x001b)
0x000f  32           reti                  ; UART body: no-op (reached via ljmp at 0x0023)
0x0010  22           ret                   ; usb_ev_noop: VECINT 0x01 OEP1_INT handler (table @0x0c95: 00 10)
0x0011  22           ret                   ; usb_ev_noop: VECINT 0x02 OEP2_INT handler
0x0012  22           ret                   ; usb_ev_noop: VECINT 0x03 OEP3_INT handler
0x0013  02 00 0a     ljmp 0x000a           ; INT1 vector slot -> RETI (INT1 unused)
0x0016  22           ret                   ; usb_ev_noop: VECINT 0x04 OEP4_INT handler ("LCALL 0x0016" XREF at 0x0c9a is the mis-decoded table itself)
0x0017  22           ret                   ; usb_ev_noop: VECINT 0x05 OEP5_INT handler
0x0018  22           ret                   ; usb_ev_noop: VECINT 0x06 OEP6_INT handler
0x0019  22           ret                   ; usb_ev_noop: VECINT 0x07 OEP7_INT handler
0x001a  22           ret                   ; usb_ev_noop: VECINT 0x09 IEP1_INT handler
0x001b  02 00 0e     ljmp 0x000e           ; TIMER1 vector slot -> RETI (Timer1 unused)
0x001e  22           ret                   ; usb_ev_noop: VECINT 0x0a IEP2_INT handler
0x001f  22           ret                   ; usb_ev_noop: VECINT 0x0b IEP3_INT handler
0x0020  22           ret                   ; usb_ev_noop: VECINT 0x0c IEP4_INT handler
0x0021  22           ret                   ; usb_ev_noop: VECINT 0x0d IEP5_INT handler
0x0022  22           ret                   ; usb_ev_noop: VECINT 0x0e IEP6_INT handler
0x0023  02 00 0f     ljmp 0x000f           ; UART vector slot -> RETI (UART unused)
```

## fcn_0026 = usb_ev_setup — EP0 SETUP dispatcher (VECINT 0x12)

Called (LCALL, ends RET) from the USB ISR when a SETUP packet completes. The prologue is a hand-tightened version of TI engEp0SetupDone (UsbEng.c:219-232): STALLClrIn/OutEp0, TOGGLEIn/OutEp0Data, EMPTY both FIFOs. Helpers: fcn_0b50 = IEPCNF0&=0xF7, OEPCNF0&=0xF7 (clear STALL, cf. hwMacro.h:27-28); fcn_0b2b = MOVX-write pending A to @DPTR then IEPDCNTX0=0, OEPDCNTX0=0 (flush; A=0 on return).

```
0x0026  12 0b 50     lcall 0x0b50          ; -> fcn_0b50: clear STALL bit (0x08) in IEPCNF0 (0xFF68) and OEPCNF0 (0xFFA8) — TI STALLClrInEp0/STALLClrOutEp0, hwMacro.h:27-28
0x0029  90 ff 68     mov dptr,#0xff68      ; DPTR = IEPCNF0  [MISSING FROM GHIDRA LISTING — restored from raw bytes 90 ff 68 at file offset 0x29]
0x002c  e0           movx a,@dptr          ; read IEPCNF0
0x002d  44 20        orl a,#0x20           ; set TOGGLE bit -> next EP0 IN transaction is DATA1 (TI TOGGLEInEp0Data {IEPCNF0 |= 0x20}, hwMacro.h:46)
0x002f  f0           movx @dptr,a          ; write IEPCNF0 back
0x0030  90 ff a8     mov dptr,#0xffa8      ; DPTR = OEPCNF0
0x0033  e0           movx a,@dptr          ; read OEPCNF0
0x0034  44 20        orl a,#0x20           ; set TOGGLE bit -> next EP0 OUT transaction is DATA1 (TI TOGGLEOutEp0Data, hwMacro.h:47)
0x0036  12 0b 2b     lcall 0x0b2b          ; -> fcn_0b2b: stores A to @DPTR (completes the OEPCNF0 write), then IEPDCNTX0 (0xFF6B) = 0 and OEPDCNTX0 (0xFFAB) = 0 — flush both EP0 FIFO counts; returns with A=0
0x0039  c2 0d        clr 0x0d              ; clear flag bit 0x21.5 on every SETUP. UNKNOWN — role unresolved; set 0x0da9 / tested 0x0be1, outside this range
0x003b  f5 09        mov 0x09,a            ; ep0_in_len_lo = 0 (A=0 from fcn_0b2b) — cancel any pending EP0 IN transfer remainder
0x003d  f5 0b        mov 0x0b,a            ; ep0_in_len_hi = 0
0x003f  90 ff 28     mov dptr,#0xff28      ; DPTR = SETPACK+0 = bmRequestType (Reg_stc1.h:17; field order per UsbEng.c:243)
0x0042  e0           movx a,@dptr          ; A = bmRequestType
0x0043  24 de        add a,#0xde           ; ADD/JZ dispatch chain: 0x22+0xde==0x100 -> Z
0x0045  60 24        jz 0x006b             ; bmReq==0x22 (class, host->dev, ENDPOINT) -> setup_class_out_endpoint
0x0047  24 81        add a,#0x81           ; cumulative +0x15f: 0xa1 -> Z
0x0049  60 28        jz 0x0073             ; bmReq==0xA1 (class, dev->host, INTERFACE) -> setup_get_input_source
0x004b  14           dec a                 ; cumulative: 0xa2 -> Z
0x004c  60 3c        jz 0x008a             ; bmReq==0xA2 (class, dev->host, ENDPOINT) -> setup_get_sample_freq
0x004e  24 81        add a,#0x81           ; cumulative: 0x21 -> Z
0x0050  60 03        jz 0x0055             ; bmReq==0x21 (class, host->dev, INTERFACE) -> setup_class_out_interface
0x0052  02 01 18     ljmp 0x0118           ; anything else (all standard requests etc.) -> 0x0118 (outside range: reads bRequest, lcall 0x0f70, then LJMP 0x2f00 = boot ROM std-request handler)
```

### 0x0055: setup_class_out_interface (bmRequestType 0x21)

Only distinguishes bRequest zero vs nonzero. Nonzero (UAC SET_CUR = 0x01, Usbaudio.h:41) latches pending-command 2 and waits for the OUT data phase; the payload is consumed by the EP0 OUT handler (VECINT 0x00 -> 0x0d25, outside range), which routes on byte 0x0d.

```
0x0055  90 ff 29     mov dptr,#0xff29      ; DPTR = SETPACK+1 = bRequest
0x0058  e0           movx a,@dptr          ; A = bRequest
0x0059  70 08        jnz 0x0063            ; bRequest != 0 -> class SET with data phase
0x005b  75 0a 0d     mov 0x0a,#0x0d        ; bRequest==0: main_action_req = 0x0d — request main-loop action 0x0d. UNKNOWN — action semantics live in the main-loop dispatcher (outside range)
0x005e  c2 0b        clr 0x0b              ; no OUT data phase expected (bit 0x21.3 = 0)
0x0060  c2 0c        clr 0x0c              ; no IN reply armed (bit 0x21.4 = 0)
0x0062  22           ret                   ; back to USB ISR dispatcher (0x0dd9)
0x0063  75 0d 02     mov 0x0d,#0x2         ; ep0_out_cmd = 2: interface-targeted class SET pending; EP0 OUT data handler will parse payload under this code
0x0066  d2 0b        setb 0x0b             ; flag OUT data phase expected (bit 0x21.3); IEP0 handler 0x0fc4 routes on this
0x0068  c2 0c        clr 0x0c              ; no IN reply armed
0x006a  22           ret                   ; done
```

### 0x006b: setup_class_out_endpoint (bmRequestType 0x22)

UAC endpoint-recipient SET_CUR — for this device the sampling-frequency set. bRequest/wValue are not even checked; command code 1 is latched.

```
0x006b  75 0d 01     mov 0x0d,#0x1         ; ep0_out_cmd = 1: endpoint-targeted class SET (sample-rate) pending
0x006e  d2 0b        setb 0x0b             ; OUT data phase expected (bit 0x21.3)
0x0070  c2 0c        clr 0x0c              ; no IN reply armed
0x0072  22           ret                   ; done
```

### 0x0073: setup_get_input_source (bmRequestType 0xA1)

One-byte class GET reply: 0x02 if state bit 0x25.4 is set, else 0x01. (NOTES.md maps 0x02 = S/PDIF input, 0x01 = analog; consistent with this code but not independently re-verified here.)

```
0x0073  12 0b 3e     lcall 0x0b3e          ; -> fcn_0b3e: xptr (0x1b:0x1c) = 0xFA18 = EP0 IN buffer (IEPBBAX0=0x43 set at 0x0976: 0xF800+0x43*8)
0x0076  30 2c 08     jnb 0x2c,0x0081       ; test input_src flag (bit 0x25.4): clear -> reply 0x01
0x0079  12 0b 17     lcall 0x0b17          ; -> fcn_0b17: DPTR = xptr (0xFA18)
0x007c  74 02        mov a,#0x2            ; reply byte = 0x02 (flag set)
0x007e  f0           movx @dptr,a          ; store reply at EP0 IN buffer[0]
0x007f  80 06        sjmp 0x0087           ; -> common arm
0x0081  12 0b 17     lcall 0x0b17          ; DPTR = xptr (0xFA18)
0x0084  74 01        mov a,#0x1            ; reply byte = 0x01 (flag clear)
0x0086  f0           movx @dptr,a          ; store reply
0x0087  02 0b 45     ljmp 0x0b45           ; tail-call fcn_0b45: IEPDCNTX0 (0xFF6B) = 1 -> hand 1-byte buffer to USB engine (arm IN), CLR bit 0x21.3, SETB bit 0x21.4 (IN reply armed), RET
```

### 0x008a: setup_get_sample_freq (bmRequestType 0xA2)

Endpoint-recipient class GET. Requires control selector wValueHi == 1 = sampling-frequency control (AUD_SAMP_FREQ_CTL 0x0001, Usbaudio.h:289), else stalls EP0. Reply is 3 bytes little-endian selected by rate_state (IRAM 0x08).

```
0x008a  90 ff 2b     mov dptr,#0xff2b      ; DPTR = SETPACK+3 = wValue high byte (field order per UsbEng.c:246)
0x008d  e0           movx a,@dptr          ; A = wValueHi = UAC control selector
0x008e  64 01        xrl a,#0x1            ; compare with 1 (SAMPLING_FREQ_CONTROL)
0x0090  60 03        jz 0x0095             ; match -> build reply
0x0092  02 10 09     ljmp 0x1009           ; else -> fcn_1009: STALL EP0 (IEPCNF0|=0x08, OEPCNF0|=0x08 per hwMacro.h:9-10 semantics, flush counts, clear bits 0x21.3/0x21.4)
0x0095  12 0b 3e     lcall 0x0b3e          ; xptr = 0xFA18 (EP0 IN buffer)
0x0098  e5 08        mov a,0x08            ; A = rate_state
0x009a  b4 01 1d     cjne a,#0x1,0x00ba    ; state 1? else try state 2
; --- state 1: report 0 Hz (00 00 00) ---
0x009d  12 0b 17     lcall 0x0b17          ; DPTR = 0xFA18
0x00a0  e4           clr a                 ; byte0 = 0x00
0x00a1  f0           movx @dptr,a          ; store freq[0]
0x00a2  05 1c        inc 0x1c              ; xptr lo++
0x00a4  e5 1c        mov a,0x1c            ; A = xptr lo
0x00a6  70 02        jnz 0x00aa            ; no wrap ->
0x00a8  05 1b        inc 0x1b              ; carry into xptr hi (16-bit pointer increment idiom)
0x00aa  12 0b 36     lcall 0x0b36          ; -> fcn_0b36: DPTR={0x1b:A}, write 0 -> freq[1]=0x00
0x00ad  05 1c        inc 0x1c              ; xptr lo++
0x00af  e5 1c        mov a,0x1c            ; A = xptr lo
0x00b1  70 02        jnz 0x00b5            ; no wrap ->
0x00b3  05 1b        inc 0x1b              ; carry into xptr hi
0x00b5  12 0b 36     lcall 0x0b36          ; freq[2]=0x00 -> reply 00 00 00 = 0 Hz
0x00b8  80 53        sjmp 0x010d           ; -> arm 3-byte IN reply
; --- state 2: report 44100 Hz (44 AC 00 LE) ---
0x00ba  e5 08        mov a,0x08            ; A = rate_state
0x00bc  b4 02 23     cjne a,#0x2,0x00e2    ; state 2? else try state 3
0x00bf  12 0b 17     lcall 0x0b17          ; DPTR = 0xFA18
0x00c2  74 44        mov a,#0x44           ; byte0 = 0x44 (44100 = 0x00AC44)
0x00c4  f0           movx @dptr,a          ; store freq[0]
0x00c5  05 1c        inc 0x1c              ; xptr lo++
0x00c7  e5 1c        mov a,0x1c            ; A = xptr lo
0x00c9  70 02        jnz 0x00cd            ; no wrap ->
0x00cb  05 1b        inc 0x1b              ; carry into xptr hi
0x00cd  f5 82        mov dpl,a             ; DPL = xptr lo (inline instead of fcn_0b36 since value != 0)
0x00cf  85 1b 83     mov dph,0x1b          ; DPH = xptr hi
0x00d2  74 ac        mov a,#0xac           ; byte1 = 0xAC
0x00d4  f0           movx @dptr,a          ; store freq[1]
0x00d5  05 1c        inc 0x1c              ; xptr lo++
0x00d7  e5 1c        mov a,0x1c            ; A = xptr lo
0x00d9  70 02        jnz 0x00dd            ; no wrap ->
0x00db  05 1b        inc 0x1b              ; carry into xptr hi
0x00dd  12 0b 36     lcall 0x0b36          ; freq[2]=0x00 -> reply 44 AC 00 = 44100
0x00e0  80 2b        sjmp 0x010d           ; -> arm 3-byte IN reply
; --- state 3: report 48000 Hz (80 BB 00 LE) ---
0x00e2  e5 08        mov a,0x08            ; A = rate_state
0x00e4  b4 03 23     cjne a,#0x3,0x010a    ; state 3? else unknown state -> stall
0x00e7  12 0b 17     lcall 0x0b17          ; DPTR = 0xFA18
0x00ea  74 80        mov a,#0x80           ; byte0 = 0x80 (48000 = 0x00BB80)
0x00ec  f0           movx @dptr,a          ; store freq[0]
0x00ed  05 1c        inc 0x1c              ; xptr lo++
0x00ef  e5 1c        mov a,0x1c            ; A = xptr lo
0x00f1  70 02        jnz 0x00f5            ; no wrap -> (Ghidra XREF from 0x0137 is a data artifact: table entry 0x01f1 mis-decoded as AJMP 0x00f1)
0x00f3  05 1b        inc 0x1b              ; carry into xptr hi
0x00f5  f5 82        mov dpl,a             ; DPL = xptr lo
0x00f7  85 1b 83     mov dph,0x1b          ; DPH = xptr hi
0x00fa  74 bb        mov a,#0xbb           ; byte1 = 0xBB
0x00fc  f0           movx @dptr,a          ; store freq[1]
0x00fd  05 1c        inc 0x1c              ; xptr lo++
0x00ff  e5 1c        mov a,0x1c            ; A = xptr lo
0x0101  70 02        jnz 0x0105            ; no wrap -> (XREF from 0x0644 is a data artifact from the descriptor region)
0x0103  05 1b        inc 0x1b              ; carry into xptr hi
0x0105  12 0b 36     lcall 0x0b36          ; freq[2]=0x00 -> reply 80 BB 00 = 48000
0x0108  80 03        sjmp 0x010d           ; -> arm 3-byte IN reply
0x010a  02 10 09     ljmp 0x1009           ; rate_state not in {1,2,3} -> STALL EP0
; --- common tail: arm the 3-byte IN reply (Ghidra splits this as FUN_CODE_0110; the 0x0596 XREF is a data artifact) ---
0x010d  90 ff 6b     mov dptr,#0xff6b      ; DPTR = IEPDCNTX0 (EP0 IN data count register)
0x0110  74 03        mov a,#0x3            ; count = 3
0x0112  f0           movx @dptr,a          ; IEPDCNTX0 = 3: NACK bit (0x80) clear -> buffer handed to USB engine, 3-byte IN reply armed
0x0113  c2 0b        clr 0x0b              ; bit 0x21.3 = 0: no OUT data phase
0x0115  d2 0c        setb 0x0c             ; bit 0x21.4 = 1: IN reply armed; IEP0 handler (0x0fca) will do status-stage cleanup on completion
0x0117  22           ret                   ; end of dispatcher; range ends here (0x0118 = std-request delegation path, owned by next annotator)
```

**Control flow leaving this range:** 0x0a09 (main init), 0x0dac (USB ISR), 0x101e (Timer0 ISR), 0x0b17/0x0b2b/0x0b36/0x0b3e/0x0b45/0x0b50 (EP0 helpers, annotated above from their listings), 0x1009 (EP0 stall), 0x0118 (std-request -> boot ROM 0x2f00).

### 5.2 Range 0x0118-0x0329

# Rev 20 annotation — 0x0118..0x032A (USB standard-request handlers + deferred-event dispatch)

## Context

The EP0 SETUP handler at 0x0026 (outside this range) clears EP0 stalls, then switches on
bmRequestType (0xFF28): 0x22/0xA1/0xA2/0x21 take class-request fast paths; everything else
lands here via `LJMP 0x0118` at 0x0052. All SETUP fields are read from the memory-mapped
SETPACK block (Reg_stc1.h:17, `SETPACK` = 0xFF28): FF28 bmRequestType, FF29 bRequest,
FF2A/FF2B wValue L/H, FF2C/FF2D wIndex L/H, FF2E/FF2F wLength L/H — layout confirmed by
usage (SET_ADDRESS takes its address from FF2A, GET_DESCRIPTOR its type from FF2B, etc.).

IRAM state used throughout (bit addresses 0x08..0x0E live in IRAM byte 0x21; the same
numbers also exist as IRAM *byte* addresses — both are used, they are different cells):

| loc | kind | meaning (evidence in this range) |
|---|---|---|
| bit 0x08 | flag | interface 1 alternate setting active (set by SET_INTERFACE, read by GET_INTERFACE) |
| bit 0x09 | flag | interface 2 alternate setting active (ditto) |
| bit 0x0A | flag | gate that also permits GET/SET_INTERFACE when not configured; cleared by SET_CONFIGURATION. Setter is elsewhere (one `SETB 0x0A` in the whole image, outside this range) — precise meaning UNKNOWN |
| bit 0x0B | flag | set = OUT data phase expected (set at 0x0066 outside range); all handlers here clear it |
| bit 0x0C | flag | set = EP0 IN data phase armed (set after arming IEPDCNTX0); cleared for no-data requests |
| bit 0x0E | flag | device configured (SET_CONFIGURATION 1 sets, 0 clears; GET_CONFIGURATION reports it) |
| byte 0x09 | var | EP0 IN transfer length, low byte |
| byte 0x0B | var | EP0 IN transfer length, high byte |
| byte 0x0A | var | deferred main-loop event code 1..14, dispatched by fcn_02ee via table 0x0300 |
| byte 0x0D | var | deferred-action code (5 = apply new USB address after status stage); consumer outside range |
| byte 0x0E | var | pending USB device address (SET_ADDRESS) |
| 0x19:0x1A | ptr | descriptor source pointer in CODE space (hi:lo) |
| 0x1B:0x1C | ptr | EP0 response buffer write pointer (hi:lo), based at 0xFA18 by fcn_0b3e |

Out-of-range helpers referenced (verified by reading their listings):
fcn_0b17 = DPTR := 0x1B:0x1C (buffer ptr); fcn_0b36 = DPL:=A, DPH:=0x1B, store 0x00;
fcn_0b3e = set buffer ptr 0x1B:0x1C := 0xFA18 (EP0 buffer RAM);
fcn_0b45 = IEPDCNTX0:=1 (arm 1-byte EP0 IN), CLR 0x0B, SETB 0x0C;
fcn_0b50 = IEPCNF0 &= ~0x08, OEPCNF0 &= ~0x08 (clear EP0 STALL bits — TI hwMacro.h:9-10 STALLInEp0/STALLOutEp0 use bit3);
fcn_0b5f = IEPDCNTX0:=0x80, OEPDCNTX0:=0x80 (NAK both EP0 directions; 0x80 = NACK bit, cf. TI ROM/hwMacro.h:53 `EMPTYInEp0 {IEPDCNTX0 = 0x80;}`), CLR 0x0B/0x0C;
fcn_0b6e = A := CODE[0x19:0x1A] (descriptor bLength fetch);
fcn_0b77 = copy next EP0 chunk from CODE ptr via fcn_0b8c, then IEPDCNTX0 &= 0x7F (clear NACK, arm IN);
fcn_0d6b = clamp transfer length 0x09/0x0B to wLength (reads 0xFF2E/0xFF2F);
fcn_1009 = IEPCNF0 |= 0x08, OEPCNF0 |= 0x08 (stall EP0 both directions), zero both count regs, CLR 0x0B/0x0C.

## fcn_0118 = std_request_dispatch

```
0x0118  90 ff 29   mov dptr,#0xff29     ; DPTR -> SETPACK+1 = bRequest (Reg_stc1.h:17 SETPACK=0xFF28)
0x011b  e0         movx a,@dptr         ; A = bRequest of current SETUP packet
0x011c  12 0f 70   lcall 0x0f70         ; -> fcn_0f70 = table_dispatch: POPs this return address (0x011f) as the
                                        ;    table pointer and never returns here; entries are [addr_hi][addr_lo][key],
                                        ;    key compared to A; terminator 00 00 followed by 2-byte default address
```

## DATA 0x011F-0x0143 — bRequest dispatch table (37 bytes)

Consumed as data by fcn_0f70 (verified: 0x0f70 does `POP DPH / POP DPL` then MOVC walks).
The first 3 bytes are 02 2F 00, which Ghidra shows as `LJMP 0x2f00` — **that instruction is
never executed**; the bytes are the table entry for bRequest 0x00. The legacy claim that this
dispatcher "ends ljmp 0x2f00 (boot ROM fallback)" is a misreading of these bytes: no path in
this range reaches 0x2F00, and unknown bRequests go to the default entry 0x02EA (stall).

```
0x011f  02 2f 00   entry: bRequest 0x00 GET_STATUS        -> 0x022f
0x0122  01 44 01   entry: bRequest 0x01 CLEAR_FEATURE     -> 0x0144
0x0125  02 9c 03   entry: bRequest 0x03 SET_FEATURE       -> 0x029c (stall)
0x0128  02 4d 05   entry: bRequest 0x05 SET_ADDRESS       -> 0x024d
0x012b  01 73 06   entry: bRequest 0x06 GET_DESCRIPTOR    -> 0x0173
0x012e  02 99 07   entry: bRequest 0x07 SET_DESCRIPTOR    -> 0x0299 (stall)
0x0131  01 5d 08   entry: bRequest 0x08 GET_CONFIGURATION -> 0x015d
0x0134  02 5b 09   entry: bRequest 0x09 SET_CONFIGURATION -> 0x025b
0x0137  01 f1 0a   entry: bRequest 0x0a GET_INTERFACE     -> 0x01f1
0x013a  02 9f 0b   entry: bRequest 0x0b SET_INTERFACE     -> 0x029f
0x013d  02 e7 0c   entry: bRequest 0x0c SYNCH_FRAME       -> 0x02e7 (stall)
0x0140  00 00      terminator (both key-position bytes zero)
0x0142  02 ea      default handler address (big-endian)   -> 0x02ea (stall + ret)
```
The key set {0,1,3,5,6,7,8,9,0A,0B,0C} is exactly the USB 1.1 standard-request set (2 and 4
are reserved). Ghidra force-disassembled this region (bogus `AJMP`/`LJMP 0xe70c` etc. at
0x0122-0x0146 and bogus XREFs like "0x012b -> 0x0073"); real code resumes at 0x0144.

## fcn_0144 = std_clear_feature (bRequest 1)

Handler RETs return to the original caller of the 0x0026 SETUP routine (fcn_0f70 already
popped the 0x011F return address off the stack).

```
0x0144  90 ff 28   mov dptr,#0xff28     ; DPTR -> SETPACK+0 = bmRequestType
0x0147  e0         movx a,@dptr         ; A = bmRequestType
0x0148  64 02      xrl a,#0x2           ; test bmRequestType == 0x02 (H2D | standard | endpoint recipient)
0x014a  70 0e      jnz 0x015a           ; any other recipient/direction -> stall
0x014c  90 ff 2c   mov dptr,#0xff2c     ; DPTR -> wIndexL (endpoint address for endpoint recipient)
0x014f  e0         movx a,@dptr         ; A = wIndexL
0x0150  70 08      jnz 0x015a           ; only endpoint 0 accepted (wIndex must be 0); CLEAR_FEATURE(HALT) on
                                        ;   any other EP stalls — a firmware quirk, stated as-is
0x0152  12 0b 50   lcall 0x0b50         ; -> fcn_0b50: IEPCNF0 &= ~0x08, OEPCNF0 &= ~0x08 = un-stall EP0 both dirs
0x0155  c2 0b      clr 0x0b             ; bit 0x0B := 0, no OUT data phase
0x0157  c2 0c      clr 0x0c             ; bit 0x0C := 0, no IN data armed (status stage handled by outer machine)
0x0159  22         ret                  ; done
0x015a  02 10 09   ljmp 0x1009          ; reject path -> fcn_1009 = stall EP0 (IEPCNF0/OEPCNF0 |= 0x08)
```

## fcn_015d = std_get_configuration (bRequest 8)

Replies one byte: current configuration value (1 if configured else 0).

```
0x015d  12 0b 3e   lcall 0x0b3e         ; buffer ptr 0x1B:0x1C := 0xFA18 (EP0 response staging in USB buffer RAM)
0x0160  30 0e 08   jnb 0x0e,0x016b      ; bit 0x0E = configured flag; not set -> reply 0
0x0163  12 0b 17   lcall 0x0b17         ; DPTR := 0x1B:0x1C = 0xFA18
0x0166  74 01      mov a,#0x1           ; configuration value 1
0x0168  f0         movx @dptr,a         ; store reply byte at 0xFA18 (buffer RAM, not an SFR)
0x0169  80 05      sjmp 0x0170          ; join
0x016b  12 0b 17   lcall 0x0b17         ; DPTR := 0xFA18
0x016e  e4         clr a                ; configuration value 0 (addressed, not configured)
0x016f  f0         movx @dptr,a         ; store reply byte
0x0170  02 0b 45   ljmp 0x0b45          ; -> fcn_0b45: IEPDCNTX0 := 1 (arm 1-byte EP0 IN), CLR 0x0B, SETB 0x0C
```

## fcn_0173 = std_get_descriptor (bRequest 6)

Selects a descriptor image in CODE space into ptr 0x19:0x1A and its length into 0x09/0x0B,
then clamps to wLength and starts the EP0 IN transfer. Descriptor bytes verified in the raw
image: 0x0596 = `12 01 10 01 00 00 00 08 BA 0D 00 10 20 00 01 02 00 01` (device descriptor,
USB 1.10, ep0 8 bytes, VID 0x0DBA, PID 0x1000, bcdDevice 0x0020, iMfr 1, iProduct 2, 1 config);
0x0670 = `09 02 36 00 02 01 00 80 F0` (config descriptor, wTotalLength 54, 2 interfaces,
bus-powered w/ bit7 legacy set, 480 mA); 0x06A6 = `04 03 09 04` (string 0, LANGID 0x0409);
0x06AA = `1E 03` + UTF-16LE "Digidesign Inc"; 0x06C8 = `60 03` + UTF-16LE "Mbox USB Audio…".

```
0x0173  90 ff 2b   mov dptr,#0xff2b     ; DPTR -> wValueH = descriptor type
0x0176  e0         movx a,@dptr         ; A = descriptor type
0x0177  b4 01 10   cjne a,#0x1,0x018a   ; type != 1 (DEVICE)? -> try CONFIGURATION
0x017a  75 19 05   mov 0x19,#0x5        ; source ptr high := 0x05
0x017d  75 1a 96   mov 0x1a,#0x96       ; source ptr low := 0x96 -> device descriptor @ CODE 0x0596
0x0180  12 0b 6e   lcall 0x0b6e         ; A := CODE[0x0596] = bLength (0x12 = 18)
0x0183  f5 09      mov 0x09,a           ; transfer length low := 18
0x0185  e4         clr a                ; 0
0x0186  f5 0b      mov 0x0b,a           ; transfer length high := 0
0x0188  80 61      sjmp 0x01eb          ; -> clamp + send
0x018a  90 ff 2b   mov dptr,#0xff2b     ; DPTR -> wValueH again
0x018d  e0         movx a,@dptr         ; A = descriptor type
0x018e  64 02      xrl a,#0x2           ; test type == 2 (CONFIGURATION)
0x0190  70 1e      jnz 0x01b0           ; not config -> try STRING
0x0192  90 ff 2a   mov dptr,#0xff2a     ; DPTR -> wValueL = descriptor index
0x0195  e0         movx a,@dptr         ; A = config index
0x0196  70 18      jnz 0x01b0           ; only config index 0 served; others fall to STRING test then stall
0x0198  75 19 06   mov 0x19,#0x6        ; source ptr high := 0x06
0x019b  75 1a 70   mov 0x1a,#0x70       ; source ptr low := 0x70 -> config descriptor set @ CODE 0x0670
0x019e  85 1a 82   mov dpl,0x1a         ; DPL := 0x70
0x01a1  85 19 83   mov dph,0x19         ; DPH := 0x06 (DPTR = 0x0670)
0x01a4  74 02      mov a,#0x2           ; offset 2
0x01a6  93         movc a,@a+dptr       ; A = CODE[0x0672] = wTotalLength low (0x36 = 54)
0x01a7  f5 09      mov 0x09,a           ; transfer length low := wTotalLength.lo
0x01a9  74 03      mov a,#0x3           ; offset 3
0x01ab  93         movc a,@a+dptr       ; A = CODE[0x0673] = wTotalLength high (0x00)
0x01ac  f5 0b      mov 0x0b,a           ; transfer length high := wTotalLength.hi
0x01ae  80 3b      sjmp 0x01eb          ; -> clamp + send (serves the whole 54-byte config set)
0x01b0  90 ff 2b   mov dptr,#0xff2b     ; DPTR -> wValueH
0x01b3  e0         movx a,@dptr         ; A = descriptor type
0x01b4  64 03      xrl a,#0x3           ; test type == 3 (STRING)
0x01b6  70 30      jnz 0x01e8           ; other types (interface/endpoint/etc.) -> stall
0x01b8  90 ff 2a   mov dptr,#0xff2a     ; DPTR -> wValueL = string index
0x01bb  e0         movx a,@dptr         ; A = string index
0x01bc  70 06      jnz 0x01c4           ; index != 0 -> check 1
0x01be  75 19 06   mov 0x19,#0x6        ; index 0: ptr := 0x06A6
0x01c1  75 1a a6   mov 0x1a,#0xa6       ;   = string descriptor 0 (LANGID array, 0x0409 US English)
0x01c4  90 ff 2a   mov dptr,#0xff2a     ; re-read string index
0x01c7  e0         movx a,@dptr         ; A = index
0x01c8  b4 01 06   cjne a,#0x1,0x01d1   ; index != 1 -> check 2
0x01cb  75 19 06   mov 0x19,#0x6        ; index 1: ptr := 0x06AA
0x01ce  75 1a aa   mov 0x1a,#0xaa       ;   = "Digidesign Inc" (iManufacturer)
0x01d1  90 ff 2a   mov dptr,#0xff2a     ; re-read string index
0x01d4  e0         movx a,@dptr         ; A = index
0x01d5  b4 02 06   cjne a,#0x2,0x01de   ; index != 2 -> fall through with ptr unchanged
0x01d8  75 19 06   mov 0x19,#0x6        ; index 2: ptr := 0x06C8
0x01db  75 1a c8   mov 0x1a,#0xc8       ;   = "Mbox USB Audio..." (iProduct)
0x01de  12 0b 6e   lcall 0x0b6e         ; A := CODE[ptr] = bLength. QUIRK: string index >= 3 reaches here with
                                        ;   0x19:0x1A holding the pointer from a PREVIOUS request (stale) —
                                        ;   firmware replies with stale descriptor data instead of stalling
0x01e1  f5 09      mov 0x09,a           ; transfer length low := bLength
0x01e3  e4         clr a                ; 0
0x01e4  f5 0b      mov 0x0b,a           ; transfer length high := 0
0x01e6  80 03      sjmp 0x01eb          ; -> clamp + send
0x01e8  02 10 09   ljmp 0x1009          ; unsupported descriptor type -> stall EP0
0x01eb  12 0d 6b   lcall 0x0d6b         ; -> fcn_0d6b: clamp 0x09/0x0B to wLength (reads 0xFF2E/0xFF2F), adjusts
                                        ;   short-transfer state so we never send more than the host asked for
0x01ee  02 0b 77   ljmp 0x0b77          ; -> fcn_0b77: copy first chunk CODE[0x19:0x1A] -> 0xFA18.. and clear
                                        ;   IEPDCNTX0 NACK bit = arm EP0 IN; continuation handled by EP0 machine
```

## fcn_01f1 = std_get_interface (bRequest 0x0A)

```
0x01f1  20 0a 03   jb 0x0a,0x01f7       ; bit 0x0A set -> allowed even if not configured. UNKNOWN — the single
                                        ;   SETB of bit 0x0A lives outside this range; exact meaning unresolved
0x01f4  30 0e 35   jnb 0x0e,0x022c      ; not configured (bit 0x0E clear) and bit 0x0A clear -> stall
0x01f7  90 ff 2c   mov dptr,#0xff2c     ; DPTR -> wIndexL = interface number
0x01fa  e0         movx a,@dptr         ; A = interface number
0x01fb  d3         setb cy              ; CY := 1 for the SUBB idiom below
0x01fc  94 02      subb a,#0x2          ; A := iface - 3 (A - 2 - CY)
0x01fe  50 2c      jnc 0x022c           ; iface >= 3 -> stall (device exposes interfaces 0..2)
0x0200  12 0b 3e   lcall 0x0b3e         ; buffer ptr 0x1B:0x1C := 0xFA18 (does NOT touch DPTR)
0x0203  e0         movx a,@dptr         ; A = wIndexL again (DPTR still 0xFF2C)
0x0204  b4 01 0b   cjne a,#0x1,0x0212   ; interface != 1 -> check interface 2
0x0207  30 08 08   jnb 0x08,0x0212      ; iface 1 but alt flag bit 0x08 clear -> reply 0 via join
0x020a  12 0b 17   lcall 0x0b17         ; DPTR := 0xFA18
0x020d  74 01      mov a,#0x1           ; bAlternateSetting = 1
0x020f  f0         movx @dptr,a         ; store reply byte at 0xFA18 (buffer RAM)
0x0210  80 17      sjmp 0x0229          ; -> arm reply
0x0212  90 ff 2c   mov dptr,#0xff2c     ; DPTR -> wIndexL
0x0215  e0         movx a,@dptr         ; A = interface number
0x0216  b4 02 0b   cjne a,#0x2,0x0224   ; interface != 2 -> reply 0
0x0219  30 09 08   jnb 0x09,0x0224      ; iface 2 but alt flag bit 0x09 clear -> reply 0
0x021c  12 0b 17   lcall 0x0b17         ; DPTR := 0xFA18
0x021f  74 02      mov a,#0x2           ; reply value 2 for iface 2 alt-active. UNKNOWN — SET_INTERFACE only
                                        ;   accepts alt 0/1, yet this reports 2; whether iface 2's descriptor
                                        ;   really numbers its alt as 2 needs the descriptor set to confirm
0x0221  f0         movx @dptr,a         ; store reply byte
0x0222  80 05      sjmp 0x0229          ; -> arm reply
0x0224  12 0b 17   lcall 0x0b17         ; DPTR := 0xFA18
0x0227  e4         clr a                ; bAlternateSetting = 0
0x0228  f0         movx @dptr,a         ; store reply byte
0x0229  02 0b 45   ljmp 0x0b45          ; -> fcn_0b45: IEPDCNTX0 := 1, arm 1-byte EP0 IN reply
0x022c  02 10 09   ljmp 0x1009          ; reject -> stall EP0
```

## fcn_022f = std_get_status (bRequest 0)

Reached via table entry key 0x00 (the `02 2F 00` bytes at 0x011F). Always replies 0x0000.

```
0x022f  12 0b 3e   lcall 0x0b3e         ; buffer ptr 0x1B:0x1C := 0xFA18
0x0232  12 0b 17   lcall 0x0b17         ; DPTR := 0xFA18
0x0235  e4         clr a                ; 0
0x0236  f0         movx @dptr,a         ; status low byte := 0x00 at 0xFA18
0x0237  05 1c      inc 0x1c             ; buffer ptr low++
0x0239  e5 1c      mov a,0x1c           ; A = new ptr low
0x023b  70 02      jnz 0x023f           ; no carry out of low byte
0x023d  05 1b      inc 0x1b             ; carry into ptr high (16-bit increment)
0x023f  12 0b 36   lcall 0x0b36         ; DPL:=A(0x19), DPH:=0x1B -> store 0x00 at 0xFA19 (status high byte)
0x0242  90 ff 6b   mov dptr,#0xff6b     ; DPTR -> IEPDCNTX0 (Reg_stc1.h:139, EP0 IN data count)
0x0245  74 02      mov a,#0x2           ; count = 2, NACK bit clear
0x0247  f0         movx @dptr,a         ; IEPDCNTX0 := 0x02 — arm 2-byte EP0 IN reply (0x0000)
0x0248  c2 0b      clr 0x0b             ; no OUT data phase
0x024a  d2 0c      setb 0x0c            ; IN data phase armed
0x024c  22         ret                  ; note: 0x0000 returned for ALL recipients — no remote-wakeup,
                                        ;   self-powered, or endpoint-halt reporting
```

## fcn_024d = std_set_address (bRequest 5)

```
0x024d  75 0d 05   mov 0x0d,#0x5        ; IRAM byte 0x0D := 5 = deferred-action code "apply new USB address"
                                        ;   (address must take effect only after the status stage; consumer of
                                        ;   code 5 is outside this range)
0x0250  90 ff 2a   mov dptr,#0xff2a     ; DPTR -> wValueL = new device address
0x0253  e0         movx a,@dptr         ; A = new address
0x0254  f5 0e      mov 0x0e,a           ; IRAM byte 0x0E := pending address (distinct from BIT 0x0E = configured)
0x0256  c2 0b      clr 0x0b             ; no OUT data phase
0x0258  c2 0c      clr 0x0c             ; no IN data phase (zero-length status handled by outer machine)
0x025a  22         ret                  ; done
```

## fcn_025b = std_set_configuration (bRequest 9)

```
0x025b  90 ff 2a   mov dptr,#0xff2a     ; DPTR -> wValueL = requested configuration
0x025e  e0         movx a,@dptr         ; A = config value
0x025f  d3         setb cy              ; SUBB idiom
0x0260  94 01      subb a,#0x1          ; A := config - 2
0x0262  40 03      jc 0x0267            ; config <= 1 -> accept
0x0264  02 10 09   ljmp 0x1009          ; config >= 2 -> stall (device has one configuration)
0x0267  90 ff 2a   mov dptr,#0xff2a     ; re-read config value
0x026a  e0         movx a,@dptr         ; A = config
0x026b  70 08      jnz 0x0275           ; nonzero -> skip deconfigure block
0x026d  c2 0a      clr 0x0a             ; config 0: clear bit 0x0A (interface-request gate)
0x026f  c2 0e      clr 0x0e             ;   clear configured flag
0x0271  c2 08      clr 0x08             ;   clear iface1 alt flag
0x0273  c2 09      clr 0x09             ;   clear iface2 alt flag  -> back to addressed state
0x0275  90 ff 2a   mov dptr,#0xff2a     ; re-read config value
0x0278  e0         movx a,@dptr         ; A = config
0x0279  b4 01 08   cjne a,#0x1,0x0284   ; != 1 -> skip
0x027c  c2 0a      clr 0x0a             ; config 1: clear bit 0x0A
0x027e  d2 0e      setb 0x0e            ;   set configured flag
0x0280  c2 08      clr 0x08             ;   iface1 alt := 0 (default)
0x0282  c2 09      clr 0x09             ;   iface2 alt := 0 (default)
0x0284  90 ff 2a   mov dptr,#0xff2a     ; re-read config value
0x0287  e0         movx a,@dptr         ; A = config
0x0288  b4 02 08   cjne a,#0x2,0x0293   ; != 2 -> skip. DEAD CODE: config 2 already stalled at 0x0262
0x028b  c2 0a      clr 0x0a             ; (unreachable) same flag pattern as config 1
0x028d  d2 0e      setb 0x0e            ; (unreachable)
0x028f  c2 08      clr 0x08             ; (unreachable)
0x0291  c2 09      clr 0x09             ; (unreachable)
0x0293  75 0a 01   mov 0x0a,#0x1        ; IRAM BYTE 0x0A := 1 — queue main-loop event 1 "configuration changed"
                                        ;   (dispatched by fcn_02ee -> handler 0x032A)
0x0296  02 0b 5f   ljmp 0x0b5f          ; -> fcn_0b5f: IEPDCNTX0:=0x80, OEPDCNTX0:=0x80 (NAK both EP0 dirs,
                                        ;   TI EMPTYInEp0 pattern), CLR 0x0B/0x0C; status stage by outer machine
```

## 0x0299 / 0x029C — unsupported-request stubs

```
0x0299  02 10 09   ljmp 0x1009          ; SET_DESCRIPTOR (7): unsupported -> stall EP0
0x029c  02 10 09   ljmp 0x1009          ; SET_FEATURE (3): unsupported -> stall EP0 (no remote wakeup / test mode)
```

## fcn_029f = std_set_interface (bRequest 0x0B)

```
0x029f  90 ff 2c   mov dptr,#0xff2c     ; DPTR -> wIndexL = interface number
0x02a2  e0         movx a,@dptr         ; A = interface
0x02a3  d3         setb cy              ; SUBB idiom
0x02a4  94 02      subb a,#0x2          ; A := iface - 3
0x02a6  50 3c      jnc 0x02e4           ; iface >= 3 -> stall
0x02a8  90 ff 2a   mov dptr,#0xff2a     ; DPTR -> wValueL = alternate setting
0x02ab  e0         movx a,@dptr         ; A = alt
0x02ac  94 01      subb a,#0x1          ; CY==1 from the taken-borrow path above, so A := alt - 2
0x02ae  50 34      jnc 0x02e4           ; alt >= 2 -> stall (only alt 0 and 1 accepted)
0x02b0  20 0a 03   jb 0x0a,0x02b6       ; bit 0x0A set -> allowed pre-configuration (same UNKNOWN gate as
                                        ;   GET_INTERFACE; setter outside range)
0x02b3  30 0e 2e   jnb 0x0e,0x02e4      ; else require configured, otherwise stall
0x02b6  90 ff 2c   mov dptr,#0xff2c     ; DPTR -> wIndexL
0x02b9  e0         movx a,@dptr         ; A = interface
0x02ba  b4 01 0d   cjne a,#0x1,0x02ca   ; not interface 1 -> check interface 2
0x02bd  90 ff 2a   mov dptr,#0xff2a     ; DPTR -> wValueL (alt)
0x02c0  e0         movx a,@dptr         ; A = alt
0x02c1  24 ff      add a,#0xff          ; CY := (alt != 0)
0x02c3  92 08      mov 0x08,c           ; bit 0x08 := iface1 non-zero alt active (streaming alt selected)
0x02c5  75 0a 02   mov 0x0a,#0x2        ; queue main-loop event 2 "iface1 alt changed" (handler 0x0386)
0x02c8  80 17      sjmp 0x02e1          ; -> finish
0x02ca  90 ff 2c   mov dptr,#0xff2c     ; DPTR -> wIndexL
0x02cd  e0         movx a,@dptr         ; A = interface
0x02ce  b4 02 0d   cjne a,#0x2,0x02de   ; not interface 2 either -> stall (SET_INTERFACE on iface 0 rejected)
0x02d1  90 ff 2a   mov dptr,#0xff2a     ; DPTR -> wValueL (alt)
0x02d4  e0         movx a,@dptr         ; A = alt
0x02d5  24 ff      add a,#0xff          ; CY := (alt != 0)
0x02d7  92 09      mov 0x09,c           ; bit 0x09 := iface2 non-zero alt active
0x02d9  75 0a 03   mov 0x0a,#0x3        ; queue main-loop event 3 "iface2 alt changed" (handler 0x03FD)
0x02dc  80 03      sjmp 0x02e1          ; -> finish
0x02de  02 10 09   ljmp 0x1009          ; interface 0 (or fall-through) -> stall
0x02e1  02 0b 5f   ljmp 0x0b5f          ; -> fcn_0b5f: NAK both EP0 count regs, CLR 0x0B/0x0C
0x02e4  02 10 09   ljmp 0x1009          ; parameter-check reject path -> stall EP0
```

## 0x02E7 / 0x02EA — SYNCH_FRAME stub and table default

```
0x02e7  02 10 09   ljmp 0x1009          ; SYNCH_FRAME (0x0C): unsupported (no adaptive iso EPs) -> stall
0x02ea  12 10 09   lcall 0x1009         ; DEFAULT table entry (unknown standard bRequest): stall EP0...
0x02ed  22         ret                  ; ...then return. NOT a boot-ROM 0x2F00 delegation.
```

Prose: every handler above ends by RET / LJMP-to-helper-that-RETs; because fcn_0f70 popped
the dispatcher's return address, control returns to whatever LCALLed the 0x0026 SETUP
routine, with bits 0x0B/0x0C telling that outer EP0 machine whether an OUT data phase is
expected or an IN reply was armed.

## fcn_02ee = device_event_dispatch

Called from the main loop (XREFs 0x0ADA, 0x0AF9, 0x0B0A). Consumes the deferred event code
that the SETUP handlers left in IRAM byte 0x0A, so heavyweight reconfiguration (DMA/codec
setup) runs outside interrupt/SETUP context.

```
0x02ee  e5 0a      mov a,0x0a           ; A = event code (IRAM BYTE 0x0A): 1..14 valid
0x02f0  14         dec a                ; A = code - 1 (0-based index; code 0 wraps to 0xFF)
0x02f1  b4 0e 00   cjne a,#0xe,0x02f4   ; compare only: CY := (code-1 < 14); both outcomes fall through
0x02f4  40 03      jc 0x02f9            ; in range 0..13 -> jump-table dispatch
0x02f6  02 05 64   ljmp 0x0564          ; code 0 (idle) or >14 -> 0x0564 (outside range — its annotator's turf)
0x02f9  90 03 00   mov dptr,#0x300      ; DPTR = base of LJMP trampoline table
0x02fc  f8         mov r0,a             ; R0 = index
0x02fd  28         add a,r0             ; A = 2*index
0x02fe  28         add a,r0             ; A = 3*index (each table slot is a 3-byte LJMP)
0x02ff  73         jmp @a+dptr          ; jump into table slot -> LJMP to the event handler
```

## 0x0300-0x0329 — event LJMP trampoline table (code, executed via JMP @A+DPTR)

These are real, executed LJMP instructions (bytes verified: 02 03 2a 02 03 86 ...). All
targets are past 0x032A and belong to other annotators; event-source attribution is from
the byte-0x0A writers found in this range plus 0x005B (event 13, seen in caller context).

```
0x0300  02 03 2a   ljmp 0x032a          ; event 1  = SET_CONFIGURATION applied (queued at 0x0293)
0x0303  02 03 86   ljmp 0x0386          ; event 2  = SET_INTERFACE iface1 alt change (queued at 0x02c5)
0x0306  02 03 fd   ljmp 0x03fd          ; event 3  = SET_INTERFACE iface2 alt change (queued at 0x02d9)
0x0309  02 04 54   ljmp 0x0454          ; event 4  — queued outside this range; handler 0x0454
0x030c  02 04 66   ljmp 0x0466          ; event 5  — queued outside this range; handler 0x0466
0x030f  02 04 78   ljmp 0x0478          ; event 6  — queued outside this range; handler 0x0478
0x0312  02 04 80   ljmp 0x0480          ; event 7  — queued outside this range; handler 0x0480
0x0315  02 04 9a   ljmp 0x049a          ; event 8  — queued outside this range; handler 0x049a
0x0318  02 04 b4   ljmp 0x04b4          ; event 9  — queued outside this range; handler 0x04b4
0x031b  02 04 bc   ljmp 0x04bc          ; event 10 — queued outside this range; handler 0x04bc
0x031e  02 04 c4   ljmp 0x04c4          ; event 11 — queued outside this range; handler 0x04c4
0x0321  02 05 11   ljmp 0x0511          ; event 12 — queued outside this range; handler 0x0511
0x0324  02 05 18   ljmp 0x0518          ; event 13 = class H2D-iface bRequest 0 (queued at 0x005b); handler 0x0518
0x0327  02 05 26   ljmp 0x0526          ; event 14 — queued outside this range; handler 0x0526
```

Range ends at 0x0329 (last byte of the LJMP at 0x0327); 0x032A = event-1 handler, next range.

## Corrections to existing notes

1. "Dispatcher ends `ljmp 0x2f00` (boot ROM std-request fallback)": the `02 2F 00` bytes at
   0x011F are table DATA for fcn_0f70 (bRequest 0x00 -> 0x022F). fcn_0f70 pops the return
   address, so the "instruction" is never executed; unknown bRequests stall via 0x02EA.
2. Ghidra's instructions at 0x0122-0x0146 (AJMPs, `LJMP 0xe70c`, `LJMP 0xea90`, `ADD A,R0`)
   are force-disassembled table data; real code resumes at 0x0144. XREFs "from 0x0596/0x0644"
   into 0x0110/0x0101 are likewise bogus (those sources are descriptor data).

### 5.3 Range 0x032A-0x0517

# Rev 20 annotation — 0x032a..0x0517 (deferred-command handler bodies, cases 1-12)

## Context (verified against listing + raw image)

This whole range is the case bodies of the **deferred USB-command dispatcher**.
`fcn_02ee` (owned by the previous range) does `A = IRAM[0x0a] - 1`, bounds-checks
against 14, and `JMP @A+DPTR` into the 14-entry LJMP table at 0x0300-0x0329.
IRAM direct byte **0x0a** is a command code queued by the SETUP/vendor-request
handlers (`mov 0x0a,#1/2/3` at 0x0293/0x02c5/0x02d9, `#7` at 0x0d35, `#0x0b/0x0c`
at 0x0af6/0x0b07, `#0x0d` at 0x005b, `#0x0e` at 0x0006) and consumed from the main
loop. Every case ends at `0x0564`, which does `clr a; mov 0x0a,a; ret`
(command consumed).

Cases 13 (0x0518) and 14 (0x0526) are past this range's upper bound.

### IRAM state used in this range (bit addresses are in bytes 0x21-0x25)

| loc | kind | role (evidence) |
|---|---|---|
| direct 0x0a | byte | pending deferred-command code; cleared at 0x0564 |
| direct 0x08 | byte | current codec-port mode id (written 1/2/3 inside fcn_0728 at 0x0753/0x0785/0x0821) |
| direct 0x22 | byte | shadow of bit-banged control word A, shifted out on P1 by fcn_0f0c; its bits are bit-addrs 0x10-0x17 |
| direct 0x23 | byte | shadow of control word B, shifted out on P1 by fcn_0e62; bits are bit-addrs 0x18-0x1f |
| direct 0x2c/0x2d | bytes | scratch (reg,val) argument pair for fcn_0c45 external-chip writes (dual use with the bits below — different address spaces) |
| bit 0x08 (0x21.0) | bit | interface-1 alt setting != 0 (set from wValueL at 0x02c3, `mov 0x08,cy`) |
| bit 0x09 (0x21.1) | bit | interface-2 alt setting != 0 (set at 0x02d7) |
| bit 0x0a (0x21.2) | bit | mode flag that is **never set anywhere in the image** (grep for opcodes d20a/920a and for direct writes to byte 0x21 both come up empty) — all `jb 0x0a` paths are dead at runtime; likely a leftover third clock mode |
| bit 0x0e (0x21.6) | bit | clock/config-mode-selected flag: set at 0x027e/0x028d when SETUP wValueL (SETPACK 0xFF2A) is 1 or 2, cleared when 0; also cleared at 0x09f7-region init and 0x0f63 |
| bit 0x16 (0x22.6) | bit | control-latch line; set by cmd4, cleared by cmd5 and on cmd11 EEPROM-test pass; hw meaning UNKNOWN |
| bit 0x17 (0x22.7) | bit | control-latch line; cleared on stream start, set on stream stop; hw meaning UNKNOWN |
| bits 0x10/0x13 (0x22.0/.3) | bits | control-latch lines, cleared on stream start, set in the 0x0e27/0x0e9d monitor region (out of range) |
| bit 0x1e (0x23.6) | bit | control word B line, cleared on stream start, set at 0x0941 and 0x102e |
| bit 0x2c (0x25.4) | bit | mode-variant flag: cleared by cmd4, set by cmd5, selects which external-chip register set cmd7/cmd8 program |
| bit 0x2d (0x25.5) | bit | cleared on stream start (0x0395/0x041c), set by cmd11 (0x04ca); consulted at 0x0e5c/0x0ecf; meaning UNKNOWN |
| bit 0x2e (0x25.6) | bit | "hardware init done" flag — set inside fcn_080b (0x0810); every caller guards `jb 0x2e` before calling fcn_080b |
| bits 0x28-0x2b (0x25.0-.3) | bits | status flags cleared on stream start; set in monitor code 0x0e27/0x0e9d (out of range) |

### Out-of-range helpers called from here (each body read and verified)

- `fcn_1001` — DMACTL0 &= 0x7F (clear DMAEN, stop DMA channel 0)
- `fcn_0fea` — IEPDCNTX0(0xFF6B)=0 and OEPDCNTX0(0xFFAB)=0: EP0 IN count=0/NACK clear (ZLP ready) + EP0 OUT re-armed — control-transfer status/re-arm
- `fcn_0ff4` — writes A to @DPTR (caller preloads CPTCNF3) and to CPTRXCNF3(0xFFD5), then GLOBCTL |= 0x01 (CPTEN — codec port enable)
- `fcn_0728(R7=mode)` — codec-port/ACG mode configure; stores mode in IRAM 0x08
- `fcn_080b` — one-time hardware init (delay loops, ACGCTL |= 0xC0, latch shifts); sets bit 0x2e
- `fcn_0e62` — bit-bangs control word B (IRAM 0x23) out on P1
- `fcn_0f0c` — bit-bangs control word A (IRAM 0x22) out on P1
- `fcn_0c45(R7=reg,R5=val)` — bit-banged 16-bit register write to an external chip on P1 (chip identity not resolvable from this range)
- `fcn_0bee(R7=addrH,R5=addrL,R3=data)` — I2C write: I2CADR(0xFFC3)=0xA0 (7-bit 0x50 = boot EEPROM), sends 2-byte address then data
- `fcn_0cdd(R7=addrH,R5=addrL)` — I2C read from device 0xA0, returns byte in R7
- `fcn_0568` — ext-chip writes (0x04,0x41) then (0x12,0x00) via fcn_0c45
- `fcn_0582` — ext-chip write (IRAM 0x2c, IRAM 0x2d) then (0x24,0x80) via fcn_0c45
- `0x0564` — common epilogue: `clr a; mov 0x0a,a; ret`

Datasheet facts used below (extracted from sles025b text): DMACTL0/1 bit7 = DMAEN;
GLOBCTL bit0 = CPTEN, bit2 = LPWR; I/OEPCNFx for isochronous EPs =
{7:EPEN, 6:ISO, 5:OVF, 4..0:BPS}, BPS 0x05 = 6 bytes/sample (= 2ch x 24-bit);
CPTCNF3 = {7:DDLY,6:TRSEN,5:CSCLKP,4:CSYNCP,3:CSYNCL,2:BYOR,1:CSCLKD,0:CSYNCD};
USBIMSK = {7:RSTR,6:SUSR,5:RESR,4:SOF,3:PSOF,2:SETUP,1:-,0:STPOW}.

---

## fcn_032a = cmd1_apply_clock_mode  (jump-table slot 0x0300; queued at 0x0293 after the wValueL(0xFF2A)∈{0,1,2} handler that sets/clears bit 0x0e)

Quiesces the audio path (both DMA channels off, codec port off), then either
re-programs the codec-port byte order for the selected mode and re-enables it,
or (mode deselected) marks hardware for re-init.

```
0x032a  12 10 01   lcall 0x1001        ; fcn_1001: DMACTL0 &= 0x7F — clear DMAEN, stop DMA ch0 (playback/OUT-EP2 path)
0x032d  90 ff ee   mov dptr,#0xffee    ; DPTR -> DMACTL1
0x0330  e0         movx a,@dptr        ; read DMACTL1
0x0331  54 7f      anl a,#0x7f         ; clear bit7 = DMAEN
0x0333  f0         movx @dptr,a        ; DMACTL1 &= 0x7F — stop DMA ch1 (record/IN-EP1 path)
0x0334  90 ff b1   mov dptr,#0xffb1    ; DPTR -> GLOBCTL
0x0337  e0         movx a,@dptr        ; read GLOBCTL
0x0338  54 fe      anl a,#0xfe         ; clear bit0 = CPTEN
0x033a  f0         movx @dptr,a        ; GLOBCTL &= 0xFE — disable codec port (required before rewriting CPT config regs, datasheet §6.5.4)
0x033b  20 0a 03   jb 0x0a,0x0341      ; if dead flag 0x21.2 set (never set in image) -> stream check
0x033e  30 0e 24   jnb 0x0e,0x0365     ; if no clock mode selected (bit 0x0e clear) -> deselect branch at 0x0365
0x0341  20 08 21   jb 0x08,0x0365      ; iface-1 stream active -> don't touch CPTCNF3 mid-stream, skip to 0x0365
0x0344  20 09 1e   jb 0x09,0x0365      ; iface-2 stream active -> same skip
0x0347  30 0a 08   jnb 0x0a,0x0352     ; dead-flag variant: skipped at runtime (bit 0x0a never set)
0x034a  90 ff de   mov dptr,#0xffde    ; DPTR -> CPTCNF3 (dead path)
0x034d  74 ac      mov a,#0xac         ; 0xAC = DDLY|CSCLKP|CSYNCL|BYOR — byte-order-reversed variant (dead path)
0x034f  12 0f f4   lcall 0x0ff4        ; fcn_0ff4: CPTCNF3=0xAC, CPTRXCNF3=0xAC, GLOBCTL|=CPTEN (dead path)
0x0352  30 0e 08   jnb 0x0e,0x035d     ; if mode flag clear skip the live config write
0x0355  90 ff de   mov dptr,#0xffde    ; DPTR -> CPTCNF3
0x0358  74 a8      mov a,#0xa8         ; 0xA8 = DDLY|CSCLKP|CSYNCL, BYOR=0 — data delay 1 clk, CSCLK neg-edge, long CSYNC, normal byte order
0x035a  12 0f f4   lcall 0x0ff4        ; fcn_0ff4: CPTCNF3=0xA8 and CPTRXCNF3=0xA8, then GLOBCTL |= 0x01 (CPTEN back on)
0x035d  20 2e 20   jb 0x2e,0x0380      ; hardware already initialized once (bit 0x2e) -> skip init
0x0360  12 08 0b   lcall 0x080b        ; fcn_080b: one-time hw init (ACGCTL|=0xC0, latch shifts, delays); sets bit 0x2e
0x0363  80 1b      sjmp 0x0380         ; -> common exit
0x0365  20 0a 03   jb 0x0a,0x036b      ; mode-deselected / stream-active branch: recompute (bit0x0a || bit0x0e)
0x0368  30 0e 06   jnb 0x0e,0x0371     ; neither flag -> R7=0
0x036b  7e 00      mov r6,#0x0         ; flag set: R6:R7 = 0:1  (bool true)
0x036d  7f 01      mov r7,#0x1         ;
0x036f  80 04      sjmp 0x0375         ;
0x0371  7e 00      mov r6,#0x0         ; flag clear: R6:R7 = 0:0 (bool false)
0x0373  7f 00      mov r7,#0x0         ;
0x0375  ef         mov a,r7            ; test bool
0x0376  64 01      xrl a,#0x1          ; A = R7 ^ 1
0x0378  4e         orl a,r6            ; A==0 iff (R6,R7)==(0,1), i.e. a mode IS selected
0x0379  60 05      jz 0x0380           ; mode selected -> nothing more to do
0x037b  c2 2e      clr 0x2e            ; mode deselected: clear "hw init done" so next mode select re-runs fcn_080b
0x037d  12 0e 62   lcall 0x0e62        ; shift control word B (IRAM 0x23) out to the P1 bit-bang latch
0x0380  12 0f ea   lcall 0x0fea        ; fcn_0fea: IEPDCNTX0=0 (EP0 IN ZLP ready) + OEPDCNTX0=0 (re-arm EP0 OUT) — completes control transfer
0x0383  02 05 64   ljmp 0x0564         ; epilogue: clear pending command byte 0x0a, ret
```

## fcn_0386 = cmd2_apply_iface1_alt  (slot 0x0303; queued at 0x02c5 when SET_INTERFACE-style request has wIndexL==1; bit 0x08 = alt!=0)

Start path requires a clock mode selected (bit 0x0e — the 0x029f handler also
stalls the request outright if bit 0x0e is clear) AND alt!=0. Note the start
path enables the **IN EP1** stream (DMACTL1); with bit 0x0e it additionally
enables **OUT EP2** (DMACTL0). Stop path disables DMA ch1 (and ch0 if 0x0e).

```
0x0386  20 0a 03   jb 0x0a,0x038c      ; (dead flag) -> alt check
0x0389  30 0e 4a   jnb 0x0e,0x03d6     ; no clock mode selected -> stop/stall branch
0x038c  30 08 47   jnb 0x08,0x03d6     ; alt==0 -> stop branch
0x038f  20 2e 03   jb 0x2e,0x0395      ; hw already initialized -> skip
0x0392  12 08 0b   lcall 0x080b        ; one-time hw init (sets bit 0x2e)
0x0395  c2 2d      clr 0x2d            ; clear flag 0x25.5 on stream start; consulted by monitor code 0x0e5c/0x0ecf — meaning UNKNOWN
0x0397  75 22 ff   mov 0x22,#0xff      ; control word A := 0xFF (all latch lines high) — line functions UNKNOWN
0x039a  c2 10      clr 0x10            ; clear latch line 0x22.0 (re-set by monitor code 0x0e2e) — hw meaning UNKNOWN
0x039c  c2 13      clr 0x13            ; clear latch line 0x22.3 (re-set at 0x0ea4) — hw meaning UNKNOWN
0x039e  c2 1e      clr 0x1e            ; clear control-word-B line 0x23.6 (set at 0x0941/0x102e) — hw meaning UNKNOWN
0x03a0  c2 17      clr 0x17            ; clear latch line 0x22.7 (set again on stream stop at 0x03e6) — hw meaning UNKNOWN
0x03a2  12 0f 0c   lcall 0x0f0c        ; shift control word A (0x22) out on P1 bit-bang
0x03a5  c2 28      clr 0x28            ; clear status flag 0x25.0 (set by monitor code 0x0e27 region)
0x03a7  c2 29      clr 0x29            ; clear status flag 0x25.1 (set at 0x0ea0)
0x03a9  c2 2a      clr 0x2a            ; clear status flag 0x25.2 (set at 0x0e2c)
0x03ab  c2 2b      clr 0x2b            ; clear status flag 0x25.3 (set at 0x0ea2)
0x03ad  c2 2c      clr 0x2c            ; clear mode-variant flag 0x25.4 (the cmd4/cmd5 toggle)
0x03af  12 0e 62   lcall 0x0e62        ; shift control word B (0x23) out
0x03b2  90 ff 60   mov dptr,#0xff60    ; DPTR -> IEPCNF1 (per Reg_stc1.h, IN EP1 config)
0x03b5  74 c5      mov a,#0xc5         ; 0xC5 = EPEN|ISO|BPS=5: enabled isochronous IN EP, 6 bytes/sample (stereo 24-bit)
0x03b7  f0         movx @dptr,a        ; IEPCNF1 = 0xC5 — enable iso IN endpoint 1
0x03b8  7f 03      mov r7,#0x3         ; mode arg = 3
0x03ba  12 07 28   lcall 0x0728        ; fcn_0728(3): configure codec port / ACG for streaming mode 3 (sets IRAM 0x08=3)
0x03bd  90 ff ee   mov dptr,#0xffee    ; DPTR -> DMACTL1
0x03c0  e0         movx a,@dptr        ; read
0x03c1  44 80      orl a,#0x80         ; set DMAEN
0x03c3  f0         movx @dptr,a        ; DMACTL1 |= 0x80 — start DMA ch1 (EP assignment in its EPNUM field, programmed in init code out of range)
0x03c4  30 0e 2a   jnb 0x0e,0x03f1     ; if clock-mode flag clear, skip the OUT-side enable (dead guard here: we required 0x0e above unless via dead 0x0a path)
0x03c7  90 ff 98   mov dptr,#0xff98    ; DPTR -> OEPCNF2 (OUT EP2 config)
0x03ca  74 c5      mov a,#0xc5         ; iso OUT EP, enabled, 6 bytes/sample
0x03cc  f0         movx @dptr,a        ; OEPCNF2 = 0xC5 — enable iso OUT endpoint 2
0x03cd  90 ff e8   mov dptr,#0xffe8    ; DPTR -> DMACTL0
0x03d0  e0         movx a,@dptr        ; read
0x03d1  44 80      orl a,#0x80         ; set DMAEN
0x03d3  f0         movx @dptr,a        ; DMACTL0 |= 0x80 — start DMA ch0 (OUT/playback path)
0x03d4  80 1b      sjmp 0x03f1         ; -> USBIMSK + exit
0x03d6  20 0a 03   jb 0x0a,0x03dc      ; stop branch: (bit0x0a || bit0x0e)?
0x03d9  30 0e 15   jnb 0x0e,0x03f1     ; no mode selected at all -> just re-arm EP0 and exit
0x03dc  20 08 12   jb 0x08,0x03f1      ; alt!=0 (shouldn't happen on this branch) -> skip stop actions
0x03df  90 ff ee   mov dptr,#0xffee    ; DPTR -> DMACTL1
0x03e2  e0         movx a,@dptr        ; read
0x03e3  54 7f      anl a,#0x7f         ; clear DMAEN
0x03e5  f0         movx @dptr,a        ; DMACTL1 &= 0x7F — stop DMA ch1 (stream stopped, alt back to 0)
0x03e6  d2 17      setb 0x17           ; raise latch line 0x22.7 on stream stop — hw meaning UNKNOWN
0x03e8  12 0f 0c   lcall 0x0f0c        ; shift control word A out (applies bit 0x17 change)
0x03eb  30 0e 03   jnb 0x0e,0x03f1     ; only if a clock mode is selected:
0x03ee  12 10 01   lcall 0x1001        ; fcn_1001: DMACTL0 &= 0x7F — also stop DMA ch0
0x03f1  90 ff fd   mov dptr,#0xfffd    ; DPTR -> USBIMSK
0x03f4  74 ff      mov a,#0xff         ; all sources
0x03f6  f0         movx @dptr,a        ; USBIMSK = 0xFF — unmask RSTR/SUSR/RESR/SOF/PSOF/SETUP/STPOW (bit1 reserved)
0x03f7  12 0f ea   lcall 0x0fea        ; EP0 status ZLP + re-arm EP0 OUT
0x03fa  02 05 64   ljmp 0x0564         ; clear command byte, ret
```

## fcn_03fd = cmd3_apply_iface2_alt  (slot 0x0306; queued at 0x02d9 when wIndexL==2; bit 0x09 = alt!=0)

Same pattern for the OUT-EP2 stream only.

```
0x03fd  30 0e 04   jnb 0x0e,0x0404     ; R7 := (bit 0x0e set) ? 1 : 0
0x0400  7f 01      mov r7,#0x1         ;
0x0402  80 02      sjmp 0x0406         ;
0x0404  7f 00      mov r7,#0x0         ;
0x0406  30 0a 04   jnb 0x0a,0x040d     ; R6 := (bit 0x0a set) ? 1 : 0  (always 0 at runtime — bit never set)
0x0409  7e 01      mov r6,#0x1         ;
0x040b  80 02      sjmp 0x040f         ;
0x040d  7e 00      mov r6,#0x0         ;
0x040f  ee         mov a,r6            ; A = R6|R7 = any mode selected?
0x0410  4f         orl a,r7            ;
0x0411  60 1f      jz 0x0432           ; no mode -> stop/idle branch
0x0413  30 09 1c   jnb 0x09,0x0432     ; iface-2 alt==0 -> stop branch
0x0416  20 2e 03   jb 0x2e,0x041c      ; hw init already done?
0x0419  12 08 0b   lcall 0x080b        ; no: run one-time hw init (sets bit 0x2e)
0x041c  c2 2d      clr 0x2d            ; clear flag 0x25.5 on stream start — meaning UNKNOWN (see 0x0395)
0x041e  90 ff 98   mov dptr,#0xff98    ; DPTR -> OEPCNF2
0x0421  74 c5      mov a,#0xc5         ; iso OUT, enabled, 6 bytes/sample
0x0423  f0         movx @dptr,a        ; OEPCNF2 = 0xC5 — enable iso OUT endpoint 2
0x0424  7f 03      mov r7,#0x3         ; mode 3
0x0426  12 07 28   lcall 0x0728        ; configure codec port/ACG for streaming mode 3
0x0429  90 ff e8   mov dptr,#0xffe8    ; DPTR -> DMACTL0
0x042c  e0         movx a,@dptr        ; read
0x042d  44 80      orl a,#0x80         ; set DMAEN
0x042f  f0         movx @dptr,a        ; DMACTL0 |= 0x80 — start DMA ch0 (playback)
0x0430  80 1c      sjmp 0x044e         ; -> EP0 re-arm + exit
0x0432  30 0e 04   jnb 0x0e,0x0439     ; stop branch: recompute R7 = bit 0x0e
0x0435  7f 01      mov r7,#0x1         ;
0x0437  80 02      sjmp 0x043b         ;
0x0439  7f 00      mov r7,#0x0         ;
0x043b  30 0a 04   jnb 0x0a,0x0442     ; R6 = bit 0x0a (always 0)
0x043e  7e 01      mov r6,#0x1         ;
0x0440  80 02      sjmp 0x0444         ;
0x0442  7e 00      mov r6,#0x0         ;
0x0444  ee         mov a,r6            ; any mode selected?
0x0445  4f         orl a,r7            ;
0x0446  60 06      jz 0x044e           ; no -> just re-arm EP0
0x0448  20 09 03   jb 0x09,0x044e      ; stream still active -> nothing to stop
0x044b  12 10 01   lcall 0x1001        ; mode selected but alt==0: DMACTL0 &= 0x7F — stop DMA ch0
0x044e  12 0f ea   lcall 0x0fea        ; EP0 ZLP status + re-arm EP0 OUT
0x0451  02 05 64   ljmp 0x0564         ; clear command byte, ret
```

## fcn_0454 = cmd4_variantA_reapply_mode  (slot 0x0309)

```
0x0454  c2 2c      clr 0x2c            ; clear mode-variant flag 0x25.4 (selects the (0x23,x)/(0x24,0x80) chip programming in cmd7/cmd8)
0x0456  d2 16      setb 0x16           ; raise latch line 0x22.6 — hw meaning UNKNOWN (cleared by cmd5 / cmd11-pass)
0x0458  12 0e 62   lcall 0x0e62        ; shift control word B (0x23) out on P1
0x045b  12 0f 0c   lcall 0x0f0c        ; shift control word A (0x22) out (applies bit 0x16)
0x045e  af 08      mov r7,0x08         ; R7 = current codec-port mode id (IRAM byte 0x08, maintained by fcn_0728)
0x0460  12 07 28   lcall 0x0728        ; re-apply the current mode configuration
0x0463  02 05 64   ljmp 0x0564         ; clear command byte, ret
```

## fcn_0466 = cmd5_variantB_set_mode1  (slot 0x030c) — mirror of cmd4

```
0x0466  d2 2c      setb 0x2c           ; set mode-variant flag 0x25.4 (cmd7/cmd8 will program regs (0x04,0x41)/(0x12,0x00) instead)
0x0468  c2 16      clr 0x16            ; drop latch line 0x22.6 — hw meaning UNKNOWN
0x046a  12 0e 62   lcall 0x0e62        ; shift control word B out
0x046d  12 0f 0c   lcall 0x0f0c        ; shift control word A out
0x0470  7f 01      mov r7,#0x1         ; mode arg = 1
0x0472  12 07 28   lcall 0x0728        ; set codec-port mode 1
0x0475  02 05 64   ljmp 0x0564         ; clear command byte, ret
```

## fcn_0478 = cmd6_set_cpt_mode1  (slot 0x030f)

```
0x0478  7f 01      mov r7,#0x1         ; mode arg = 1
0x047a  12 07 28   lcall 0x0728        ; set codec-port mode 1 (ACG/CPT reprogram in fcn_0728)
0x047d  02 05 64   ljmp 0x0564         ; clear command byte, ret
```

## fcn_0480 = cmd7_set_cpt_mode2_progchip  (slot 0x0312; command queued at 0x0d35)

```
0x0480  7f 02      mov r7,#0x2         ; mode arg = 2
0x0482  12 07 28   lcall 0x0728        ; set codec-port mode 2
0x0485  30 2c 06   jnb 0x2c,0x048e     ; variant flag clear -> other register set
0x0488  12 05 68   lcall 0x0568        ; fcn_0568: ext-chip regs (0x04,0x41) then (0x12,0x00) via fcn_0c45 — register meanings UNKNOWN (chip unidentified from this range)
0x048b  02 05 64   ljmp 0x0564         ; clear command byte, ret
0x048e  75 2c 23   mov 0x2c,#0x23      ; scratch pair: reg = 0x23 (direct byte 0x2c, NOT bit 0x2c)
0x0491  e4         clr a               ; val = 0
0x0492  f5 2d      mov 0x2d,a          ; scratch: val byte (direct 0x2d)
0x0494  12 05 82   lcall 0x0582        ; fcn_0582: ext-chip write (0x23,0x00) then (0x24,0x80) — register meanings UNKNOWN
0x0497  02 05 64   ljmp 0x0564         ; clear command byte, ret
```

## fcn_049a = cmd8_set_cpt_mode3_progchip  (slot 0x0315) — as cmd7 but mode 3 and val 0x40

```
0x049a  7f 03      mov r7,#0x3         ; mode arg = 3
0x049c  12 07 28   lcall 0x0728        ; set codec-port mode 3
0x049f  30 2c 06   jnb 0x2c,0x04a8     ; variant flag clear -> (0x23,0x40) set
0x04a2  12 05 68   lcall 0x0568        ; ext-chip regs (0x04,0x41),(0x12,0x00) — meanings UNKNOWN
0x04a5  02 05 64   ljmp 0x0564         ; clear command byte, ret
0x04a8  75 2c 23   mov 0x2c,#0x23      ; reg = 0x23
0x04ab  75 2d 40   mov 0x2d,#0x40      ; val = 0x40
0x04ae  12 05 82   lcall 0x0582        ; ext-chip write (0x23,0x40) then (0x24,0x80) — meanings UNKNOWN
0x04b1  02 05 64   ljmp 0x0564         ; clear command byte, ret
```

## fcn_04b4 = cmd9_set_cpt_mode4  (slot 0x0318)

```
0x04b4  7f 04      mov r7,#0x4         ; mode arg = 4
0x04b6  12 07 28   lcall 0x0728        ; set codec-port mode 4
0x04b9  02 05 64   ljmp 0x0564         ; clear command byte, ret
```

## fcn_04bc = cmd10_set_cpt_mode5  (slot 0x031b)

```
0x04bc  7f 05      mov r7,#0x5         ; mode arg = 5
0x04be  12 07 28   lcall 0x0728        ; set codec-port mode 5
0x04c1  02 05 64   ljmp 0x0564         ; clear command byte, ret
```

## fcn_04c4 = cmd11_eeprom_selftest  (slot 0x031e)

Diagnostic: complements the byte at I2C-EEPROM address 0x1FFF (last byte of an
8K part, addressed as dev 0xA0 with a 2-byte sub-address by fcn_0bee/fcn_0cdd —
outside the firmware image, so nondestructive), verifies the write, and reports
the result on latch line 0x16.

```
0x04c4  20 2e 03   jb 0x2e,0x04ca      ; hw init already done?
0x04c7  12 08 0b   lcall 0x080b        ; no: run one-time hw init (sets bit 0x2e)
0x04ca  d2 2d      setb 0x2d           ; set flag 0x25.5 (only place it is set; cleared on stream start) — meaning UNKNOWN
0x04cc  7f 03      mov r7,#0x3         ; mode arg = 3
0x04ce  12 07 28   lcall 0x0728        ; set codec-port mode 3
0x04d1  75 2c 04   mov 0x2c,#0x4       ; scratch reg = 0x04 (direct byte 0x2c)
0x04d4  75 2d 41   mov 0x2d,#0x41      ; scratch val = 0x41
0x04d7  ad 2d      mov r5,0x2d         ; R5 = 0x41
0x04d9  af 2c      mov r7,0x2c         ; R7 = 0x04
0x04db  12 0c 45   lcall 0x0c45        ; ext-chip write reg 0x04 = 0x41 (bit-bang on P1) — register meaning UNKNOWN
0x04de  7d ff      mov r5,#0xff        ; EEPROM address low byte = 0xFF
0x04e0  7f 1f      mov r7,#0x1f        ; EEPROM address high byte = 0x1F -> addr 0x1FFF
0x04e2  12 0c dd   lcall 0x0cdd        ; fcn_0cdd: I2C read dev 0xA0 (boot EEPROM) at 0x1FFF -> R7
0x04e5  8f 2c      mov 0x2c,r7         ; save read byte
0x04e7  63 2c ff   xrl 0x2c,#0xff      ; complement it (guaranteed-different test pattern)
0x04ea  ab 2c      mov r3,0x2c         ; R3 = data to write
0x04ec  7f 1f      mov r7,#0x1f        ; addr high = 0x1F (R5 still 0xFF from 0x04de)
0x04ee  12 0b ee   lcall 0x0bee        ; fcn_0bee: I2C write EEPROM[0x1FFF] = ~old  (I2CADR=0xA0, 2-byte address, then data)
0x04f1  7d ff      mov r5,#0xff        ; addr low = 0xFF again (R7 still 0x1F)
0x04f3  12 0c dd   lcall 0x0cdd        ; read back EEPROM[0x1FFF] -> R7
0x04f6  8f 2d      mov 0x2d,r7         ; save readback
0x04f8  e5 2d      mov a,0x2d          ; A = readback
0x04fa  b5 2c 02   cjne a,0x2c,0x04ff  ; compare with written pattern
0x04fd  c2 16      clr 0x16            ; match -> EEPROM write test PASSED: drop latch line 0x16 (pass indicator)
0x04ff  12 0f 0c   lcall 0x0f0c        ; shift control word A out — publishes the test result on the latch
0x0502  75 2c 12   mov 0x2c,#0x12      ; scratch reg = 0x12
0x0505  e4         clr a               ; val = 0
0x0506  f5 2d      mov 0x2d,a          ;
0x0508  ad 2d      mov r5,0x2d         ; R5 = 0x00
0x050a  af 2c      mov r7,0x2c         ; R7 = 0x12
0x050c  12 0c 45   lcall 0x0c45        ; ext-chip write reg 0x12 = 0x00 — register meaning UNKNOWN
0x050f  80 53      sjmp 0x0564         ; clear command byte, ret
```

## fcn_0511 = cmd12_set_cpt_mode1  (slot 0x0321) — duplicate of cmd6 with a short jump

```
0x0511  7f 01      mov r7,#0x1         ; mode arg = 1
0x0513  12 07 28   lcall 0x0728        ; set codec-port mode 1
0x0516  80 4c      sjmp 0x0564         ; clear command byte, ret  (range ends here; 0x0518 = cmd13 belongs to next annotator)
```

---

### Flow leaving the range
All paths converge on `0x0564` (clears IRAM 0x0a, ret) — next annotator's range.
Helpers 0x0568/0x0582/0x0728/0x080b/0x0bee/0x0c45/0x0cdd/0x0e62/0x0f0c/0x0fea/
0x0ff4/0x1001 are out of range; their behavior stated above was read directly
from their bodies in the listing, not inferred.

### 5.4 Range 0x0518-0x0727

# Rev 20 — annotated range 0x0518 … 0x0727 (0x0728 exclusive)

Two distinct things live in this range:

* **0x0518 – 0x0595 — code** (62 instructions, 5 functions): two event handlers off the
  main-loop dispatcher, the dispatcher's shared epilogue, and two small serial-control
  helper routines.
* **0x0596 – 0x0727 — pure data** (402 bytes): the whole USB descriptor set (device,
  two configuration sets, three strings). Ghidra force-disassembled this as
  instructions; every "instruction" listed by Ghidra from 0x0596 to 0x0727 is a
  misdecode, and every XREF into 0x0000-0x0300 that Ghidra shows *from* an address in
  0x0596-0x0727 is a phantom (e.g. `CODE:0596 LCALL 0x0110` is really the device
  descriptor's `12 01 10` = bLength/bDescriptorType/bcdUSB-lo). It is decoded as data
  below.

## State this range depends on

| Location | Kind | Meaning (evidence) |
|---|---|---|
| IRAM byte 0x0A | pending-event code | Written 1..0x0E by ISR/SETUP paths; dispatcher fcn 0x02EE does `A=0x0A; DEC A; CJNE #0x0E; JC` then `JMP @A+DPTR` into the 3-byte ljmp table at 0x0300. Entry 0x0324 → 0x0518 (event 0x0D), entry 0x0327 → 0x0526 (event 0x0E). |
| IRAM bit 0x0E (= byte 0x21 bit 6) | "device configured" | Set by SET_CONFIGURATION for wValue 1 or 2 (0x027E, 0x028D); cleared for wValue 0 (0x026F), on bus-reset re-init (0x0F65) and at 0x09F9. |
| IRAM bit 0x0A (= byte 0x21 bit 2) | vestigial | Cleared at 0x026D, 0x027C, 0x028B, 0x09F7, 0x0F63 — and **never set anywhere in the image** (grepped `SETB 0x0a`, none). So the `MOV C,0x0E / ORL C,0x0A` guard at 0x0526 reduces to "configured?". |
| IRAM bit 0x1E (= byte 0x23 bit 6) | trailing data-line state for the 8-bit serial channel | Tested at 0x0F32 inside fcn 0x0F0C to choose whether P1.7 is left high or low after the byte is clocked out. |
| IRAM bytes 0x23 / 0x25 | shadow bytes for the 16-bit shift-register latch shifted out by fcn 0x0E62 (data P1.0, clock P1.2, strobe P1.1) | |
| IRAM byte 0x22 | shadow byte for the 8-bit serial channel shifted out by fcn 0x0F0C (data P1.7, clock P1.5, frame P1.6) | |
| IRAM bytes 0x2C / 0x2D | SDCC-style static parameter slots (register, value) for the 3-byte serial control write fcn 0x0C45 | |

Callees referenced from this range (all outside it, annotated by other owners):

* `0x0BEE` — I²C master byte write. Sets `I2CADR(0xFFC3)=0xA0`, spins a delay, then
  writes `I2CDATO(0xFFC1) = R7`, waits ACC.3 of `I2CSTA(0xFFC0)`, writes `= R5`, waits,
  sets `I2CCTL(0xFFC0) |= 0x01`, writes `= R3`. i.e. **write(byte R3) to 16-bit address
  R7:R5 of the I²C slave at 0xA0** — the TAS1020B boot-EEPROM slave address.
* `0x0C45` — 3-byte MSB-first bit-banged serial write on P1.4 (data) / P1.3 (clock):
  emits `0x20`, then R7, then R5, bracketed by `CLR 0x2F` / `SETB 0x2F` + `LCALL 0x0E62`
  (chip-select carried in latch bit 0x2F = IRAM 0x25 bit 7). Project NOTES.md calls this
  device the CS8427; that identification is not verified against a CS8427 datasheet in
  this repo, so register meanings below are left unnamed.
* `0x0E62` / `0x0F0C` — the two shift-register writers described in the table above.
* `0x08CB` — master hardware init (clears USBCTL, P1=0, P3=0xFF, TH0=0xCE, …).
* `0x0970` — USB endpoint descriptor-block init (OEPCNF/IEPCNF/byte-count registers).

---

## fcn_0518 = evt0d_invalidate_boot_eeprom  (event code 0x0D)

Reached from the dispatcher table (`0x0324: ljmp 0x0518`). Event 0x0D is queued at
0x005B by the SETUP decoder when `SETPACK[0] (0xFF28) == 0x21` (class request, host→device,
recipient interface) **and** `SETPACK[1] (0xFF29) == 0x00`. The handler destroys the first
byte of the boot EEPROM and then completes the control transfer.

```
0x0518  e4        clr a                 ; A = 0 — the single data byte to write
0x0519  fb        mov r3,a              ; R3 = 0x00 = data byte argument of fcn_0bee
0x051a  fd        mov r5,a              ; R5 = 0x00 = low byte of the 16-bit EEPROM address
0x051b  ff        mov r7,a              ; R7 = 0x00 = high byte of the 16-bit EEPROM address
0x051c  12 0b ee  lcall 0x0bee          ; -> i2c_write_byte: slave 0xA0, addr 0x0000, data 0x00.
                                        ;    Writes zero over EEPROM offset 0 (the header byte the
                                        ;    boot ROM checks). Purpose = force the next boot into
                                        ;    the boot-ROM DFU path — INFERRED from the target, not
                                        ;    from any TI source.
0x051f  90 ff ab  mov dptr,#0xffab      ; DPTR -> OEPDCNTX0 (EP0 OUT data count / NAK), Reg_stc1.h:203
0x0522  e4        clr a                 ; A = 0
0x0523  f0        movx @dptr,a          ; OEPDCNTX0 = 0 — clear NAK / release EP0 OUT for the next
                                        ;    packet (same write+comment as TI UsbEng.c:632)
0x0524  80 3e     sjmp 0x0564           ; -> dispatcher epilogue (clear pending-event byte, return)
```

## fcn_0526 = evt0e_usb_suspend_enter_and_resume  (event code 0x0E)

Reached from `0x0327: ljmp 0x0526`. Event 0x0E is queued by the **USB suspend vector**:
the INT0 handler at 0x0DAC reads `VECINT (0xFFB2)` into A, computes `DPTR = 0x0C93 + 2*VECINT`,
fetches a 16-bit target and jumps to it. Table entry for VECINT = 0x16 lives at
0x0C93 + 0x2C = 0x0CBF and contains `00 06` → 0x0006, whose entire body is
`mov 0x0a,#0x0e ; ret`. Datasheet §2.2.5.1 names VECINT 0x16 the USB function-suspend
vector *and* states the MCU must not set PCON.IDL inside an ISR — which is exactly why the
vector only queues an event and the IDL write happens here, in main-loop context.

Instructions 0x0526-0x0543 are the *entering suspend* half; execution stops at the IDL
write. Datasheet §2.2.5.2: the resume interrupt automatically clears IDL and, after the
RETI, execution continues with the instruction after the one that set IDL — i.e. at 0x0546.
So 0x0546-0x0563 is the *resume* half: disconnect, re-mask interrupts, re-init hardware,
re-connect.

```
0x0526  a2 0e     mov c,0x0e            ; C = bit 0x0E (IRAM 0x21.6) = "device is configured"
0x0528  72 0a     orl c,0x0a            ; C |= bit 0x0A (IRAM 0x21.2) — a flag that is cleared in
                                        ;    5 places and set in none, so this OR is a no-op
0x052a  50 38     jnc 0x0564            ; not configured -> just clear the event and return
0x052c  90 ff e1  mov dptr,#0xffe1      ; DPTR -> ACGCTL (Reg_stc1.h:60)
0x052f  e0        movx a,@dptr          ; read ACGCTL
0x0530  54 3f     anl a,#0x3f           ; clear bit7 MCLKO2EN and bit6 MCLKO1EN (datasheet §6.5.3.11)
0x0532  f0        movx @dptr,a          ; ACGCTL write-back: both master-clock outputs off (driven 0)
0x0533  e4        clr a                 ; A = 0
0x0534  f5 25     mov 0x25,a            ; latch shadow high byte = 0 (also clears CS bit 0x2F)
0x0536  f5 23     mov 0x23,a            ; latch shadow low byte = 0
0x0538  12 0e 62  lcall 0x0e62          ; shift the 16 zero bits out on P1.0/P1.2 and strobe P1.1.
                                        ; UNKNOWN — which external part this latch drives (analog
                                        ; mux/mute/LED?); would need the Mbox schematic to resolve.
0x053b  75 22 ff  mov 0x22,#0xff        ; 8-bit serial channel shadow = 0xFF (all ones)
0x053e  c2 1e     clr 0x1e              ; bit 0x1E (IRAM 0x23.6) = 0 -> fcn_0f0c leaves P1.7 low at end
0x0540  12 0f 0c  lcall 0x0f0c          ; clock 0xFF out on P1.7/P1.5 framed by P1.6.
                                        ; UNKNOWN — which device sits on this second serial channel.
0x0543  43 87 01  orl 0x87,#0x01        ; PCON |= IDL (PCON = SFR 0x87, per TI ROM/Utils.SRC:17;
                                        ;    IDL = bit 0, datasheet §2.2.5.1) -> all TAS1020B clocks
                                        ;    stop; the MCU halts here until a USB resume/INT0 wakes it
;--- execution resumes here after the resume interrupt auto-clears IDL ---
0x0546  90 ff fc  mov dptr,#0xfffc      ; DPTR -> USBCTL (Reg_stc1.h:100)
0x0549  e0        movx a,@dptr          ; read USBCTL
0x054a  54 7f     anl a,#0x7f           ; clear bit7 CONT (pull-up / connect); keep FEN, SDW_OK
0x054c  f0        movx @dptr,a          ; USBCTL write-back -> present a disconnect to the host
0x054d  a3        inc dptr              ; DPTR -> 0xFFFD USBIMSK
0x054e  74 9f     mov a,#0x9f           ; mask = RSTR|SOF|PSOF|SETUP|(res)|STPOW, SUSR+RESR OFF
0x0550  f0        movx @dptr,a          ; USBIMSK = 0x9F (bit names: datasheet §6.5.1.3)
0x0551  12 08 cb  lcall 0x08cb          ; -> master hw init (USBCTL=0, P1=0, P3=0xFF, TMOD/TH0=0xCE …)
0x0554  12 09 70  lcall 0x0970          ; -> USB endpoint descriptor-block init (OEPCNF/IEPCNF, counts)
0x0557  d2 8c     setb 0x8c             ; TCON.4 = TR0 -> restart Timer 0 (the ms tick)
0x0559  d2 a8     setb 0xa8             ; IE.0 = EX0 -> re-enable the INT0 (USB) interrupt
0x055b  d2 af     setb 0xaf             ; IE.7 = EA  -> global interrupt enable
0x055d  90 ff fc  mov dptr,#0xfffc      ; DPTR -> USBCTL
0x0560  e0        movx a,@dptr          ; read USBCTL
0x0561  44 80     orl a,#0x80           ; set bit7 CONT
0x0563  f0        movx @dptr,a          ; USBCTL write-back -> re-assert the pull-up, host re-enumerates
                                        ;    (compare TI UsbEng.c:647 "USBCTL = 0xC0 // connect PUR")
```

Note the ordering consequence: the device comes back from suspend by *disconnecting and
reconnecting*, not by resuming the existing session.

## fcn_0564 = evt_dispatch_epilogue

Shared tail used by eight handlers (XREFs: 0x02F6, 0x0383, 0x03FA, 0x0451, 0x0463,
0x0475, 0x047D, 0x048B plus the two sjmp/ljmp from this range).

```
0x0564  e4        clr a                 ; A = 0
0x0565  f5 0a     mov 0x0a,a            ; pending-event byte = 0 -> dispatcher idles until re-armed
0x0567  22        ret                   ; back to the main loop
```

## fcn_0568 = serial_ctl_write_04_41_then_12_00

Called from 0x0488 (event 7 handler, 0x0480) and 0x04A2 (event 8 handler, 0x049A), in both
cases only when flag bit 0x2C is set. Argument passing is the SDCC static-slot idiom: the
register number goes to IRAM 0x2C and the value to IRAM 0x2D, then both are copied into
R7/R5 for the call.

```
0x0568  75 2c 04  mov 0x2c,#0x04        ; param slot 1 = target register 0x04
0x056b  75 2d 41  mov 0x2d,#0x41        ; param slot 2 = value 0x41.
                                        ; UNKNOWN — meaning of register 0x04 / value 0x41 on the
                                        ; P1.4/P1.3 device; no datasheet for that part in-repo.
0x056e  ad 2d     mov r5,0x2d           ; R5 = value  (fcn_0c45 3rd byte)
0x0570  af 2c     mov r7,0x2c           ; R7 = register (fcn_0c45 2nd byte; 1st byte is a fixed 0x20)
0x0572  12 0c 45  lcall 0x0c45          ; -> serial write {0x20, 0x04, 0x41}
0x0575  75 2c 12  mov 0x2c,#0x12        ; param slot 1 = target register 0x12
0x0578  e4        clr a                 ; A = 0
0x0579  f5 2d     mov 0x2d,a            ; param slot 2 = value 0x00
0x057b  ad 2d     mov r5,0x2d           ; R5 = 0x00
0x057d  af 2c     mov r7,0x2c           ; R7 = 0x12
0x057f  02 0c 45  ljmp 0x0c45           ; tail call -> serial write {0x20, 0x12, 0x00}; returns to caller
```

## fcn_0582 = serial_ctl_write_caller_pair_then_24_80

Called from 0x0494 (with 0x2C=0x23, 0x2D=0x00) and 0x04AE (with 0x2C=0x23, 0x2D=0x40) —
i.e. the caller has already loaded the parameter slots; this routine sends that pair and
then unconditionally sends register 0x24 = 0x80.

```
0x0582  ad 2d     mov r5,0x2d           ; R5 = caller-supplied value
0x0584  af 2c     mov r7,0x2c           ; R7 = caller-supplied register number
0x0586  12 0c 45  lcall 0x0c45          ; -> serial write {0x20, reg, val}
0x0589  75 2c 24  mov 0x2c,#0x24        ; param slot 1 = register 0x24
0x058c  75 2d 80  mov 0x2d,#0x80        ; param slot 2 = value 0x80.
                                        ; UNKNOWN — meaning of register 0x24 / value 0x80.
0x058f  ad 2d     mov r5,0x2d           ; R5 = 0x80
0x0591  af 2c     mov r7,0x2c           ; R7 = 0x24
0x0593  02 0c 45  ljmp 0x0c45           ; tail call -> serial write {0x20, 0x24, 0x80}
```

---

# DATA REGION 0x0596 – 0x0727 (402 bytes) — USB descriptors

Not code. Verified from `xxd` of `rev20_firmware_code.bin` (file offset == CPU address).
Reachability: the GET_DESCRIPTOR path builds a code pointer in IRAM 0x19 (high) / 0x1A
(low) and reads it with MOVC via fcn 0x0B6E. Every load of that pointer in the whole
image is an immediate:

| Site | Pointer | Selected when |
|---|---|---|
| 0x017A | 0x0596 | `SETPACK[3] (0xFF2B)` descriptor type == 1 (DEVICE) |
| 0x0198 | 0x0670 | type == 2 (CONFIGURATION) and index (0xFF2A) == 0 |
| 0x01BE | 0x06A6 | type == 3 (STRING), index 0 |
| 0x01CB | 0x06AA | type == 3, index 1 |
| 0x01D8 | 0x06C8 | type == 3, index 2 |

There is no computed write to 0x19/0x1A (only `INC 0x1A`/`INC 0x19` walking the pointer in
fcn 0x0BA8) and no `mov dptr,#0x05xx/0x06xx` anywhere, so **the 200-byte Audio-Class
configuration set at 0x05A8 is unreachable in this image** — rev20 enumerates with the
54-byte vendor-class (bInterfaceClass 0xFF) set at 0x0670 instead.

## 0x0596 (18 bytes) — DEVICE descriptor
```
0596: 12 01 10 01 00 00 00 08 ba 0d 00 10 20 00 01 02 00 01
```
| Off | Field | Value |
|---|---|---|
| 0x0596 | bLength | 0x12 (18) |
| 0x0597 | bDescriptorType | 0x01 DEVICE |
| 0x0598 | bcdUSB | 0x0110 (USB 1.1) |
| 0x059A | bDeviceClass | 0x00 (per-interface) |
| 0x059B | bDeviceSubClass | 0x00 |
| 0x059C | bDeviceProtocol | 0x00 |
| 0x059D | bMaxPacketSize0 | 0x08 |
| 0x059E | idVendor | 0x0DBA (Digidesign) |
| 0x05A0 | idProduct | 0x1000 |
| 0x05A2 | bcdDevice | 0x0020 — **Rev 20**, matches the brief's known fact |
| 0x05A4 | iManufacturer | 0x01 → string at 0x06AA |
| 0x05A5 | iProduct | 0x02 → string at 0x06C8 |
| 0x05A6 | iSerialNumber | 0x00 (none) |
| 0x05A7 | bNumConfigurations | 0x01 |

## 0x05A8 (200 bytes) — USB Audio Class CONFIGURATION set (present but never returned)

Configuration header @0x05A8: `09 02 c8 00 03 01 00 80 f0` →
wTotalLength 0x00C8 = 200 (matches 0x05A8..0x066F exactly), bNumInterfaces 3,
bConfigurationValue 1, iConfiguration 0, bmAttributes 0x80 (bus-powered, no remote wakeup),
bMaxPower 0xF0 = 480 mA.

| Off | Bytes | Decode |
|---|---|---|
| 0x05B1 | `09 04 00 00 00 01 01 00 00` | INTERFACE 0, alt 0, 0 EPs, class 0x01 AUDIO, subclass 0x01 AUDIOCONTROL, proto 0, iInterface 0 |
| 0x05BA | `0a 24 01 00 01 48 00 02 01 02` | CS_INTERFACE / HEADER: bcdADC 0x0100, wTotalLength 0x0048 (72), bInCollection 2, baInterfaceNr = 1, 2 |
| 0x05C4 | `0c 24 02 01 01 01 05 02 03 00 00 00` | INPUT_TERMINAL ID 1, wTerminalType 0x0101 (USB streaming), bAssocTerminal 5, 2 ch, wChannelConfig 0x0003 (L+R), iChannelNames 0, iTerminal 0 |
| 0x05D0 | `0c 24 02 02 01 06 03 02 03 00 00 00` | INPUT_TERMINAL ID 2, wTerminalType 0x0601, bAssocTerminal 3, 2 ch, 0x0003 |
| 0x05DC | `0c 24 02 06 05 06 00 02 03 00 00 00` | INPUT_TERMINAL ID 6, wTerminalType 0x0605, bAssocTerminal 0, 2 ch, 0x0003 |
| 0x05E8 | `09 24 03 03 03 06 02 01 00` | OUTPUT_TERMINAL ID 3, wTerminalType 0x0603, bAssocTerminal 2, bSourceID 1 |
| 0x05F1 | `09 24 03 04 01 01 01 05 00` | OUTPUT_TERMINAL ID 4, wTerminalType 0x0101 (USB streaming), bAssocTerminal 1, bSourceID 5 |
| 0x05FA | `08 24 05 05 02 02 06 00` | SELECTOR_UNIT ID 5, 2 input pins: sources 2 and 6, iSelector 0 |
| 0x0602 | `09 04 01 00 00 01 02 00 00` | INTERFACE 1, alt 0, 0 EPs, AUDIO / AUDIOSTREAMING (zero-bandwidth alt) |
| 0x060B | `09 04 01 01 01 01 02 00 00` | INTERFACE 1, alt 1, 1 EP, AUDIOSTREAMING |
| 0x0614 | `07 24 01 09 01 01 00` | AS_GENERAL: bTerminalLink 0x09 (**no terminal with ID 9 exists in this AC set — IDs present are 1,2,3,4,5,6**), bDelay 1, wFormatTag 0x0001 PCM |
| 0x061B | `0e 24 02 01 02 03 18 02 44 ac 00 80 bb 00` | FORMAT_TYPE_I: 2 ch, bSubframeSize 3, bBitResolution 0x18 (24), bSamFreqType 2, tSamFreq 0x00AC44 = 44100, 0x00BB80 = 48000 |
| 0x0629 | `09 05 81 05 30 01 01 00 00` | ENDPOINT 0x81 (IN), bmAttributes 0x05 = isochronous + asynchronous, wMaxPacketSize 0x0130 = 304, bInterval 1, bRefresh 0, bSynchAddress 0 |
| 0x0632 | `07 25 01 01 01 00 02` | CS_ENDPOINT / EP_GENERAL: bmAttributes 0x01 (sampling-frequency control), bLockDelayUnits 1, wLockDelay 0x0200 |
| 0x0639 | `09 04 02 00 00 01 02 00 00` | INTERFACE 2, alt 0, 0 EPs, AUDIOSTREAMING |
| 0x0642 | `09 04 02 01 01 01 02 00 00` | INTERFACE 2, alt 1, 1 EP |
| 0x064B | `07 24 01 08 01 01 00` | AS_GENERAL: bTerminalLink 0x08 (**again no matching terminal ID**), bDelay 1, PCM |
| 0x0652 | `0e 24 02 01 02 03 18 02 44 ac 00 80 bb 00` | FORMAT_TYPE_I: 2 ch, 3-byte subframes, 24 bit, 44100 + 48000 |
| 0x0660 | `09 05 02 05 30 01 01 00 00` | ENDPOINT 0x02 (OUT), iso + async, wMaxPacketSize 0x0130 = 304, bInterval 1 |
| 0x0669 | `07 25 01 01 01 00 02` | CS_ENDPOINT / EP_GENERAL, same fields as 0x0632 |

## 0x0670 (54 bytes) — the CONFIGURATION set rev20 actually returns

```
0670: 09 02 36 00 02 01 00 80 f0
0679: 09 04 00 00 00 ff 00 00 00
0682: 09 04 01 00 00 ff 00 00 00
068b: 09 04 01 01 02 ff 00 00 00
0694: 09 05 81 05 30 01 01 00 00
069d: 09 05 02 05 30 01 01 00 00
```
| Off | Decode |
|---|---|
| 0x0670 | CONFIGURATION: wTotalLength 0x0036 = 54 (self-consistent: 6 × 9), bNumInterfaces 2, bConfigurationValue 1, iConfiguration 0, bmAttributes 0x80, bMaxPower 0xF0 = 480 mA |
| 0x0679 | INTERFACE 0, alt 0, 0 EPs, bInterfaceClass 0xFF **vendor specific**, subclass 0, protocol 0 |
| 0x0682 | INTERFACE 1, alt 0, 0 EPs, class 0xFF (zero-bandwidth alt) |
| 0x068B | INTERFACE 1, alt 1, 2 EPs, class 0xFF |
| 0x0694 | ENDPOINT 0x81 (IN), bmAttributes 0x05 iso/async, wMaxPacketSize 0x0130 = 304, bInterval 1, bRefresh 0, bSynchAddress 0 |
| 0x069D | ENDPOINT 0x02 (OUT), bmAttributes 0x05 iso/async, wMaxPacketSize 0x0130 = 304, bInterval 1 |

This is consistent with SET_CONFIGURATION accepting wValue 1 *or* 2 (0x0279 / 0x0288) and
with SET_INTERFACE gating on alt setting; both endpoints live on interface 1 alt 1 here,
where the audio-class set puts them on interfaces 1 and 2.

## 0x06A6 / 0x06AA / 0x06C8 — STRING descriptors

```
06a6: 04 03 09 04                                   -> bLength 4, type 3, wLANGID[0] = 0x0409 (en-US)
06aa: 1e 03 "D.i.g.i.d.e.s.i.g.n. .I.n.c."          -> bLength 0x1E (30), type 3, 14 UTF-16LE chars:
                                                       "Digidesign Inc"   (iManufacturer = 1)
06c8: 60 03 "M.b.o.x. .U.S.B. .A.u.d.i.o. .D.e.v.i.c.e. .c.o.p.y.r.i.g.h.t. .D.i.g.i.d.e.s.i.g.n. .2.0.0.1."
                                                    -> bLength 0x60 (96), type 3, 47 UTF-16LE chars:
                                                       "Mbox USB Audio Device copyright Digidesign 2001"
                                                       (iProduct = 2)
```
The last string ends at 0x0727 inclusive; 0x0728 is the first byte of the next function
(`8f 2e  mov 0x2e,r7`), which is owned by another annotator.

---

## Findings worth escalating

1. **The UAC descriptor set at 0x05A8 is dead data in rev20.** GET_DESCRIPTOR(CONFIGURATION)
   returns the 54-byte vendor-class set at 0x0670. Verified exhaustively: only five
   descriptor-pointer immediates exist in the image, none is 0x05A8, the pointer is never
   computed, and nothing writes into code space 0x05xx/0x06xx.
2. **Both AS_GENERAL descriptors in the unused audio set carry impossible bTerminalLink
   values** (0x09 at 0x0614, 0x08 at 0x064B) against terminal IDs 1-6.
3. **The suspend path is complete and datasheet-conformant**: VECINT 0x16 → 0x0006 queues
   event 0x0E → 0x0526 sets PCON.IDL outside any ISR, exactly as §2.2.5.1 requires; the
   resume half then reconnects rather than resuming the session.

### 5.5 Range 0x0728-0x080A

# Rev 20 annotation — 0x0728..0x080B

Exactly one function occupies this range: `fcn_0728 = audio_clock_mode_apply`.
There are **no data bytes** in this range; every byte from 0x0728 to 0x080A is
reachable code, and 0x080A `RET` is the last byte (0x080B starts the next
function, owned by another annotator).

## Function table

| addr   | name                   | summary |
|--------|------------------------|---------|
| 0x0728 | audio_clock_mode_apply | R7 = mode (1,2,3,5 special; else no clock change). Reprograms TAS1020B ACG1/ACG2 + MCLK routing, writes reg 4 of the external serial audio chip, re-arms iso EP1-IN/EP2-OUT configs, settles with a busy-wait. |

## Out-of-range helpers referenced (verified by reading their listings, owned by other annotators)

- `fcn_0e62` — bit-bangs the 8-bit value in IRAM 0x23 MSB-first on P1.0 (data)
  with P1.2 clock pulses; if bit 0x30 (IRAM 0x26.0) is set it continues with the
  8 bits of IRAM 0x25; ends with a P1.1 latch pulse. I.e. pushes the firmware's
  16-bit control shift register {0x23,0x25} to external hardware.
  Bit addresses 0x18-0x1F are the bits of byte 0x23; 0x28-0x2F are the bits of
  byte 0x25.
- `fcn_0e18` — entered with DPTR preloaded; writes 0x10 to @DPTR and 0x10 to
  0xFFF6. Called here with DPTR=0xFFE2, so it sets ACG1DCTL=0x10 and
  ACG2DCTL=0x10 (DIVM=0001b = divide-by-2, DIVI=000b = divide-by-1;
  datasheet §6.5.3.10).
- `fcn_0dec` — writes ACG1FRQ{2,1,0} = ACG2FRQ{2,1,0} = 61 A8 0F, then falls
  into `fcn_0e0f`.
- `fcn_0e0f` — entered with DPTR/A preloaded: MOVX @DPTR,A (here completes the
  ACG2FRQ0 write), then ACGCTL(0xFFE1)=0x06, RET.
- `0x0deb` — mid-function entry point one byte before fcn_0dec: raw byte at
  0x0deb is 0xF0 = MOVX @DPTR,A (verified with xxd), so `lcall 0x0deb` with
  DPTR=0xFFB1 writes A to GLOBCTL and then falls through fcn_0dec+fcn_0e0f
  (reprograms both ACGs to 61 A8 0F and sets ACGCTL=0x06), returning with
  DPTR=0xFFE1.
- `fcn_0e20` — sets IRAM 0x31=0x04, 0x32=0x40 (pending serial-chip register
  write: reg 4, value 0x40).
- `fcn_0c45` — 3-byte serial write clocked MSB-first on P1.4 (data) / P1.3
  (clock): byte0 = constant 0x20, byte1 = R7 (register number), byte2 = R5
  (value). Chip select = shift-register bit 0x2F (byte 0x25 bit 7), driven low
  via fcn_0e62 before and high after the 24 clocks. Prior notes (NOTES.md,
  unverified) identify the target as the CS8427 S/PDIF transceiver; 0x20
  matches a fixed chip-address/write header but no CS8427 datasheet exists in
  this repo, so the device identity is carried over as *plausible, unverified*.

## ACG frequency value derivation (verified)

TAS1020B datasheet §2.2.6.1: synthesizer output = 600/N MHz where N is the
24-bit ACGnFRQ value in 6.18 fixed-point; equivalently
ACGnFRQ = (600,000,000 / f_Hz) x 2^18. The datasheet's own worked example
(24.576 MHz) yields 61 A8 00.

- `6A 4B 20` (written by the mode-2 branch here) = N 26.5731 → 22.579 MHz =
  512 x 44.1 kHz. TI SoftPll.c (ti_uac_reference/Application/SoftPll.c,
  softPllInit, lines 29-31) programs exactly ACGFRQ2=0x6A, ACGFRQ1=0x4B,
  ACGFRQ0=0x20 with the comment "PLL1 = 22.5792 MHz".
- `61 A8 0F` (written by fcn_0dec, used by the mode-3 branch) = the datasheet's
  24.576 MHz value 61 A8 00 plus 0x0F LSBs of trim (≈4 Hz/LSB at this
  frequency per the datasheet's resolution example) = 512 x 48 kHz, nominal.

With ACGnDCTL=0x10 (÷2), MCLKO is 11.2896 / 12.288 MHz = 256·fs — matching the
TI comment "22.5792/2 == 11.2896MHZ".

## IRAM / bit variables used in this range

| loc        | role (as used here) |
|------------|---------------------|
| IRAM 0x2E  | saved mode argument (copy of R7) |
| IRAM 0x2F/0x30 | 16-bit up-counter for the settle busy-wait (hi/lo); also zeroed at entry |
| IRAM 0x08  | current clock-mode state variable (written 1,2,3,5 per branch; read back by callers, e.g. `mov r7,0x08; lcall 0x0728` at 0x045E) |
| IRAM 0x31/0x32 | pending serial-chip write: register number / value, consumed by the common tail |
| IRAM 0x23  | control shift-register byte A (pushed by fcn_0e62 on P1.0/P1.2, latched by P1.1) |
| bit 0x18/0x19 (0x23.0/.1) | shift-reg outputs set only in mode 5 — board-level meaning UNKNOWN (external wiring not documented in repo) |
| bit 0x1A/0x1B (0x23.2/.3) | shift-reg outputs cleared at entry, re-set at end of reconfig — pattern consistent with muting during clock change, but board-level meaning UNKNOWN |

Note on bogus XREFs: Ghidra shows references into 0x072A/0x072D/0x0753 from
0x06D2, 0x06E6 and 0x071E. Those source addresses lie inside the
0x0596-0x0717 descriptor/data region that the sweep force-disassembled as
code; the \"references\" are descriptor bytes misread as jump opcodes and are
not real control flow. The real entries are the LCALLs listed at 0x0728 and
the fall-in at 0x072A does not exist.

## fcn_0728 — audio_clock_mode_apply(R7 = mode)

Callers (all in the mode-change/vendor-request area 0x0300-0x04C0, other
annotator's range) pass R7 = 1, 2, 3, 4, 5, or the current state `0x08`.

### Entry: save arg, mute, default both ACG dividers to /2

```
0x0728  8f 2e     mov 0x2e,r7        ; save mode argument in IRAM 0x2E
0x072a  e4        clr a              ; A = 0
0x072b  f5 2f     mov 0x2f,a         ; IRAM 0x2F = 0 (delay-counter hi, cleared early)
0x072d  f5 30     mov 0x30,a         ; IRAM 0x30 = 0 (delay-counter lo)
0x072f  c2 1a     clr 0x1a           ; clear control shift-reg bit 0x23.2 (deasserted during clock change; board meaning UNKNOWN — likely mute/relay, wiring not in repo)
0x0731  c2 1b     clr 0x1b           ; clear control shift-reg bit 0x23.3 (same UNKNOWN board meaning)
0x0733  12 0e 62  lcall 0x0e62       ; -> fcn_0e62: push shift register {0x23[,0x25]} out P1.0/P1.2, latch P1.1 — applies the cleared bits to hardware
0x0736  90 ff e2  mov dptr,#0xffe2   ; DPTR = ACG1DCTL (Reg_stc1.h: ACGDCTL 0xFFE2)
0x0739  12 0e 18  lcall 0x0e18       ; -> fcn_0e18: ACG1DCTL=0x10, ACG2DCTL=0x10 → DIVM=÷2, DIVI=÷1 for both synthesizer outputs (datasheet §6.5.3.10)
```

### 4-way dispatch on mode (SDCC/Keil-style subtract chain)

```
0x073c  e5 2e     mov a,0x2e         ; A = mode
0x073e  24 fe     add a,#0xfe        ; A = mode - 2
0x0740  60 1d     jz 0x075f          ; mode 2 → internal 44.1 kHz branch
0x0742  14        dec a              ; A = mode - 3
0x0743  60 49     jz 0x078e          ; mode 3 → internal 48 kHz branch
0x0745  24 fe     add a,#0xfe        ; A = mode - 5
0x0747  60 50     jz 0x0799          ; mode 5 → codec-port/ACG-undivided branch
0x0749  24 04     add a,#0x4         ; A = mode - 1
0x074b  70 78     jnz 0x07c5         ; any other mode (4, 8, ...) → common tail, no clock change
```

### Mode 1: MCLK from external MCLKI pin (externally recovered clock)

```
0x074d  90 ff e1  mov dptr,#0xffe1   ; DPTR = ACGCTL
0x0750  74 0d     mov a,#0xd         ; 0x0D = MCLKO1S=01b (MCLKO1 ← mclki÷I), DIVEN=1, MCLKO2S=01b (MCLKO2 ← mclki÷I); MCLKO1EN/2EN still 0 (datasheet §6.5.3.11)
0x0752  f0        movx @dptr,a       ; SFR write ACGCTL=0x0D: route both master clock outputs from the MCLKI input pin (external clock source; per project context MCLKI carries the S/PDIF-recovered clock — inference, wiring not in repo)
0x0753  75 08 01  mov 0x08,#0x1      ; state var 0x08 = mode 1
0x0756  75 31 04  mov 0x31,#0x4      ; pending serial-chip write: register 4
0x0759  75 32 41  mov 0x32,#0x41     ; ... value 0x41 (vs 0x40 for internal-clock modes; bit-level meaning needs CS8427 datasheet — UNVERIFIED device identity)
0x075c  02 07 c5  ljmp 0x07c5        ; → common tail
```

### Mode 2: internal ACG at 22.5792 MHz (512 x 44.1 kHz)

Writes N=0x6A4B20 into both synthesizers. TI SoftPll.c programs the identical
three bytes with the comment "PLL1 = 22.5792 MHz". Note the datasheet rule
that the synthesizer only loads on the FRQ0 write — both sequences correctly
write FRQ0 last.

```
0x075f  90 ff e6  mov dptr,#0xffe6   ; DPTR = ACG1FRQ1
0x0762  74 4b     mov a,#0x4b        ; middle byte of N
0x0764  f0        movx @dptr,a       ; SFR write ACG1FRQ1=0x4B
0x0765  90 ff e5  mov dptr,#0xffe5   ; DPTR = ACG1FRQ2
0x0768  74 6a     mov a,#0x6a        ; top byte of N
0x076a  f0        movx @dptr,a       ; SFR write ACG1FRQ2=0x6A
0x076b  90 ff e7  mov dptr,#0xffe7   ; DPTR = ACG1FRQ0
0x076e  74 20     mov a,#0x20        ; low byte of N
0x0770  f0        movx @dptr,a       ; SFR write ACG1FRQ0=0x20 — loads N=0x6A4B20 → 600/26.5731 = 22.5792 MHz (ACG1)
0x0771  90 ff f8  mov dptr,#0xfff8   ; DPTR = ACG2FRQ1
0x0774  74 4b     mov a,#0x4b        ;
0x0776  f0        movx @dptr,a       ; SFR write ACG2FRQ1=0x4B
0x0777  90 ff f7  mov dptr,#0xfff7   ; DPTR = ACG2FRQ2
0x077a  74 6a     mov a,#0x6a        ;
0x077c  f0        movx @dptr,a       ; SFR write ACG2FRQ2=0x6A
0x077d  90 ff f9  mov dptr,#0xfff9   ; DPTR = ACG2FRQ0 (write deferred into helper)
0x0780  74 20     mov a,#0x20        ; A = low byte
0x0782  12 0e 0f  lcall 0x0e0f       ; -> fcn_0e0f: MOVX @DPTR,A → ACG2FRQ0=0x20 (loads ACG2 = 22.5792 MHz too), then ACGCTL=0x06 = MCLKO1←acg1_clk÷M, DIVEN=1, MCLKO2S=10b→acg2_clk÷M (internal synth routing, outputs not yet enabled)
0x0785  75 08 02  mov 0x08,#0x2      ; state var 0x08 = mode 2
0x0788  12 0e 20  lcall 0x0e20       ; -> fcn_0e20: pending serial write = reg 4, value 0x40
0x078b  02 07 c5  ljmp 0x07c5        ; → common tail
```

### Mode 3: internal ACG at 24.576 MHz (512 x 48 kHz)

```
0x078e  12 0d ec  lcall 0x0dec       ; -> fcn_0dec: ACG1FRQ=ACG2FRQ=61 A8 0F (datasheet example 24.576 MHz = 61 A8 00, +0x0F LSB trim), then ACGCTL=0x06 (internal routing) via fcn_0e0f
0x0791  75 08 03  mov 0x08,#0x3      ; state var 0x08 = mode 3
0x0794  12 0e 20  lcall 0x0e20       ; pending serial write = reg 4, value 0x40
0x0797  80 2c     sjmp 0x07c5        ; → common tail
```

### Mode 5: codec-port reconfig, ACG1 undivided, ACG at 48 kHz family

This branch bounces the codec port (GLOBCTL.CPTEN), programs the I2S-mode-5
receive-side SCLK2 divider, and leaves ACG1 with no output divider.

```
0x0799  90 ff b1  mov dptr,#0xffb1   ; DPTR = GLOBCTL
0x079c  e0        movx a,@dptr       ; read GLOBCTL
0x079d  54 fe     anl a,#0xfe        ; clear bit0 = CPTEN (datasheet GLOBCTL table: bit0 = codec port enable)
0x079f  f0        movx @dptr,a       ; SFR write GLOBCTL &= ~CPTEN — disable codec port interface during reconfig
0x07a0  90 ff d4  mov dptr,#0xffd4   ; DPTR = CPTRXCNF4 (codec port receive config 4, used only in I2S Mode 5, datasheet §6.5.4.13)
0x07a3  74 01     mov a,#0x1         ; DIVB2=001b
0x07a5  f0        movx @dptr,a       ; SFR write CPTRXCNF4=0x01 → SCLK2 = MCLKO2 / 2
0x07a6  90 ff b1  mov dptr,#0xffb1   ; DPTR = GLOBCTL again
0x07a9  e0        movx a,@dptr       ; read GLOBCTL
0x07aa  44 01     orl a,#0x1         ; set CPTEN
0x07ac  12 0d eb  lcall 0x0deb       ; -> 0x0deb (byte 0xF0 = MOVX @DPTR,A, verified in binary): GLOBCTL |= CPTEN, then falls through fcn_0dec+fcn_0e0f: both ACGs = 61 A8 0F (24.576 MHz) and ACGCTL=0x06; returns with DPTR=0xFFE1
0x07af  a3        inc dptr           ; DPTR = 0xFFE2 = ACG1DCTL (relies on fcn_0e0f leaving DPTR at ACGCTL)
0x07b0  e4        clr a              ; A = 0
0x07b1  f0        movx @dptr,a       ; SFR write ACG1DCTL=0x00 → DIVM=÷1, DIVI=÷1: MCLKO1 = full 24.576 MHz (overrides the ÷2 set at entry)
0x07b2  90 ff f6  mov dptr,#0xfff6   ; DPTR = ACG2DCTL
0x07b5  74 10     mov a,#0x10        ; DIVM=0001b
0x07b7  f0        movx @dptr,a       ; SFR write ACG2DCTL=0x10 → MCLKO2 = 24.576/2 = 12.288 MHz
0x07b8  d2 18     setb 0x18          ; set control shift-reg bit 0x23.0 — mode-5-only output, board meaning UNKNOWN (external wiring not in repo)
0x07ba  d2 19     setb 0x19          ; set control shift-reg bit 0x23.1 — same UNKNOWN
0x07bc  12 0e 62  lcall 0x0e62       ; push shift register to hardware (applies bits 0x18/0x19 now, while 0x1A/0x1B still low)
0x07bf  75 08 05  mov 0x08,#0x5      ; state var 0x08 = mode 5
0x07c2  12 0e 20  lcall 0x0e20       ; pending serial write = reg 4, value 0x40
```

### Common tail: serial-chip reg-4 write, enable MCLK outputs, re-arm iso EPs, unmute, settle

```
0x07c5  ad 32     mov r5,0x32        ; R5 = pending value (0x40/0x41, or caller-preset for modes without a branch)
0x07c7  af 31     mov r7,0x31        ; R7 = pending register number (4)
0x07c9  12 0c 45  lcall 0x0c45       ; -> fcn_0c45: 3-byte serial write {0x20, reg, val} on P1.4/P1.3, CS = shift-reg bit 0x25.7 — programs the external audio chip's clock/control register 4 (prior notes: CS8427; identity UNVERIFIED here)
0x07cc  90 ff e1  mov dptr,#0xffe1   ; DPTR = ACGCTL
0x07cf  e0        movx a,@dptr       ; read ACGCTL (0x0D or 0x06 per branch)
0x07d0  44 c0     orl a,#0xc0        ; set bit7 MCLKO2EN + bit6 MCLKO1EN
0x07d2  f0        movx @dptr,a       ; SFR write ACGCTL |= 0xC0 — enable both master clock output pins (datasheet §6.5.3.11)
0x07d3  90 ff 63  mov dptr,#0xff63   ; DPTR = IEPDCNTX1
0x07d6  e4        clr a              ; A = 0
0x07d7  f0        movx @dptr,a       ; SFR write IEPDCNTX1=0 — clear IN EP1 X-buffer byte count (empty the iso IN buffers after clock change)
0x07d8  90 ff 67  mov dptr,#0xff67   ; DPTR = IEPDCNTY1
0x07db  f0        movx @dptr,a       ; SFR write IEPDCNTY1=0 — clear IN EP1 Y-buffer byte count
0x07dc  90 ff 9b  mov dptr,#0xff9b   ; DPTR = OEPDCNTX2
0x07df  f0        movx @dptr,a       ; SFR write OEPDCNTX2=0 — clear OUT EP2 X-buffer byte count
0x07e0  90 ff 9f  mov dptr,#0xff9f   ; DPTR = OEPDCNTY2
0x07e3  f0        movx @dptr,a       ; SFR write OEPDCNTY2=0 — clear OUT EP2 Y-buffer byte count
0x07e4  90 ff 60  mov dptr,#0xff60   ; DPTR = IEPCNF1
0x07e7  74 c5     mov a,#0xc5        ; 0xC5 iso layout (datasheet §6.4.4.6.2): IEPEN=1, ISO=1, OVF=0, BPS=00101b = 6 bytes/sample (24-bit stereo frame)
0x07e9  f0        movx @dptr,a       ; SFR write IEPCNF1=0xC5 — IN EP1 enabled, isochronous, 6 bytes per audio frame (capture stream)
0x07ea  90 ff 98  mov dptr,#0xff98   ; DPTR = OEPCNF2
0x07ed  f0        movx @dptr,a       ; SFR write OEPCNF2=0xC5 — OUT EP2 enabled, isochronous, 6 bytes per frame (playback stream; OUT iso layout §6.4.3.6.2 = OEPEN|ISO|OVF|BPS, identical encoding)
0x07ee  d2 1a     setb 0x1a          ; re-assert shift-reg bit 0x23.2 (deasserted at entry; UNKNOWN board meaning, pattern = unmute/re-engage)
0x07f0  d2 1b     setb 0x1b          ; re-assert shift-reg bit 0x23.3 (same UNKNOWN)
0x07f2  12 0e 62  lcall 0x0e62       ; push shift register — apply re-asserted bits to hardware
0x07f5  e4        clr a              ; A = 0
0x07f6  f5 2f     mov 0x2f,a         ; delay counter hi = 0
0x07f8  f5 30     mov 0x30,a         ; delay counter lo = 0
```

Settle busy-wait: counts {0x2F:0x30} up until lo==0xFF AND hi==0x0F, i.e.
roughly 0x0F00+0xFF ≈ 4000 iterations of a ~6-cycle loop — a fixed software
delay letting the new clocks/PLL settle before the caller resumes streaming.

```
0x07fa  05 30     inc 0x30           ; lo++            (loop head; also entered from out-of-range 0x0A48, which reuses this delay+RET tail)
0x07fc  e5 30     mov a,0x30         ; A = lo
0x07fe  70 02     jnz 0x0802         ; no wrap → skip hi increment
0x0800  05 2f     inc 0x2f           ; lo wrapped 0x00 → hi++
0x0802  b4 ff f5  cjne a,#0xff,0x07fa ; keep looping until lo == 0xFF
0x0805  e5 2f     mov a,0x2f         ; A = hi
0x0807  b4 0f f0  cjne a,#0xf,0x07fa ; keep looping until hi == 0x0F (total ≈ 0x1000 lo-increments)
0x080a  22        ret                ; done — clocks reprogrammed and settled (also jump target from out-of-range 0x0FBA)
```

Flow leaving this range: LCALL targets 0x0E62, 0x0E18, 0x0E0F, 0x0E20, 0x0DEB/0x0DEC, 0x0C45 (all summarized above, owned by other annotators); incoming jumps from 0x0A48 (into the delay loop at 0x0802) and 0x0FBA (to the RET at 0x080A).

### 5.6 Range 0x080B-0x0A08

# Rev 20 annotation — range 0x080b-0x0a09 (exclusive)

All raw bytes in this range were verified against `rev20_firmware_code.bin`
(`xxd -s 0x080b -l 0x1fe`) and match the Ghidra listing byte-for-byte.
The whole range is code; there are **no data regions**.

## IRAM state used by this range (verified against listing cross-references)

| Loc | What it is (evidence) |
|-----|-----------------------|
| byte 0x22 (bits 0x10-0x17) | shadow byte for serial device B; fcn_0f0c (outside range, read) shifts it out on P1.7=data / P1.5=clock with P1.6 pulled low first (select) |
| byte 0x23 (bits 0x18-0x1f) | first byte of a 16-bit external shift-register/latch chain; fcn_0e62 (outside range, read) shifts 0x23 then 0x25 MSB-first on P1.0=data / P1.2=clock and pulses P1.1 as latch |
| byte 0x25 (bits 0x28-0x2f) | second byte of that latch chain. Bit 0x2f (=0x25.7) is cleared by fcn_0c45 before, and set after, each 3-byte serial transaction — it is the CS/enable line of the fcn_0c45 serial target, routed through the latch chain |
| bytes 0x2e/0x2f | scratch: busy-delay counters, wait-loop counters, and (reg,value) staging for the chip-write helpers. NOTE: bit addresses 0x2e/0x2f (= 0x25.6/0x25.7) are distinct from these bytes and both are used in this range |
| byte 0x08 | status/state code read back by the EP0 GET handler at 0x0098 (compares #1/#2/#3 and returns different payloads). Mode handlers set it to 1/2/3/5 (0x0753/0x0785/0x0791/0x07bf). Exact host-visible semantics UNKNOWN |
| byte 0x0a | main-loop action code; read at 0x0ad6/0x02ee (verified), 0 = idle |
| bytes 0x09/0x0b | cleared here; used by EP0 transfer code (NOTES.md calls them EP0 IN byte counts — unverified) |
| byte 0x0c | initialized to 0xFE here; consumer not identified in this pass — UNKNOWN |
| bits 0x21.0/1/2/6 (0x08/0x09/0x0a/0x0e) | event/pending flags cleared at end of usb_ep_dma_init; semantics owned by EP0/main-loop annotators |

Out-of-range helpers referenced (each read in the listing for this pass):
- **fcn_0c45** — 3-byte bit-banged serial write on P1.4=data/P1.3=clock: sends constant `0x20` (`7b20` at 0x0c4b — matches CS8427 I2C write address 0b0010000+W; chip identity *likely*, not hardware-verified), then register (R7, saved to 0x33), then value (R5). Clears bit 0x25.7 + shifts latch chain (CS low) first.
- **fcn_0e62** — shifts IRAM 0x23 then 0x25 out the P1.0/P1.2/P1.1 latch chain (16 bits + latch pulse).
- **fcn_0f0c** — shifts IRAM 0x22 out P1.7/P1.5 with P1.6 low (second serial device).
- **fcn_0deb** — `movx @dptr,a` then falls into fcn_0dec: programs ACGFRQ2/1/0 = 0x61,0xA8,0x0F and ACG2FRQ2/1/0 the same (N=0x61A80F/2^18≈24.414 → 600/N ≈ 24.576 MHz, the datasheet §2.2.6.1 example value +0x0F LSB), then ACGCTL=0x06 (DIVEN=1, MCLKO2 source=acg2_clk÷M, MCLKO1 source=acg_clk÷M), leaves DPTR=0xFFE1, RET.
- **fcn_0e17** — `inc dptr` (0xFFE1→0xFFE2=ACG1DCTL), writes 0x10, then ACG2DCTL(0xFFF6)=0x10: DIVM=0001b (÷2), DIVI=000b (÷1) → MCLKO = 24.576/2 = **12.288 MHz = 256×48 kHz**.

---

## fcn_080b = audio_path_reconfig_ext_chips (0x080b-0x08a5)

Called (LCALL) from 0x0360, 0x0392, 0x0419, 0x04c7 — the sample-rate /
clock-source mode handlers — and via AJMP stub 0x0fb4. The stub table at
0x0fa9+ also enters at 0x080c/0x080e (with a caller-supplied value in A)
and at 0x0821 (skipping the latch-chain reset). It clears both latch
shadows, re-enables the master clock outputs, walks the latch bits up in
a timed sequence, pulses the serial chip's CS/reset line, then rewrites
the external chip's register file.

```
0x080b  e4         clr a                 ; A=0 (full entry: reset both latch shadows)
0x080c  f5 25      mov 0x25,a            ; latch-chain shadow byte 2 = 0 (alt entry from 0x0fb1 with caller's A)
0x080e  f5 23      mov 0x23,a            ; latch-chain shadow byte 1 = 0 (alt entry from 0x0fb7)
0x0810  d2 2e      setb 0x2e             ; set BIT 0x2e = 0x25.6 in latch shadow; UNKNOWN — board net driven by this latch output not identified
0x0812  75 2e ff   mov 0x2e,#0xff        ; BYTE 0x2e = 0xff: busy-delay counter (distinct location from bit 0x2e above)
0x0815  d5 2e fd   djnz 0x2e,0x0815      ; ~255-iteration busy delay
0x0818  12 0e 62   lcall 0x0e62          ; -> fcn_0e62: shift 0x23/0x25 out P1.0/P1.2, pulse P1.1 latch (chain now 0x0000 + 0x25.6)
0x081b  12 0d ec   lcall 0x0dec          ; -> fcn_0dec: ACGFRQ/ACG2FRQ=0x61A80F (~24.576MHz synth), ACGCTL=0x06 (DIVEN, MCLKO srcs); leaves DPTR=0xFFE1
0x081e  12 0e 17   lcall 0x0e17          ; -> fcn_0e17: ACG1DCTL=0x10, ACG2DCTL=0x10 (÷2 → 12.288MHz MCLK) — uses DPTR left by 0x0dec
0x0821  75 08 03   mov 0x08,#0x3         ; state byte 0x08=3 (code returned by EP0 GET handler at 0x0098; host-visible meaning UNKNOWN); alt entry from 0x0fab
0x0824  90 ff e1   mov dptr,#0xffe1      ; DPTR -> ACGCTL (Reg_stc1.h)
0x0827  e0         movx a,@dptr          ; read ACGCTL (currently 0x06 from 0x0dec)
0x0828  44 c0      orl a,#0xc0           ; set bits 7|6 = MCLKO2EN|MCLKO1EN (datasheet §6.5.3.11)
0x082a  f0         movx @dptr,a          ; ACGCTL |= 0xC0: enable both MCLK output pins
0x082b  75 2e ff   mov 0x2e,#0xff        ; delay counter
0x082e  d5 2e fd   djnz 0x2e,0x082e      ; busy delay (let MCLK start/settle)
0x0831  d2 1a      setb 0x1a             ; latch shadow bit 0x23.2 = 1; UNKNOWN — external net not identified (codec/relay/mute control candidate)
0x0833  d2 1b      setb 0x1b             ; latch shadow bit 0x23.3 = 1; UNKNOWN — external net not identified
0x0835  12 0e 62   lcall 0x0e62          ; shift chain out (apply 0x23.2/0x23.3)
0x0838  75 2e ff   mov 0x2e,#0xff        ; delay counter
0x083b  d5 2e fd   djnz 0x2e,0x083b      ; busy delay
0x083e  d2 2f      setb 0x2f             ; bit 0x25.7 = 1 (serial-chip CS/enable line high = idle)
0x0840  d2 1c      setb 0x1c             ; latch shadow bit 0x23.4 = 1; UNKNOWN — external net not identified
0x0842  12 0e 62   lcall 0x0e62          ; shift chain out
0x0845  75 2e ff   mov 0x2e,#0xff        ; delay counter
0x0848  d5 2e fd   djnz 0x2e,0x0848      ; busy delay
0x084b  c2 2f      clr 0x2f              ; bit 0x25.7 = 0 ...
0x084d  12 0e 62   lcall 0x0e62          ; ... shift out: drive CS/reset line LOW
0x0850  d2 2f      setb 0x2f             ; bit 0x25.7 = 1 ...
0x0852  12 0e 62   lcall 0x0e62          ; ... shift out: line HIGH again — a full low pulse on the serial chip's CS/reset before programming it
0x0855  12 08 a6   lcall 0x08a6          ; -> extchip_write_reg4_zero: ext reg 0x04 = 0x00 (via fcn_0c45; chip likely CS8427, reg semantics unverified)
0x0858  75 2e 13   mov 0x2e,#0x13        ; reg = 0x13
0x085b  75 2f 10   mov 0x2f,#0x10        ; value = 0x10
0x085e  12 08 bd   lcall 0x08bd          ; -> extchip_write_2e_2f: ext reg 0x13 = 0x10
0x0861  12 08 a6   lcall 0x08a6          ; ext reg 0x04 = 0x00 again (written before and after reg 0x13)
0x0864  75 2e 04   mov 0x2e,#0x4         ; reg = 0x04
0x0867  75 2f 40   mov 0x2f,#0x40        ; value = 0x40
0x086a  12 08 bd   lcall 0x08bd          ; ext reg 0x04 = 0x40
0x086d  75 2e 01   mov 0x2e,#0x1         ; reg = 0x01
0x0870  75 2f 01   mov 0x2f,#0x1         ; value = 0x01
0x0873  12 08 c4   lcall 0x08c4          ; -> extchip_write_2e_2f_dup: ext reg 0x01 = 0x01
0x0876  75 2e 02   mov 0x2e,#0x2         ; reg = 0x02
0x0879  75 2f 20   mov 0x2f,#0x20        ; value = 0x20
0x087c  12 08 c4   lcall 0x08c4          ; ext reg 0x02 = 0x20
0x087f  75 2e 03   mov 0x2e,#0x3         ; reg = 0x03
0x0882  75 2f 0c   mov 0x2f,#0xc         ; value = 0x0C
0x0885  ad 2f      mov r5,0x2f           ; R5 = value (fcn_0c45 calling convention)
0x0887  af 2e      mov r7,0x2e           ; R7 = register
0x0889  12 0c 45   lcall 0x0c45          ; ext reg 0x03 = 0x0C (inlined instead of thunk)
0x088c  75 2e 05   mov 0x2e,#0x5         ; reg = 0x05
0x088f  12 08 b3   lcall 0x08b3          ; -> extchip_write_val05: ext reg 0x05 = 0x05
0x0892  75 2e 06   mov 0x2e,#0x6         ; reg = 0x06
0x0895  12 08 b3   lcall 0x08b3          ; ext reg 0x06 = 0x05
0x0898  75 2e 11   mov 0x2e,#0x11        ; reg = 0x11
0x089b  75 2f ff   mov 0x2f,#0xff        ; value = 0xFF
0x089e  ad 2f      mov r5,0x2f           ; R5 = 0xFF
0x08a0  af 2e      mov r7,0x2e           ; R7 = 0x11
0x08a2  12 0c 45   lcall 0x0c45          ; ext reg 0x11 = 0xFF
0x08a5  22         ret                   ; done — external chip register file programmed
```

Register-write summary sent to the fcn_0c45 serial target (order):
reg 04=00, 13=10, 04=00, 04=40, 01=01, 02=20, 03=0C, 05=05, 06=05, 11=FF.
The 0x20 chip-address preamble matches a CS8427 I2C write; the register
numbers are consistent with a CS8427 map (1/2=Misc Ctrl, 3=Data Flow,
4=Clock Source, 5/6=serial port formats) but **no CS8427 datasheet exists
in this repo, so the field-level meaning of these values is unverified**.

## fcn_08a6 = extchip_write_reg4_zero (0x08a6-0x08b2)

```
0x08a6  75 2e 04   mov 0x2e,#0x4         ; register = 0x04
0x08a9  e4         clr a                 ; A = 0
0x08aa  f5 2f      mov 0x2f,a            ; value = 0x00
0x08ac  ad 2f      mov r5,0x2f           ; R5 = value
0x08ae  af 2e      mov r7,0x2e           ; R7 = register
0x08b0  02 0c 45   ljmp 0x0c45           ; tail-jump -> fcn_0c45: ext reg 0x04 = 0x00, RET from there
```

## fcn_08b3 = extchip_write_val05 (0x08b3-0x08bc)

```
0x08b3  75 2f 05   mov 0x2f,#0x5         ; value = 0x05 (caller pre-loads register in 0x2e)
0x08b6  ad 2f      mov r5,0x2f           ; R5 = 0x05
0x08b8  af 2e      mov r7,0x2e           ; R7 = register
0x08ba  02 0c 45   ljmp 0x0c45           ; tail-jump -> fcn_0c45: ext reg [0x2e] = 0x05
```

## fcn_08bd = extchip_write_2e_2f (0x08bd-0x08c3)

```
0x08bd  ad 2f      mov r5,0x2f           ; R5 = value from IRAM 0x2f
0x08bf  af 2e      mov r7,0x2e           ; R7 = register from IRAM 0x2e
0x08c1  02 0c 45   ljmp 0x0c45           ; tail-jump -> fcn_0c45: ext reg [0x2e] = [0x2f]
```

## fcn_08c4 = extchip_write_2e_2f_dup (0x08c4-0x08ca)

Byte-identical duplicate of fcn_08bd (two static copies emitted).

```
0x08c4  ad 2f      mov r5,0x2f           ; R5 = value
0x08c6  af 2e      mov r7,0x2e           ; R7 = register
0x08c8  02 0c 45   ljmp 0x0c45           ; tail-jump -> fcn_0c45
```

## fcn_08cb = hw_master_init (0x08cb-0x096f)

Called from startup (0x0aad, immediately before usb_ep_dma_init at 0x0ab0)
and from the soft-reset path (0x0551). Labels 0x0902/0x0904 are entered
from the 0x0596-0x0717 dynamic-reconfig region (XREFs 0x066f, 0x0638 —
that region is partly force-disassembled data, its annotator owns those
sites); entry at 0x0902 re-runs the whole C-port + ACG + CPTEN tail with
DPTR pre-set by the jumper.

```
0x08cb  e4         clr a                 ; A=0
0x08cc  f5 2e      mov 0x2e,a            ; wait-loop counter low = 0 (used at 0x0946)
0x08ce  f5 2f      mov 0x2f,a            ; wait-loop counter high = 0
0x08d0  90 ff fc   mov dptr,#0xfffc      ; DPTR -> USBCTL
0x08d3  f0         movx @dptr,a          ; USBCTL = 0x00: CONT=0 (D+ pullup PUR Hi-Z = USB disconnected), FEN=0, RWUP=0, FRSTE=0
0x08d4  90 ff b0   mov dptr,#0xffb0      ; DPTR -> MEMCFG
0x08d7  04         inc a                 ; A = 1
0x08d8  f0         movx @dptr,a          ; MEMCFG = 0x01: SDW=1, code fetches from shadow program RAM (app already runs from it)
0x08d9  e4         clr a                 ; A = 0
0x08da  f5 90      mov 0x90,a            ; P1 = 0x00: all bit-bang lines (data/clock/latch/CS) driven low
0x08dc  75 b0 ff   mov 0xb0,#0xff        ; P3 = 0xFF: release P3 (quasi-bidir inputs / high)
0x08df  75 8c ce   mov 0x8c,#0xce        ; TH0 = 0xCE: Timer0 initial high byte (tick timer; ISR reload behavior outside this range)
0x08e2  f5 8a      mov 0x8a,a            ; TL0 = 0
0x08e4  f5 8d      mov 0x8d,a            ; TH1 = 0
0x08e6  f5 8b      mov 0x8b,a            ; TL1 = 0
0x08e8  75 89 11   mov 0x89,#0x11        ; TMOD = 0x11: Timer0 mode 1 (16-bit), Timer1 mode 1
0x08eb  f5 88      mov 0x88,a            ; TCON = 0: timers stopped, flags cleared, INT0/1 level-triggered
0x08ed  c2 af      clr 0xaf              ; EA = 0: global interrupts off during init
0x08ef  c2 ac      clr 0xac              ; ES = 0: serial interrupt disabled
0x08f1  c2 aa      clr 0xaa              ; EX1 = 0: external int 1 disabled
0x08f3  d2 a9      setb 0xa9             ; ET0 = 1: Timer0 tick interrupt enabled (vector 0x000B)
0x08f5  c2 ab      clr 0xab              ; ET1 = 0: Timer1 interrupt disabled
0x08f7  d2 a8      setb 0xa8             ; EX0 = 1: INT0 = USB engine interrupt enabled (vector 0x0003)
0x08f9  f5 b8      mov 0xb8,a            ; IP = 0: all interrupts low priority
0x08fb  a3         inc dptr              ; DPTR 0xFFB0 -> 0xFFB1 = GLOBCTL
0x08fc  74 06      mov a,#0x6            ; value 0x06
0x08fe  f0         movx @dptr,a          ; GLOBCTL = 0x06: LPWR=1 (normal power), P3PUDIS=1 (P3 pullups off); CPTEN=0 for now (set at 0x093a)
0x08ff  90 ff e0   mov dptr,#0xffe0      ; DPTR -> CPTCNF1
0x0902  74 0d      mov a,#0xd            ; value 0x0D (label entered from 0x066f in dynamic-reconfig region)
0x0904  f0         movx @dptr,a          ; CPTCNF1 = 0x0D: NTSL=00001b (2 time slots/frame), MODE=101b (I2S mode 5: 1 OUT + 1 IN at different rates) — datasheet §6.5.4.1 (label entered from 0x0638)
0x0905  90 ff df   mov dptr,#0xffdf      ; DPTR -> CPTCNF2
0x0908  74 e5      mov a,#0xe5           ; value 0xE5
0x090a  f0         movx @dptr,a          ; CPTCNF2 = 0xE5: TSL0L=11b (slot0 = 32 CSCLK), BPTSL=100b (24 data bits/slot), TSLL=101b (32 CSCLK/slot) — 24-bit-in-32 I2S
0x090b  90 ff de   mov dptr,#0xffde      ; DPTR -> CPTCNF3
0x090e  74 ac      mov a,#0xac           ; value 0xAC
0x0910  f0         movx @dptr,a          ; CPTCNF3 = 0xAC: DDLY=1 (1-clk data delay, I2S framing), TRSEN=0, CSCLKP=1, CSYNCP=0, CSYNCL=1 (LRCK-style sync), BYOR=1 (byte-swap DMA data), CSCLK/CSYNC = outputs
0x0911  90 ff dd   mov dptr,#0xffdd      ; DPTR -> CPTCNF4
0x0914  74 03      mov a,#0x3            ; value 0x03
0x0916  f0         movx @dptr,a          ; CPTCNF4 = 0x03: ATSL=0, CPTBLK=0, DIVB=011b → CSCLK = MCLKO/4 (12.288MHz/4 = 3.072MHz = 64fs @48k)
0x0917  90 ff dc   mov dptr,#0xffdc      ; DPTR -> CPTCTL
0x091a  74 50      mov a,#0x50           ; value 0x50
0x091c  f0         movx @dptr,a          ; CPTCTL = 0x50: RXIE=1, TXIE=1 (C-port rx-full/tx-empty interrupts enabled); CRST=0 (in mode 5 the CRESET pin is repurposed as SCLK2)
0x091d  90 ff d6   mov dptr,#0xffd6      ; DPTR -> CPTRXCNF2 (mode-5 receive side)
0x0920  74 25      mov a,#0x25           ; value 0x25
0x0922  f0         movx @dptr,a          ; CPTRXCNF2 = 0x25: BPTSL=100b (24 bits), TSLL=101b (32 CSCLK) — RX format mirrors TX
0x0923  90 ff d5   mov dptr,#0xffd5      ; DPTR -> CPTRXCNF3
0x0926  74 ac      mov a,#0xac           ; value 0xAC
0x0928  f0         movx @dptr,a          ; CPTRXCNF3 = 0xAC: same framing bits as CPTCNF3 for the receive interface
0x0929  90 ff d4   mov dptr,#0xffd4      ; DPTR -> CPTRXCNF4
0x092c  74 03      mov a,#0x3            ; value 0x03
0x092e  12 0d eb   lcall 0x0deb          ; -> fcn_0deb: writes CPTRXCNF4=0x03 (RX DIVB=011b) then falls into fcn_0dec: ACGFRQ/ACG2FRQ=0x61A80F (~24.576MHz), ACGCTL=0x06; leaves DPTR=0xFFE1
0x0931  12 0e 17   lcall 0x0e17          ; -> fcn_0e17: ACG1DCTL=0x10 and ACG2DCTL=0x10 (DIVM=÷2, DIVI=÷1) → MCLKO 12.288MHz
0x0934  90 ff b1   mov dptr,#0xffb1      ; DPTR -> GLOBCTL
0x0937  e0         movx a,@dptr          ; read GLOBCTL (0x06)
0x0938  44 01      orl a,#0x1            ; set bit 0 = CPTEN
0x093a  f0         movx @dptr,a          ; GLOBCTL |= 0x01: enable codec port (config regs are fully programmed first, as datasheet requires)
0x093b  75 08 03   mov 0x08,#0x3         ; state byte 0x08 = 3 (EP0-GET status code; see IRAM table — semantics UNKNOWN)
0x093e  e4         clr a                 ; A = 0
0x093f  f5 22      mov 0x22,a            ; serial-device-B shadow byte 0x22 = 0
0x0941  d2 1e      setb 0x1e             ; latch shadow bit 0x23.6 = 1; UNKNOWN — external net not identified
0x0943  12 0f 0c   lcall 0x0f0c          ; -> fcn_0f0c: shift 0x22 (=0x00) out P1.7/P1.5 with P1.6 select — clear device B's register
0x0946  e5 2f      mov a,0x2f            ; wait loop: A = counter high byte (0x2e/0x2f zeroed at entry)
0x0948  f4         cpl a                 ; A = ~0x2f
0x0949  70 04      jnz 0x094f            ; if 0x2f != 0xFF, keep looping (Z-flag path below)
0x094b  e5 2e      mov a,0x2e            ; else check low byte
0x094d  64 0f      xrl a,#0xf            ; A = 0 iff 0x2e == 0x0F
0x094f  60 0a      jz 0x095b             ; exit when 0x2f==0xFF and 0x2e==0x0F (~4095 iterations settle delay)
0x0951  05 2f      inc 0x2f              ; bump inner counter
0x0953  e5 2f      mov a,0x2f            ; test for wrap
0x0955  70 ef      jnz 0x0946            ; not wrapped -> loop
0x0957  05 2e      inc 0x2e              ; wrapped: bump outer counter
0x0959  80 eb      sjmp 0x0946           ; loop
0x095b  75 22 ff   mov 0x22,#0xff        ; device-B shadow = 0xFF ...
0x095e  c2 10      clr 0x10              ; ... clear bit 0x22.0 (shadow now 0xFE); UNKNOWN — device-B bit function not identified
0x0960  c2 13      clr 0x13              ; ... clear bit 0x22.3 (shadow now 0xF6); UNKNOWN — device-B bit function not identified
0x0962  c2 1e      clr 0x1e              ; latch shadow bit 0x23.6 back to 0 (was set at 0x0941)
0x0964  12 0f 0c   lcall 0x0f0c          ; shift 0x22=0xF6 out to device B
0x0967  e4         clr a                 ; A = 0
0x0968  f5 25      mov 0x25,a            ; latch shadow byte 2 = 0
0x096a  f5 23      mov 0x23,a            ; latch shadow byte 1 = 0
0x096c  12 0e 62   lcall 0x0e62          ; shift all-zero latch chain out (all latch outputs low)
0x096f  22         ret                   ; hardware base state established; USB still disconnected
```

## fcn_0970 = usb_ep_dma_init (0x0970-0x0a08)

Straight-line SFR programming. Buffer addresses follow the TI formula
`addr = 0xF800 + reg*8` (ROM/Mmap.h: `STC_BUFFER_BASE_ADDR 0xF800`,
`INPACK_OFFSET = (addr-0xF800)>>3`). Note: these bytes put the EP0 **OUT**
X buffer at 0xFA10 and EP0 **IN** at 0xFA18 — the brief's parenthetical
("EP0 IN 0xFA10") is swapped relative to what these bytes program.

```
0x0970  90 ff a9   mov dptr,#0xffa9      ; DPTR -> OEPBBAX0 (EP0 OUT X buffer base)
0x0973  74 42      mov a,#0x42           ; 0x42*8 = 0x210
0x0975  f0         movx @dptr,a          ; OEPBBAX0 = 0x42: EP0 OUT X buffer @ 0xFA10
0x0976  90 ff 69   mov dptr,#0xff69      ; DPTR -> IEPBBAX0 (EP0 IN X buffer base)
0x0979  04         inc a                 ; A = 0x43
0x097a  f0         movx @dptr,a          ; IEPBBAX0 = 0x43: EP0 IN X buffer @ 0xFA18 (8 bytes above OUT)
0x097b  90 ff ab   mov dptr,#0xffab      ; DPTR -> OEPDCNTX0
0x097e  e4         clr a                 ; A = 0
0x097f  f0         movx @dptr,a          ; OEPDCNTX0 = 0: byte count 0, NAK bit clear — EP0 OUT X armed to receive
0x0980  90 ff 6b   mov dptr,#0xff6b      ; DPTR -> IEPDCNTX0
0x0983  f0         movx @dptr,a          ; IEPDCNTX0 = 0: EP0 IN X count cleared
0x0984  90 ff af   mov dptr,#0xffaf      ; DPTR -> OEPDCNTY0
0x0987  f0         movx @dptr,a          ; OEPDCNTY0 = 0 (Y buffer count cleared; single-buffer mode anyway)
0x0988  90 ff 6f   mov dptr,#0xff6f      ; DPTR -> IEPDCNTY0
0x098b  f0         movx @dptr,a          ; IEPDCNTY0 = 0
0x098c  90 ff aa   mov dptr,#0xffaa      ; DPTR -> OEPBSIZ0
0x098f  04         inc a                 ; A = 1
0x0990  f0         movx @dptr,a          ; OEPBSIZ0 = 1: EP0 OUT buffer size 1*8 = 8 bytes (EP0 max packet 8)
0x0991  90 ff 6a   mov dptr,#0xff6a      ; DPTR -> IEPBSIZ0
0x0994  f0         movx @dptr,a          ; IEPBSIZ0 = 1: EP0 IN buffer 8 bytes
0x0995  90 ff a8   mov dptr,#0xffa8      ; DPTR -> OEPCNF0
0x0998  74 84      mov a,#0x84           ; value 0x84
0x099a  f0         movx @dptr,a          ; OEPCNF0 = 0x84: OEPEN=1 + OEPIE=1 (control EP0 OUT enabled with interrupt; not stalled, single-buffered)
0x099b  90 ff 68   mov dptr,#0xff68      ; DPTR -> IEPCNF0
0x099e  f0         movx @dptr,a          ; IEPCNF0 = 0x84: IEPEN=1 + IEPIE=1 (EP0 IN enabled with interrupt)
0x099f  90 ff 99   mov dptr,#0xff99      ; DPTR -> OEPBBAX2 (audio playback endpoint)
0x09a2  74 44      mov a,#0x44           ; 0x44*8 = 0x220
0x09a4  f0         movx @dptr,a          ; OEPBBAX2 = 0x44: OUT EP2 X buffer @ 0xFA20 (right after EP0 buffers)
0x09a5  90 ff 61   mov dptr,#0xff61      ; DPTR -> IEPBBAX1 (audio record endpoint)
0x09a8  74 94      mov a,#0x94           ; 0x94*8 = 0x4A0
0x09aa  f0         movx @dptr,a          ; IEPBBAX1 = 0x94: IN EP1 X buffer @ 0xFCA0
0x09ab  90 ff 9a   mov dptr,#0xff9a      ; DPTR -> OEPBSIZ2
0x09ae  74 50      mov a,#0x50           ; 0x50*8 = 640
0x09b0  f0         movx @dptr,a          ; OEPBSIZ2 = 0x50: OUT EP2 buffer 640 bytes (room for >1ms of 48k 24-bit stereo = 288 B/frame)
0x09b1  90 ff 62   mov dptr,#0xff62      ; DPTR -> IEPBSIZ1
0x09b4  f0         movx @dptr,a          ; IEPBSIZ1 = 0x50: IN EP1 buffer 640 bytes
0x09b5  90 ff 9b   mov dptr,#0xff9b      ; DPTR -> OEPDCNTX2
0x09b8  e4         clr a                 ; A = 0
0x09b9  f0         movx @dptr,a          ; OEPDCNTX2 = 0: clear OUT EP2 X byte count
0x09ba  90 ff 63   mov dptr,#0xff63      ; DPTR -> IEPDCNTX1
0x09bd  f0         movx @dptr,a          ; IEPDCNTX1 = 0: clear IN EP1 X byte count
0x09be  90 ff 98   mov dptr,#0xff98      ; DPTR -> OEPCNF2
0x09c1  74 c5      mov a,#0xc5           ; value 0xC5
0x09c3  f0         movx @dptr,a          ; OEPCNF2 = 0xC5 (iso layout §6.4.3.6.2): OEPEN=1, ISO=1, BPS=00101b = 6 bytes/sample-frame (24-bit stereo)
0x09c4  90 ff 60   mov dptr,#0xff60      ; DPTR -> IEPCNF1
0x09c7  f0         movx @dptr,a          ; IEPCNF1 = 0xC5: IEPEN=1, ISO=1, 6 bytes/sample-frame
0x09c8  90 ff ea   mov dptr,#0xffea      ; DPTR -> DMATSL0
0x09cb  74 03      mov a,#0x3            ; value 0x03
0x09cd  f0         movx @dptr,a          ; DMATSL0 = 0x03: DMA ch0 serves C-port time slots 0 and 1 (L+R)
0x09ce  90 ff e9   mov dptr,#0xffe9      ; DPTR -> DMATSH0
0x09d1  74 80      mov a,#0x80           ; value 0x80
0x09d3  f0         movx @dptr,a          ; DMATSH0 = 0x80: BPTS=10b = 3 bytes per time slot (24-bit samples); slots 13:8 = 0
0x09d4  90 ff f0   mov dptr,#0xfff0      ; DPTR -> DMATSL1
0x09d7  74 03      mov a,#0x3            ; value 0x03
0x09d9  f0         movx @dptr,a          ; DMATSL1 = 0x03: DMA ch1 slots 0 and 1
0x09da  90 ff ef   mov dptr,#0xffef      ; DPTR -> DMATSH1
0x09dd  74 80      mov a,#0x80           ; value 0x80
0x09df  f0         movx @dptr,a          ; DMATSH1 = 0x80: 3 bytes/slot
0x09e0  90 ff e8   mov dptr,#0xffe8      ; DPTR -> DMACTL0
0x09e3  74 02      mov a,#0x2            ; value 0x02
0x09e5  f0         movx @dptr,a          ; DMACTL0 = 0x02: DMAEN=0 (NOT enabled yet), EPDIR=0 (OUT), EPNUM=2 → ch0 will serve OUT EP2 playback when armed
0x09e6  90 ff ee   mov dptr,#0xffee      ; DPTR -> DMACTL1
0x09e9  74 09      mov a,#0x9            ; value 0x09
0x09eb  f0         movx @dptr,a          ; DMACTL1 = 0x09: DMAEN=0, EPDIR=1 (IN), EPNUM=1 → ch1 will serve IN EP1 record when armed
0x09ec  90 ff fd   mov dptr,#0xfffd      ; DPTR -> USBIMSK
0x09ef  74 9f      mov a,#0x9f           ; value 0x9F
0x09f1  f0         movx @dptr,a          ; USBIMSK = 0x9F: enable RSTR(7)+SOF(4)+PSOF(3)+SETUP(2)+STPOW(0) ints; bit1 reserved (written 1, reads 0); SUSR/RESR masked
0x09f2  90 ff ff   mov dptr,#0xffff      ; DPTR -> USBFADR
0x09f5  e4         clr a                 ; A = 0
0x09f6  f0         movx @dptr,a          ; USBFADR = 0: USB function address 0 (default/pre-enumeration)
0x09f7  c2 0a      clr 0x0a              ; clear bit 0x21.2 (pending-event flag; owned by EP0/main-loop code)
0x09f9  c2 0e      clr 0x0e              ; clear bit 0x21.6 (flag; consumer outside range)
0x09fb  c2 08      clr 0x08              ; clear bit 0x21.0 (flag; consumer outside range)
0x09fd  c2 09      clr 0x09              ; clear bit 0x21.1 (flag; consumer outside range)
0x09ff  f5 09      mov 0x09,a            ; BYTE 0x09 = 0 (EP0 transfer counter per NOTES — unverified; distinct from bit 0x09 above)
0x0a01  f5 0b      mov 0x0b,a            ; byte 0x0b = 0 (companion EP0 counter — unverified)
0x0a03  75 0c fe   mov 0x0c,#0xfe        ; byte 0x0c = 0xFE; UNKNOWN — consumer of 0x0c not identified in this pass
0x0a06  f5 0a      mov 0x0a,a            ; byte 0x0a = 0: main-loop action code = idle (read at 0x0ad6/0x02ee)
0x0a08  22         ret                   ; USB engine configured; CONT is still 0 — connect happens elsewhere
```

### Cross-range observations for neighbouring annotators
- The 0x0fa9-0x0fbd stub table AJMPs into this range at 0x0821, 0x080a,
  0x080b, 0x080c, 0x080e — and also at **0x0809 and 0x080d, which are
  mid-instruction** in the canonical decode (0x0809 = last byte of the
  `cjne` at 0x0807... actually 0x0809 is the byte `f0` = `movx @dptr,a`
  followed by `ret` at 0x080a, a valid overlapping tail; 0x080d = operand
  byte of `mov 0x25,a`). Whoever owns 0x0fa9+ must decide whether those
  stubs are live code or force-disassembled table data.
- FUN_08cb's C-port tail is re-entered at 0x0902/0x0904 from the
  0x0596-0x0717 dynamic-reconfig region with caller-prepared DPTR/A —
  that region's annotator owns the semantics of those entries.
- 0x07c9-0x080a (just below this range) writes the same OEPCNF2/IEPCNF1 =
  0xC5 values and ends with the same 0x0FFF-style wait loop; the `ret` at
  0x080a is also used as a jump-stub target (XREF 0x0fba).

### 5.7 Range 0x0A09-0x0B1D

# Rev 20 annotation — 0x0a09..0x0b1e

IRAM/bit variables tracked in this range (evidence given inline):
- **SP = 0x33** — stack grows from 0x34 up.
- **IRAM 0x0a** = pending event code, consumed by `fcn_02ee` (event dispatcher: `MOV A,0x0a; DEC A;` bounds-check vs 0x0e, `JMP @A+DPTR` into LJMP table at 0x0300 — verified in listing).
- **IRAM 0x27** = latched-P3.1 event state flag (0 = "event 0x0b not yet posted", 1 = posted; gates one 0x0b/0x0c pair per toggle).
- **IRAM 0x28:0x29** = 16-bit startup delay counter (hi:lo).
- **IRAM 0x2a, 0x2b** = written 0x00/0x10 here; no other direct reference anywhere in the listing (checked both byte refs and bit addrs 0x50-0x5F) — purpose UNKNOWN, possibly vestigial.
- **IRAM 0x20 (byte)** = last-sampled P3 value (stored by `fcn_0ed5` at 0x0f07: `MOV 0x20,R5` after `MOV R5,0xb0`=P3). Hence **bit 0x01** (= byte 0x20 bit 1) = latched state of pin **P3.1**.
- **bit 0x20** (= byte 0x24 bit 0) = millisecond tick flag, set by Timer0 ISR at 0x101e (`SETB 0x20`, TH0 reload 0xCE — verified in listing, matches brief).
- **bit 0x22** (= byte 0x24 bit 2) = cleared once at 0x0aab; no other reference found in the listing — UNKNOWN.
- **IRAM 0x1b:0x1c** = XDATA write pointer (hi:lo) used by the EP0 code; loaded with 0xFA10 = EP0 IN buffer here.

---

## fcn_0a09 — c51_startup (reset entry; `ljmp 0x0a09` at CODE:0000)

Classic Keil-style startup: wipe IRAM, set SP, run static-init records, enter main.

```
0x0a09  78 7f     mov r0,#0x7f        ; IRAM clear loop counter/pointer: start at 0x7F
0x0a0b  e4        clr a               ; A = 0 (fill value)
0x0a0c  f6        mov @r0,a           ; IRAM[R0] = 0
0x0a0d  d8 fd     djnz r0,0x0a0c      ; loop R0=0x7F..0x01 — clears IRAM 0x01-0x7F (0x00 untouched)
0x0a0f  75 81 33  mov sp,#0x33        ; SP = 0x33; stack lives at 0x34..top of IRAM
0x0a12  02 0a 50  ljmp 0x0a50         ; -> init-interpreter entry: point DPTR at init table 0x0f9c
```

## fcn_0a15..0x0a93 — c51_init_interpreter (Keil ?C?INIT equivalent)

Interprets the compile-time static-initializer record table at CODE **0x0f9c** (table itself is outside this range; first records seen there are 1-byte IDATA inits: `01 22 00`, `01 20 00`, ... = IRAM[0x22]=0, IRAM[0x20]=0 etc.). Record header byte: bits 7:6 = type, bit 5 = "big block" (extra length byte), bits 4:0/5:0 = length. Types decoded from the `ADD A,A` trick at 0x0a6c: 00=IDATA (`MOV @R0`), 10=paged XDATA (`MOVX @R0`), 01=XDATA block (16-bit dest), 11=bit-init into IRAM 0x20-0x2F. A 0x00 header terminates and control goes to main. All of this is verified instruction-by-instruction below — no external citation needed.

```
0x0a15  02 0a 95  ljmp 0x0a95         ; table terminator seen (JZ at 0x0a57) -> enter main
                                       ; (trampoline: JZ has only 8-bit reach)
```
IDATA / paged-XDATA record executor (type bits 00 and 10; carry from 0x0a6c selects internal vs MOVX write):
```
0x0a18  e4        clr a               ; A=0 for MOVC
0x0a19  93        movc a,@a+dptr      ; fetch record byte: 8-bit destination start address
0x0a1a  a3        inc dptr            ; advance table pointer
0x0a1b  f8        mov r0,a            ; R0 = destination address
0x0a1c  e4        clr a               ; A=0
0x0a1d  93        movc a,@a+dptr      ; fetch next init data byte
0x0a1e  a3        inc dptr            ; advance
0x0a1f  40 03     jc 0x0a24           ; CY (set iff record type was 10) -> paged-XDATA write
0x0a21  f6        mov @r0,a           ; type 00: write byte to IRAM[R0]
0x0a22  80 01     sjmp 0x0a25         ; skip MOVX variant
0x0a24  f2        movx @r0,a          ; type 10: write byte to XDATA page (MOVX @R0, PDATA-style)
0x0a25  08        inc r0              ; next destination address
0x0a26  df f4     djnz r7,0x0a1c      ; R7 = byte count; loop over record data
0x0a28  80 29     sjmp 0x0a53         ; record done -> fetch next record header
```
Bit-init record executor (type 11; each data byte = value-bit7 | byteoffset-bits5:3 | bitpos-bits2:0):
```
0x0a2a  e4        clr a               ; A=0
0x0a2b  93        movc a,@a+dptr      ; fetch bit-descriptor byte
0x0a2c  a3        inc dptr            ; advance
0x0a2d  f8        mov r0,a            ; save descriptor in R0
0x0a2e  54 07     anl a,#0x7          ; A = bit position 0..7
0x0a30  24 0c     add a,#0xc          ; A = bitpos + 0x0c = PC-relative index of mask table at 0x0a48
0x0a32  c8        xch a,r0            ; R0 = mask index, A = descriptor again
0x0a33  c3        clr cy              ; clear carry for RLC
0x0a34  33        rlc a               ; A = desc<<1, CY = desc bit7 (the set/clear flag)
0x0a35  c4        swap a              ; nibble swap: A[3:0] now = desc bits 6:3
0x0a36  54 0f     anl a,#0xf          ; isolate byte offset (desc>>3)&0xF
0x0a38  44 20     orl a,#0x20         ; A = 0x20 + offset = IRAM address in bit-addressable space
0x0a3a  c8        xch a,r0            ; R0 = target IRAM address, A = mask index
0x0a3b  83        movc a,@a+pc        ; A = bit mask from table at 0x0a48 (0x0a3c + 0x0c + bitpos)
0x0a3c  40 04     jc 0x0a42           ; CY = desired bit value: 1 -> set path
0x0a3e  f4        cpl a               ; ~mask
0x0a3f  56        anl a,@r0           ; clear the bit in IRAM[R0]
0x0a40  80 01     sjmp 0x0a43         ; join
0x0a42  46        orl a,@r0           ; set the bit in IRAM[R0]
0x0a43  f6        mov @r0,a           ; write byte back
0x0a44  df e4     djnz r7,0x0a2a      ; loop over R7 descriptor bytes
0x0a46  80 0b     sjmp 0x0a53         ; record done -> next header
```
**DATA 0x0a48-0x0a4f** — Ghidra force-disassembled this as `AJMP 0x0802 / INC A / JBC ...`; it is actually the 8-byte bit-mask table read by the `MOVC A,@A+PC` at 0x0a3b. Raw bytes verified with xxd:
```
0x0a48  01 02 04 08 10 20 40 80   ; bit-mask table: mask = 1<<bitpos, indexed by bit position 0..7
```
(The spurious `XREF from CODE:0a4c` on 0x0a8f and the xref onto 0x0802 come from this mis-disassembly.)

Interpreter entry + record-header fetch/decoder:
```
0x0a50  90 0f 9c  mov dptr,#0xf9c     ; DPTR = init record table at CODE 0x0f9c (entry from startup)
0x0a53  e4        clr a               ; A=0
0x0a54  7e 01     mov r6,#0x1         ; default outer count = 1 (small record)
0x0a56  93        movc a,@a+dptr      ; fetch record header byte
0x0a57  60 bc     jz 0x0a15           ; header 0x00 = end of table -> ljmp main (0x0a95)
0x0a59  a3        inc dptr            ; advance
0x0a5a  ff        mov r7,a            ; R7 = header (keep type bits for later)
0x0a5b  54 3f     anl a,#0x3f         ; A = length field (low 6 bits)
0x0a5d  30 e5 09  jnb acc.5,0x0a69    ; bit5 clear -> small record, R7 low bits = count
0x0a60  54 1f     anl a,#0x1f         ; big record: high count = header bits 4:0
0x0a62  fe        mov r6,a            ; R6 = high byte of count
0x0a63  e4        clr a               ; A=0
0x0a64  93        movc a,@a+dptr      ; fetch low count byte
0x0a65  a3        inc dptr            ; advance
0x0a66  60 01     jz 0x0a69           ; low byte 0 -> no partial page
0x0a68  0e        inc r6              ; +1 outer iteration for the partial low-count page
0x0a69  cf        xch a,r7            ; R7 = low count, A = header byte again
0x0a6a  54 c0     anl a,#0xc0         ; isolate type bits 7:6
0x0a6c  25 e0     add a,a             ; A=A+A: Z/C now encode type (00:Z, 10:Z+C, 01:-, 11:C)
0x0a6e  60 a8     jz 0x0a18           ; type 00 (Z,no C) or 10 (Z,C) -> byte-init executor (C picks MOVX)
0x0a70  40 b8     jc 0x0a2a           ; type 11 -> bit-init executor
```
XDATA block-init executor (type 01; 16-bit destination, streams count bytes via DPTR juggling):
```
0x0a72  e4        clr a               ; A=0
0x0a73  93        movc a,@a+dptr      ; fetch destination address high byte
0x0a74  a3        inc dptr            ; advance
0x0a75  fa        mov r2,a            ; R2 = dest high
0x0a76  e4        clr a               ; A=0
0x0a77  93        movc a,@a+dptr      ; fetch destination address low byte
0x0a78  a3        inc dptr            ; advance
0x0a79  f8        mov r0,a            ; R0 = dest low
0x0a7a  e4        clr a               ; A=0
0x0a7b  93        movc a,@a+dptr      ; fetch init data byte (DPTR = table pointer)
0x0a7c  a3        inc dptr            ; advance table pointer
0x0a7d  c8        xch a,r0            ; \
0x0a7e  c5 82     xch a,dpl           ;  swap DPTR <-> {R2:R0}: DPTR becomes destination,
0x0a80  c8        xch a,r0            ;  R2:R0 hold the saved table pointer
0x0a81  ca        xch a,r2            ;  (data byte preserved in A through the swaps)
0x0a82  c5 83     xch a,dph           ; /
0x0a84  ca        xch a,r2            ; ...
0x0a85  f0        movx @dptr,a        ; write data byte to XDATA[dest]
0x0a86  a3        inc dptr            ; dest++
0x0a87  c8        xch a,r0            ; \
0x0a88  c5 82     xch a,dpl           ;  swap back: DPTR = table pointer,
0x0a8a  c8        xch a,r0            ;  R2:R0 = incremented destination
0x0a8b  ca        xch a,r2            ; /
0x0a8c  c5 83     xch a,dph           ; ...
0x0a8e  ca        xch a,r2            ; ...
0x0a8f  df e9     djnz r7,0x0a7a      ; inner count (low byte)          [XREF from 0a4c is bogus — see data note]
0x0a91  de e7     djnz r6,0x0a7a      ; outer count (256-byte pages)
0x0a93  80 be     sjmp 0x0a53         ; record done -> next header
```

## fcn_0a95 — main

Entered from the init interpreter's terminator via 0x0a15. Never returns.

```
0x0a95  e4        clr a               ; A=0
0x0a96  f5 27     mov 0x27,a          ; IRAM 0x27 = 0: P3.1-event state flag cleared
0x0a98  74 ff     mov a,#0xff         ; A=0xFF
0x0a9a  f5 28     mov 0x28,a          ; delay counter high = 0xFF
0x0a9c  f5 29     mov 0x29,a          ; delay counter low = 0xFF ({0x28:0x29} = 0xFFFF)
0x0a9e  75 2a 00  mov 0x2a,#0x0       ; IRAM 0x2a = 0 — UNKNOWN purpose: no other reference to
                                       ; byte 0x2a (or bits 0x50-0x57) anywhere in the listing
0x0aa1  75 2b 10  mov 0x2b,#0x10      ; IRAM 0x2b = 0x10 — UNKNOWN purpose: likewise unreferenced
0x0aa4  c2 af     clr ea              ; bit 0xAF = IE.7 (EA): global interrupt disable during init
0x0aa6  90 ff fd  mov dptr,#0xfffd    ; DPTR -> USBIMSK (Reg_stc1.h:101)
0x0aa9  e4        clr a               ; A=0
0x0aaa  f0        movx @dptr,a        ; USBIMSK = 0x00: mask ALL USB interrupt sources
                                       ; (same move as TI ROM RomBoot.c:35 `USBIMSK = 0;`)
0x0aab  c2 22     clr 0x22            ; clear bit 0x22 (byte 0x24 bit 2) — UNKNOWN: this is the
                                       ; only reference to bit 0x22 in the whole listing
0x0aad  12 08 cb  lcall 0x08cb        ; -> fcn_08cb: master hardware init (timers/CPT/DMA per brief;
                                       ; owned by another annotator — out of range)
0x0ab0  12 09 70  lcall 0x0970        ; -> fcn_0970: second init stage (out of range; not traced here)
```
Startup busy-wait: decrement 16-bit {0x28:0x29} from 0xFFFF until it underflows. The `SETB CY` + `SUBB #0` pair computes `{28:29} - 1` for the comparison, so the loop exits when the counter reaches 0.
```
0x0ab3  d3        setb cy             ; CY=1 so the SUBB chain effectively compares counter >= 1
0x0ab4  e5 29     mov a,0x29          ; A = counter low
0x0ab6  94 00     subb a,#0x0         ; A = low - 0 - 1 (borrow propagates)
0x0ab8  e5 28     mov a,0x28          ; A = counter high
0x0aba  94 00     subb a,#0x0         ; high - 0 - borrow
0x0abc  40 0a     jc 0x0ac8           ; borrow -> counter == 0 -> delay done
0x0abe  e5 29     mov a,0x29          ; A = low (to detect 0x00 before decrement)
0x0ac0  15 29     dec 0x29            ; counter low--
0x0ac2  70 ef     jnz 0x0ab3          ; low wasn't 0 -> no borrow into high
0x0ac4  15 28     dec 0x28            ; low wrapped 0x00->0xFF: counter high--
0x0ac6  80 eb     sjmp 0x0ab3         ; keep waiting (pure software delay, ~64K iterations)
```
Go live: start the tick timer, enable interrupts, attach to USB.
```
0x0ac8  d2 8c     setb tr0            ; bit 0x8C = TCON.4 (TR0): start Timer0 (ms tick; ISR at
                                       ; 0x101e sets bit 0x20 and reloads TH0=0xCE — verified)
0x0aca  d2 af     setb ea             ; EA=1: global interrupt enable
0x0acc  90 ff fc  mov dptr,#0xfffc    ; DPTR -> USBCTL (Reg_stc1.h:100)
0x0acf  e0        movx a,@dptr        ; read USBCTL (read-modify-write, preserves boot-ROM bits)
0x0ad0  44 80     orl a,#0x80         ; set bit7 = CONT (D+ pull-up connect; bit meaning per TI
                                       ; UsbEng.c:647 comment "connect PUR")
0x0ad2  f0        movx @dptr,a        ; write back: device now visible on the USB bus
```
Forever loop. Two halves: (a) fast path while no ms tick pending — drain the event queue; (b) once per ms tick — scan P3 inputs, refresh the P1 bit-banged control chains, and edge-detect latched P3.1 into events 0x0b/0x0c.
```
0x0ad3  20 20 09  jb 0x20,0x0adf      ; ms-tick flag set (by T0 ISR)? -> tick work at 0x0adf
0x0ad6  e5 0a     mov a,0x0a          ; A = pending event code IRAM[0x0a]
0x0ad8  60 f9     jz 0x0ad3           ; no event -> spin on tick flag
0x0ada  12 02 ee  lcall 0x02ee        ; -> fcn_02ee event dispatcher (DEC A; 3-byte LJMP table at
                                       ; 0x0300, codes 1..0x0e; >=0x0f -> 0x0564) — out of range
0x0add  80 f4     sjmp 0x0ad3         ; back to top of idle loop
```
Per-millisecond work:
```
0x0adf  12 0e d5  lcall 0x0ed5        ; -> fcn_0ed5: sample P3, compare with last value (IRAM 0x20),
                                       ; edge-detect P3.3/P3.4/P3.5, store new P3 into IRAM 0x20;
                                       ; returns R7 = IRAM 0x06 flags, bit0 set on any new edge
0x0ae2  ef        mov a,r7            ; A = scanner result flags
0x0ae3  30 e0 06  jnb acc.0,0x0aec    ; no input change this tick -> skip control-chain refresh
0x0ae6  12 0f 0c  lcall 0x0f0c        ; -> fcn_0f0c: bit-bang IRAM 0x22 (byte) out on P1 (data P1.7,
                                       ; clock P1.5, per listing at 0x0f13-0x0f39) — serial ctrl chain
0x0ae9  12 0e 62  lcall 0x0e62        ; -> fcn_0e62: bit-bang IRAM 0x23 then 0x25 out on P1 (data
                                       ; P1.0, clock P1.2, strobe P1.1) — second serial ctrl chain
```
Latched-P3.1 edge -> event 0x0b / 0x0c. bit 0x01 = byte 0x20 bit 1 = last-sampled state of pin P3.1 (byte 0x20 is written with P3 in fcn_0ed5). What P3.1 is wired to on the Mbox board is UNKNOWN from firmware alone.
```
0x0aec  20 01 0d  jb 0x01,0x0afc      ; P3.1 latched HIGH -> check the "released/high" branch
0x0aef  e5 27     mov a,0x27          ; P3.1 low: A = state flag
0x0af1  70 09     jnz 0x0afc          ; already reported this low period -> skip
0x0af3  75 27 01  mov 0x27,#0x1       ; mark low-event posted
0x0af6  75 0a 0b  mov 0x0a,#0xb       ; queue event code 0x0b (dispatcher entry 0x031e -> 0x04c4)
0x0af9  12 02 ee  lcall 0x02ee        ; dispatch it immediately
0x0afc  30 01 0e  jnb 0x01,0x0b0d     ; P3.1 latched LOW -> skip high branch
0x0aff  e5 27     mov a,0x27          ; P3.1 high: A = state flag
0x0b01  b4 01 09  cjne a,#0x1,0x0b0d  ; only fire if a low-event was previously posted
0x0b04  e4        clr a               ; A=0
0x0b05  f5 27     mov 0x27,a          ; reset state flag
0x0b07  75 0a 0c  mov 0x0a,#0xc       ; queue event code 0x0c (dispatcher entry 0x0321 -> 0x0511)
0x0b0a  12 02 ee  lcall 0x02ee        ; dispatch it immediately
0x0b0d  c2 20     clr 0x20            ; consume the ms-tick flag
0x0b0f  80 c2     sjmp 0x0ad3         ; loop forever
```

## fcn_0b11 — ep0in_ptr_load_fa10

Called from 0x0d2d and 0x0d4a (EP0 OUT payload examiners at 0x0d25+, which read one byte via this pointer and turn it into event codes 4-8). Loads the shared XDATA pointer with 0xFA10, the EP0 IN buffer address (brief: "EP0 IN buffer 0xFA10 (Rev 20)"), then falls through into fcn_0b17.

```
0x0b11  75 1b fa  mov 0x1b,#0xfa      ; pointer high byte = 0xFA
0x0b14  75 1c 10  mov 0x1c,#0x10      ; pointer low byte = 0x10 -> {0x1b:0x1c} = 0xFA10 (EP0 IN buf)
```

## fcn_0b17 — dptr_from_ep0_ptr (fall-through target of 0b11; also called directly)

Callers: 0x0079, 0x0081, 0x009d, 0x00bf, 0x00e7, 0x0163, 0x016b (SETUP/EP0 handler region) and 0x0b9c (EP0 IN copy loop at 0x0b8c). Pure helper: DPTR = pointer pair.

```
0x0b17  85 1c 82  mov dpl,0x1c        ; DPL = pointer low
0x0b1a  85 1b 83  mov dph,0x1b        ; DPH = pointer high -> DPTR = EP0 buffer cursor
0x0b1d  22        ret                 ; return with DPTR ready for MOVX
```

(Range ends here; 0x0b1e begins fcn_0b1e, owned by the next annotator.)

---

### Control-flow exits from this range
- 0x08cb (master hw init), 0x0970 (second init) — called once from main.
- 0x02ee (event dispatcher, jump table at 0x0300) — called from three sites.
- 0x0ed5 (P3 scanner), 0x0f0c / 0x0e62 (P1 bit-bang chains) — per-tick.
- Init table data at 0x0f9c is read (MOVC) by the interpreter but lives outside this range.

### 5.8 Range 0x0B1E-0x0C44

# Rev 20 annotation — 0x0b1e .. 0x0c44 (range end 0x0c45 exclusive)

This range is pure code: the firmware's EP0 control-endpoint primitive library
(clear-stall, arm/NACK, IN-data chunk transmitter) plus one I2C EEPROM
byte-write routine. No data regions. All SFR names below resolved from
`reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h`; EPCNF/DCNTX bit meanings
from `ROM/hwMacro.h` (STALL=0x08 lines 9-40, TOGGLE=0x20 lines 46-47,
EMPTYOutEp0 `OEPDCNTX0=0` line 50, `IEPDCNTX0=0x80` line 53 = NACK-hold,
ZEROPACKInEp0 `IEPDCNTX0=0x00` line 57); I2CSTA bits from `ROM/i2c.h`
(STOP_WRITE=0x01, STOP_READ=0x02, XMIT_DATA_EMPTY=0x08, RCV_DATA_FULL=0x80,
lines 33-40).

## IRAM state variables used in this range (tracked across the whole range)

| loc | kind | role |
|---|---|---|
| 0x09 | byte | EP0 IN remaining-byte count, low part (loaded by SETUP handlers at 0x003b, 0x0183, 0x01a7, 0x01e1, 0x0d89) |
| 0x0b | byte | EP0 IN remaining-byte count, high part (loaded alongside 0x09; borrow in 0x0b8c reloads 0x09=0xFF and decrements this) |
| 0x18 | byte | bytes copied into the current EP0 IN packet (0..8) |
| 0x19:0x1a | byte pair | source pointer into CODE space (descriptor/response data), high:low |
| 0x1b:0x1c | byte pair | destination pointer into XDATA, high:low; 0xFA18 = EP0 IN buffer (set by 0x0b3e) or 0xFA10 = EP0 OUT buffer (set by 0x0b11, just before this range) |
| bit 0x0b (= IRAM 0x21 bit 3) | bit | EP0 IN transfer in progress / more data pending (tested by ISR continuation at 0x0fc4, main-loop at 0x0d25) |
| bit 0x0c (= IRAM 0x21 bit 4) | bit | EP0 IN final packet armed / transfer complete (tested at 0x0fca) |
| bit 0x0d (= IRAM 0x21 bit 5) | bit | "send terminating ZLP after an exactly-full final packet" — cleared at 0x0039/0x0d8f, set at 0x0da9 (outside this range) after a length comparison; consumed at 0x0be1 |

Note on EP0 buffer addresses: at 0x0970-0x097a (outside this range, bytes
verified: `90 ff a9 74 42 f0 90 ff 69 04 f0`) the firmware writes
OEPBBAX0(0xFFA9)=0x42 and IEPBBAX0(0xFF69)=0x43. Per TI Mmap.h:55 buffer base
is 0xF800 and offsets are in 8-byte units, so EP0 OUT = 0xFA10 and
EP0 IN = 0xFA18. ANNOTATION_BRIEF.md's line "EP0 IN buffer 0xFA10 (Rev 20),
EP0 OUT 0xFA18" is swapped relative to these verified bytes.

---

## fcn_0b1e = ep0_clear_stall_toggle_and_arm  (called from 0x0d67, 0x0fe6)

Clears STALL and data-TOGGLE on both EP0 directions and re-arms both. This is
the same register footprint as TI engEp0SetupDone (UsbEng.c:220-232:
STALLClrInEp0/OutEp0 + toggle manipulation + EMPTYOutEp0/EMPTYInEp0), except
Rev 20 CLEARS the toggle bit (ANL #0xD7 = clear 0x20|0x08) where TI's macro
sets it. Falls through into fcn_0b2b, which performs the OEPCNF0 write-back
and the DCNTX arming.

```
0x0b1e  90 ff 68     mov dptr,#0xff68      ; DPTR -> IEPCNF0 (EP0 IN config, Reg_stc1.h:109)
0x0b21  e0           movx a,@dptr          ; read IEPCNF0
0x0b22  54 d7        anl a,#0xd7           ; clear bit5 TOGGLE (hwMacro.h:46) and bit3 STALL (hwMacro.h:9)
0x0b24  f0           movx @dptr,a          ; write back IEPCNF0: EP0 IN unstalled, toggle=DATA0
0x0b25  90 ff a8     mov dptr,#0xffa8      ; DPTR -> OEPCNF0 (EP0 OUT config, Reg_stc1.h:170)
0x0b28  e0           movx a,@dptr          ; read OEPCNF0
0x0b29  54 d7        anl a,#0xd7           ; clear TOGGLE + STALL in the read value (written by fall-through at 0x0b2b)
```

## fcn_0b2b = ep0_store_cnf_and_arm_both  (fall-through from 0x0b1e; called from 0x0036, 0x0fd5, 0x1016)

Direct callers pre-load DPTR=OEPCNF0 and A with a modified config value
(verified at 0x0030-0x0036: A = OEPCNF0|0x20 set-toggle; at 0x1010-0x1016:
A = OEPCNF0|0x08 set-stall), so the first instruction completes the caller's
read-modify-write. Then both EP0 data-count registers are written 0:
IEPDCNTX0=0 is TI's ZEROPACKInEp0 (hwMacro.h:57 — arms a zero-length IN
packet, NACK bit clear) and OEPDCNTX0=0 is TI's EMPTYOutEp0 (hwMacro.h:50 —
clears NACK so the next OUT packet can be received).

```
0x0b2b  f0           movx @dptr,a          ; write back caller-prepared EP0 CNF value (OEPCNF0 in all observed paths)
0x0b2c  90 ff 6b     mov dptr,#0xff6b      ; DPTR -> IEPDCNTX0 (EP0 IN byte count/NACK, Reg_stc1.h:139)
0x0b2f  e4           clr a                 ; A = 0
0x0b30  f0           movx @dptr,a          ; IEPDCNTX0 = 0: arm zero-length IN packet (ZEROPACKInEp0, hwMacro.h:57)
0x0b31  90 ff ab     mov dptr,#0xffab      ; DPTR -> OEPDCNTX0 (EP0 OUT byte count/NACK, Reg_stc1.h:203)
0x0b34  f0           movx @dptr,a          ; OEPDCNTX0 = 0: clear NACK, re-arm EP0 OUT (EMPTYOutEp0, hwMacro.h:50)
0x0b35  22           ret                   ; done
```

## fcn_0b36 = ep0_buf_clear_byte  (called from 0x00aa, 0x00b5, 0x00dd, 0x0105, 0x023f)

Zeroes one byte inside the page addressed by IRAM 0x1b (0xFA = USB packet
buffer RAM once 0x0b11/0x0b3e have run), at offset A. The SETUP dispatcher
uses it to zero individual response-buffer bytes.

```
0x0b36  f5 82        mov dpl,a             ; DPL = byte offset (caller's A)
0x0b38  85 1b 83     mov dph,0x1b          ; DPH = buffer page from IRAM 0x1b (0xFA)
0x0b3b  e4           clr a                 ; A = 0
0x0b3c  f0           movx @dptr,a          ; XDATA[0x1b:A] = 0 — clear one EP0 buffer byte
0x0b3d  22           ret                   ; done
```

## fcn_0b3e = ep0_ptr_set_in_buf  (called from 0x0073, 0x0095, 0x015d, 0x0200, 0x022f, 0x0b8f)

Points the IRAM XDATA pointer at the EP0 IN packet buffer (0xFA18 — see
buffer-address note above). Companion fcn_0b11 (just before this range) sets
the same pointer to 0xFA10 (EP0 OUT buffer).

```
0x0b3e  75 1b fa     mov 0x1b,#0xfa        ; dest pointer high = 0xFA (USB packet buffer RAM page)
0x0b41  75 1c 18     mov 0x1c,#0x18        ; dest pointer low = 0x18 -> 0xFA18 = EP0 IN buffer (IEPBBAX0=0x43 @0x0976)
0x0b44  22           ret                   ; done
```

## fcn_0b45 = ep0_send_1byte  (called from 0x0087, 0x0170, 0x0229)

Arms EP0 IN with exactly 1 byte (byte already placed in the buffer by the
caller); count written with bit7 (NACK) clear, so the packet is immediately
released. State bits mark the transfer as complete.

```
0x0b45  90 ff 6b     mov dptr,#0xff6b      ; DPTR -> IEPDCNTX0
0x0b48  74 01        mov a,#0x1            ; count = 1, NACK bit (0x80) clear
0x0b4a  f0           movx @dptr,a          ; IEPDCNTX0 = 1: send 1-byte IN packet
0x0b4b  c2 0b        clr 0x0b              ; bit 0x0b = 0: no more IN data pending
0x0b4d  d2 0c        setb 0x0c             ; bit 0x0c = 1: EP0 IN transfer complete
0x0b4f  22           ret                   ; done
```

## fcn_0b50 = ep0_clear_stall_both  (called from 0x0026, 0x0152)

Byte-for-byte the TI macros STALLClrInEp0 + STALLClrOutEp0 (hwMacro.h:27-28),
executed by TI's engEp0SetupDone on every new SETUP (UsbEng.c:223-224).
Caller 0x0026 is the head of the Rev 20 SETUP dispatcher.

```
0x0b50  90 ff 68     mov dptr,#0xff68      ; DPTR -> IEPCNF0
0x0b53  e0           movx a,@dptr          ; read IEPCNF0
0x0b54  54 f7        anl a,#0xf7           ; clear bit3 STALL only (STALLClrInEp0, hwMacro.h:27)
0x0b56  f0           movx @dptr,a          ; write back: EP0 IN unstalled
0x0b57  90 ff a8     mov dptr,#0xffa8      ; DPTR -> OEPCNF0
0x0b5a  e0           movx a,@dptr          ; read OEPCNF0
0x0b5b  54 f7        anl a,#0xf7           ; clear bit3 STALL (STALLClrOutEp0, hwMacro.h:28)
0x0b5d  f0           movx @dptr,a          ; write back: EP0 OUT unstalled
0x0b5e  22           ret                   ; done
```

## fcn_0b5f = ep0_nack_both  (called from 0x0296, 0x02e1)

Holds both EP0 directions NACKing (bit7 of DCNTX = NACK per hwMacro.h:53
`EMPTYInEp0 {IEPDCNTX0 = 0x80;}`) and clears the IN-transfer state bits —
used to park EP0 while a request is processed / on error paths.

```
0x0b5f  90 ff 6b     mov dptr,#0xff6b      ; DPTR -> IEPDCNTX0
0x0b62  74 80        mov a,#0x80           ; NACK bit set, count 0
0x0b64  f0           movx @dptr,a          ; IEPDCNTX0 = 0x80: hold EP0 IN NACKing (hwMacro.h:53)
0x0b65  90 ff ab     mov dptr,#0xffab      ; DPTR -> OEPDCNTX0
0x0b68  f0           movx @dptr,a          ; OEPDCNTX0 = 0x80: hold EP0 OUT NACKing
0x0b69  c2 0b        clr 0x0b              ; bit 0x0b = 0: no IN data pending
0x0b6b  c2 0c        clr 0x0c              ; bit 0x0c = 0: no IN transfer complete either (idle/aborted)
0x0b6d  22           ret                   ; done
```

## fcn_0b6e = code_read_byte_at_srcptr  (called from 0x0180, 0x01de, 0x0b99)

Reads one byte of CODE space at the 16-bit source pointer IRAM 0x19:0x1a
(descriptor tables / canned responses live in code space).

```
0x0b6e  85 1a 82     mov dpl,0x1a          ; DPL = source pointer low (IRAM 0x1a)
0x0b71  85 19 83     mov dph,0x19          ; DPH = source pointer high (IRAM 0x19)
0x0b74  e4           clr a                 ; A = 0 (MOVC index)
0x0b75  93           movc a,@a+dptr        ; A = CODE[0x19:0x1a] — fetch next source byte
0x0b76  22           ret                   ; done
```

## fcn_0b77 = ep0_in_start_transfer  (called from 0x01ee, 0x0fc7)

Loads the first (or next) up-to-8-byte chunk into the EP0 IN buffer, then
releases it by clearing the NACK bit that 0x0b8c left set. 0x01ee = SETUP
dispatcher starting a data stage; 0x0fc7 = IN-done interrupt continuation
sending the next chunk.

```
0x0b77  12 0b 8c     lcall 0x0b8c          ; -> ep0_in_fill_chunk: copy <=8 bytes, IEPDCNTX0 = 0x80|count
0x0b7a  90 ff 6b     mov dptr,#0xff6b      ; DPTR -> IEPDCNTX0
0x0b7d  e0           movx a,@dptr          ; read back 0x80|count
0x0b7e  54 7f        anl a,#0x7f           ; clear bit7 NACK
0x0b80  f0           movx @dptr,a          ; IEPDCNTX0 = count: release the loaded IN packet to the host
0x0b81  22           ret                   ; done
```

## fcn_0b82 = ep0_arm_zlp_and_out  (called from 0x0d64, 0x0f43)

Zero-length-IN + OUT re-arm without touching the CNF registers: the
zero-length IN serves as the control-transfer status-stage ACK
(ZEROPACKInEp0), the OUT write re-opens reception (EMPTYOutEp0).

```
0x0b82  90 ff ab     mov dptr,#0xffab      ; DPTR -> OEPDCNTX0
0x0b85  e4           clr a                 ; A = 0
0x0b86  f0           movx @dptr,a          ; OEPDCNTX0 = 0: clear NACK, re-arm EP0 OUT (EMPTYOutEp0)
0x0b87  90 ff 6b     mov dptr,#0xff6b      ; DPTR -> IEPDCNTX0
0x0b8a  f0           movx @dptr,a          ; IEPDCNTX0 = 0: arm zero-length IN packet = status-stage ACK
0x0b8b  22           ret                   ; done
```

## fcn_0b8c = ep0_in_fill_chunk  (called from 0x0b77 only)

The EP0 IN transmit engine. Preconditions set by the SETUP handlers:
IRAM 0x19:0x1a = code-space source of the response, IRAM 0x09 (low) /
IRAM byte 0x0b (high) = remaining byte count, bit 0x0d = whether a
terminating ZLP is required if the data ends exactly on an 8-byte boundary
(set at 0x0da9 outside this range). Copies up to 8 bytes into the IN buffer
at 0xFA18, writes the byte count into IEPDCNTX0 with NACK still held (the
caller 0x0b77 releases it), and updates the pending/done bits that the IN
interrupt path (0x0fc4/0x0fca) and main loop (0x0d25) poll.

```
0x0b8c  e4           clr a                 ; A = 0
0x0b8d  f5 18        mov 0x18,a            ; packet fill counter 0x18 = 0 (bytes copied this packet)
0x0b8f  12 0b 3e     lcall 0x0b3e          ; -> ep0_ptr_set_in_buf: dest 0x1b:0x1c = 0xFA18 (EP0 IN buffer)
```
Copy loop — one byte per iteration, max 8 (checked at loop bottom):
```
0x0b92  e5 09        mov a,0x09            ; A = remaining count low byte
0x0b94  d3           setb cy               ; \ SDCC unsigned "A < 1" idiom:
0x0b95  94 00        subb a,#0x0           ; / computes A-0-1; carry set iff A==0
0x0b97  40 2d        jc 0x0bc6             ; remaining==0 -> exit loop, finalize packet
0x0b99  12 0b 6e     lcall 0x0b6e          ; -> code_read_byte_at_srcptr: A = CODE[0x19:0x1a]
0x0b9c  12 0b 17     lcall 0x0b17          ; -> fcn_0b17 (just before range): DPTR = dest ptr 0x1b:0x1c
0x0b9f  f0           movx @dptr,a          ; store byte into EP0 IN buffer
0x0ba0  05 1c        inc 0x1c              ; dest pointer low++
0x0ba2  e5 1c        mov a,0x1c            ; test for low-byte wrap
0x0ba4  70 02        jnz 0x0ba8            ; no wrap -> skip
0x0ba6  05 1b        inc 0x1b              ; carry into dest pointer high
0x0ba8  05 1a        inc 0x1a              ; source pointer low++
0x0baa  e5 1a        mov a,0x1a            ; test for low-byte wrap
0x0bac  70 02        jnz 0x0bb0            ; no wrap -> skip
0x0bae  05 19        inc 0x19              ; carry into source pointer high
0x0bb0  d5 09 0c     djnz 0x09,0x0bbf      ; remaining-low--; nonzero -> continue at 0x0bbf
0x0bb3  e5 0b        mov a,0x0b            ; low hit 0: check remaining count HIGH byte (IRAM byte 0x0b)
0x0bb5  d3           setb cy               ; \ same "A < 1" idiom:
0x0bb6  94 00        subb a,#0x0           ; / carry set iff high byte == 0
0x0bb8  40 05        jc 0x0bbf             ; high==0 too -> count truly exhausted, continue (loop exits at top)
0x0bba  75 09 ff     mov 0x09,#0xff        ; borrow: reload low byte with 0xFF
0x0bbd  15 0b        dec 0x0b              ; and decrement high byte
0x0bbf  05 18        inc 0x18              ; packet fill counter++
0x0bc1  e5 18        mov a,0x18            ; check packet size
0x0bc3  b4 08 cc     cjne a,#0x8,0x0b92    ; loop until 8 bytes copied (EP0 max packet) or count exhausted
```
Packet finalize — load count into IEPDCNTX0 with NACK held, set state bits:
```
0x0bc6  90 ff 6b     mov dptr,#0xff6b      ; DPTR -> IEPDCNTX0
0x0bc9  74 80        mov a,#0x80           ; NACK bit
0x0bcb  f0           movx @dptr,a          ; IEPDCNTX0 = 0x80: hold IN NACKing while count is composed
0x0bcc  e0           movx a,@dptr          ; read back 0x80
0x0bcd  45 18        orl a,0x18            ; OR in the byte count (0..8)
0x0bcf  f0           movx @dptr,a          ; IEPDCNTX0 = 0x80|count: packet loaded but NOT released (0x0b77 clears bit7)
0x0bd0  d2 0b        setb 0x0b             ; bit 0x0b = 1: IN transfer in progress (default; may be undone below)
0x0bd2  c2 0c        clr 0x0c              ; bit 0x0c = 0: not complete (default)
0x0bd4  e5 09        mov a,0x09            ; remaining low
0x0bd6  70 15        jnz 0x0bed            ; more data remains -> return with pending=1
0x0bd8  e5 0b        mov a,0x0b            ; remaining high
0x0bda  70 11        jnz 0x0bed            ; more data remains -> return with pending=1
0x0bdc  e5 18        mov a,0x18            ; count exhausted: was this final packet exactly full?
0x0bde  b4 08 08     cjne a,#0x8,0x0be9    ; not 8 bytes -> short packet already terminates transfer
0x0be1  30 0d 05     jnb 0x0d,0x0be9       ; full 8-byte final packet: ZLP flag (bit 0x0d, set @0x0da9) clear -> done
0x0be4  d2 0b        setb 0x0b             ; ZLP needed: keep pending=1 so next IN-done sends a zero-length packet
0x0be6  c2 0c        clr 0x0c              ; not complete yet
0x0be8  22           ret                   ; return (ZLP pending)
0x0be9  c2 0b        clr 0x0b              ; bit 0x0b = 0: nothing more to send
0x0beb  d2 0c        setb 0x0c             ; bit 0x0c = 1: EP0 IN transfer complete
0x0bed  22           ret                   ; return
```

## fcn_0bee = eeprom_write_byte  (called from 0x04ee, 0x051c)

Polled I2C write of three bytes to slave address 0xA0 — the configuration
EEPROM (TI eeprom.h:10 `EEPROM_I2C_ADDR 0xA0`). Wire sequence: subaddress
high (R7), subaddress low (2nd param, read from IRAM 0x05 = caller's R5),
data byte (R3) with STOP_WRITE flagged before the final byte — the same
order as TI's word-addressed write path in I2CAccess (I2c.c:54-79 for
address phase, I2c.c:146-160 for data byte + STOP_WRITE) and as the
companion word-addressed read fcn_0cdd (outside range, bytes verified).
Caller 0x04de-0x04fd does write(0x1F,0xFF, ~read(0x1F,0xFF)) then reads back
and compares — an EEPROM presence/write test at address 0x1FFF. I2CSTA bit
tests use ACC bit addresses: 0xE3 = ACC.3 = XMIT_DATA_EMPTY (i2c.h:36).

```
0x0bee  ae 05        mov r6,0x05           ; R6 = 2nd byte param (IRAM 0x05 = bank-0 R5 of caller) = subaddr low
0x0bf0  90 ff c0     mov dptr,#0xffc0      ; DPTR -> I2CSTA/I2CCTL (same address, Reg_stc1.h:37-38)
0x0bf3  e0           movx a,@dptr          ; read I2CSTA
0x0bf4  54 fc        anl a,#0xfc           ; clear bit0 STOP_WRITE + bit1 STOP_READ (i2c.h:33-34)
0x0bf6  f0           movx @dptr,a          ; write back: no stop pending
0x0bf7  90 ff c3     mov dptr,#0xffc3      ; DPTR -> I2CADR (Reg_stc1.h:41)
0x0bfa  74 a0        mov a,#0xa0           ; slave address 0xA0, R/W bit 0 = write (EEPROM, eeprom.h:10)
0x0bfc  f0           movx @dptr,a          ; I2CADR = 0xA0: select EEPROM for writing
0x0bfd  7d ff        mov r5,#0xff          ; \ delay counter R4:R5 = 0x00FF
0x0bff  7c 00        mov r4,#0x0           ; /
0x0c01  ed           mov a,r5              ; 16-bit decrement delay loop (255 iterations)
0x0c02  1d           dec r5                ;   R5--
0x0c03  70 01        jnz 0x0c06            ;   old R5 nonzero -> no borrow
0x0c05  1c           dec r4                ;   borrow into R4
0x0c06  ed           mov a,r5              ;   test R5|R4 == 0
0x0c07  4c           orl a,r4              ;
0x0c08  70 f7        jnz 0x0c01            ; loop until counter exhausted (short pre-transfer settle delay)
0x0c0a  90 ff c1     mov dptr,#0xffc1      ; DPTR -> I2CDATO (Reg_stc1.h:39)
0x0c0d  ef           mov a,r7              ; A = param1 = subaddress high byte
0x0c0e  f0           movx @dptr,a          ; I2CDATO = subaddr high — starts the I2C transaction
0x0c0f  90 ff c0     mov dptr,#0xffc0      ; DPTR -> I2CSTA
0x0c12  e0           movx a,@dptr          ; read status
0x0c13  30 e3 f9     jnb 0xe3,0x0c0f       ; wait for ACC.3 = XMIT_DATA_EMPTY (0x08, i2c.h:36); no timeout, no error check
0x0c16  90 ff c1     mov dptr,#0xffc1      ; DPTR -> I2CDATO
0x0c19  ee           mov a,r6              ; A = subaddress low byte
0x0c1a  f0           movx @dptr,a          ; I2CDATO = subaddr low
0x0c1b  90 ff c0     mov dptr,#0xffc0      ; DPTR -> I2CSTA
0x0c1e  e0           movx a,@dptr          ; read status
0x0c1f  30 e3 f9     jnb 0xe3,0x0c1b       ; wait XMIT_DATA_EMPTY again
0x0c22  90 ff c0     mov dptr,#0xffc0      ; DPTR -> I2CSTA
0x0c25  e0           movx a,@dptr          ; read status
0x0c26  44 01        orl a,#0x1            ; set bit0 STOP_WRITE: issue STOP after next byte (i2c.h:33; cf. I2c.c:151)
0x0c28  f0           movx @dptr,a          ; write back
0x0c29  a3           inc dptr              ; DPTR -> 0xFFC1 = I2CDATO
0x0c2a  eb           mov a,r3              ; A = param3 = data byte
0x0c2b  f0           movx @dptr,a          ; I2CDATO = data byte (final byte, STOP follows)
0x0c2c  90 ff c0     mov dptr,#0xffc0      ; DPTR -> I2CSTA
0x0c2f  e0           movx a,@dptr          ; read status
0x0c30  20 e3 06     jb 0xe3,0x0c39        ; XMIT_DATA_EMPTY set -> transmission done, go drain delay counter
0x0c33  74 ff        mov a,#0xff           ; still transmitting:
0x0c35  fc           mov r4,a              ;   R4 = 0xFF \ preload post-write delay counter 0xFFFF
0x0c36  fd           mov r5,a              ;   R5 = 0xFF /
0x0c37  80 f3        sjmp 0x0c2c           ; re-poll status
0x0c39  ed           mov a,r5              ; drain loop: A = R5|R4 (note: if TX was already empty at first
0x0c3a  4c           orl a,r4              ;   poll, R4:R5 are 0 left over from the 0x0c01 loop -> immediate ret;
0x0c3b  60 07        jz 0x0c44             ;   otherwise ~65535 iterations = EEPROM write-cycle settle delay)
0x0c3d  ed           mov a,r5              ; 16-bit decrement:
0x0c3e  1d           dec r5                ;   R5--
0x0c3f  70 f8        jnz 0x0c39            ;   old R5 nonzero -> no borrow
0x0c41  1c           dec r4                ;   borrow into R4
0x0c42  80 f5        sjmp 0x0c39           ; loop until zero
0x0c44  22           ret                   ; done (no status/ACK-error returned to caller)
```

Control flow out of this range: next byte 0x0c45 begins FUN_CODE_0c45
(codec/CPT-related, `mov 0x33,r7` — owned by another annotator).

### 5.9 Range 0x0C45-0x0DEB

# Rev 20 annotation — 0x0c45..0x0dec

Context entering the range: 0x0c2c-0x0c44 (previous annotator's range) is an
I2C busy-wait/timeout helper polling I2CSTA. My range starts at fcn_0c45.

IRAM variables tracked in this range (bank 0 registers at 0x00-0x07):
- byte 0x0a = pending main-loop command code (set to 4/5/6/7/8 here; 0x0d and 0x0e set by SETUP dispatcher / suspend stub)
- byte 0x0d = which class request is awaiting its EP0 OUT data stage (1 = bmReqType 0x22 SET_CUR-to-endpoint, 2 = bmReqType 0x21 bRequest!=0; set at 0x006b/0x0063)
- bytes 0x09/0x0b = EP0 IN transfer length low/high
- bit 0x0b (byte 0x21.3) = EP0 OUT data stage pending; bit 0x0c (byte 0x21.4) = EP0 IN transfer in progress (used by IEP0 handler 0x0fc4); bit 0x0d (byte 0x21.5) = transfer shorter than wLength flag
- byte 0x33 = saved R7 argument of fcn_0c45
- bytes 0x23/0x25 = 16-bit control-latch word shifted out by fcn_0e62 (P1.0 data / P1.2 clock / P1.1 latch); bit 0x2f = byte 0x25.7 = chip-select-type control bit for the fcn_0c45 target device

## fcn_0c45 = cs8427_ctl_write(reg=R7, data=R5)  [chip id: likely]

Bit-bangs three bytes MSB-first on P1.4 (data) / P1.3 (clock, pulsed high
then low per bit): first the constant 0x20, then R7, then R5. 0x20 matches
the CS8427 chip-address/write byte (0b0010000_0); existing NOTES.md agrees
(treated as unverified — but the 0x20 byte, 3-byte format, and the project's
known CS8427 hardware make the identification likely). Before the transfer
it clears bit 0x2f (bit 7 of latch byte 0x25) and re-shifts the external
control latch via fcn_0e62 (verified: 0x0e62 shifts IRAM 0x23 then 0x25 out
P1.0/P1.2 and pulses P1.1); after the 3 bytes it sets bit 0x2f and shifts
again — i.e. the target device's select/control line lives in the external
latch and is held in the '0' state during the transfer.

```
0x0c45  8f 33     mov 0x33,r7      ; save arg1 (register/MAP address) in IRAM 0x33
0x0c47  a9 05     mov r1,0x05      ; R1 = direct 0x05 = bank-0 R5 = arg2 (data byte), saved before R-bank use
0x0c49  7c 08     mov r4,#0x8      ; R4 = 8 = bit counter for first byte
0x0c4b  7b 20     mov r3,#0x20     ; R3 = current shift byte = 0x20 (chip-address/write byte; CS8427 = 0b0010000+W, likely)
0x0c4d  7a 01     mov r2,#0x1      ; R2 = byte-phase state = 1 (sending byte 1 of 3)
0x0c4f  c2 2f     clr 0x2f         ; clear bit 0x2f (IRAM 0x25.7) — select/control bit in external latch word, driven low for the transfer
0x0c51  12 0e 62  lcall 0x0e62     ; -> fcn_0e62: shift latch word IRAM 0x23:0x25 out P1.0/P1.2, pulse P1.1 (asserts the select line)
; --- per-bit loop: rotate current byte left once, output former MSB on P1.4, pulse clock P1.3 ---
0x0c54  ec        mov a,r4         ; A = remaining bit count
0x0c55  60 20     jz 0x0c77        ; all 8 bits of this byte sent -> advance byte-phase state machine
0x0c57  78 01     mov r0,#0x1      ; R0 = 1 (rotate-count seed; R0+1 total DJNZ passes = 1 rotate)
0x0c59  af 03     mov r7,0x03      ; R7 = direct 0x03 = bank-0 R3 = current shift byte
0x0c5b  ef        mov a,r7         ; A = current shift byte
0x0c5c  08        inc r0           ; R0 = 2 (DJNZ executes RL exactly once)
0x0c5d  80 01     sjmp 0x0c60      ; enter rotate loop at the DJNZ
0x0c5f  23        rl a             ; rotate left 1: former bit7 lands in bit0
0x0c60  d8 fd     djnz r0,0x0c5f   ; loop: with R0=2 this rotates exactly once
0x0c62  fb        mov r3,a         ; store rotated byte back — next iteration exposes the next bit
0x0c63  30 e0 05  jnb 0xe0,0x0c6b  ; test ACC.0 = the bit just rotated out of MSB position
0x0c66  43 90 10  orl 0x90,#0x10   ; bit=1: P1.4 (serial data line) high
0x0c69  80 03     sjmp 0x0c6e      ; skip the data-low path
0x0c6b  53 90 ef  anl 0x90,#0xef   ; bit=0: P1.4 low
0x0c6e  43 90 08  orl 0x90,#0x8    ; P1.3 (serial clock) high — data valid on rising edge
0x0c71  53 90 f7  anl 0x90,#0xf7   ; P1.3 low — end of clock pulse
0x0c74  1c        dec r4           ; one bit sent
0x0c75  80 dd     sjmp 0x0c54      ; next bit
; --- byte-phase state machine: 1 -> send saved R7, 2 -> send saved R5, 3 -> done ---
0x0c77  ba 01 08  cjne r2,#0x1,0x0c82 ; finished byte 1?
0x0c7a  7a 02     mov r2,#0x2      ; phase 2
0x0c7c  ab 33     mov r3,0x33      ; next byte = saved R7 = register/MAP address
0x0c7e  7c 08     mov r4,#0x8      ; reload 8-bit counter
0x0c80  80 d2     sjmp 0x0c54      ; send byte 2
0x0c82  ba 02 08  cjne r2,#0x2,0x0c8d ; finished byte 2?
0x0c85  7a 03     mov r2,#0x3      ; phase 3
0x0c87  ab 01     mov r3,0x01      ; next byte = direct 0x01 = bank-0 R1 = saved arg2 data byte
0x0c89  7c 08     mov r4,#0x8      ; reload 8-bit counter
0x0c8b  80 c7     sjmp 0x0c54      ; send byte 3
0x0c8d  d2 2f     setb 0x2f        ; all 3 bytes sent: raise select/control bit 0x25.7 again
0x0c8f  12 0e 62  lcall 0x0e62     ; -> fcn_0e62: re-shift latch word (deasserts the select line)
0x0c92  22        ret              ; done
```

## data_0c93 = usb_int_vector_table (0x0c93-0x0cdc, 74 bytes — DATA, Ghidra misdisassembled)

The INT0 ISR (fcn_0dac below) computes DPTR = 0x0c93 + VECINT*2 and fetches a
big-endian handler address (high byte first via `movc`). VECINT holds TI's
interrupt-source codes 0x00-0x24 (Reg_stc1.h lines 234-270; TI UsbEng.c
engEx0 compares VECINT directly against these), NO_INT=0x24 giving exactly
37 entries = 74 bytes, ending exactly at 0x0cdc. Raw bytes verified by xxd:

```
0c93: 0d25 0010 0011 0012 0016 0017 0018 0019
0ca3: 0fc4 001a 001e 001f 0020 0021 0022 1031
0cb3: 1032 103e 0026 1033 1034 1035 0006 0f43
0cc3: 1036 1037 103e 103e 1038 1039 103e 103a
0cd3: 103b 103c 103e 103e 103d
```

| idx | TI name (Reg_stc1.h) | handler | what it is (target verified in listing) |
|-----|------|---------|-------------|
| 0x00 | OEP0_INT | 0x0d25 | EP0 OUT data handler (in this range) |
| 0x01-0x07 | OEP1..OEP7_INT | 0x0010,0x0011,0x0012,0x0016,0x0017,0x0018,0x0019 | RET stubs (each byte = 0x22) |
| 0x08 | IEP0_INT | 0x0fc4 | EP0 IN done handler (continues IN transfer via bits 0x0b/0x0c) |
| 0x09-0x0e | IEP1..IEP6_INT | 0x001a,0x001e,0x001f,0x0020,0x0021,0x0022 | RET stubs |
| 0x0f | IEP7_INT | 0x1031 | RET stub |
| 0x10 | STPOW_INT | 0x1032 | RET stub (setup-overwrite ignored) |
| 0x11 | (reserved) | 0x103e | RET stub |
| 0x12 | SETUP_INT | 0x0026 | SETUP dispatcher (matches brief's known fact 0x0026-0x0118) |
| 0x13 | PSOF_INT | 0x1033 | RET stub |
| 0x14 | SOF_INT | 0x1034 | RET stub |
| 0x15 | RESR_INT | 0x1035 | RET stub |
| 0x16 | SUSR_INT | 0x0006 | `mov 0x0a,#0xe; ret` — posts main-loop command 0x0e on suspend |
| 0x17 | RSTR_INT | 0x0f43 | bus-reset handler (calls 0x0b82, writes OEPDCNTX2/IEPDCNTX1) |
| 0x18 | CPRX_INT | 0x1036 | RET stub |
| 0x19 | CPTX_INT | 0x1037 | RET stub |
| 0x1a | DPRX_INT | 0x103e | RET stub |
| 0x1b | DPTX_INT | 0x103e | RET stub |
| 0x1c | I2CRX_INT | 0x1038 | RET stub |
| 0x1d | I2CTX_INT | 0x1039 | RET stub |
| 0x1e | (reserved) | 0x103e | RET stub |
| 0x1f | XINT_INT | 0x103a | RET stub |
| 0x20-0x23 | (undefined) | 0x103b,0x103c,0x103e,0x103e | RET stubs |
| 0x24 | NO_INT | 0x103d | RET stub |

(The bogus Ghidra instructions CODE:0c93-0x0cdc, including the fake
`LCALL 0x0016` at 0c9a and labels LAB_0caa/0cb8/0cc4/0ccc/0cd0/0cd8/0cdc,
are artifacts of force-disassembling this table.)

## fcn_0cdd = i2c_eeprom_read_byte(addr_hi=R7, addr_lo=R5) -> R7 (and R6)

Hardware-I2C random read of one byte from slave 0xA0 (the boot EEPROM) at a
16-bit word address. Matches TI ROM I2c.c I2CRead word-address path
step-for-step (I2c.c:60-73 subaddress MSB/LSB writes, I2c.c:99-108
read-address + STOP_READ + dummy I2CDATO write + wait RCV_DATA_FULL + read
I2CDATI; TI uses dummy 0xFF, this build uses 0x00 — same clocking purpose).
I2CSTA bits per I2c.h: 0x01 STOP_WRITE, 0x02 STOP_READ, 0x08
XMIT_DATA_EMPTY, 0x80 RCV_DATA_FULL. Ghidra's LAB_0ce0/0ce4 labels into this
function are bogus xrefs from the misdisassembled table above. Callers:
0x04e2, 0x04f3 (EEPROM parameter reads, owned by another annotator).

```
0x0cdd  90 ff c0  mov dptr,#0xffc0 ; DPTR -> I2CSTA (Reg_stc1.h:38)
0x0ce0  e0        movx a,@dptr     ; read I2CSTA
0x0ce1  54 fc     anl a,#0xfc      ; clear STOP_WRITE|STOP_READ bits
0x0ce3  f0        movx @dptr,a     ; SFR write: I2CSTA &= 0xFC — no stop condition yet
0x0ce4  7e a0     mov r6,#0xa0     ; R6 = 0xA0 = I2C write address of boot EEPROM
0x0ce6  90 ff c3  mov dptr,#0xffc3 ; DPTR -> I2CADR (Reg_stc1.h:41)
0x0ce9  ee        mov a,r6         ; A = 0xA0
0x0cea  f0        movx @dptr,a     ; SFR write: I2CADR = 0xA0 (address EEPROM for write of subaddress)
0x0ceb  90 ff c1  mov dptr,#0xffc1 ; DPTR -> I2CDATO (Reg_stc1.h:39)
0x0cee  ef        mov a,r7         ; A = word-address high byte (arg1)
0x0cef  f0        movx @dptr,a     ; SFR write: I2CDATO = addr_hi (I2c.c:63 'Write MSB address first')
0x0cf0  90 ff c0  mov dptr,#0xffc0 ; DPTR -> I2CSTA
0x0cf3  e0        movx a,@dptr     ; read I2CSTA
0x0cf4  30 e3 f9  jnb 0xe3,0x0cf0  ; spin until bit3 XMIT_DATA_EMPTY (I2c.c:69 WaitOnI2C(XMIT_DATA_EMPTY)) — no timeout here
0x0cf7  90 ff c1  mov dptr,#0xffc1 ; DPTR -> I2CDATO
0x0cfa  ed        mov a,r5         ; A = word-address low byte (arg2)
0x0cfb  f0        movx @dptr,a     ; SFR write: I2CDATO = addr_lo (I2c.c:73)
0x0cfc  90 ff c0  mov dptr,#0xffc0 ; DPTR -> I2CSTA
0x0cff  e0        movx a,@dptr     ; read I2CSTA
0x0d00  30 e3 f9  jnb 0xe3,0x0cfc  ; spin until XMIT_DATA_EMPTY again (I2c.c:79)
0x0d03  43 06 01  orl 0x06,#0x1    ; IRAM 0x06 = bank-0 R6: 0xA0 -> 0xA1 = EEPROM read address (I2C_READ_ADDR, I2c.h:24)
0x0d06  90 ff c3  mov dptr,#0xffc3 ; DPTR -> I2CADR
0x0d09  ee        mov a,r6         ; A = 0xA1
0x0d0a  f0        movx @dptr,a     ; SFR write: I2CADR = 0xA1 (repeated-start, now reading)
0x0d0b  90 ff c1  mov dptr,#0xffc1 ; DPTR -> I2CDATO
0x0d0e  e4        clr a            ; A = 0
0x0d0f  f0        movx @dptr,a     ; SFR write: I2CDATO = 0x00 dummy — clocks the read byte in (TI uses 0xFF, I2c.c:102)
0x0d10  90 ff c0  mov dptr,#0xffc0 ; DPTR -> I2CSTA
0x0d13  e0        movx a,@dptr     ; read I2CSTA
0x0d14  44 02     orl a,#0x2       ; set STOP_READ — NAK+stop after this (single-byte read, I2c.c:101)
0x0d16  f0        movx @dptr,a     ; SFR write: I2CSTA |= 0x02 (STOP_READ)
0x0d17  90 ff c0  mov dptr,#0xffc0 ; DPTR -> I2CSTA
0x0d1a  e0        movx a,@dptr     ; read I2CSTA
0x0d1b  30 e7 f9  jnb 0xe7,0x0d17  ; spin until bit7 RCV_DATA_FULL (I2c.c:103)
0x0d1e  90 ff c2  mov dptr,#0xffc2 ; DPTR -> I2CDATI (Reg_stc1.h:40)
0x0d21  e0        movx a,@dptr     ; read the received EEPROM byte
0x0d22  fe        mov r6,a         ; return value copy in R6
0x0d23  ff        mov r7,a         ; return value in R7 (Keil BYTE return register)
0x0d24  22        ret              ; done
```

## fcn_0d25 = ep0_out_data_handler (OEP0_INT, vector-table entry 0x00)

No direct callers — reached only through the vector table. Handles
completion of an EP0 OUT data stage for the two class requests the SETUP
dispatcher deferred: at 0x006b bmRequestType 0x22 (class, host-to-device,
endpoint — UAC SET_CUR to the iso endpoint) sets IRAM 0x0d=1 and bit 0x0b;
at 0x0063 bmRequestType 0x21 with bRequest!=0 sets 0x0d=2 and bit 0x0b.
Helper 0x0b11 (verified) loads IRAM 0x1b:0x1c = 0xFA:0x10 and falls into
0x0b17 which returns DPTR=0xFA10, the EP0 buffer this firmware uses for the
received OUT payload. For state 1 the first payload byte is the LSB of the
little-endian UAC tSampleFreq: 44100 = 0x00AC44 -> first byte 0x44; 48000 =
0x00BB80 -> first byte 0x80 (arithmetic verified). The decoded result is
posted as a command code in IRAM 0x0a for the main loop.

```
0x0d25  30 0b 3f  jnb 0x0b,0x0d67  ; bit 0x0b = OUT-data-stage-pending; if clear this OUT is unexpected -> cleanup path
0x0d28  e5 0d     mov a,0x0d       ; A = IRAM 0x0d = which class request is pending (1 or 2)
0x0d2a  b4 01 18  cjne a,#0x1,0x0d45 ; not state 1 -> try state 2
0x0d2d  12 0b 11  lcall 0x0b11     ; -> fcn_0b11/0b17: DPTR = 0xFA10 (EP0 buffer holding the OUT payload)
0x0d30  e0        movx a,@dptr     ; A = payload byte 0 = tSampleFreq LSB
0x0d31  ff        mov r7,a         ; keep a copy for the further compares
0x0d32  b4 44 03  cjne a,#0x44,0x0d38 ; 0x44 = LSB of 44100 (0xAC44)?
0x0d35  75 0a 07  mov 0x0a,#0x7    ; yes: post main-loop command 7 = switch to 44.1 kHz
0x0d38  ef        mov a,r7         ; restore payload byte
0x0d39  b4 80 03  cjne a,#0x80,0x0d3f ; 0x80 = LSB of 48000 (0xBB80)?
0x0d3c  75 0a 08  mov 0x0a,#0x8    ; yes: post command 8 = switch to 48 kHz
0x0d3f  ef        mov a,r7         ; restore payload byte
0x0d40  70 03     jnz 0x0d45       ; not zero -> done with state-1 decode
0x0d42  75 0a 06  mov 0x0a,#0x6    ; byte 0x00: post command 6 (rate/mode meaning not resolvable in this range — command consumer owns it)
0x0d45  e5 0d     mov a,0x0d       ; re-load pending-request state
0x0d47  b4 02 0f  cjne a,#0x2,0x0d59 ; not state 2 -> finish
0x0d4a  12 0b 11  lcall 0x0b11     ; DPTR = 0xFA10 again (EP0 OUT payload)
0x0d4d  e0        movx a,@dptr     ; A = payload byte 0 (boolean control value)
0x0d4e  b4 01 05  cjne a,#0x1,0x0d56 ; value == 1?
0x0d51  75 0a 04  mov 0x0a,#0x4    ; yes: post command 4 (control ON case; exact control owned by cmd consumer)
0x0d54  80 03     sjmp 0x0d59      ; skip the else
0x0d56  75 0a 05  mov 0x0a,#0x5    ; else: post command 5 (control OFF case)
0x0d59  c2 0b     clr 0x0b         ; data stage consumed: clear OUT-pending flag
0x0d5b  c2 0c     clr 0x0c         ; clear IN-transfer-in-progress flag too
0x0d5d  90 ff 68  mov dptr,#0xff68 ; DPTR -> IEPCNF0 (Reg_stc1.h:109)
0x0d60  e0        movx a,@dptr     ; read IEPCNF0
0x0d61  44 20     orl a,#0x20      ; set bit5 TOGLE = DATA1 for the upcoming status-stage IN (USB: status stage is always DATA1)
0x0d63  f0        movx @dptr,a     ; SFR write: IEPCNF0 |= 0x20
0x0d64  02 0b 82  ljmp 0x0d64->0x0b82 ; tail-call fcn_0b82: OEPDCNTX0 (0xFFAB) = 0 and IEPDCNTX0 (0xFF6B) = 0 — clears the NAK bit (byte-count bit7) on both EP0 buffers, re-arming OUT and queueing a zero-length IN (status-stage ZLP); RET from there
0x0d67  12 0b 1e  lcall 0x0b1e     ; unexpected OUT: fcn_0b1e clears TOGLE|STALL (&= 0xD7) on IEPCNF0 and OEPCNF0 (falls into 0b2b for the store)
0x0d6a  22        ret              ; done
```

## fcn_0d6b = ep0_clamp_len_to_wlength (called from 0x01eb)

Clamps the queued EP0 IN response length (IRAM 0x09 = low byte, 0x0b = high
byte — same pair initialized at 0x003b/0x003d) to the SETUP packet's
wLength at 0xFF2E/0xFF2F (SETPACK base 0xFF28, Reg_stc1.h:17-18; wLength is
bytes 6-7 of the SETUP packet, little-endian, so 0xFF2E = LSB). Then sets
bit 0x0d if the (possibly clamped) length is still less than wLength —
the "response shorter than host requested" flag used by the EP0 IN path to
terminate the transfer with a short packet. The second comparison tests the
low bytes first and only falls back to the high bytes on equality; given
length <= wLength after the clamp, low(len) < low(wLen) does imply
len != wLen, but the len < wLength case with low(len) > low(wLen) (e.g.
0x00FF vs 0x0100) leaves the flag clear — noted as-is, semantics of that
corner belong to the consumer of bit 0x0d.

```
0x0d6b  90 ff 2f  mov dptr,#0xff2f ; DPTR -> SETPACK+7 = wLength high byte
0x0d6e  e0        movx a,@dptr     ; A = wLength.hi
0x0d6f  ff        mov r7,a         ; R7 = wLength.hi
0x0d70  e5 0b     mov a,0x0b       ; A = queued length high byte
0x0d72  d3        setb cy          ; borrow-in for a strict > test
0x0d73  9f        subb a,r7        ; compute len.hi - wLen.hi - 1
0x0d74  50 0f     jnc 0x0d85       ; no borrow -> len.hi > wLen.hi -> length exceeds wLength -> clamp
0x0d76  e0        movx a,@dptr     ; re-read wLength.hi
0x0d77  b5 0b 17  cjne a,0x0b,0x0d91 ; high bytes differ (so len.hi < wLen.hi, len < wLength) -> skip clamp
0x0d7a  90 ff 2e  mov dptr,#0xff2e ; high bytes equal: DPTR -> wLength low byte
0x0d7d  e0        movx a,@dptr     ; A = wLength.lo
0x0d7e  ff        mov r7,a         ; R7 = wLength.lo
0x0d7f  e5 09     mov a,0x09       ; A = queued length low byte
0x0d81  d3        setb cy          ; borrow-in for strict > test
0x0d82  9f        subb a,r7        ; len.lo - wLen.lo - 1
0x0d83  40 0c     jc 0x0d91        ; borrow -> len.lo <= wLen.lo -> len <= wLength -> no clamp
0x0d85  90 ff 2e  mov dptr,#0xff2e ; clamp path: DPTR -> wLength.lo
0x0d88  e0        movx a,@dptr     ; A = wLength.lo
0x0d89  f5 09     mov 0x09,a       ; length.lo = wLength.lo
0x0d8b  a3        inc dptr         ; DPTR -> wLength.hi (0xFF2F)
0x0d8c  e0        movx a,@dptr     ; A = wLength.hi
0x0d8d  f5 0b     mov 0x0b,a       ; length.hi = wLength.hi (length now == wLength exactly)
0x0d8f  c2 0d     clr 0x0d         ; clear short-transfer flag (we will send exactly wLength)
0x0d91  90 ff 2e  mov dptr,#0xff2e ; second pass: DPTR -> wLength.lo
0x0d94  e0        movx a,@dptr     ; A = wLength.lo
0x0d95  ff        mov r7,a         ; R7 = wLength.lo
0x0d96  e5 09     mov a,0x09       ; A = length.lo
0x0d98  c3        clr cy           ; plain compare
0x0d99  9f        subb a,r7        ; length.lo - wLength.lo
0x0d9a  40 0d     jc 0x0da9        ; length.lo < wLength.lo -> (with len<=wLen) len < wLength -> set flag
0x0d9c  e0        movx a,@dptr     ; re-read wLength.lo
0x0d9d  b5 09 0b  cjne a,0x09,0x0dab ; low bytes differ (length.lo > wLength.lo) -> return, flag stays clear
0x0da0  a3        inc dptr         ; low bytes equal: DPTR -> wLength.hi
0x0da1  e0        movx a,@dptr     ; A = wLength.hi
0x0da2  ff        mov r7,a         ; R7 = wLength.hi
0x0da3  e5 0b     mov a,0x0b       ; A = length.hi
0x0da5  c3        clr cy           ; plain compare
0x0da6  9f        subb a,r7        ; length.hi - wLength.hi
0x0da7  50 02     jnc 0x0dab       ; length.hi >= wLength.hi -> lengths equal -> return, flag clear
0x0da9  d2 0d     setb 0x0d        ; length < wLength: flag short transfer (short packet / early termination)
0x0dab  22        ret              ; done
```

## fcn_0dac = usb_int0_isr (INT0 vector: 0x0003 `ljmp 0x0dac`)

The application's USB interrupt handler — the compiled equivalent of TI
UsbEng.c engEx0() (which reads VECINT, EA=0, switches on the source, clears
VECINT, EA=1), implemented as a dispatch through the 37-entry table at
0x0c93. Runs in register bank 2 (PSW=0x10), so `mov r2,0x16` below is a
bank-2 R6->R2 register copy. Unlike TI's ROM version (which leaves VECINT
uncleared for SETUP_INT so the app also gets an ISR, UsbEng.c:253-261),
this handler writes VECINT=0 unconditionally after the sub-handler returns.

```
0x0dac  c0 e0     push a           ; save ACC
0x0dae  c0 f0     push b           ; save B
0x0db0  c0 83     push dph         ; save DPH
0x0db2  c0 82     push dpl         ; save DPL
0x0db4  c0 d0     push psw         ; save PSW
0x0db6  75 d0 10  mov psw,#0x10    ; select register bank 2 (ISR-private R0-R7 at IRAM 0x10-0x17)
0x0db9  c2 af     clr 0xaf         ; EA=0 — no interrupt nesting while dispatching (matches TI engEx0 'EA = 0')
0x0dbb  90 ff b2  mov dptr,#0xffb2 ; DPTR -> VECINT (Reg_stc1.h:23)
0x0dbe  e0        movx a,@dptr     ; A = interrupt source code 0x00..0x24 (OEP0_INT..NO_INT, Reg_stc1.h:234-270)
0x0dbf  ff        mov r7,a         ; keep a copy (not used after — dead store)
0x0dc0  25 e0     add a,a          ; A = source*2 = byte offset into 2-byte-entry table
0x0dc2  24 93     add a,#0x93      ; low byte of 0x0c93 + offset
0x0dc4  f5 82     mov dpl,a        ; DPL = table entry address low
0x0dc6  e4        clr a            ; A = 0
0x0dc7  34 0c     addc a,#0xc      ; high byte 0x0c + carry from the low-byte add
0x0dc9  f5 83     mov dph,a        ; DPTR -> table entry at 0x0c93 + 2*VECINT
0x0dcb  e4        clr a            ; index 0 for movc
0x0dcc  93        movc a,@a+dptr   ; A = handler address HIGH byte (table is big-endian)
0x0dcd  fe        mov r6,a         ; bank-2 R6 (IRAM 0x16) = handler.hi
0x0dce  74 01     mov a,#0x1       ; index 1
0x0dd0  93        movc a,@a+dptr   ; A = handler address LOW byte
0x0dd1  aa 16     mov r2,0x16      ; R2 = IRAM 0x16 = bank-2 R6 = handler.hi (reg-to-reg copy via direct addr)
0x0dd3  f9        mov r1,a         ; R1 = handler.lo
0x0dd4  7b ff     mov r3,#0xff     ; UNKNOWN — R3 is not read by the trampoline at 0x0f96 (verified: it only uses R2:R1); likely a compiler calling-convention artifact
0x0dd6  12 0f 96  lcall 0x0f96     ; -> jump trampoline: DPH=R2, DPL=R1, clr A, jmp @A+DPTR — effectively CALL handler (its RET returns here)
0x0dd9  90 ff b2  mov dptr,#0xffb2 ; DPTR -> VECINT again
0x0ddc  e4        clr a            ; A = 0
0x0ddd  f0        movx @dptr,a     ; SFR write: VECINT = 0 — acknowledge/clear the interrupt vector (datasheet/TI: cleared by writing 0)
0x0dde  d2 af     setb 0xaf        ; EA=1 — re-enable interrupts
0x0de0  d0 d0     pop psw          ; restore PSW (back to caller's register bank)
0x0de2  d0 82     pop dpl          ; restore DPL
0x0de4  d0 83     pop dph          ; restore DPH
0x0de6  d0 f0     pop b            ; restore B
0x0de8  d0 e0     pop a            ; restore ACC
0x0dea  32        reti             ; return from interrupt
```

## fcn_0deb = sfr_store_then_cpt_cfg_tail

Single shared-tail instruction: callers load DPTR/A, LCALL here to perform
the store, and execution falls straight into fcn_0dec (0x0dec+, next
annotator's range: codec-port config writes starting with 0xFFE6=0xA8).
Verified callers: 0x07ac arrives with DPTR=0xFFB1 (GLOBCTL, Reg_stc1.h:22)
and A=GLOBCTL|0x01 (bit0 = codec port enable); 0x092e arrives with
DPTR=0xFFD4 (CPTRXCNF4, Reg_stc1.h:55) and A=0x03.

```
0x0deb  f0        movx @dptr,a     ; SFR write, caller-supplied: GLOBCTL |= 0x01 (from 0x07ac) or CPTRXCNF4 = 0x03 (from 0x092e); falls through into fcn_0dec (0x0dec, out of range — codec-port register setup)
```

Flow exits the range at 0x0dec (fcn_0dec, next annotator).

### 5.10 Range 0x0DEC-0x0F6F

# Rev 20 annotation — 0x0dec .. 0x0f70 (exclusive)

Context: the range sits between the USB-interrupt ISR epilogue (0x0dde `SETB EA` / POP / RETI,
owned by the previous range) and the Keil-style jump/case helpers at 0x0f70/0x0f96 (owned by the
next range). Everything in this range is code; no data bytes. One instruction at 0x0deb
(`F0  MOVX @DPTR,A`, one byte, just below this range) is a tail entry that callers
(0x092e: CPTRXCNF1=3; 0x07ac: GLOBCTL|=0x01 CPTEN) use to perform one SFR write before falling
into 0x0dec.

Bit-address convention used below: 8051 bit addresses 0x00-0x7F map to IRAM bytes 0x20-0x2F
(bit addr = (byte-0x20)*8 + bit). SFR 0x90 = P1, SFR 0xB0 = P3, bit 0xAF = EA, bits 0xE0..0xE7 = ACC.0..7.

IRAM variables tracked in this range:
- byte 0x20: previous P3 snapshot (button scan); its bits 0x03/0x04/0x05 = previous P3.3/P3.4/P3.5
- byte 0x21 bits (bit addrs 0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e): EP0/SETUP request state flags,
  used by the request machinery at 0x01f1-0x0448 and by the IEP0 handler 0x0fc4; cleared on bus reset
- byte 0x22 (bits 0x10-0x17): 8-bit image of the mux shift register committed by 0x0f0c
  (bits 0-2 = channel-A 3-way select pattern, bits 3-5 = channel-B, bit 6 = common flag)
- byte 0x23 (bits 0x18-0x1f): first byte of the 16-bit control shift image committed by 0x0e62;
  bit 6 (bit addr 0x1e) doubles as the "leave P1.7/P1.6 high" flag inside 0x0f0c
- byte 0x25 (bits 0x28-0x2f): second byte of the 16-bit control image; bits 0/2 and 1/3 are the
  two button-state pairs, bit 7 (0x2f) is the chip-select line framed around 0x0c45 transactions
- byte 0x26 bit 0 (bit addr 0x30): "first byte still pending" loop flag inside 0x0e62
- byte 0x06: event flags returned by the button scan (bit 0 = settings changed)
- bytes 0x31/0x32: pending serial-control (CS8427 per NOTES.md) subaddress/value pair
- byte 0x05 = bank-0 R5 accessed by direct address (compiler artifact for R5->R7 copies)

---

## fcn_0dec = acg_set_freq_48k_family
Callers: 0x078e (sample-rate select, code 3), 0x081b (init path). Programs both ACG frequency
synthesizers to N = 0x61A80F/2^18 = 24.4141197 -> f = 600/N = 24.5759424 MHz (48 kHz family,
512*48k nominal 24.576 MHz; datasheet SLES025B example value for exactly 24.576 MHz is
61 A8 00 — firmware adds 0x0F LSBs, ~2.3 ppm low). Per datasheet, writing FRQ0 last latches the
24-bit value into the synthesizer. The 44.1 kHz twin of this function is inline at 0x075f-0x0782
(value 6A 4B 20 -> 22.5790 MHz) and reuses the 0x0e0f tail below.

```
0x0dec  90 ff e6   mov dptr,#0xffe6      ; DPTR = ACGFRQ1 (Reg_stc1.h:67)
0x0def  74 a8      mov a,#0xa8           ; middle byte of 24-bit N = 0x61A80F
0x0df1  f0         movx @dptr,a          ; ACGFRQ1 = 0xA8
0x0df2  90 ff e5   mov dptr,#0xffe5      ; DPTR = ACGFRQ2 (MSB, integer part bits; Reg_stc1.h:66)
0x0df5  74 61      mov a,#0x61           ; 0x61 -> integer part 011000.01... (N ~ 24.414)
0x0df7  f0         movx @dptr,a          ; ACGFRQ2 = 0x61
0x0df8  90 ff e7   mov dptr,#0xffe7      ; DPTR = ACGFRQ0 (LSB; Reg_stc1.h:68)
0x0dfb  74 0f      mov a,#0xf            ; LSB 0x0F (nominal-exact would be 0x00)
0x0dfd  f0         movx @dptr,a          ; ACGFRQ0 = 0x0F — LSB write LOADS synth 1 (datasheet: write FRQ0 last)
0x0dfe  90 ff f8   mov dptr,#0xfff8      ; DPTR = ACG2FRQ1 (Reg_stc1.h:72)
0x0e01  74 a8      mov a,#0xa8           ; same value for synthesizer 2
0x0e03  f0         movx @dptr,a          ; ACG2FRQ1 = 0xA8
0x0e04  90 ff f7   mov dptr,#0xfff7      ; DPTR = ACG2FRQ2 (Reg_stc1.h:71)
0x0e07  74 61      mov a,#0x61           ; MSB
0x0e09  f0         movx @dptr,a          ; ACG2FRQ2 = 0x61
0x0e0a  90 ff f9   mov dptr,#0xfff9      ; DPTR = ACG2FRQ0 (Reg_stc1.h:73)
0x0e0d  74 0f      mov a,#0xf            ; LSB; falls through into fcn_0e0f which performs the write
```

## fcn_0e0f = acg_commit_and_ctl (tail entry, caller pre-loads DPTR and A)
Direct caller 0x0782 arrives with DPTR=0xFFF9 (ACG2FRQ0), A=0x20 — the last byte of the
44.1 kHz value 0x6A4B20 (N = 26.5734 -> 600/N = 22.5790 MHz, nominal 22.5792 = 512*44.1k).
Fall-through from 0x0dec arrives with DPTR=0xFFF9, A=0x0F.

```
0x0e0f  f0         movx @dptr,a          ; ACG2FRQ0 write — loads synthesizer 2 with new 24-bit N
0x0e10  90 ff e1   mov dptr,#0xffe1      ; DPTR = ACGCTL (Reg_stc1.h:60)
0x0e13  74 06      mov a,#0x6            ; 0x06 = DIVEN(bit2)=1, MCLKO2S[1:0]=10b (source = ACG2 clk after /M2),
                                          ;   MCLKO1S[1:0]=00b (source = ACG1 clk after /M1), MCLKO1EN=MCLKO2EN=0
                                          ;   (outputs still disabled; callers enable them later via ACGCTL |= 0xC0 at 0x07cc/0x0827)
0x0e15  f0         movx @dptr,a          ; ACGCTL = 0x06
0x0e16  22         ret                   ; return with DPTR=0xFFE1 — exploited by fcn_0e17 callers
```

## fcn_0e17 / fcn_0e18 = acg_dividers_div2
Two entries. 0x0e17 (callers 0x0931, 0x081e): entered right after a call that returned with
DPTR=0xFFE1 (ACGCTL); INC makes DPTR=0xFFE2 = ACG1DCTL. 0x0e18 (caller 0x0739): entered with
DPTR already loaded with #0xFFE2. Sets both ACG output dividers to divide-by-2, so
MCLK = 24.576/2 = 12.288 MHz (256*48k) or 22.5792/2 = 11.2896 MHz (256*44.1k).

```
0x0e17  a3         inc dptr              ; DPTR: 0xFFE1 -> 0xFFE2 = ACG1DCTL (Reg_stc1.h: ACG1DCTL not listed by name;
                                          ;   datasheet 6.5.3.10, address FFE2h)
0x0e18  74 10      mov a,#0x10           ; 0x10 = DIVM=0001b (divide by 2), DIVI=000b (divide by 1)
0x0e1a  f0         movx @dptr,a          ; ACG1DCTL = 0x10 -> ACG1 output = synth1/2
0x0e1b  90 ff f6   mov dptr,#0xfff6      ; DPTR = ACG2DCTL (Reg_stc1.h:70)
0x0e1e  f0         movx @dptr,a          ; ACG2DCTL = 0x10 -> ACG2 output = synth2/2 (DIVM=0001b)
0x0e1f  22         ret                   ; done — both MCLK paths now 256*fs
```

## fcn_0e20 = queue_cs8427_reg4_val40
Callers 0x0788/0x0794/0x07c2 (each sample-rate path). Only queues two bytes; the common code at
0x07c5 does `MOV R5,0x32 / MOV R7,0x31 / LCALL 0x0c45`, and 0x0c45 (verified from its bytes)
shifts the 3-byte packet [0x20, R7, R5] MSB-first on P1.4 data / P1.3 clock, framed by clearing
then setting bit 0x2f (byte-0x25 bit 7, a chip-select line shifted out via fcn_0e62).
NOTES.md identifies the target as the CS8427 S/PDIF transceiver; chip identity and the meaning of
its register 4 = 0x40 are NOT re-verified here (no CS8427 datasheet in the repo).

```
0x0e20  75 31 04   mov 0x31,#0x4         ; pending serial-control subaddress = 4
0x0e23  75 32 40   mov 0x32,#0x40        ; pending value = 0x40
0x0e26  22         ret                   ; consumed at 0x07c5 -> lcall 0x0c45 (R7=[0x31], R5=[0x32])
```

## fcn_0e27 = button_a_cycle_3state
Called from 0x0ef4 (p3_button_scan) on a rising edge of P3.3. Advances a 3-position selector:
state pair (bit0x28, bit0x2a) cycles (0,x) -> (1,1) -> (1,0) -> (0,0) -> ..., and writes the
matching pattern into mux-image bits 0x10/0x11/0x12 (IRAM 0x22 bits 0-2): 101 -> 110 -> 011
(one bit low per state — one-cold select). Consistent with a Mbox front-panel source button
cycling three input modes; which physical signal each image bit drives is not hardware-verified.

```
0x0e27  20 28 0c   jb 0x28,0x0e36        ; state bit A (byte0x25.0): if already set, go check phase bit
0x0e2a  d2 28      setb 0x28             ; state (0,x) -> (1,1): enter position 1
0x0e2c  d2 2a      setb 0x2a             ; set phase bit (byte0x25.2)
0x0e2e  d2 10      setb 0x10             ; mux image bit0 = 1   \
0x0e30  c2 11      clr 0x11              ; mux image bit1 = 0    > pattern 101 (position 1)
0x0e32  d2 12      setb 0x12             ; mux image bit2 = 1   /
0x0e34  80 1c      sjmp 0x0e52           ; -> common tail (bit 0x16 gating)
0x0e36  30 28 0f   jnb 0x28,0x0e48       ; (unreachable guard: 0x28 known set here) -> position-3 code if clear
0x0e39  30 2a 0c   jnb 0x2a,0x0e48       ; if phase bit clear we were in position 2 -> wrap to position 3
0x0e3c  d2 28      setb 0x28             ; state (1,1) -> (1,0): enter position 2
0x0e3e  c2 2a      clr 0x2a              ; clear phase bit
0x0e40  d2 10      setb 0x10             ; mux image bit0 = 1   \
0x0e42  d2 11      setb 0x11             ; mux image bit1 = 1    > pattern 110 (position 2)
0x0e44  c2 12      clr 0x12              ; mux image bit2 = 0   /
0x0e46  80 0a      sjmp 0x0e52           ; -> common tail
0x0e48  c2 28      clr 0x28              ; state -> (0,0): enter position 3 (next press restarts cycle)
0x0e4a  c2 2a      clr 0x2a              ; clear phase bit
0x0e4c  c2 10      clr 0x10              ; mux image bit0 = 0   \
0x0e4e  d2 11      setb 0x11             ; mux image bit1 = 1    > pattern 011 (position 3)
0x0e50  d2 12      setb 0x12             ; mux image bit2 = 1   /
0x0e52  20 2c 02   jb 0x2c,0x0e57        ; common tail: if condition bit 0x2c (byte0x25.4) set, skip setting 0x16
0x0e55  d2 16      setb 0x16             ; mux image bit6 = 1 when 0x2c clear
0x0e57  30 2c 02   jnb 0x2c,0x0e5c       ; if 0x2c set ...
0x0e5a  c2 16      clr 0x16              ; ... force image bit6 = 0
0x0e5c  30 2d 02   jnb 0x2d,0x0e61       ; if condition bit 0x2d (byte0x25.5) set ...
0x0e5f  c2 16      clr 0x16              ; ... also force image bit6 = 0. UNKNOWN — what sets 0x2c/0x2d (set outside this range)
0x0e61  22         ret                   ; caller commits image via 0x0f0c/0x0e62 when flag says changed
```

## fcn_0e62 = shiftreg16_commit_p1_0_1_2
The 16-bit control shift-register commit. Shifts IRAM byte 0x23 then byte 0x25, MSB first:
data on P1.0, one clock pulse per bit on P1.2, one latch pulse on P1.1 at the end.
(NOTES.md describes this same routine at legacy +0x12-shifted addresses 0x0E74/0x0EA8.)
Uses direct address 0x05 (= bank-0 R5) so it must run with register bank 0 selected — true for
all its callers (mainline, 11 XREFs including the serial-control writer 0x0c51 and main loop 0x0ae9).

```
0x0e62  7e 08      mov r6,#0x8           ; bit counter = 8
0x0e64  ad 23      mov r5,0x23           ; R5 = first image byte (IRAM 0x23)
0x0e66  d2 30      setb 0x30             ; flag (byte0x26.0): second byte still to send
0x0e68  ee         mov a,r6              ; top of per-bit loop
0x0e69  60 20      jz 0x0e8b             ; all 8 bits of current byte done -> byte-switch logic
0x0e6b  78 01      mov r0,#0x1           ; rotate-count seed (R0 ends up 2 -> exactly one RL)
0x0e6d  af 05      mov r7,0x05           ; R7 = direct[0x05] = bank-0 R5 (compiler-style reg copy)
0x0e6f  ef         mov a,r7              ; A = current shift byte
0x0e70  08         inc r0                ; R0 = 2
0x0e71  80 01      sjmp 0x0e74           ; enter rotate loop at the DJNZ
0x0e73  23         rl a                  ; rotate left: old bit7 -> bit0
0x0e74  d8 fd      djnz r0,0x0e73        ; executes RL exactly once (R0: 2->1 rotates, 1->0 exits)
0x0e76  fd         mov r5,a              ; store rotated byte back (next MSB now in bit7... after next RL)
0x0e77  30 e0 05   jnb 0xe0,0x0e7f       ; test ACC.0 = the bit just rotated out (MSB-first)
0x0e7a  43 90 01   orl 0x90,#0x1         ; P1.0 = 1 (serial data high)
0x0e7d  80 03      sjmp 0x0e82           ;
0x0e7f  53 90 fe   anl 0x90,#0xfe        ; P1.0 = 0 (serial data low)
0x0e82  43 90 04   orl 0x90,#0x4         ; P1.2 = 1 (clock high)
0x0e85  53 90 fb   anl 0x90,#0xfb        ; P1.2 = 0 (clock low) — one bit clocked out
0x0e88  1e         dec r6                ; bit counter--
0x0e89  80 dd      sjmp 0x0e68           ; next bit
0x0e8b  30 30 08   jnb 0x30,0x0e96       ; both bytes sent? (flag clear) -> latch
0x0e8e  c2 30      clr 0x30              ; mark second byte in progress
0x0e90  ad 25      mov r5,0x25           ; R5 = second image byte (IRAM 0x25 — contains button states + CS bit 0x2f)
0x0e92  7e 08      mov r6,#0x8           ; 8 more bits
0x0e94  80 d2      sjmp 0x0e68           ; shift second byte
0x0e96  43 90 02   orl 0x90,#0x2         ; P1.1 = 1 — latch/strobe the 16 shifted bits into the register
0x0e99  53 90 fd   anl 0x90,#0xfd        ; P1.1 = 0 — latch released
0x0e9c  22         ret                   ;
```

## fcn_0e9d = button_b_cycle_3state
Mirror image of fcn_0e27 for the second channel: called from 0x0f01 on a rising edge of P3.4.
State pair = bits 0x29/0x2b (byte 0x25 bits 1,3); output pattern = mux-image bits 0x13/0x14/0x15
(byte 0x22 bits 3-5), same 101 -> 110 -> 011 sequence, same bit-0x16 tail.

```
0x0e9d  20 29 0c   jb 0x29,0x0eac        ; state bit B set? no -> enter position 1
0x0ea0  d2 29      setb 0x29             ; state -> (1,1)
0x0ea2  d2 2b      setb 0x2b             ; phase bit (byte0x25.3)
0x0ea4  d2 13      setb 0x13             ; mux image bit3 = 1   \
0x0ea6  c2 14      clr 0x14              ; mux image bit4 = 0    > pattern 101 (position 1)
0x0ea8  d2 15      setb 0x15             ; mux image bit5 = 1   /
0x0eaa  80 19      sjmp 0x0ec5           ; -> common tail
0x0eac  30 2b 0c   jnb 0x2b,0x0ebb       ; phase clear -> was position 2 -> wrap to position 3
0x0eaf  d2 29      setb 0x29             ; state -> (1,0): position 2
0x0eb1  c2 2b      clr 0x2b              ;
0x0eb3  d2 13      setb 0x13             ; mux image bit3 = 1   \
0x0eb5  d2 14      setb 0x14             ; mux image bit4 = 1    > pattern 110 (position 2)
0x0eb7  c2 15      clr 0x15              ; mux image bit5 = 0   /
0x0eb9  80 0a      sjmp 0x0ec5           ; -> common tail
0x0ebb  c2 29      clr 0x29              ; state -> (0,0): position 3
0x0ebd  c2 2b      clr 0x2b              ;
0x0ebf  c2 13      clr 0x13              ; mux image bit3 = 0   \
0x0ec1  d2 14      setb 0x14             ; mux image bit4 = 1    > pattern 011 (position 3)
0x0ec3  d2 15      setb 0x15             ; mux image bit5 = 1   /
0x0ec5  20 2c 02   jb 0x2c,0x0eca        ; common tail, identical to 0x0e52:
0x0ec8  d2 16      setb 0x16             ; image bit6 = 1 when bit 0x2c clear
0x0eca  30 2c 02   jnb 0x2c,0x0ecf       ;
0x0ecd  c2 16      clr 0x16              ; forced 0 when 0x2c set
0x0ecf  30 2d 02   jnb 0x2d,0x0ed4       ;
0x0ed2  c2 16      clr 0x16              ; forced 0 when 0x2d set. UNKNOWN — 0x2c/0x2d producers are outside this range
0x0ed4  22         ret                   ;
```

## fcn_0ed5 = p3_button_scan
Called from the main loop at 0x0adf. Edge-detects three P3 inputs against the previous snapshot
in IRAM 0x20 and dispatches: P3.5 rising -> 0x1028 (out of range — owned by next annotator),
P3.3 rising -> button_a_cycle_3state, P3.4 rising -> button_b_cycle_3state. Returns R7 = IRAM 0x06
(bit0 = "settings changed"); the main loop (0x0ae3) tests R7 bit0 and, if set, commits both
shift registers (LCALL 0x0f0c then 0x0e62).

```
0x0ed5  e4         clr a                 ;
0x0ed6  fe         mov r6,a              ; R6 = 0 (unused remnant / high byte of return)
0x0ed7  ad b0      mov r5,0xb0           ; R5 = P3 (SFR 0xB0) — read current button/input pins
0x0ed9  ed         mov a,r5              ;
0x0eda  b5 20 03   cjne a,0x20,0x0ee0    ; compare with previous P3 snapshot (IRAM 0x20)
0x0edd  7f 00      mov r7,#0x0           ; unchanged: return 0 (no events)
0x0edf  22         ret                   ;
0x0ee0  20 05 0a   jb 0x05,0x0eed        ; prev P3.5 (bit 5 of snapshot byte 0x20) was 1 -> not a rising edge
0x0ee3  ed         mov a,r5              ;
0x0ee4  30 e5 06   jnb 0xe5,0x0eed       ; current P3.5 = 0 -> no edge
0x0ee7  12 10 28   lcall 0x1028          ; P3.5 rising edge -> fcn_1028 (out of range; owned by next annotator)
0x0eea  43 06 01   orl 0x06,#0x1         ; set event flag bit0 (IRAM 0x06): settings changed
0x0eed  20 03 0a   jb 0x03,0x0efa        ; prev P3.3 was 1 -> skip
0x0ef0  ed         mov a,r5              ;
0x0ef1  30 e3 06   jnb 0xe3,0x0efa       ; current P3.3 = 0 -> skip
0x0ef4  12 0e 27   lcall 0x0e27          ; P3.3 rising edge -> button A: cycle 3-state selector
0x0ef7  43 06 01   orl 0x06,#0x1         ; set changed flag
0x0efa  20 04 0a   jb 0x04,0x0f07        ; prev P3.4 was 1 -> skip
0x0efd  ed         mov a,r5              ;
0x0efe  30 e4 06   jnb 0xe4,0x0f07       ; current P3.4 = 0 -> skip
0x0f01  12 0e 9d   lcall 0x0e9d          ; P3.4 rising edge -> button B: cycle 3-state selector
0x0f04  43 06 01   orl 0x06,#0x1         ; set changed flag
0x0f07  8d 20      mov 0x20,r5           ; store new P3 snapshot
0x0f09  af 06      mov r7,0x06           ; return event flags in R7
0x0f0b  22         ret                   ;
```

## fcn_0f0c = shiftreg8_commit_p1_7_6_5
Commits the 8-bit mux/control image IRAM 0x22 to a second shift register: data on P1.7,
clock on P1.5, latch on P1.6, MSB first. rev20_dynamic_reconfig.md calls this "mux commit
(74HC595)" — pin behavior verified here from the bytes; the 74HC595 part identity is from notes,
not verified. 8 call sites (main loop 0x0ae6, init 0x0943/0x0964, request handlers 0x03a2..0x04ff).

```
0x0f0c  7e 08      mov r6,#0x8           ; bit counter = 8
0x0f0e  ad 22      mov r5,0x22           ; R5 = mux image byte (IRAM 0x22)
0x0f10  53 90 bf   anl 0x90,#0xbf        ; P1.6 = 0 — drop latch before shifting
0x0f13  78 01      mov r0,#0x1           ; per-bit rotate seed (same idiom as 0x0e6b)
0x0f15  af 05      mov r7,0x05           ; R7 = bank-0 R5 = current shift byte
0x0f17  ef         mov a,r7              ;
0x0f18  08         inc r0                ; R0 = 2
0x0f19  80 01      sjmp 0x0f1c           ;
0x0f1b  23         rl a                  ; rotate left once (old bit7 -> bit0)
0x0f1c  d8 fd      djnz r0,0x0f1b        ; exactly one RL per bit
0x0f1e  fd         mov r5,a              ; store rotated byte back
0x0f1f  30 e0 05   jnb 0xe0,0x0f27       ; ACC.0 = bit shifted out (MSB first)
0x0f22  43 90 80   orl 0x90,#0x80        ; P1.7 = 1 (data high)
0x0f25  80 03      sjmp 0x0f2a           ;
0x0f27  53 90 7f   anl 0x90,#0x7f        ; P1.7 = 0 (data low)
0x0f2a  43 90 20   orl 0x90,#0x20        ; P1.5 = 1 (clock high)
0x0f2d  53 90 df   anl 0x90,#0xdf        ; P1.5 = 0 (clock low)
0x0f30  de e1      djnz r6,0x0f13        ; next bit (8 total)
0x0f32  30 1e 04   jnb 0x1e,0x0f39       ; bit 0x1e (byte0x23.6): choose ending mode
0x0f35  43 90 c0   orl 0x90,#0xc0        ; P1.7 = P1.6 = 1 and LEFT high (latch held asserted).
                                          ;   UNKNOWN — purpose of the held-high mode; 0x0941 uses it with image = 0x00 during init
0x0f38  22         ret                   ;
0x0f39  53 90 7f   anl 0x90,#0x7f        ; normal ending: P1.7 = 0 (data line low)
0x0f3c  43 90 40   orl 0x90,#0x40        ; P1.6 = 1 — latch pulse
0x0f3f  53 90 bf   anl 0x90,#0xbf        ; P1.6 = 0 — latch released
0x0f42  22         ret                   ;
```

## fcn_0f43 = usb_rstr_handler (USB bus reset)
No direct code XREF: reached through the USB interrupt dispatch table at 0x0c93 (2-byte
big-endian handler addresses indexed by interrupt source code; the ISR at 0x0dbb reads VECINT,
doubles it, and jumps via the Keil-style helper 0x0f96 with DPH taken from bank-2 R6 = the
table's high byte). Verified in the raw image: bytes 0F 43 at offset 0x0cc1 = entry 23 =
RSTR_INT 0x17 (Reg_stc1.h:258); the same table maps SETUP_INT 0x12 -> 0x0026, the known SETUP
dispatcher, confirming the indexing. Functionally this is Digidesign's version of TI
engUsbInit() (ROM/UsbEng.c:608-651): same IEPCNF0=OEPCNF0=0x84 and USBCTL=CONT|FEN, but it also
clears the audio endpoints' data-count registers and the function address, and masks a
different interrupt set (0x9F vs TI's 0xE5 — SOF+PSOF enabled instead of SUSR+RESR).

```
0x0f43  12 0b 82   lcall 0x0b82          ; -> fcn_0b82: OEPDCNTX0 (0xFFAB) = 0, IEPDCNTX0 (0xFF6B) = 0
                                          ;   (clear EP0 OUT/IN byte counts & NAK state); returns with A = 0
0x0f46  90 ff 9b   mov dptr,#0xff9b      ; DPTR = OEPDCNTX2 (Reg_stc1.h:205) — OUT EP2 = audio playback EP
0x0f49  f0         movx @dptr,a          ; OEPDCNTX2 = 0 — clear byte count / NAK
0x0f4a  90 ff 63   mov dptr,#0xff63      ; DPTR = IEPDCNTX1 (Reg_stc1.h:140) — IN EP1 = audio record EP
0x0f4d  f0         movx @dptr,a          ; IEPDCNTX1 = 0 — clear byte count / NAK
0x0f4e  90 ff ff   mov dptr,#0xffff      ; DPTR = USBFADR (Reg_stc1.h:103)
0x0f51  f0         movx @dptr,a          ; USBFADR = 0 — device address back to default per USB reset semantics
0x0f52  90 ff a8   mov dptr,#0xffa8      ; DPTR = OEPCNF0 (Reg_stc1.h:170)
0x0f55  74 84      mov a,#0x84           ; 0x84 = UBME(bit7) | USBIE(bit2): EP enabled, X-buffer, interrupt
                                          ;   enabled, no stall — exact value TI writes (UsbEng.c:614,626)
0x0f57  f0         movx @dptr,a          ; OEPCNF0 = 0x84
0x0f58  90 ff 68   mov dptr,#0xff68      ; DPTR = IEPCNF0 (Reg_stc1.h:109)
0x0f5b  f0         movx @dptr,a          ; IEPCNF0 = 0x84 (same A)
0x0f5c  90 ff fc   mov dptr,#0xfffc      ; DPTR = USBCTL (Reg_stc1.h:100)
0x0f5f  e0         movx a,@dptr          ; read-modify-write (preserve SDW_OK etc.)
0x0f60  44 c0      orl a,#0xc0           ; set CONT(bit7, PUR pullup connect) | FEN(bit6, function enable)
                                          ;   — datasheet 6.5.1.4; FEN is cleared by a USB reset so must be re-set here
0x0f62  f0         movx @dptr,a          ; USBCTL |= 0xC0
0x0f63  c2 0a      clr 0x0a              ; clear EP0-request state flag (byte0x21.2 — used at 0x01f1/0x02b0/0x033b...)
0x0f65  c2 0e      clr 0x0e              ; clear EP0-request state flag (byte0x21.6 — used at 0x0160/0x01f4...);
                                          ;   individual flag semantics owned by the 0x0100-0x0500 annotator
0x0f67  c2 08      clr 0x08              ; clear EP0-request state flag (byte0x21.0 — used at 0x0207/0x0341...)
0x0f69  c2 09      clr 0x09              ; clear EP0-request state flag (byte0x21.1 — used at 0x0219/0x0344...)
0x0f6b  a3         inc dptr              ; DPTR: 0xFFFC -> 0xFFFD = USBIMSK (Reg_stc1.h:101)
0x0f6c  74 9f      mov a,#0x9f           ; 0x9F = RSTR(7) | SOF(4) | PSOF(3) | SETUP(2) | rsvd(1) | STPOW(0)
                                          ;   (datasheet 6.5.1.3; TI engUsbInit uses 0xE5 = RSTR|SUSR|RESR|SETUP|STPOW)
0x0f6e  f0         movx @dptr,a          ; USBIMSK = 0x9F — enable reset/SOF/PSOF/SETUP/STPOW interrupts
0x0f6f  22         ret                   ; back to the USB ISR dispatch (which clears VECINT and RETIs)
```

Flow leaving this range: 0x1028 (P3.5 handler), 0x0b82 (EP0 count clear, verified above),
0x0f96 (jump helper that dispatches INTO 0x0f43) — all owned by other annotators.

### 5.11 Range 0x0F70-0x1027

# Rev 20 annotation — 0x0f70..0x1027

Context entering this range: 0x0f43-0x0f6f (previous annotator's range) is the
RSTR_INT bus-reset handler, entry 0x17 of the USB vector dispatch table at
0x0c93. That table (0x0c93-0x0cc2, 24 big-endian 16-bit entries indexed by
VECINT 0x00-0x17 per Reg_stc1.h:234-258) is what makes two functions in THIS
range reachable: index 0x08 (IEP0_INT) = 0x0fc4, and the trampoline 0x0f96 is
the LCALL target the ISR (0x0dac) uses to invoke every entry.

IRAM state used in this range (names assigned from cross-range usage):
- bit 0x0b (= IRAM 0x21.3) `ep0_in_more_data` — set at 0x0066/0x006e/0x0bd0/0x0be4
  when an EP0 IN transfer still has chunks to send; cleared when done/stalled.
- bit 0x0c (= IRAM 0x21.4) `ep0_in_final_phase` — set at 0x0115/0x024a/0x0b4d/0x0beb
  when the last IN packet (or ZLP/status packet) has been queued.
- byte 0x0d `pending_request` — pending deferred std request; 0x024d stores
  #0x05 (= USB_REQ_SET_ADDRESS, Usb.h:54) here.
- byte 0x0e `pending_dev_addr` — 0x0254 stores wValue-low (SETPACK[2] @0xFF2A) here.
  (Distinct from BIT 0x0e = IRAM 0x21.6, a streaming flag used at 0x033e etc.)
- bit 0x20 (= IRAM 0x24.0) `timer0_tick_flag` — set by the Timer-0 ISR.

## fcn_0f70 = switch_case_dispatch (compiler casejump helper)

Called with A = selector (from 0x011c: A = bRequest read from XDATA 0xFF29 =
SETPACK+1, Reg_stc1.h:17). The LCALL's return address is NOT returned to: it is
popped into DPTR and used as the base of an inline table of 3-byte entries
{addr_hi, addr_lo, key}. An entry whose first two bytes are both zero ends the
table; the two bytes AFTER that terminator are the big-endian default-handler
address. This is the standard Keil C51 case-jump idiom.

```
0x0f70  d0 83        pop dph               ; DPH = high byte of return addr = inline table base
0x0f72  d0 82        pop dpl               ; DPL = low byte; DPTR -> first {hi,lo,key} entry after the LCALL
0x0f74  f8           mov r0,a              ; R0 = selector (bRequest from caller 0x0118)
; -- entry-scan loop --
0x0f75  e4           clr a                 ; offset 0 into current entry
0x0f76  93           movc a,@a+dptr        ; A = entry byte0 (addr_hi)
0x0f77  70 12        jnz 0x0f8b            ; addr_hi != 0 -> real entry, go compare key
0x0f79  74 01        mov a,#0x1            ; offset 1
0x0f7b  93           movc a,@a+dptr        ; A = entry byte1 (addr_lo)
0x0f7c  70 0d        jnz 0x0f8b            ; addr_lo != 0 -> real entry, go compare key
0x0f7e  a3           inc dptr              ; both zero: terminator entry.
0x0f7f  a3           inc dptr              ; DPTR -> default-address bytes (terminator+2); A==0 here
; -- take jump: address at DPTR[0],DPTR[1] --
0x0f80  93           movc a,@a+dptr        ; A = addr_hi (A was 0 on both arrival paths)
0x0f81  f8           mov r0,a              ; stash addr_hi
0x0f82  74 01        mov a,#0x1            ; offset 1
0x0f84  93           movc a,@a+dptr        ; A = addr_lo
0x0f85  f5 82        mov dpl,a             ; DPL = addr_lo
0x0f87  88 83        mov dph,r0            ; DPH = addr_hi
0x0f89  e4           clr a                 ; A = 0
0x0f8a  73           jmp @a+dptr           ; jump to selected case handler (never returns here)
; -- key compare --
0x0f8b  74 02        mov a,#0x2            ; offset 2 = key byte      (from 0x0f77/0x0f7c)
0x0f8d  93           movc a,@a+dptr        ; A = entry key
0x0f8e  68           xrl a,r0              ; compare key with selector (A=0 iff equal)
0x0f8f  60 ef        jz 0x0f80             ; match -> jump via this entry's {hi,lo}
0x0f91  a3           inc dptr              ; no match:
0x0f92  a3           inc dptr              ;   advance DPTR by 3
0x0f93  a3           inc dptr              ;   to next entry
0x0f94  80 df        sjmp 0x0f75           ; scan next entry
```

The one table using this helper starts at 0x011f (outside this range; owned by
the dispatcher annotator). Note its first triplet is 02 2f 00, which Ghidra
force-decoded as `LJMP 0x2f00` — as table data it means {handler 0x022f,
key 0x00 = GET_STATUS}.

## fcn_0f96 = jmp_r2r1_trampoline

The USB INT0 ISR (0x0dac, vector 0x0003) runs with PSW=0x10 (register bank 2,
R0-R7 at IRAM 0x10-0x17). It fetches a big-endian handler address from the
table at 0x0c93 + 2*VECINT (hi byte -> R6, i.e. direct IRAM 0x16; lo byte -> R1)
and does `LCALL 0x0f96`; the handler's RET returns into the ISR epilogue at
0x0dd9.

```
0x0f96  8a 83        mov dph,r2            ; DPH = R2 (= handler addr hi; ISR did MOV R2,0x16 = bank-2 R6)
0x0f98  89 82        mov dpl,r1            ; DPL = R1 (= handler addr lo)
0x0f9a  e4           clr a                 ; A = 0
0x0f9b  73           jmp @a+dptr           ; jump to per-VECINT handler; its RET returns to ISR at 0x0dd9
```

## DATA 0x0f9c-0x0fc3 (40 bytes) — unreferenced stub sled, purpose UNKNOWN

Raw bytes (rev20_firmware_code.bin, file offset == address):
```
0f9c: 01 22 00  01 20 00  01 25 00  01 23 00  01 24 00  01 21 00
0fb4(cont): 01 09 00  01 0c 00  01 0b 00  01 0e 00  01 0a 00  01 0d 00  01 08
0fc2: 03 00
```
Structure: thirteen 3-byte entries, each `01 xx` followed by a 00 pad (the last
entry `01 08` is followed by the two bytes 03 00). If executed, each `01 xx` is
an AJMP into the 0x0800 page: targets 0x0822, 0x0820, 0x0825, 0x0823, 0x0824,
0x0821, 0x0809, 0x080c, 0x080b, 0x080e, 0x080a, 0x080d, 0x0808. Ghidra's sweep
force-decoded part of this region (misaligned, starting at 0x0fa9) which is why
0x080a/0x080b/0x080c/0x080e/0x0821 carry XREFs "from 0x0fab..0x0fc0".
Evidence this is dead data, not live code:
- No code reference to any address in 0x0f9c-0x0fc3 exists in the listing (the
  ISR dispatch jumps via the 0x0c93 table, whose 24 entries are all elsewhere).
- Several AJMP targets (0x0808, 0x0809, 0x0820, 0x0822, 0x0823, 0x0825) land
  mid-instruction in the current code at 0x0805-0x0827.
UNKNOWN — looks like a vestigial jump-stub sled from an earlier build/link
layout (stride and shape resemble an interrupt-stub table); resolving it would
need a build artifact or another firmware revision with the same block in a
consistent position.

> **Superseded — see §3.1 R1.** This block is the Keil C51 static-initializer
> record table read by `MOV DPTR,#0x0F9C` at 0x0A50 (a data reference Ghidra
> shows no XREF for). Its records parse exactly as IDATA initializations and it
> is terminated by the `00` at 0x0FC3.

## fcn_0fc4 = usb_iep0_done_handler (VECINT 0x08 = IEP0_INT)

Reached only via the vector dispatch table (entry at 0x0ca3 = 0f c4). Handles
"EP0 IN transaction complete": (1) more data -> send next chunk; (2) final
phase -> prep OUT status stage; (3) deferred SET_ADDRESS commit (TI
UsbEng.c:363-372, USB_WAIT_ADDR_ACK case) then re-arm EP0 for the next SETUP.

```
0x0fc4  30 0b 03     jnb 0x0b,0x0fca       ; ep0_in_more_data clear? skip. (bit 0x0b = IRAM 0x21.3)
0x0fc7  02 0b 77     ljmp 0x0b77           ; more data: -> fcn_0b77 = ep0_send_next_chunk (calls 0x0b8c to copy next
                                           ;   chunk into EP0 IN buf, then IEPDCNTX0 &= 0x7F releasing NACK); RETs to ISR
0x0fca  30 0c 0b     jnb 0x0c,0x0fd8       ; ep0_in_final_phase clear? -> check deferred request. (bit 0x0c = 0x21.4)
0x0fcd  c2 0c        clr 0x0c              ; consume final-phase flag
0x0fcf  90 ff a8     mov dptr,#0xffa8      ; DPTR = OEPCNF0 (Reg_stc1.h:170)
0x0fd2  e0           movx a,@dptr          ; read OEPCNF0
0x0fd3  44 20        orl a,#0x20           ; set bit5 TOGLE = expect DATA1 (TI hwMacro.h TOGGLEOutEp0Data)
0x0fd5  02 0b 2b     ljmp 0x0b2b           ; -> tail of fcn_0b1e: writes A to OEPCNF0, then IEPDCNTX0=0 (ZLP IN armed)
                                           ;   and OEPDCNTX0=0 (EP0 OUT armed) = arm status stage; RETs to ISR
0x0fd8  e5 0d        mov a,0x0d            ; A = pending_request (IRAM byte 0x0d; 0x024d wrote #5 there)
0x0fda  b4 05 09     cjne a,#0x5,0x0fe6    ; not SET_ADDRESS(5, Usb.h:54) pending? skip commit
0x0fdd  90 ff ff     mov dptr,#0xffff      ; DPTR = USBFADR (Reg_stc1.h:103)
0x0fe0  e5 0e        mov a,0x0e            ; A = pending_dev_addr (stored from wValue-low at 0x0254)
0x0fe2  f0           movx @dptr,a          ; USBFADR = new device address — deferred write AFTER the status-stage
                                           ;   IN completes, as USB 2.0 requires (cf. TI UsbEng.c:370)
0x0fe3  e4           clr a                 ; A = 0
0x0fe4  f5 0d        mov 0x0d,a            ; pending_request = 0 (consume)
0x0fe6  12 0b 1e     lcall 0x0b1e          ; fcn_0b1e = ep0_rearm_for_setup: IEPCNF0 &= 0xD7, OEPCNF0 &= 0xD7
                                           ;   (clear STALL bit3 + TOGLE bit5 both dirs), IEPDCNTX0=0, OEPDCNTX0=0
0x0fe9  22           ret                   ; back to ISR epilogue (0x0dd9: VECINT=0, EA=1, pops, RETI)
```

## fcn_0fea = ep0_arm_zlp_in_and_out

Callers: 0x0380, 0x03f7, 0x044e (class/vendor request completion paths).

```
0x0fea  90 ff 6b     mov dptr,#0xff6b      ; DPTR = IEPDCNTX0 (Reg_stc1.h:139)
0x0fed  e4           clr a                 ; A = 0
0x0fee  f0           movx @dptr,a          ; IEPDCNTX0 = 0: arm zero-length EP0 IN packet (hwMacro.h ZEROPACKInEp0)
0x0fef  90 ff ab     mov dptr,#0xffab      ; DPTR = OEPDCNTX0 (Reg_stc1.h:203)
0x0ff2  f0           movx @dptr,a          ; OEPDCNTX0 = 0: empty/arm EP0 OUT (hwMacro.h EMPTYOutEp0)
0x0ff3  22           ret                   ; done
```

## fcn_0ff4 = codec_port_cfg3_commit (shared tail; enter with DPTR/A set)

Entered with DPTR=0xFFDE (CPTCNF3) and A=config value: 0x034f passes A=0xAC,
0x035a passes A=0xA8 (choice depends on streaming flags 0x0a/0x0e — callers'
range). Writes the 4th codec-port config byte to both global and RX-side
registers, then enables the codec port.

```
0x0ff4  f0           movx @dptr,a          ; CPTCNF3 (0xFFDE, Reg_stc1.h:52) = A (0xAC or 0xA8 per caller)
0x0ff5  90 ff d5     mov dptr,#0xffd5      ; DPTR = CPTRXCNF3 (Reg_stc1.h:56)
0x0ff8  f0           movx @dptr,a          ; CPTRXCNF3 = same value (RX side mirrors global cfg byte 3)
0x0ff9  90 ff b1     mov dptr,#0xffb1      ; DPTR = GLOBCTL (Reg_stc1.h:22)
0x0ffc  e0           movx a,@dptr          ; read GLOBCTL
0x0ffd  44 01        orl a,#0x1            ; set bit0 CPTEN (datasheet GLOBCTL table: bit0 = CPTEN, codec port enable)
0x0fff  f0           movx @dptr,a          ; GLOBCTL |= CPTEN: enable codec port interface
0x1000  22           ret                   ; done
```

## fcn_1001 = dma0_disable

Callers: 0x032a, 0x03ee, 0x044b (stream stop/reconfigure paths; 0x032a then
also clears DMACTL1 bit7 and GLOBCTL.CPTEN itself).

```
0x1001  90 ff e8     mov dptr,#0xffe8      ; DPTR = DMACTL0 (Reg_stc1.h:76)
0x1004  e0           movx a,@dptr          ; read DMACTL0
0x1005  54 7f        anl a,#0x7f           ; clear bit7 DMAEN (datasheet DMACTL reg table @FFE8h: bit7 = DMAEN)
0x1007  f0           movx @dptr,a          ; DMACTL0: DMA channel 0 disabled
0x1008  22           ret                   ; done
```

## fcn_1009 = ep0_stall_both

Standard-request error exit (8 callers in the SETUP dispatcher: 0x0092, 0x010a,
0x015a, 0x01e8, 0x022c, 0x0264, 0x0299, 0x02ea). Equivalent of TI's
STALLInEp0 + STALLOutEp0 (hwMacro.h:9-10).

```
0x1009  90 ff 68     mov dptr,#0xff68      ; DPTR = IEPCNF0 (Reg_stc1.h:109)
0x100c  e0           movx a,@dptr          ; read IEPCNF0
0x100d  44 08        orl a,#0x8            ; set bit3 STALL (hwMacro.h STALLInEp0 = IEPCNF0 |= 0x08)
0x100f  f0           movx @dptr,a          ; IEPCNF0: EP0 IN stalled
0x1010  90 ff a8     mov dptr,#0xffa8      ; DPTR = OEPCNF0 (Reg_stc1.h:170)
0x1013  e0           movx a,@dptr          ; read OEPCNF0
0x1014  44 08        orl a,#0x8            ; set bit3 STALL (STALLOutEp0)
0x1016  12 0b 2b     lcall 0x0b2b          ; -> tail of fcn_0b1e: writes A to OEPCNF0 (stall committed), then
                                           ;   IEPDCNTX0 = 0 and OEPDCNTX0 = 0 (reset both EP0 byte counts)
0x1019  c2 0b        clr 0x0b              ; ep0_in_more_data = 0 (abort any in-flight IN transfer state)
0x101b  c2 0c        clr 0x0c              ; ep0_in_final_phase = 0
0x101d  22           ret                   ; done
```

## fcn_101e = timer0_isr_tick (vector 0x000B: `LJMP 0x101e`)

```
0x101e  c2 af        clr 0xaf              ; EA = 0 (IE.7): global interrupt disable around flag+reload
0x1020  d2 20        setb 0x20             ; set tick flag, IRAM bit 0x20 = byte 0x24 bit0 (polled by main loop)
0x1022  75 8c ce     mov 0x8c,#0xce        ; TH0 (core SFR 0x8C) = 0xCE: reload timer-0 high byte
                                           ;   (system tick per ANNOTATION_BRIEF known facts: TH0 reload 0xCE)
0x1025  d2 af        setb 0xaf             ; EA = 1: re-enable interrupts
0x1027  32           reti                  ; return from interrupt
```

Range ends here; 0x1028 (flag-toggle helper called from 0x0ee7) belongs to the
next annotator.

### 5.12 Range 0x1028-0x13FF

# Rev 20 annotation — range 0x1028–0x1400

## Function table

| addr | name | purpose |
|---|---|---|
| 0x1028 | `toggle_flag_bit1e` | Toggle IRAM bit 0x1E (byte 0x23 bit 6); called on P3.5 rising edge from `FUN_CODE_0ed5` (0x0EE7); flag consumed by P1 output routine at 0x0F32 (sets vs pulses P1.6/P1.7). |

## fcn_1028 = toggle_flag_bit1e

Sole caller is `0x0EE7` (verified XREF in Ghidra listing). The caller (`FUN_CODE_0ed5`)
reads P3 into R5, compares against the previous-P3 snapshot kept in IRAM byte 0x20,
and LCALLs here only when P3.5 transitions 0→1 (JB 0x05 = old snapshot bit 5 must be
clear at 0x0EE0; JNB 0xE5 = current ACC.5 must be set at 0x0EE4). Immediately after
the call the caller does `ORL 0x06,#0x01` (sets a "state changed, refresh outputs"
flag byte). This function itself is a pure toggle of bit-addressable bit 0x1E
(= IRAM byte 0x23, bit 6). Elsewhere: 0x0941 SETB 0x1e (init path), 0x039E/0x053E/0x0962
CLR 0x1e, and the consumer 0x0F32 `JNB 0x1e` which selects between `ORL P1,#0xC0`
(P1.6+P1.7 high) and a P1.7-low / P1.6-pulse sequence. Net effect: a front-panel
switch on P3.5 toggles a latched output state driven onto P1.7. The exact hardware
function of P1.7 (NOTES.md variously and unverifiably guesses input-mux swap or
48V phantom) is not determinable from this range.

```
0x1028  30 1e 03     jnb 0x1e,0x102e       ; if flag bit 0x1E (IRAM 0x23.6) is clear, jump to the SETB branch
0x102b  c2 1e        clr 0x1e              ; flag was set -> clear it (toggle: 1 -> 0)
0x102d  22           ret                   ; return with flag now 0
; LAB_102e (reached when flag was clear):
0x102e  d2 1e        setb 0x1e             ; flag was clear -> set it (toggle: 0 -> 1)
0x1030  22           ret                   ; return with flag now 1
```

## 0x1031–0x103e: 0x22 fill (disassembles as 14 unreferenced RETs)

No XREF anywhere in the listing targets any address in 0x1031–0x103E (verified: the
only labels/XREFs in this area are 0x1028 and 0x102E). These are fill bytes of value
0x22 after the last real function; they happen to decode as harmless `RET`
instructions. Bytes verified against rev20_firmware_code.bin offset 0x1031:
`22 22 22 22 22 22 22 22 22 22 22 22 22 22`.

```
0x1031  22           ret                   ; 0x22 fill byte, unreferenced (padding after last function)
0x1032  22           ret                   ; 0x22 fill, unreferenced
0x1033  22           ret                   ; 0x22 fill, unreferenced
0x1034  22           ret                   ; 0x22 fill, unreferenced
0x1035  22           ret                   ; 0x22 fill, unreferenced
0x1036  22           ret                   ; 0x22 fill, unreferenced
0x1037  22           ret                   ; 0x22 fill, unreferenced
0x1038  22           ret                   ; 0x22 fill, unreferenced
0x1039  22           ret                   ; 0x22 fill, unreferenced
0x103a  22           ret                   ; 0x22 fill, unreferenced
0x103b  22           ret                   ; 0x22 fill, unreferenced
0x103c  22           ret                   ; 0x22 fill, unreferenced
0x103d  22           ret                   ; 0x22 fill, unreferenced
0x103e  22           ret                   ; 0x22 fill, unreferenced (last non-0xFF byte of the image region)
```

> **Note (assembly):** these fourteen bytes are *not* unreferenced — the VECINT
> dispatch table at 0x0C93 (decoded in §5.9) points its unused interrupt sources at
> 0x1031-0x103E precisely so that those vectors execute a `RET`. The bytes are still
> 0x22 fill in the sense that they carry no other logic, but they are reached at
> runtime whenever one of those masked/unused USB interrupt sources fires. No XREF
> appears in the listing because the table is data.

## 0x103f–0x13ff: erased-EEPROM padding (0xFF x 961)

Verified byte-for-byte against rev20_firmware_code.bin: every byte in
0x103F..0x13FF is 0xFF (961 bytes; the 0xFF run actually continues beyond this
range's upper bound — Ghidra shows the GAP extending to the end of the code
region owned by other annotators).  Unprogrammed/erased filler; not code, not
referenced by anything.

## Notes on IRAM state touched in this range

- **bit 0x1E** (byte 0x23, bit 6): latched toggle state for the P3.5 front-panel
  switch; drives P1.7 level selection in the routine ending at 0x0F42.
  Cleared during init/reconfig paths (0x039E, 0x053E, 0x0962), set at 0x0941.
- No SFR (XDATA 0xFF00–0xFFFF) access occurs anywhere in this range.

---

## 6. Data section decodes

Every non-code byte in the image, in address order. Full field-by-field decodes of the
descriptor block live in §5.4 and of the VECINT table in §5.9; this section is the index and
carries the parts not fully tabulated there.

### 6.1 0x011F-0x0143 — bRequest dispatch table (37 bytes)

Consumed by `switch_case_dispatch` (0x0F70) as {addr_hi, addr_lo, key} triplets. Eleven
entries for the USB 1.1 standard-request set {0,1,3,5,6,7,8,9,0x0A,0x0B,0x0C}, a `00 00`
terminator at 0x0140 and the big-endian default handler address `02 EA` at 0x0142. Full
entry list in §5.2. The leading bytes `02 2F 00` are data, not `LJMP 0x2F00`: **this
firmware never delegates any request to the boot ROM.**

### 6.2 0x0300-0x0329 — event trampoline table (42 bytes, executed)

Fourteen 3-byte `LJMP` instructions reached by `JMP @A+DPTR` with A = 3*(event−1). Targets:
0x032A, 0x0386, 0x03FD, 0x0454, 0x0466, 0x0478, 0x0480, 0x049A, 0x04B4, 0x04BC, 0x04C4,
0x0511, 0x0518, 0x0526. Executed code rather than pure data, listed here because it is a
table by construction.

### 6.3 0x0596-0x0727 — USB descriptor block (402 bytes)

| Offset | Length | Descriptor | Returned to host? |
|---|---|---|---|
| 0x0596 | 18 | DEVICE — USB 1.10, EP0 8 bytes, VID 0x0DBA, PID 0x1000, bcdDevice 0x0020, iMfr 1, iProduct 2, 1 configuration | yes (type 1) |
| 0x05A8 | 200 | CONFIGURATION set, USB **Audio Class**: 3 interfaces, AudioControl + two AudioStreaming interfaces, terminals 1/2/3/4/6 + selector unit 5, 24-bit stereo at 44100/48000, EP 0x81 IN and 0x02 OUT, both iso/async, wMaxPacketSize 304 | **no — unreachable dead data** |
| 0x0670 | 54 | CONFIGURATION set, **vendor-specific** (bInterfaceClass 0xFF): 2 interfaces, interface 1 alt 1 carrying EP 0x81 IN and EP 0x02 OUT, iso/async, wMaxPacketSize 304, bus-powered, 480 mA | yes (type 2, index 0) |
| 0x06A6 | 4 | STRING 0 — LANGID 0x0409 (en-US) | yes |
| 0x06AA | 30 | STRING 1 — "Digidesign Inc" (iManufacturer) | yes |
| 0x06C8 | 96 | STRING 2 — "Mbox USB Audio Device copyright Digidesign 2001" (iProduct) | yes |

Two consequences worth carrying forward: (a) Rev 20 enumerates as a **vendor-class** device,
so a stock host audio-class driver will not bind to it — the 200-byte UAC set is present in
the image but no code path can reach it; (b) the two `AS_GENERAL` descriptors inside that
unused UAC set carry `bTerminalLink` values 0x09 and 0x08, which match no terminal ID in
their own AudioControl set (IDs 1,2,3,4,5,6).

### 6.4 0x0A48-0x0A4F — bit-mask table (8 bytes)

`01 02 04 08 10 20 40 80` — read by `MOVC A,@A+PC` at 0x0A3B in the C51 initializer's
bit-record executor; index = bit position 0..7.

### 6.5 0x0C93-0x0CDC — VECINT dispatch table (74 bytes)

37 big-endian 16-bit handler addresses, indexed by `VECINT` (TI source codes 0x00-0x24).
Full entry-by-entry decode in §5.9. Live handlers: 0x00→0x0D25, 0x08→0x0FC4, 0x12→0x0026,
0x16→0x0006, 0x17→0x0F43; the remaining 32 entries point at one-byte `RET`s (0x0010-0x0022
in the vector area and 0x1031-0x103E in the tail fill).

### 6.6 0x0F9C-0x0FC3 — C51 static-initializer record table (40 bytes)

Read by `MOV DPTR,#0x0F9C` at 0x0A50 and interpreted by the record walker at 0x0A18-0x0A93.
Thirteen type-00 (IDATA) records of length 1, each `01 <dest> <value>`, then the `00`
terminator:

| Bytes | Effect |
|---|---|
| `01 22 00` | IRAM 0x22 = 0x00 (8-bit mux shift image) |
| `01 20 00` | IRAM 0x20 = 0x00 (P3 snapshot) |
| `01 25 00` | IRAM 0x25 = 0x00 (latch chain byte B) |
| `01 23 00` | IRAM 0x23 = 0x00 (latch chain byte A) |
| `01 24 00` | IRAM 0x24 = 0x00 (tick/misc flag byte) |
| `01 21 00` | IRAM 0x21 = 0x00 (EP0/USB flag byte) |
| `01 09 00` | IRAM 0x09 = 0x00 (EP0 IN length low) |
| `01 0C 00` | IRAM 0x0C = 0x00 |
| `01 0B 00` | IRAM 0x0B = 0x00 (EP0 IN length high) |
| `01 0E 00` | IRAM 0x0E = 0x00 (pending device address) |
| `01 0A 00` | IRAM 0x0A = 0x00 (pending event code) |
| `01 0D 00` | IRAM 0x0D = 0x00 (pending deferred request) |
| `01 08 03` | IRAM 0x08 = 0x03 (clock-mode state = 48 kHz family) |
| `00` @0x0FC3 | terminator → `JZ 0x0A15` → `LJMP 0x0A95` (main) |

(See §3.1 R1: the 0x0F70 annotator read this block as an unreferenced AJMP sled. The record
parse above, the `MOV DPTR,#0x0F9C` at 0x0A50, and the agreement of `01 08 03` with
`hw_master_init` writing IRAM 0x08 = 3 settle it.)

### 6.7 0x1031-0x103E and 0x103F-0x1FED — fills

0x1031-0x103E: fourteen 0x22 (`RET`) bytes, used as the landing pads for all unused VECINT
sources (§6.5). 0x103F to end-of-image: 0xFF erase fill, verified over the whole run.

### 6.8 Strings in the image

Only three, all UTF-16LE inside string descriptors: "Digidesign Inc" (0x06AC),
"Mbox USB Audio Device copyright Digidesign 2001" (0x06CA), plus the LANGID array at 0x06A6.
There are no ASCII strings, version banners or debug text anywhere else in the image.

---

## 7. Open questions

Everything the twelve annotators marked UNKNOWN, collected and de-duplicated, with what
would resolve each. Items resolved during merge (bit 0x0D, the 0x0F9C table, the EP0 buffer
direction, bit 0x0A's setter) are **not** listed here — see §3.1.

### 7.1 Board-level wiring (the largest cluster)

The firmware drives three off-chip channels whose loads are not identifiable from code:
the 16-bit latch chain (IRAM 0x23:0x25 on P1.0/P1.2/P1.1), the 8-bit mux chain (IRAM 0x22 on
P1.7/P1.5/P1.6), and the 3-byte serial control channel (P1.4/P1.3, chip-select via latch bit
0x2F). Individually unresolved:

| Signal | What the firmware does with it | What would resolve it |
|---|---|---|
| Latch bits 0x18/0x19 (0x23.0/.1) | Set only in codec-port mode 5 | Mbox schematic / board tracing |
| Latch bits 0x1A/0x1B (0x23.2/.3) | Cleared before a clock change, re-asserted after; also asserted by `fcn_080b` | Schematic; the pattern is consistent with mute/relay but is not proof |
| Latch bit 0x1C (0x23.4) | Set once at 0x0840 during path reconfig | Schematic |
| Latch bit 0x1E (0x23.6) | Toggled by the P3.5 front-panel switch; selects the P1.7-held-high ending in `fcn_0f0c` | Schematic + a hardware test (toggle the switch, observe P1.7 and the audio path). `NOTES.md` guesses input-mux swap *or* 48 V phantom — the two guesses contradict each other and neither is verified |
| Mux bits 0x10-0x12 and 0x13-0x15 (0x22) | Two independent 3-position one-cold selectors advanced by P3.3 / P3.4 | Schematic; front-panel behaviour test (press each button, observe which input/monitor path changes) |
| Mux bit 0x16 (0x22.6) | Set only when bits 0x2C and 0x2D are both clear; also the EEPROM-self-test pass indicator | Schematic |
| Mux bit 0x17 (0x22.7) | Cleared on stream start, set on stream stop | Schematic |
| P3.1 | Latched level edge-detected in `main`, posts events 0x0B (EEPROM self-test) and 0x0C (codec mode 1) | Schematic; likely a jumper/button since it gates a diagnostic |
| P3.3 / P3.4 / P3.5 | Three front-panel inputs, rising-edge dispatched | Schematic / panel mapping |
| MCLKI pin (clock mode 1) | Both MCLKO outputs sourced from the external MCLKI input | Schematic — the project context assumes it carries the S/PDIF-recovered clock, which is an inference, not a verified fact |

### 7.2 The external serial-control chip

`fcn_0c45` writes {0x20, register, value} triplets. Registers written anywhere in the image:
0x01=0x01, 0x02=0x20, 0x03=0x0C, 0x04=0x00/0x40/0x41, 0x05=0x05, 0x06=0x05, 0x11=0xFF,
0x12=0x00, 0x13=0x10, 0x23=0x00/0x40, 0x24=0x80. The leading 0x20 matches a CS8427
chip-address/write byte and `NOTES.md` names the CS8427, but no CS8427 datasheet exists in
this repo, so **no register or bit meaning is asserted anywhere in this document**.
*Resolution:* obtain the CS8427 (or candidate part) datasheet and re-check the register map
against this write list; the 0x04 = 0x40-vs-0x41 split between internal- and external-clock
modes is the sharpest available test.

### 7.3 Unexplained IRAM cells

| Cell | Observation | Resolution path |
|---|---|---|
| byte 0x0C | Written 0xFE once, at 0x0A03; no reader found by any annotator | Whole-image search for reads of direct 0x0C, or a build artifact / other revision showing the variable's use |
| byte 0x2A | Written 0x00 once at 0x0A9E; no other reference | Same |
| byte 0x2B | Written 0x10 once at 0x0AA1; no other reference | Same. The value 0x10 hints at a count or mask that a later revision consumes — comparing with Rev 22 would test that |
| bit 0x22 (0x24.2) | Cleared once at 0x0AAB; no other reference | Same |
| bit 0x2D (0x25.5) | Set only by the EEPROM self-test (cmd 11), cleared on stream start, consumed only to force mux bit 0x16 low | Schematic (what bit 0x16 drives) would give it meaning: it looks like "diagnostic mode engaged, suppress the normal panel indication" |
| bit 0x2E (0x25.6) | Doubles as the "hardware init done" flag and as a latch output (§3.1 R6) | Schematic, to learn what the latch output does while init is considered done |
| R3 = 0xFF at 0x0DD4 in the USB ISR | Loaded before the dispatch trampoline, which never reads R3 | Compiler calling-convention artifact is the working explanation; a matching SDCC/Keil code-generation sample would confirm |

### 7.4 Behavioural questions in the USB layer

1. **GET_INTERFACE replies 2 for interface 2** (0x021F) while SET_INTERFACE only accepts alt
   0 or 1 (0x02AE). In the configuration set actually returned, interface 2 does not exist at
   all. *Resolution:* bus capture of a real host talking to a stock Mbox, or acceptance that
   the branch is unreachable in the returned configuration (it is reachable only if a host
   sends GET_INTERFACE for interface 2 anyway, which the code answers rather than stalls).
2. **String index ≥ 3 replies with a stale pointer** (0x01DE) instead of stalling — the
   firmware serves whatever descriptor the previous request selected. *Resolution:* none
   needed for understanding; noted as a real quirk of the implementation.
3. **CLEAR_FEATURE only accepts wIndex 0** (0x0150), so `CLEAR_FEATURE(ENDPOINT_HALT)` on the
   iso endpoints stalls. *Resolution:* as above — behaviour is established, its intent is not.
4. **`ep0_clamp_len_to_wlength` corner case** (0x0D9D): when length < wLength but
   low(length) > low(wLength) (e.g. 0x00FF vs 0x0100), the short-transfer flag is left clear.
   *Resolution:* decide whether any real request can hit it — with a maximum response of 200
   bytes and hosts asking for ≤ 0xFFFF, it needs wLength ≥ 0x0100 with a smaller low byte,
   which GET_DESCRIPTOR(config, wLength=0x0100) would produce. A bus test would settle
   whether the resulting transfer terminates correctly.
5. **Event/command 6** (posted when the SET_CUR sample-rate payload's first byte is 0x00,
   0x0D42) sets codec-port mode 1 = external MCLKI clock. Whether hosts actually send a
   zero sampling frequency, and what the device is expected to do, is unverified.
6. **`USBIMSK = 0xFF` at 0x03F6** (cmd 2) unmasks SUSR and RESR, which every other site
   masks (0x9F). So suspend/resume interrupts are only live after interface 1's alt setting
   has been applied at least once. Whether that is deliberate is unknown; it is at least
   consistent with "only sleep once the host has started streaming".

### 7.5 Semantics owned by no range

- **Codec-port "mode" numbering** (IRAM 0x08 ∈ {1,2,3,5}, with 4 accepted by callers but
  having no branch in `fcn_0728`): mode 4 falls through the dispatch chain and performs only
  the common tail (serial write, MCLK enable, EP re-arm, settle) with no clock change. Why
  cmd 9 posts mode 4 at all is unknown. *Resolution:* Rev 22 comparison, or host-side tool
  captures showing which command the Digidesign software issues.
- **Event 0x0D (boot-EEPROM invalidate)**: the *effect* is certain (write 0x00 over EEPROM
  offset 0), the *intent* — force the next boot into the boot-ROM DFU path — is inferred from
  the target address, not from any TI source. *Resolution:* boot-ROM behaviour on a zeroed
  signature byte, which the boot-ROM audit notes in this repo could confirm.
