# Digidesign Mbox 1 — Firmware Rev 22 — Master Annotated Disassembly

## 1. Header

**What this file is.** The consolidated, address-ordered annotated disassembly of the
Digidesign Mbox 1 USB-audio-interface application firmware, revision 22, running on a
Texas Instruments TAS1020B (8051-core USB streaming controller). It merges twelve
independently-annotated address ranges into one reference artifact: memory map, IRAM
state table, function index, execution narrative, the full instruction-by-instruction
listing, decoded data sections, and collected open questions.

**Source image.**
`/Users/seth/projects/mbox/.claude/worktrees/rev20-re-notes/firmware_stock/rev22_firmware_code.bin`
- Size: 8174 bytes (0x1FEE).
- Reset vector at file offset 0: `02 09 2a` = `LJMP 0x092a` (verified).
- Load address: **0x0000** — CPU code address equals file offset throughout (every
  annotator verified descriptor/table decodes with `xxd` at file-offset == CPU-address).
- Real code + data occupy 0x0000–0x1035. From **0x1036 to EOF (0x1FED)** every byte is
  0xFF (erased/unprogrammed fill — byte-verified: `set(bytes[0x1036:]) == {0xFF}`).

**How it was produced.** A Ghidra sweep of the image (`rev22_ghidra.txt`) was manually
corrected range-by-range against the raw bytes. Ghidra force-disassembled several data
regions (the USB descriptor block, the VECINT dispatch table, the Keil init table, the
bit-mask table, and the 0xFF fill) as bogus instructions; those were re-identified as
data and decoded by hand. SFR names come from
`reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h`; hardware-macro names from
`ROM/hwMacro.h`; behavioral cross-checks against the TI USB Audio Class reference
(`ROM/Usb.c`, `ROM/UsbEng.c`, `ROM/I2c.c`, `ROM/eeprom.h`, `ROM/i2c.h`) and the TAS1020B
datasheet (SLES025B). Class-request semantics were cross-checked against the
hardware-tested Linux driver (`snd_mbox1_set_clk_source` / `snd_mbox1_set_input_source`).

**Coverage stats.**
- 12 annotated ranges span **0x0000–0x1400** contiguously (no gaps, no overlaps except
  the shared range boundaries, which are handled by "next annotator" hand-offs).
- Code + data fully annotated: **0x0000–0x1035** (4150 bytes).
- 0x1036–0x13FF: 0xFF fill, annotated by range 12. 0x1400–0x1FED: 0xFF fill, beyond the
  last annotated range but byte-verified all-0xFF here.
- Functions identified: **~40** (see §3). Every executed instruction in 0x0000–0x1035 is
  reproduced in §5; nothing was summarized away.

---

## 2. Memory Map

### 2.1 Code layout by subsystem

| Range | Subsystem |
|---|---|
| 0x0000–0x0025 | 8051 hardware interrupt vectors + USB endpoint no-op RET stubs + RETI stubs |
| 0x0026–0x010a | `usb_setup_handler` (SETUP_INT): EP0 prologue + audio-class request dispatch |
| 0x010b–0x02f2 | Standard (Chapter-9) USB request dispatcher + handlers (GET/SET_*) |
| 0x02f3–0x0566 | `usb_deferred_action_dispatch`: main-loop side of control requests (config, iface, input-select, clock modes, CS8427/EEPROM probes, DFU trigger, suspend) |
| 0x0567–0x057c | CS8427 register-write helpers (`fcn_0567`, `fcn_0575`) |
| 0x057d–0x070e | **DATA**: USB descriptor block (device, 2 config sets, 3 strings) |
| 0x070f–0x07eb | `audio_clock_set_mode` (`fcn_070f`): ACG sample-clock / rate switch |
| 0x07ec–0x0890 | `hw_clock_codec_init`: MCU/codec-port/ACG cold init |
| 0x0891–0x0929 | `usb_ep_dma_init`: EP0 + iso EP1/EP2 + DMA channel setup |
| 0x092a–0x0938 | `keil_c51_startup` (?C_STARTUP) |
| 0x0939–0x09b5 | `keil_c_init_interpreter` (?C_INIT) + bit-mask table 0x0969 |
| 0x09b6–0x0a3e | `audio_hw_bringup` (`fcn_09b6`): ACG + external-latch + codec register init |
| 0x0a3f–0x0aba | `main_loop` |
| 0x0abb–0x0b1e | `ep0_in_send_chunk` (`fcn_0abb`): EP0 IN data-stage copy engine |
| 0x0b1f–0x0c30 | 13 EP0 / I2C / divide / dispatch leaf helpers |
| 0x0c31–0x0c7c | `fcn_0c31`: 3-wire (SPI-style) CS8427 register write |
| 0x0c7d–0x0cc6 | **DATA**: VECINT interrupt dispatch table (37 big-endian entries) |
| 0x0cc7–0x0d10 | `oep0_int_handler` (OEP0_INT): EP0 OUT data-stage for class requests |
| 0x0d11–0x0d57 | `i2c_eeprom_read_byte` |
| 0x0d58–0x0d9d | `sof_int_handler` (SOF_INT): audio-OUT DMA 6-byte frame alignment check |
| 0x0d9e–0x0dde | `ep0_clamp_len_to_wlength` |
| 0x0ddf–0x0e1a | `usb_isr_int0_vecdispatch` (INT0 = USB ISR) |
| 0x0e1b–0x0e55 | `panel_state_cycle_A` |
| 0x0e56–0x0e8e | `shiftreg_out16_p1` (`fcn_0e56`) |
| 0x0e8f–0x0ec6 | `panel_state_cycle_B` |
| 0x0ec7–0x0ef2 | ACG synthesizer programming chain (`fcn_0ec7`/`fcn_0ec8`/`fcn_0ee8`) |
| 0x0ef3–0x0efb | `acg_dividers_div2` (`fcn_0ef3`/`fcn_0ef4`) |
| 0x0efc–0x0f30 | `shiftreg_out8_p1hi` (`fcn_0efc`) |
| 0x0f31–0x0f63 | `p3_edge_poll_dispatch` |
| 0x0f64–0x0f90 | `usb_rstr_handler` (RSTR_INT) |
| 0x0f91–0x0fb9 | `ep0_in_done_handler` (IEP0_INT) |
| 0x0fba–0x0fe1 | **DATA**: Keil ?C_INIT table |
| 0x0fe2–0x1000 | `cport_cnf3_write_enable`, `dma0_disable`, `stage_ctrl_pair_12_00` |
| 0x1001–0x1015 | `ep0_stall_both` (`fcn_1001`) |
| 0x1016–0x101f | `timer0_tick_isr` (TF0 ISR) |
| 0x1020–0x1028 | `toggle_bit1E_state` (`fcn_1020`) |
| 0x1029–0x1035 | 13× 0x22 (RET) padding, unreachable |
| 0x1036–0x1FED | 0xFF erased fill |

### 2.2 IRAM state-variable table (merged across all ranges)

Legend: **byte** = direct-addressed data byte; **bit 0xNN** = bit-addressable bit (8051
mapping: bit `0xNN` lives in byte `0x20 + (0xNN>>3)`, position `0xNN & 7`). Where a byte
address and a bit address collide numerically (e.g. byte 0x2c vs bit 0x2c) they are
**different objects** — bit 0x2c is byte 0x25 bit 4; byte 0x2c is direct RAM 0x2c.

| Loc | Name | Meaning / evidence |
|---|---|---|
| byte 0x08 | `clock_mode_id` | Current audio clock/rate mode: 1/2/3/5. Written by `audio_clock_set_mode` (0x073a/0x0769/0x0772/0x079d), `hw_clock_codec_init` (0x085c=3), `audio_hw_bringup` (0x09ca=3); read by GET-sampling-freq (0x0094: 1→00 00 00, 2→44 AC 00, 3→80 BB 00). Init value 3 (?C_INIT). Also reloaded into R7 as the mode arg (case 4, 0x0464). |
| byte 0x09 | `xfer_len_lo` | EP0 IN remaining-length low byte (descriptor send). Loaded 0x01a0/0x01dc; clamped by `fcn_0d9e`; decremented by `fcn_0abb`. Zeroed at SETUP (0x003b) and ?C_INIT. |
| byte 0x0a | `pending_action` | Main-loop deferred-action code 1..14, dispatched by `usb_deferred_action_dispatch` (jump table 0x030c). Writers: SET_CONFIG=1, iface1=2, iface2=3, input analog=4, S/PDIF=5, clock modes 6/7/8, P3.1 events 0x0b/0x0c, suspend 0x0e. Cleared at 0x0563 and ?C_INIT. |
| byte 0x0b | `xfer_len_hi` | EP0 IN remaining-length high byte. **Distinct from bit 0x0b.** Loaded 0x01a5/0x01df; clamp/decrement as 0x09. Zeroed at SETUP (0x003d) and ?C_INIT. |
| byte 0x0c | (scratch/state) | Zeroed at ?C_INIT; used in 0x00b3–0x00d8 sampling-freq path; init 0. Exact standalone role not otherwise pinned. |
| byte 0x0d | `ctl_state` / `pending_cmd` | Control-transfer pending-command code: 1 = SET clock/sample-rate (0x0066), 2 = SET input source (0x0061), 5 = SET_ADDRESS pending (0x024d). Consumed by `oep0_int_handler` (states 1/2) and `ep0_in_done_handler` (state 5 → write USBFADR). Zeroed at ?C_INIT. |
| byte 0x0e | `pending_addr` | Saved wValueL of SET_ADDRESS (written 0x0254); written to USBFADR at 0x0fad. Init 0. |
| byte 0x18 | `pkt_bytecount` | Bytes copied into current EP0 IN packet (0..8); `fcn_0abb` (0x0abc/0x0af0/0x0af4), OR'd into IEPDCNTX0 at 0x0afe. |
| bytes 0x19:0x1a | `desc_ptr` hi:lo | CODE-space source pointer (descriptor being sent). Set by GET_DESCRIPTOR (0x017e etc.); DPTR-loaded by `fcn_0b6e`; incremented by `fcn_0abb`. |
| bytes 0x1b:0x1c | `dma_bcnt_prev` hi:lo | Previous-SOF snapshot of DMABCNT0 (`sof_int_handler`, 0x0d73/0x0d75, compared 0x0d6a/0x0d6f). |
| bytes 0x1d:0x1e | `ep0_cursor` hi:lo | Write/read cursor into the EP0 packet buffer. Set to 0xFA10 by `fcn_0b1f`, 0xFA18 by `fcn_0b37`; DPTR-loaded by `fcn_0b25`; DPH source for `fcn_0b5b`. (See §2.5 reconciliation for which is IN vs OUT.) |
| byte 0x20 | `p3_shadow` | Last-sampled P3 GPIO value (`fcn_0f31`, stored 0x0f61). Its bits alias bit-addresses 0x00–0x07: bit 0x01=P3.1 shadow, 0x03=P3.3, 0x04=P3.4, 0x05=P3.5. Init 0. |
| bit 0x01 (0x20.1) | `p3_1_shadow` | P3.1 shadow bit; main loop edge-detects it → events 0x0b (low) / 0x0c (high). Signal identity UNKNOWN. |
| bits 0x03/0x04/0x05 (0x20.3/.4/.5) | prev P3.3/P3.4/P3.5 | Previous levels for rising-edge detection in `fcn_0f31`. |
| byte 0x21 | `usb_flags` | USB state-flag byte; bit-addresses 0x08–0x0f live here. Init 0. |
| bit 0x08 (0x21.0) | `if1_alt` | Interface-1 alternate setting != 0. Set 0x02c1, tested 0x0203; cleared at RSTR/init. |
| bit 0x09 (0x21.1) | `if2_alt` | Interface-2 alternate setting != 0. Set 0x02d5, tested 0x0215; cleared at RSTR/init. |
| bit 0x0a (0x21.2) | dead flag | Tested as an OR-alternative to `configured` in GET/SET_INTERFACE (0x01ed/0x02ae) and in `usb_deferred_action_dispatch` (config-1-active flag at 0x0347 etc.). **Two readings collide — see §2.5 reconciliation.** Never `setb`-set in the Ch-9 paths; cleared at 0x026b/0x027a/init. |
| bit 0x0b (0x21.3) | `ep0_more_data` / OUT-data-pending | Dual use: (a) "more EP0 IN data pending" in the IN engine (set 0x0b01/0x0b15, tested 0x0f91); (b) "EP0 OUT data-phase pending" set by SETUP for SET requests (0x0069), tested by `oep0_int_handler` (0x0cc7). Cleared 0x0248/0x02e8/0x0cfb/etc. |
| bit 0x0c (0x21.4) | `ep0_data_loaded` | EP0 IN data stage loaded/complete. Set 0x024a/0x0b1c, cleared 0x0b03/0x02ea/0x0cfd; consumed 0x0f98. |
| bit 0x0d (0x21.5) | `ep0_short_resp` / ZLP-needed | "Response shorter than wLength" / "send terminating ZLP". Set by `fcn_0d9e` (0x0ddc), cleared at SETUP (0x0039) and 0x0dc2; consumed 0x0b12. (Cleared at every SETUP; standalone meaning first flagged UNKNOWN, resolved by `fcn_0d9e`.) |
| bit 0x0e (0x21.6) | `configured` | SET_CONFIGURATION(1) accepted. Set 0x027c, cleared 0x026d/init; read by GET_CONFIGURATION (0x015f) and the audio-config gates. |
| byte 0x22 | `ctrl_img_A` | 8-bit external shift-register image ("panel control/LED" chain). Shifted out MSB-first by `fcn_0efc` (data=P1.7, clock=P1.5, latch=P1.6). Bit-addresses 0x10–0x17 alias this byte. Init 0. |
| bits 0x10/0x11/0x12 (0x22.0/.1/.2) | channel-A one-cold outputs | Written by `panel_state_cycle_A`. External wiring UNKNOWN. |
| bit 0x13 (0x22.3) | (shared) | Cleared in teardown (0x03a0); also channel-B output low bit in `panel_state_cycle_B`. |
| bits 0x14/0x15 (0x22.4/.5) | channel-B one-cold outputs | Written by `panel_state_cycle_B`. External wiring UNKNOWN. |
| bit 0x16 (0x22.6) | control line | `= !bit0x2c && !bit0x2d` (computed in both panel state machines); also toggled by input-select (0x045c setb / 0x046b clr) and the EEPROM write-probe (0x0501 clr). External wiring UNKNOWN. |
| bit 0x17 (0x22.7) | run/stop-like line | Cleared 0x03a4 (alt-1 record path), set 0x03ea (alt-0 stop path). External wiring UNKNOWN. |
| byte 0x23 | `ctrl_img_B0` | First byte of the 16-bit control-latch word; shifted MSB-first by `fcn_0e56` (data=P1.0, clock=P1.2, latch=P1.1), sent before 0x25. Bit-addresses 0x18–0x1f alias this byte. Init 0. |
| bits 0x18/0x19 (0x23.0/.1) | mode-5 lines | Set only in clock mode 5 (0x0796/0x0798). UNKNOWN. |
| bits 0x1a/0x1b (0x23.2/.3) | clock-switch lines | Cleared then restored around clock switches (0x0716/0x0718 → 0x07cf/0x07d1); set as a pair in bring-up (0x09d8/0x09da). UNKNOWN. |
| bit 0x1c (0x23.4) | bring-up line | Set 0x09e5. UNKNOWN. |
| bit 0x1e (0x23.6) | shifter-mode toggle | Toggled by `fcn_1020` on P3.5 edge; selects `fcn_0efc`'s final P1.6/P1.7 drive pattern (0x0f20). Also set/cleared as a latch-style flag (0x0862/0x0883). UNKNOWN physical meaning. |
| byte 0x24 | (bit holder) | Holds bit-addresses 0x20–0x27. Init 0. |
| bit 0x20 (0x24.0) | `tick_flag` | Timer-0 millisecond tick. Set only by TF0 ISR (0x1018); consumed/cleared by main loop (0x0a7d/0x0ab7). |
| bit 0x22 (0x24.2) | (init flag) | Cleared once at 0x0a55. Purpose UNKNOWN. |
| byte 0x25 | `ctrl_img_B1` / status | Second byte of the 16-bit control-latch word (`fcn_0e56`), AND audio-path status bits. Bit-addresses 0x28–0x2f alias this byte. Zeroed on stream teardown (`fcn_09b6`) and suspend (0x0533). Init 0. |
| bits 0x28/0x2a (0x25.0/.2) | channel-A state | 3-state cycle in `panel_state_cycle_A`. |
| bits 0x29/0x2b (0x25.1/.3) | channel-B state | 3-state cycle in `panel_state_cycle_B`. |
| bit 0x2c (0x25.4) | `input_src` | Input-source select. **Polarity: set = S/PDIF, clear = analog** (see §2.5). Read by GET-input-source (0x0071); written by input-select actions 4 (clr) / 5 (setb). |
| bit 0x2d (0x25.5) | (probe flag) | Set in case 11 (0x04ce), cleared in cases 2/3 (0x0399/0x041c). Purpose UNKNOWN. Also feeds the bit-0x16 computation. |
| bit 0x2e (0x25.6) | `teardown_done` | One-shot stream/codec-teardown latch: set by `fcn_09b6` (0x09bb), tested before every teardown call. |
| bit 0x2f (0x25.7) | CS8427 chip-select | Chip-select level for the CS8427 serial write, pushed out via the shift chain by `fcn_0e56` (`fcn_0c31` drops it low before, raises after). |
| bytes 0x2c / 0x2d | `cs_reg_shadow` / `cs_val_shadow` | (register, value) shadow pair for CS8427 writes via `fcn_0567`/`fcn_0575`/`fcn_0c31`; also EEPROM readback temporaries in case 11. **Separate objects from bits 0x2c/0x2d.** |
| byte 0x2e | mode-param / delay-hi | Saved clock-mode parameter in `audio_clock_set_mode` (0x070f); busy-delay counter hi in `hw_clock_codec_init`. Also written by `fcn_070f`-callers as R7 stash. |
| byte 0x2f | delay-lo / bit holder | Busy-delay counter lo (0x0712 / 0x07d7 / 0x086x). Holds bit-address 0x30 too. |
| bit 0x30 (0x26.0) | shift "2nd byte pending" | `fcn_0e56` internal loop flag. |
| byte 0x27 | `p3_1_latch` | Edge-detect latch for P3.1 events (main loop 0x0a40/0x0a9d/0x0aae). |
| bytes 0x28:0x29 | startup delay ctr | 16-bit 0xFFFF busy-wait counter in `main_loop` (0x0a44/0x0a46). (Overlaps bit holders numerically but used as direct bytes here.) |
| bytes 0x2a / 0x2b | (init only) | Set 0x00 / 0x10 at 0x0a48/0x0a4b; not read in the annotated code. UNKNOWN use. |
| bytes 0x31 / 0x32 | serial reg/val staging | Register number (always 4) and value (0x41 ext / 0x40 int) staged for the CS8427 write in `audio_clock_set_mode` (0x073d–0x07a3). |
| direct 0x05 | bank-0 R5 alias | Read by direct address in `fcn_0bda`/`fcn_0c31` (Keil "Rn = Rm" idiom). |
| direct 0x07 | bank-0 R7 alias | Return-flag register in `fcn_0f31`. |
| direct 0x16 | bank-2 R6 alias | Read as `mov r2,0x16` in the ISR (Keil idiom); byte 0x16 itself is never written. |
| SP = 0x32 | stack pointer | Set by ?C_STARTUP (0x0930); stack grows from 0x33. IRAM 0x01–0x7F cleared at reset. |

### 2.3 XDATA / SFR usage

The TAS1020B maps its USB/codec peripheral registers into XDATA (`MOVX @DPTR`). No
general-purpose XDATA RAM variables are used by this firmware except the USB packet
buffer RAM (page 0xFA/0xFC). Notable XDATA regions:

| XDATA | Use |
|---|---|
| 0xFA10 | EP0 OUT X-buffer (OEPBBAX0 = 0x42 → 0xF800 + 0x42·8). Class-request OUT payloads read here by `oep0_int_handler`. |
| 0xFA18 | EP0 IN X-buffer (IEPBBAX0 = 0x43 → 0xFA18). Descriptor/reply bytes assembled here. |
| 0xFA20–0xFC9F | OUT EP2 iso audio (playback) X-buffer, 640 bytes (OEPBBAX2 = 0x44, OEPBSIZ2 = 0x50). |
| 0xFCA0 + | IN EP1 iso audio (record) X-buffer, 640 bytes (IEPBBAX1 = 0x94, IEPBSIZ1 = 0x50; nominal end 0xFF20 overlaps EP-config SFR space — actual iso DMA bound UNKNOWN). |
| 0xFF00–0xFFFF | TAS1020B SFR block (SETPACK 0xFF28, EP configs, DMA, ACG, codec-port, I2C, USBCTL/USBIMSK/USBFADR/VECINT). Every SFR write is annotated inline in §5. |

External (non-XDATA) hardware is driven by GPIO bit-banging on Port 1:
- **16-bit control/LED latch** — `fcn_0e56`: data=P1.0, clock=P1.2, latch=P1.1; sends
  IRAM 0x23 then 0x25.
- **8-bit control/LED latch** — `fcn_0efc`: data=P1.7, clock=P1.5, latch=P1.6; sends
  IRAM 0x22.
- **CS8427 S/PDIF transceiver** (likely) — `fcn_0c31`: data=P1.4, clock=P1.3; 3-byte
  frame {0x20, reg, val}, MSB-first, CS via bit 0x2f through the 16-bit latch.
- **Config EEPROM (24Cxx, I2C addr 0xA0)** — `fcn_0bda` (write) / `fcn_0d11` (read) via
  the hardware I2C block (I2CCTL/I2CADR/I2CDATO/I2CDATI at 0xFFC0–0xFFC3).

### 2.4 8051 hardware interrupt vectors

| Vector | Addr | Target | Meaning |
|---|---|---|---|
| RESET | 0x0000 | `LJMP 0x092a` | ?C_STARTUP |
| IE0 / INT0 | 0x0003 | `LJMP 0x0ddf` | USB engine interrupt (VECINT dispatch) |
| TF0 | 0x000b | `LJMP 0x1016` | Timer-0 millisecond tick |
| IE1 | 0x0013 | `LJMP 0x000a` | unused → RETI stub |
| TF1 | 0x001b | `LJMP 0x000e` | unused → RETI stub |
| SI (UART) | 0x0023 | `LJMP 0x000f` | unused → RETI stub |
| TF2 | 0x002b | (inside SETUP handler) | Timer 2 never enabled |

### 2.5 Reconciliation notes

Conflicts between annotators, resolved with the better-evidenced reading; both sides
recorded.

1. **EP0 IN vs OUT buffer address (0xFA10 / 0xFA18).** Range 0x0b1f–0x0c31 named
   `fcn_0b1f` (sets cursor = 0xFA10) `ep0_in_setptr` and `fcn_0b37` (sets cursor =
   0xFA18) `ep0_out_setptr`. This is **backwards**. The authoritative SFR programming in
   `fcn_0891` writes OEPBBAX0 = 0x42 → **0xFA10 = EP0 OUT** and IEPBBAX0 = 0x43 →
   **0xFA18 = EP0 IN**. Consistent corroboration: the SETUP handler and GET handlers
   assemble IN replies at 0xFA18 (via `fcn_0b37`), and `oep0_int_handler` reads the
   **OUT** class-request payload from 0xFA10 (via `fcn_0b1f`). Range 0x09b6–0x0b1f
   (`fcn_0abb`) and range 0x0000 both use 0xFA18 as the IN buffer, agreeing with
   `fcn_0891`. **Resolution: 0xFA10 = EP0 OUT, 0xFA18 = EP0 IN.** In §5 the range-8
   listing is preserved verbatim; treat its "EP0 IN buffer"/"EP0 OUT buffer" labels on
   `fcn_0b1f`/`fcn_0b37` as swapped per this note. The functions are accordingly better
   named `ep0_out_setptr` (0x0b1f) and `ep0_in_setptr` (0x0b37).

2. **Input-source polarity (bit 0x2c).** Range 0x02f3–0x0567 tentatively labeled
   deferred-action case 4 (`clr 0x2c`, 0x045a) as "likely S/PDIF" and case 5 (`setb
   0x2c`, 0x0469) as "likely analog", flagging both as unverified guesses from NOTES.md.
   Range 0x0c31–0x0ddf decoded `oep0_int_handler` against the hardware-tested Linux
   driver: SET-input-source payload `0x01` (= analog per `snd_mbox1_set_input_source`) →
   action **4** (`clr 0x2c`), and payload `0x02` (S/PDIF) → action **5** (`setb 0x2c`).
   Range 0x0000 independently found GET-input-source returns 1 when bit 0x2c clear and 2
   when set (1=analog, 2=S/PDIF per the driver). **Resolution: bit 0x2c clear = analog,
   set = S/PDIF** (so case 4 = analog, case 5 = S/PDIF). Range 3's tentative labels are
   the reversed guess; the range-3 listing text is preserved in §5 but is superseded by
   this note.

3. **`fcn_09b6` purpose — "teardown" vs "bring-up".** Range 0x02f3–0x0567 (which only
   *calls* `fcn_09b6`) described it as a one-shot "stream/codec teardown" that zeroes
   bytes 0x25/0x23 and sets latch bit 0x2e. Range 0x09b6–0x0b1f *read the full body* and
   found it is an audio hardware **bring-up** sequence: it zeroes the latch word, sets
   bit 0x2e, programs both ACG synthesizers, enables MCLK outputs, stages control-latch
   bits with delays, and writes ten CS8427/codec registers. **Resolution: `fcn_09b6` =
   `audio_hw_bringup`** (the full-body reading). Both are consistent on the opening
   (zero latch, set 0x2e); the callers use it at points where the external audio path is
   (re)initialized, and the "one-shot" guard (bit 0x2e) prevents re-running it.

4. **Reset entry address.** The annotation brief carried a stale caveat that the rev22
   entry might be 0x0dff. The reset vector bytes are `02 09 2a` = `LJMP 0x092a`
   (verified in the image). **Resolution: reset → 0x092a.**

5. **`fcn_0b4d` OUT half is a no-op.** Two annotators independently confirmed that
   `fcn_0b4d` reads OEPCNF0 and computes `&0xD7` but never writes it back (bytes
   `90 ff a8 e0 54 d7 22`). Recorded as a likely firmware bug / vestigial code, not a
   decode error. Only the IEPCNF0 modification takes effect.

---

## 3. Function Index (sorted by address)

Confidence: **certain** unless noted. "Callers" lists code addresses that reach the
function (LCALL/LJMP/SJMP/table dispatch); interrupt entries reached only via the VECINT
table are marked "(VECINT table)".

| Addr | Name | Purpose | Conf. | Callers |
|---|---|---|---|---|
| 0x0000 | `reset_vector` | `LJMP 0x092a` — reset entry | certain | (hardware reset) |
| 0x0003 | `int0_vector` | `LJMP 0x0ddf` — USB ISR entry | certain | (hardware INT0) |
| 0x0006 | `usb_susr_handler` | USB SUSPEND: sets `pending_action`=0x0e, RET | certain | (VECINT 0x16) |
| 0x000a | `reti_stub_ie1` | bare RETI (IE1 unused) | certain | 0x0013 |
| 0x000b | `tf0_vector` | `LJMP 0x1016` — Timer-0 tick | certain | (hardware TF0) |
| 0x000e | `reti_stub_tf1` | bare RETI (TF1 unused) | certain | 0x001b |
| 0x000f | `reti_stub_si` | bare RETI (UART unused) | certain | 0x0023 |
| 0x0010–0x0022 | `usb_ep_int_noop_stubs` | 13 single-byte RET stubs for OEP1-7/IEP1-6 | certain | (VECINT table) |
| 0x0026 | `usb_setup_handler` | SETUP_INT: EP0 prologue + audio-class dispatch | certain | (VECINT 0x12) |
| 0x0100 | `thunk_stall_ep0` | `LJMP 0x02ef` (stall EP0) | certain | 0x00da, 0x08e(within), 0x05d5/0x065b/0x070d |
| 0x0103 | `ep0_arm_in_3bytes` | arm 3-byte EP0 IN reply | certain | 0x00fe, 0x05ba |
| 0x010b | `usb_std_request_dispatch` | Standard request dispatcher (jump table 0x011e) | certain | 0x0052 |
| 0x0145 | `std_clear_feature` | CLEAR_FEATURE (EP0 halt only) | certain | table[1] |
| 0x015c | `std_get_configuration` | GET_CONFIGURATION → 1 byte | certain | table[8] |
| 0x0177 | `std_get_descriptor` | GET_DESCRIPTOR (device/config/string) | certain | table[6] |
| 0x01ed | `std_get_interface` | GET_INTERFACE → 1 byte | certain | table[10] |
| 0x022f | `std_get_status` | GET_STATUS → 0x0000 | certain | table[0] |
| 0x0247 | `ep0_arm_in_and_done` | write IEPDCNTX0=A, arm IN, set flags | certain | 0x0174/0x022a/0x0108/0x016f |
| 0x024d | `std_set_address` | SET_ADDRESS (deferred write) | certain | table[5] |
| 0x0259 | `std_set_configuration` | SET_CONFIGURATION (0/1) | certain | table[9] |
| 0x029b | `std_stall_unsupported` | stall for SET_FEATURE/SET_DESCRIPTOR/SYNCH_FRAME | certain | table[3/7/12] |
| 0x029d | `std_set_interface` | SET_INTERFACE (iface1/2, alt 0/1) | certain | table[11] |
| 0x02e8 | `ep0_done_no_data` | clear bits 0x0b/0x0c, RET | certain | 0x005e/0x0156/0x0256/0x0299 |
| 0x02ef | `ep0_stall_both` | stall EP0 both dirs (→ fcn_1001) | certain | many |
| 0x02f3 | `usb_deferred_action_dispatch` | main-loop deferred control-request executor (14-way) | certain | 0x0a84/0x0aa3/0x0ab4 |
| 0x0567 | `cs8427_write_reg04_val41` | CS8427 reg 0x04 := 0x41 | likely | 0x0485, 0x04a7 |
| 0x0575 | `cs8427_write_shadowed` | CS8427 write of preloaded 0x2c/0x2d | likely | 0x0494, 0x04b5 |
| 0x070f | `audio_clock_set_mode` | program ACG for clock mode R7 (1/2/3/5) | certain | 0x03be/0x0426/0x047f/0x04a1/0x04d2/0x0512 |
| 0x07ec | `hw_clock_codec_init` | MCU/codec-port/ACG cold init | certain | 0x0550, 0x0a57 |
| 0x0891 | `usb_ep_dma_init` | EP0 + iso EP1/EP2 + DMA setup, USBIMSK/USBFADR | certain | 0x0553, 0x0a5a |
| 0x0904 | (secondary entry) | DMACTL/IMSK/FADR tail of `usb_ep_dma_init` | likely | 0x061f (LJMP; needs DPTR=0xFFE8) |
| 0x0910 | (secondary entry) | USBIMSK/USBFADR/flag reinit (bus-reset style) | likely | 0x0cbc (ACALL; needs DPTR=0xFFFD) |
| 0x092a | `keil_c51_startup` | ?C_STARTUP: clear IRAM, SP=0x32, run init, → main | certain | 0x0000 |
| 0x0939 | `keil_c_init_interpreter` | ?C_INIT: interpret table at 0x0FBA | certain | 0x0971 (from 0x0933) |
| 0x09b6 | `audio_hw_bringup` | ACG + external-latch + codec register bring-up | likely | 0x0366/0x0396/0x0419/0x04cb |
| 0x0a3f | `main_loop` | post-init forever loop | certain | 0x0936 (from ?C_INIT) |
| 0x0abb | `ep0_in_send_chunk` | EP0 IN data-stage ≤8-byte copy engine | certain | 0x0b63 |
| 0x0b1f | `ep0_out_setptr` (range named it `ep0_in_setptr`) | cursor := 0xFA10, fall into ep0_load_dptr | certain | 0x0ccf, 0x0cec |
| 0x0b25 | `ep0_load_dptr` | DPTR := cursor (0x1d:0x1e) | certain | 0x0074/0x007d/0x0099/0x00b8/0x00dd/0x0162/0x0232/0x0acd |
| 0x0b2c | `ep0_store_byte_and_arm_zlp` | store A, then IEPDCNTX0=OEPDCNTX0=0 (or =A via 0x0b2e) | likely | 0x0036/0x0d0d/0x0fb6/0x100e; 0x0296 (0x0b2e) |
| 0x0b37 | `ep0_in_setptr` (range named it `ep0_out_setptr`) | cursor := 0xFA18 (no DPTR load) | certain | 0x006e/0x0091/0x015c/0x01fc/0x022f/0x0abe |
| 0x0b3e | `ep0_clear_stall_both` | IEPCNF0/OEPCNF0 &= ~0x08 | certain | 0x0026, 0x0153 |
| 0x0b4d | `ep0_clear_stall_toggle` | IEPCNF0 &= ~0x28 (OUT half is a no-op — see §2.5.5) | certain | 0x0d0a, 0x0fb3 |
| 0x0b5b | `ep0_buf_store_zero` | store 0 at 0x1d:A | certain | 0x00a6/0x00fb/0x023f |
| 0x0b63 | `ep0_in_stage_and_go` | call `fcn_0abb`, clear IEPDCNTX0 NAK | likely | 0x01e9, 0x0f94 |
| 0x0b6e | `load_dptr_from_ptr19` | DPTR := desc_ptr (0x19:0x1a) | certain | 0x019a/0x01d7/0x0ac8 |
| 0x0b75 | `ep0_flush_arm` | OEPDCNTX0=0, IEPDCNTX0=0 (status stage) | likely | 0x0d06, 0x0f64 |
| 0x0b7f | `udiv16` | unsigned 16÷16 divide runtime helper | certain | 0x0d79 |
| 0x0bd4 | `jmp_via_r2r1` | computed jump to R2:R1 | certain | 0x0e06 |
| 0x0bda | `i2c_eeprom_write3` | write 3 bytes (addr-hi, addr-lo, data) to EEPROM 0xA0 | likely | 0x04f2, 0x051b |
| 0x0c31 | `fcn_0c31` = `cs8427_write_reg` | 3-wire CS8427 register write (0x20, reg, val) | likely | 0x09fc–0x0a3b (10×), 0x04df/0x050d, 0x0571/0x0579, 0x07aa |
| 0x0cc7 | `oep0_int_handler` | OEP0_INT: EP0 OUT data-stage for class requests | certain | (VECINT 0x00) |
| 0x0d0a | `oep0_clear_stall_and_rearm` | tail of `oep0_int_handler` (no OUT data expected) | certain | 0x0cc7 |
| 0x0d11 | `i2c_eeprom_read_byte` | random-read 1 byte from EEPROM 0xA0 → R7 | certain | 0x04e6, 0x04f7 |
| 0x0d58 | `sof_int_handler` | SOF_INT: audio-OUT DMA 6-byte alignment resync | certain | (VECINT 0x14) |
| 0x0d9e | `ep0_clamp_len_to_wlength` | clamp EP0 IN length to wLength, set short flag | certain | 0x01e6 |
| 0x0ddf | `usb_isr_int0_vecdispatch` | INT0 USB ISR: read VECINT, dispatch via 0x0c7d table | certain | (hardware INT0 via 0x0003) |
| 0x0e1b | `panel_state_cycle_A` | 3-state cycle, channel-A control/LED outputs | likely | 0x0f4e |
| 0x0e56 | `shiftreg_out16_p1` | bit-bang 16 bits (0x23,0x25) on P1.0/P1.2/P1.1 | certain | 0x071a/0x0384/0x088d/0x09c1/0x09dc/0x09e7/0x09f0/0x09f5/0x0a93/0x0c3b/0x0c79/0x0537 |
| 0x0e8f | `panel_state_cycle_B` | 3-state cycle, channel-B control/LED outputs | likely | 0x0f5b |
| 0x0ec7 | `sfr_write_then_acg_program` | write caller A@DPTR, fall into ACG program | certain | 0x078a, 0x084f |
| 0x0ec8 | `acg_both_synths_24576khz` | ACG1+ACG2 = 24.576 MHz | certain | 0x076f, 0x09c4 |
| 0x0ee8 | `acg2frq0_load_and_acgctl` | load ACG2FRQ0, ACGCTL=0x06 | certain | 0x0766 (also fall-through) |
| 0x0ef3 | `acg_dividers_div2` | ACG1DCTL/ACG2DCTL = 0x10 (÷2) | certain | 0x0852, 0x09c7 (0x0ef4 from 0x0720) |
| 0x0efc | `shiftreg_out8_p1hi` | bit-bang byte 0x22 on P1.7/P1.5/P1.6 | certain | 0x0864/0x0885/0x03a6/0x03ec/0x0461/0x0470/0x0503/0x0a90 |
| 0x0f31 | `p3_edge_poll_dispatch` | poll P3, dispatch on P3.5/P3.3/P3.4 edges | certain | 0x0a89 |
| 0x0f64 | `usb_rstr_handler` | RSTR_INT: bus-reset EP0/USB re-init | certain | (VECINT 0x17) |
| 0x0f91 | `ep0_in_done_handler` | IEP0_INT: EP0 IN complete (chunk / status / SET_ADDR) | certain | (VECINT 0x08) |
| 0x0fe2 | `cport_cnf3_write_enable` | CPTCNF3/CPTRXCNF3 = A, GLOBCTL |= CPTEN | certain | 0x0358, 0x0360 |
| 0x0ff2 | `dma0_disable` | DMACTL0 &= ~DMAEN | certain | 0x0336/0x03f2/0x044b |
| 0x0ffa | `stage_ctrl_pair_12_00` | IRAM 0x2c=0x12, 0x2d=0x00 | likely | 0x0488/0x04aa/0x0506 |
| 0x1001 | `ep0_stall_both` (impl) | IEPCNF0/OEPCNF0 |= STALL, clear bits 0x0b/0x0c | certain | 0x02ef |
| 0x1016 | `timer0_tick_isr` | TF0 ISR: set tick flag, reload TH0=0xCE | certain | (hardware TF0 via 0x000b) |
| 0x1020 | `toggle_bit1E_state` | toggle bit 0x1e (0x23.6) | certain | 0x0f41 |


## 4. Execution Narrative

### 4.1 Reset → init → steady state

**Reset.** On power-up the 8051 core jumps from the reset vector `0x0000` to
`keil_c51_startup` at **0x092a**. It clears IRAM 0x7F down to 0x01 (`mov r0,#0x7f; clr
a; mov @r0,a; djnz r0`), sets `SP = 0x32` (stack grows from 0x33), and jumps to the
?C_INIT interpreter entry at **0x0971**.

**Static init.** `keil_c_init_interpreter` (0x0939/0x0971) walks the Keil compiler init
table at CODE **0x0FBA**. For rev22 that table is a list of IDATA byte-writes: it zeroes
the flag/state bytes 0x20–0x25 and 0x09–0x0e, and sets `clock_mode_id` (byte 0x08) = 3.
On the terminating 0x00 record it jumps (0x0978 → 0x0936) to `main_loop` at **0x0a3f**.

**Main-loop one-time setup (0x0a3f–0x0a7c).** `main_loop` initializes the P3.1 edge
latch and a 16-bit startup counter, disables interrupts (`EA=0`), masks all USB
interrupts (`USBIMSK=0`), then calls the two big init routines:
- `hw_clock_codec_init` (**0x07ec**): disconnects USB (`USBCTL=0`), keeps code in shadow
  RAM (`MEMCFG=1`), sets up ports/timers (`TMOD=0x11`, `TH0=0xCE` ms-tick reload, `ET0`
  and `EX0` enabled, `EA` left off), programs the codec port for I2S mode 5 (24-bit in
  32-clock slots, CSCLK=MCLK/4), programs both ACG synthesizers to 24.576 MHz then ÷2 =
  12.288 MHz (256·48 kHz), enables the codec port (`GLOBCTL` CPTEN), and resets both
  external GPIO shift chains with a ~4096-iteration settle delay between latch states.
- `usb_ep_dma_init` (**0x0891**): programs EP0 IN/OUT control buffers (0xFA18 / 0xFA10, 8
  bytes, interrupt-enabled), the iso audio endpoints (OUT EP2 at 0xFA20 / IN EP1 at
  0xFCA0, both CNF=0xC5 = ISO, 6 bytes/sample), binds DMA channel 0 → OUT EP2 and channel
  1 → IN EP1 (3 bytes/slot, slots 0+1, DMAEN off), sets `USBIMSK=0x9F`, `USBFADR=0`, and
  clears the USB state flags.

It then runs a ~0xFFFF busy-wait (let hardware settle), starts Timer 0 (`TR0=1`), enables
global interrupts (`EA=1`), and sets `USBCTL.CONT` — connecting the DP pull-up so the
host now sees the device and begins enumeration.

**Steady state (forever loop 0x0a7d–0x0ab9).** Each pass:
- If the millisecond tick flag (bit 0x20, set by the Timer-0 ISR) is **clear**, the loop
  checks `pending_action` (byte 0x0a); if non-zero it calls
  `usb_deferred_action_dispatch` (0x02f3) to perform the slow hardware side of whatever
  control request the SETUP/OUT interrupt path queued, otherwise it spins.
- If the tick flag is **set**, it polls the front panel: `p3_edge_poll_dispatch` (0x0f31)
  samples P3, and on rising edges of P3.5/P3.3/P3.4 runs `toggle_bit1E_state` /
  `panel_state_cycle_A` / `panel_state_cycle_B`. If any control changed it re-shifts the
  two GPIO latch words. It then edge-detects P3.1, queuing `pending_action` 0x0b (P3.1
  low) or 0x0c (P3.1 high) and dispatching immediately, and finally clears the tick flag.

So the device is interrupt-driven for all USB and audio-frame work; the main loop only
(a) executes the deferred, slow, hardware-touching tail of control requests, and (b)
services the front panel once per millisecond tick.

### 4.2 USB control-transfer flow

A SETUP packet raises SETUP_INT. The **INT0 ISR** (`usb_isr_int0_vecdispatch`, 0x0ddf)
reads VECINT (0xFFB2), indexes the big-endian dispatch table at 0x0c7d
(`entry = 0x0c7d + 2·VECINT`), and calls the handler through `jmp_via_r2r1`. For
SETUP_INT (0x12) that handler is `usb_setup_handler` (0x0026).

`usb_setup_handler` runs the TI `engEp0SetupDone` prologue (clear EP0 stalls, set both
data toggles, empty both EP0 FIFOs) then dispatches on `bmRequestType`:
- `0x22` (class OUT, endpoint) → `pending_cmd`=1 (SET clock/sample-rate), mark EP0 OUT
  data-phase pending, RET.
- `0x21` (class OUT, interface) → `pending_cmd`=2 (SET input source) — unless `bRequest`
  is 0, which queues `pending_action`=0x0d and ends with no data phase.
- `0xA1` (class IN, interface) → GET input source: reply 1 byte (2 if bit 0x2c set =
  S/PDIF, else 1 = analog), arm EP0 IN.
- `0xA2` (class IN, endpoint) → GET sampling frequency: build a 3-byte LE reply from
  `clock_mode_id` (1→00 00 00, 2→44 AC 00 = 44100, 3→80 BB 00 = 48000; else stall), arm
  EP0 IN.
- anything else → `LJMP 0x010b` = `usb_std_request_dispatch`.

`usb_std_request_dispatch` (0x010b) reads `bRequest`, stalls if ≥ 13, else jumps through
the 13-slot LJMP table at 0x011e to the standard handlers (GET_STATUS, CLEAR_FEATURE,
SET_ADDRESS, GET/SET_DESCRIPTOR (stall on SET), GET/SET_CONFIGURATION,
GET/SET_INTERFACE; SET_FEATURE/SET_DESCRIPTOR/SYNCH_FRAME stall). GET handlers assemble
their reply in the EP0 IN buffer and arm it; SET_CONFIGURATION / SET_INTERFACE record
the new state, queue `pending_action` (1/2/3), and NAK EP0 both directions until the
main loop applies the change.

**EP0 IN data stage.** For multi-packet GET_DESCRIPTOR replies, `std_get_descriptor`
loads `desc_ptr` and `xfer_len`, clamps to wLength (`ep0_clamp_len_to_wlength`), and
sends the first ≤8-byte chunk via `ep0_in_stage_and_go` → `ep0_in_send_chunk`. Each host
ACK raises IEP0_INT → `ep0_in_done_handler` (0x0f91), which sends the next chunk while
bit 0x0b is set, and terminates with a ZLP if the last packet was a full 8 bytes and the
short-flag (bit 0x0d) demands it.

**EP0 OUT data stage.** For SET clock/input-source (`bmRequestType` 0x22/0x21), the host
follows SETUP with an OUT data packet, raising OEP0_INT → `oep0_int_handler` (0x0cc7).
It reads the payload from 0xFA10 and, based on `pending_cmd`, queues the matching
`pending_action`: clock payload 0x44→7 (44.1 kHz), 0x80→8 (48 kHz), 0x00→6 (S/PDIF sync);
input payload 0x01→4 (analog), else→5 (S/PDIF). It then sets the EP0 IN toggle for the
DATA1 status stage and re-arms both EP0 buffers.

**SET_ADDRESS** is deferred: `std_set_address` records the address in byte 0x0e and sets
`pending_cmd`=5; the actual `USBFADR` write happens in `ep0_in_done_handler` (0x0faf)
only after the status-IN completes, as USB 2.0 requires.

**Deferred execution.** The main loop calls `usb_deferred_action_dispatch` (0x02f3),
which is a 14-way jump table over `pending_action`:
1 = SET_CONFIGURATION (tear down DMA/codec port, reprogram C-port frame format);
2/3 = SET_INTERFACE iface1/iface2 (enable iso EP1-IN/EP2-OUT + DMA, apply clock mode 3,
unmask all USB interrupts incl. SOF for streaming);
4/5 = input-select analog/S/PDIF (toggle bit 0x2c + control lines, re-apply mode);
6/7/8/9/10/12 = apply audio clock mode 1..5 (via `audio_clock_set_mode`, plus CS8427
register writes for modes 2/3);
11 = mode 3 + CS8427 kick + EEPROM 0x1FFF write-probe;
13 = write EEPROM[0x0000]=0 (likely DFU trigger — invalidate the boot header so the next
reset enters the boot ROM's DFU device);
14 = USB suspend (`PCON.IDL`) then, on resume, disconnect → re-init → reconnect.
Each EP0-ACKing case ends by clearing both EP0 NAK bits (zero-length IN = status ACK)
and every path clears `pending_action` at 0x0563.

### 4.3 Interrupt paths (summary)

- **INT0 / USB (0x0003 → 0x0ddf).** Save context, `EA=0`, read VECINT, table-dispatch
  via 0x0c7d, run handler, clear VECINT, `EA=1`, RETI. Handlers: SETUP→0x0026,
  IEP0→0x0f91, OEP0→0x0cc7, SOF→0x0d58, RSTR→0x0f64, SUSR→0x0006; all unused
  endpoint/codec/DMA/I2C vectors → RET stubs (0x0010–0x0022, 0x1029–0x1035).
- **Timer 0 / TF0 (0x000b → 0x1016).** `EA=0`, set tick flag (bit 0x20), reload
  `TH0=0xCE`, `EA=1`, RETI. Drives the ~1 ms main-loop cadence.
- **SOF_INT (0x14 → 0x0d58).** Every USB frame, sample DMABCNT0; if it advanced and is
  not a whole number of 6-byte audio frames, resync OUT EP2 (disable DMA0, zero EP2
  counts, rewrite OEPCNF2=0xC5, re-enable DMA0).
- **RSTR_INT (0x17 → 0x0f64).** Bus reset: zero EP0/EP1/EP2 counts, `USBFADR=0`, EP0
  CNF=0x84, `USBCTL |= CONT|FEN`, clear USB flags, `USBIMSK=0x9F`.
- **SUSR_INT (0x16 → 0x0006).** Suspend: set `pending_action`=0x0e and RET; the real
  suspend/resume work runs in the main loop (case 14) because the datasheet forbids
  setting `PCON.IDL` inside an ISR.
- **IE1/TF1/SI.** Unused; their vectors trampoline to bare RETI stubs.

---

## 5. Full Annotated Listing

All twelve ranges concatenated in address order. Every instruction line from the input
annotations is preserved. Data regions (descriptors, VECINT table, ?C_INIT table,
mask table, 0xFF fill) are reproduced in §6 to avoid duplicating them here; their
address spans are marked inline below. Where §2.5 recorded a reconciliation, the
listing text is the annotator's original — read it together with the reconciliation
note (e.g. `fcn_0b1f`/`fcn_0b37` IN/OUT labels; case-4/5 input polarity).

### 5.1 — 0x0000–0x0025 : vectors and USB interrupt stubs

```
0x0000  02 09 2a     ljmp 0x092a           ; RESET vector -> main init (clears IRAM 0x01-0x7F, SP=0x32, ljmp 0x0971)
0x0003  02 0d df     ljmp 0x0ddf           ; IE0/INT0 vector -> USB ISR (reads VECINT 0xFFB2, dispatches via table at 0x0c7d)
0x0006  75 0a 0e     mov 0x0a,#0x0e        ; usb_susr_handler (SUSPEND, VECINT 0x16 via table 0x0ca9): main-loop action byte IRAM 0x0A = 0x0E
0x0009  22           ret                   ; return to USB ISR epilogue (VECINT cleared there at 0x0e09-0x0e0d)
0x000a  32           reti                  ; RETI stub — target of unused IE1 vector trampoline at 0x0013
0x000b  02 10 16     ljmp 0x1016           ; TF0 vector -> timer0 ISR: EA off, SETB bit 0x20, TH0=0xCE reload, EA on, RETI
0x000e  32           reti                  ; RETI stub — target of unused TF1 vector trampoline at 0x001b
0x000f  32           reti                  ; RETI stub — target of unused serial vector trampoline at 0x0023
0x0010  22           ret                   ; no-op USB int handler: OEP1_INT (0x01) — vector table entry 0x0c7f = 00 10
0x0011  22           ret                   ; no-op: OEP2_INT (0x02) — table entry 0x0c81
0x0012  22           ret                   ; no-op: OEP3_INT (0x03) — table entry 0x0c83
0x0013  02 00 0a     ljmp 0x000a           ; IE1 hardware vector -> RETI stub (external int 1 unused)
0x0016  22           ret                   ; no-op: OEP4_INT (0x04) — table entry 0x0c85
0x0017  22           ret                   ; no-op: OEP5_INT (0x05)
0x0018  22           ret                   ; no-op: OEP6_INT (0x06)
0x0019  22           ret                   ; no-op: OEP7_INT (0x07)
0x001a  22           ret                   ; no-op: IEP1_INT (0x09)
0x001b  02 00 0e     ljmp 0x000e           ; TF1 hardware vector -> RETI stub (timer 1 unused)
0x001e  22           ret                   ; no-op: IEP2_INT (0x0a)
0x001f  22           ret                   ; no-op: IEP3_INT (0x0b)
0x0020  22           ret                   ; no-op: IEP4_INT (0x0c)
0x0021  22           ret                   ; no-op: IEP5_INT (0x0d)
0x0022  22           ret                   ; no-op: IEP6_INT (0x0e)
0x0023  02 00 0f     ljmp 0x000f           ; SI (UART) hardware vector -> RETI stub (UART unused)
```
Note: the TF2 vector slot 0x002B falls inside the SETUP handler's instruction stream —
Timer 2 interrupt is never enabled.

### 5.2 — 0x0026–0x010a : usb_setup_handler (SETUP_INT)

```
; --- prologue = TI engEp0SetupDone (UsbEng.c:219-233) ---
0x0026  12 0b 3e     lcall 0x0b3e          ; clear STALL: IEPCNF0 &= ~0x08, OEPCNF0 &= ~0x08 (STALLClrIn/OutEp0)
0x0029  90 ff 68     mov dptr,#0xff68      ; DPTR = IEPCNF0 (EP0 IN config; Ghidra mis-decoded — bytes verified in .bin)
0x002c  e0           movx a,@dptr          ; read IEPCNF0
0x002d  44 20        orl a,#0x20           ; set bit5 = data TOGGLE -> next IN is DATA1 (TOGGLEInEp0Data)
0x002f  f0           movx @dptr,a          ; write IEPCNF0 |= 0x20
0x0030  90 ff a8     mov dptr,#0xffa8      ; DPTR = OEPCNF0 (EP0 OUT config)
0x0033  e0           movx a,@dptr          ; read OEPCNF0
0x0034  44 20        orl a,#0x20           ; set bit5 = data TOGGLE for OUT (TOGGLEOutEp0Data)
0x0036  12 0b 2c     lcall 0x0b2c          ; commit OEPCNF0|=0x20, then IEPDCNTX0=0 and OEPDCNTX0=0; returns A=0
0x0039  c2 0d        clr 0x0d              ; clear bit 0x0d (IRAM 0x21.5) — EP0 flag cleared at every SETUP
0x003b  f5 09        mov 0x09,a            ; IRAM 0x09 = 0 (A=0 from helper)
0x003d  f5 0b        mov 0x0b,a            ; IRAM 0x0B = 0
; --- dispatch on bmRequestType (SETPACK[0] @ 0xFF28) ---
0x003f  90 ff 28     mov dptr,#0xff28      ; DPTR = SETPACK+0 = bmRequestType
0x0042  e0           movx a,@dptr          ; A = bmRequestType
0x0043  24 de        add a,#0xde           ; A = bmReq + 0xDE: zero iff bmReq == 0x22 (host->dev, class, ENDPOINT)
0x0045  60 1f        jz 0x0066             ; 0x22 -> SET clock/sampling-freq pending
0x0047  24 81        add a,#0x81           ; running sum: zero iff bmReq == 0xA1 (dev->host, class, INTERFACE)
0x0049  60 23        jz 0x006e             ; 0xA1 -> GET input source
0x004b  14           dec a                 ; zero iff bmReq == 0xA2 (dev->host, class, ENDPOINT)
0x004c  60 38        jz 0x0086             ; 0xA2 -> GET sampling frequency
0x004e  24 81        add a,#0x81           ; zero iff bmReq == 0x21 (host->dev, class, INTERFACE)
0x0050  60 03        jz 0x0055             ; 0x21 -> SET input source (or special bRequest==0 case)
0x0052  02 01 0b     ljmp 0x010b           ; anything else -> standard-request dispatcher at 0x010b
; --- bmReq == 0x21: class OUT to interface ---
0x0055  90 ff 29     mov dptr,#0xff29      ; DPTR = SETPACK+1 = bRequest
0x0058  e0           movx a,@dptr          ; A = bRequest
0x0059  70 06        jnz 0x0061            ; bRequest != 0 -> normal SET-input-source path
0x005b  75 0a 0d     mov 0x0a,#0x0d        ; bRequest == 0: main-loop action = 0x0D
0x005e  02 02 e8     ljmp 0x02e8           ; -> clr bit 0x0b, clr bit 0x0c, ret (no EP0 data phase pending)
0x0061  75 0d 02     mov 0x0d,#0x02        ; IRAM 0x0D = 2: command "set input source"
0x0064  80 03        sjmp 0x0069           ; join common SET tail
; --- bmReq == 0x22: class OUT to endpoint ---
0x0066  75 0d 01     mov 0x0d,#0x01        ; IRAM 0x0D = 1: command "set clock/sampling frequency"
0x0069  d2 0b        setb 0x0b             ; bit 0x21.3 = 1: EP0 OUT data phase pending (payload arrives next)
0x006b  c2 0c        clr 0x0c              ; bit 0x21.4 = 0: no IN data phase
0x006d  22           ret                   ; back to USB ISR
; --- bmReq == 0xA1: GET input source -> 1-byte reply ---
0x006e  12 0b 37     lcall 0x0b37          ; pointer 0x1D:0x1E = 0xFA18 (EP0 buffer used for IN replies here)
0x0071  30 2c 09     jnb 0x2c,0x007d       ; bit 0x25.4 clear -> reply 1 (analog); set -> reply 2 (S/PDIF)
0x0074  12 0b 25     lcall 0x0b25          ; DPTR = 0xFA18
0x0077  74 02        mov a,#0x02           ; reply byte = 2
0x0079  f0           movx @dptr,a          ; store reply into EP0 buffer @0xFA18
0x007a  02 01 6f     ljmp 0x016f           ; -> IEPDCNTX0 = 1 via 0x0247 (arm 1-byte IN reply)
0x007d  12 0b 25     lcall 0x0b25          ; DPTR = 0xFA18
0x0080  74 01        mov a,#0x01           ; reply byte = 1
0x0082  f0           movx @dptr,a          ; store reply
0x0083  02 01 6f     ljmp 0x016f           ; arm 1-byte IN reply (same tail)
; --- bmReq == 0xA2: GET sampling frequency -> 3-byte little-endian reply ---
0x0086  90 ff 2b     mov dptr,#0xff2b      ; DPTR = SETPACK+3 = wValue high byte
0x0089  e0           movx a,@dptr          ; A = wValueH
0x008a  64 01        xrl a,#0x01           ; compare with 1 (control selector in wValueH; only value 1 accepted)
0x008c  60 03        jz 0x0091             ; match -> build reply
0x008e  02 02 ef     ljmp 0x02ef           ; else stall EP0
0x0091  12 0b 37     lcall 0x0b37          ; pointer 0x1D:0x1E = 0xFA18
0x0094  e5 08        mov a,0x08            ; A = IRAM 0x08 = current clock state code
0x0096  b4 01 1a     cjne a,#0x1,0x00b3    ; code != 1 -> try 2
; code 1: reply 00 00 00
0x0099  12 0b 25     lcall 0x0b25          ; DPTR = 0xFA18
0x009c  e4           clr a                 ; A = 0
0x009d  f0           movx @dptr,a          ; byte0 = 0x00
0x009e  05 1e        inc 0x1e              ; advance pointer low
0x00a0  e5 1e        mov a,0x1e            ; A = new low byte
0x00a2  70 02        jnz 0x00a6            ; no wrap
0x00a4  05 1d        inc 0x1d              ; carry into pointer high
0x00a6  12 0b 5b     lcall 0x0b5b          ; byte1 = 0x00 (helper writes 0 at 0x1D:A)
0x00a9  05 1e        inc 0x1e              ; advance pointer
0x00ab  e5 1e        mov a,0x1e            ; A = low byte
0x00ad  70 4c        jnz 0x00fb            ; -> write byte2 = 0x00 and finish
0x00af  05 1d        inc 0x1d              ; carry
0x00b1  80 48        sjmp 0x00fb           ; -> finish
0x00b3  e5 08        mov a,0x08            ; reload clock code
0x00b5  b4 02 20     cjne a,#0x2,0x00d8    ; code != 2 -> try 3
; code 2: reply 44 AC 00 = 44100 Hz little-endian
0x00b8  12 0b 25     lcall 0x0b25          ; DPTR = 0xFA18
0x00bb  74 44        mov a,#0x44           ; low byte of 44100 (0x00AC44)
0x00bd  f0           movx @dptr,a          ; byte0 = 0x44
0x00be  05 1e        inc 0x1e              ; advance pointer
0x00c0  e5 1e        mov a,0x1e            ; A = low
0x00c2  70 02        jnz 0x00c6            ; no wrap
0x00c4  05 1d        inc 0x1d              ; carry
0x00c6  f5 82        mov dpl,a             ; DPTR = 0x1D:A (reload pointer inline)
0x00c8  85 1d 83     mov dph,0x1d          ; "
0x00cb  74 ac        mov a,#0xac           ; middle byte of 44100
0x00cd  f0           movx @dptr,a          ; byte1 = 0xAC
0x00ce  05 1e        inc 0x1e              ; advance pointer
0x00d0  e5 1e        mov a,0x1e            ; A = low
0x00d2  70 02        jnz 0x00d6            ; no wrap
0x00d4  05 1d        inc 0x1d              ; carry
0x00d6  80 23        sjmp 0x00fb           ; -> byte2 = 0x00, finish
0x00d8  e5 08        mov a,0x08            ; reload clock code
0x00da  b4 03 23     cjne a,#0x3,0x0100    ; code != 3 -> 0x0100 thunk = stall EP0
; code 3: reply 80 BB 00 = 48000 Hz little-endian
0x00dd  12 0b 25     lcall 0x0b25          ; DPTR = 0xFA18
0x00e0  74 80        mov a,#0x80           ; low byte of 48000 (0x00BB80)
0x00e2  f0           movx @dptr,a          ; byte0 = 0x80
0x00e3  05 1e        inc 0x1e              ; advance pointer
0x00e5  e5 1e        mov a,0x1e            ; A = low
0x00e7  70 02        jnz 0x00eb            ; no wrap
0x00e9  05 1d        inc 0x1d              ; carry
0x00eb  f5 82        mov dpl,a             ; DPTR = 0x1D:A
0x00ed  85 1d 83     mov dph,0x1d          ; "
0x00f0  74 bb        mov a,#0xbb           ; middle byte of 48000
0x00f2  f0           movx @dptr,a          ; byte1 = 0xBB
0x00f3  05 1e        inc 0x1e              ; advance pointer
0x00f5  e5 1e        mov a,0x1e            ; A = low
0x00f7  70 02        jnz 0x00fb            ; no wrap
0x00f9  05 1d        inc 0x1d              ; carry
; --- common tail: third byte + arm reply ---
0x00fb  12 0b 5b     lcall 0x0b5b          ; byte2 = 0x00 (all three rates < 0x010000)
0x00fe  80 03        sjmp 0x0103           ; skip over stall thunk
0x0100  02 02 ef     ljmp 0x02ef           ; thunk_stall_ep0 (also reused by callers 0x070d, 0x05d5, 0x065b)
0x0103  90 ff 6b     mov dptr,#0xff6b      ; DPTR = IEPDCNTX0 (also entered from 0x05ba)
0x0106  74 03        mov a,#0x03           ; count = 3, NAK bit7 clear -> arms 3-byte IN reply
0x0108  02 02 47     ljmp 0x0247           ; tail: IEPDCNTX0=3; clr bit 0x0b; setb bit 0x0c; ret
```

### 5.3 — 0x010b–0x02f2 : standard USB request dispatcher and handlers

```
; --- fcn_010b = usb_std_request_dispatch ---
0x010b  90 ff 29   mov dptr,#0xff29      ; SETPACK+1 = bRequest
0x010e  e0         movx a,@dptr          ; A = bRequest
0x010f  b4 0d 00   cjne a,#0xd,0x0112    ; sets CY = (bRequest < 13)
0x0112  40 03      jc 0x0117             ; bRequest 0..12 -> dispatch
0x0114  02 02 ef   ljmp 0x02ef           ; bRequest >= 13 -> ep0_stall_both
0x0117  90 01 1e   mov dptr,#0x11e       ; base of LJMP jump table below
0x011a  f8         mov r0,a              ; R0 = bRequest
0x011b  28         add a,r0              ; A = 2*bRequest
0x011c  28         add a,r0              ; A = 3*bRequest (each table slot is a 3-byte LJMP)
0x011d  73         jmp @a+dptr           ; dispatch into table
; --- jump table 0x011e-0x0144 (13 executable LJMP slots, bRequest*3) ---
0x011e  02 02 2f   ljmp 0x022f           ; bRequest 0  GET_STATUS        -> std_get_status
0x0121  02 01 45   ljmp 0x0145           ; bRequest 1  CLEAR_FEATURE     -> std_clear_feature
0x0124  02 02 ef   ljmp 0x02ef           ; bRequest 2  (reserved)        -> stall
0x0127  02 02 9b   ljmp 0x029b           ; bRequest 3  SET_FEATURE       -> stall (unsupported)
0x012a  02 02 ef   ljmp 0x02ef           ; bRequest 4  (reserved)        -> stall
0x012d  02 02 4d   ljmp 0x024d           ; bRequest 5  SET_ADDRESS       -> std_set_address
0x0130  02 01 77   ljmp 0x0177           ; bRequest 6  GET_DESCRIPTOR    -> std_get_descriptor
0x0133  02 02 9b   ljmp 0x029b           ; bRequest 7  SET_DESCRIPTOR    -> stall (unsupported)
0x0136  02 01 5c   ljmp 0x015c           ; bRequest 8  GET_CONFIGURATION -> std_get_configuration
0x0139  02 02 59   ljmp 0x0259           ; bRequest 9  SET_CONFIGURATION -> std_set_configuration
0x013c  02 01 ed   ljmp 0x01ed           ; bRequest 10 GET_INTERFACE     -> std_get_interface
0x013f  02 02 9d   ljmp 0x029d           ; bRequest 11 SET_INTERFACE     -> std_set_interface
0x0142  02 02 9b   ljmp 0x029b           ; bRequest 12 SYNCH_FRAME       -> stall (unsupported)
; --- std_clear_feature (bRequest=1) ---
0x0145  90 ff 28   mov dptr,#0xff28      ; SETPACK+0 = bmRequestType
0x0148  e0         movx a,@dptr          ; A = bmRequestType
0x0149  64 02      xrl a,#0x2            ; == 0x02 (OUT, standard, endpoint recipient)?
0x014b  70 0c      jnz 0x0159            ; no -> stall
0x014d  90 ff 2c   mov dptr,#0xff2c      ; SETPACK+4 = wIndexL (endpoint number)
0x0150  e0         movx a,@dptr          ; A = wIndexL
0x0151  70 06      jnz 0x0159            ; only EP0 (wIndex==0) supported; others -> stall
0x0153  12 0b 3e   lcall 0x0b3e          ; -> fcn_0b3e: clear STALL bit3, both EP0 dirs
0x0156  02 02 e8   ljmp 0x02e8           ; -> ep0_done_no_data
0x0159  02 02 ef   ljmp 0x02ef           ; -> ep0_stall_both
; --- std_get_configuration (bRequest=8) ---
0x015c  12 0b 37   lcall 0x0b37          ; ep0_cursor = 0xFA18
0x015f  30 0e 08   jnb 0x0e,0x016a       ; bit 0x0e = configured flag; if not configured, return 0
0x0162  12 0b 25   lcall 0x0b25          ; DPTR = ep0_cursor (0xFA18)
0x0165  74 01      mov a,#0x1            ; response byte = 1 (current configuration)
0x0167  f0         movx @dptr,a          ; store into EP0 IN buffer
0x0168  80 05      sjmp 0x016f           ; join
0x016a  12 0b 25   lcall 0x0b25          ; DPTR = ep0_cursor
0x016d  e4         clr a                 ; response byte = 0 (unconfigured)
0x016e  f0         movx @dptr,a          ; store into EP0 IN buffer
0x016f  90 ff 6b   mov dptr,#0xff6b      ; IEPDCNTX0 (also jumped to from class-request path 0x007a/0x0083)
0x0172  74 01      mov a,#0x1            ; byte count = 1, NAK bit clear
0x0174  02 02 47   ljmp 0x0247           ; -> ep0_arm_in_and_done
; --- std_get_descriptor (bRequest=6) ---
0x0177  90 ff 2b   mov dptr,#0xff2b      ; SETPACK+3 = wValueH (descriptor type)
0x017a  e0         movx a,@dptr          ; A = descriptor type
0x017b  b4 01 08   cjne a,#0x1,0x0186    ; type 1 = DEVICE?
0x017e  75 19 05   mov 0x19,#0x5         ; desc_ptr hi = 0x05
0x0181  75 1a 7d   mov 0x1a,#0x7d        ; desc_ptr = 0x057D (device descriptor)
0x0184  80 51      sjmp 0x01d7           ; -> length = descriptor[0] (bLength=0x12)
0x0186  90 ff 2b   mov dptr,#0xff2b      ; reload wValueH
0x0189  e0         movx a,@dptr          ; A = descriptor type
0x018a  64 02      xrl a,#0x2            ; type 2 = CONFIGURATION?
0x018c  70 1b      jnz 0x01a9            ; no -> try STRING
0x018e  90 ff 2a   mov dptr,#0xff2a      ; SETPACK+2 = wValueL (config descriptor index)
0x0191  e0         movx a,@dptr          ; A = index
0x0192  70 15      jnz 0x01a9            ; only index 0 exists; nonzero -> STRING check and stalls
0x0194  75 19 06   mov 0x19,#0x6         ; desc_ptr hi = 0x06
0x0197  75 1a 57   mov 0x1a,#0x57        ; desc_ptr = 0x0657 (config descriptor set)
0x019a  12 0b 6e   lcall 0x0b6e          ; DPTR = desc_ptr
0x019d  74 02      mov a,#0x2            ; offset 2 of config descriptor
0x019f  93         movc a,@a+dptr        ; A = wTotalLength low (0x36)
0x01a0  f5 09      mov 0x09,a            ; xfer_len_lo = wTotalLength.lo
0x01a2  74 03      mov a,#0x3            ; offset 3
0x01a4  93         movc a,@a+dptr        ; A = wTotalLength high (0x00)
0x01a5  f5 0b      mov 0x0b,a            ; xfer_len_hi = wTotalLength.hi
0x01a7  80 3d      sjmp 0x01e6           ; -> clamp to wLength and send first chunk
0x01a9  90 ff 2b   mov dptr,#0xff2b      ; reload wValueH
0x01ac  e0         movx a,@dptr          ; A = descriptor type
0x01ad  64 03      xrl a,#0x3            ; type 3 = STRING?
0x01af  70 32      jnz 0x01e3            ; any other type -> stall
0x01b1  90 ff 2a   mov dptr,#0xff2a      ; wValueL = string index
0x01b4  e0         movx a,@dptr          ; A = string index
0x01b5  70 06      jnz 0x01bd            ; index != 0 -> check 1/2
0x01b7  75 19 06   mov 0x19,#0x6         ; string 0 (LANGID array)
0x01ba  75 1a 8d   mov 0x1a,#0x8d        ; desc_ptr = 0x068D (04 03 09 04)
0x01bd  90 ff 2a   mov dptr,#0xff2a      ; reload string index
0x01c0  e0         movx a,@dptr          ; A = index
0x01c1  b4 01 06   cjne a,#0x1,0x01ca    ; index 1 = manufacturer?
0x01c4  75 19 06   mov 0x19,#0x6         ; string 1
0x01c7  75 1a 91   mov 0x1a,#0x91        ; desc_ptr = 0x0691 ("Digidesign Inc")
0x01ca  90 ff 2a   mov dptr,#0xff2a      ; reload string index
0x01cd  e0         movx a,@dptr          ; A = index
0x01ce  b4 02 06   cjne a,#0x2,0x01d7    ; index 2 = product?
0x01d1  75 19 06   mov 0x19,#0x6         ; string 2
0x01d4  75 1a af   mov 0x1a,#0xaf        ; desc_ptr = 0x06AF ("Mbox USB...")
0x01d7  12 0b 6e   lcall 0x0b6e          ; DPTR = desc_ptr. QUIRK: string index >= 3 leaves desc_ptr STALE
0x01da  e4         clr a                 ; offset 0
0x01db  93         movc a,@a+dptr        ; A = descriptor[0] = bLength
0x01dc  f5 09      mov 0x09,a            ; xfer_len_lo = bLength
0x01de  e4         clr a                 ; 0
0x01df  f5 0b      mov 0x0b,a            ; xfer_len_hi = 0
0x01e1  80 03      sjmp 0x01e6           ; -> send
0x01e3  02 02 ef   ljmp 0x02ef           ; unsupported descriptor type -> stall EP0
0x01e6  12 0d 9e   lcall 0x0d9e          ; fcn_0d9e: clamp xfer_len to wLength; bit 0x0d = host asked for more than we have
0x01e9  12 0b 63   lcall 0x0b63          ; fcn_0b63: copy first <=8 bytes -> 0xFA18, arm EP0 IN; sets bit 0x0b if more chunks
0x01ec  22         ret                   ; continuation chunks sent from EP0-IN-ACK path (0x0f91)
; --- std_get_interface (bRequest=10) ---
0x01ed  20 0a 03   jb 0x0a,0x01f3        ; bit 0x0a: dead flag (never set) — always falls through
0x01f0  30 0e 39   jnb 0x0e,0x022c       ; must be configured, else stall
0x01f3  90 ff 2c   mov dptr,#0xff2c      ; SETPACK+4 = wIndexL (interface number)
0x01f6  e0         movx a,@dptr          ; A = wIndexL
0x01f7  d3         setb cy               ; prepare A - 2 - 1
0x01f8  94 02      subb a,#0x2           ; A = wIndexL - 3
0x01fa  50 30      jnc 0x022c            ; wIndexL >= 3 -> stall (interfaces 0..2 exist)
0x01fc  12 0b 37   lcall 0x0b37          ; ep0_cursor = 0xFA18 (does not touch DPTR)
0x01ff  e0         movx a,@dptr          ; DPTR still 0xFF2C: A = wIndexL again
0x0200  b4 01 0b   cjne a,#0x1,0x020e    ; interface 1?
0x0203  30 08 08   jnb 0x08,0x020e       ; and its alt flag set?
0x0206  12 0b 25   lcall 0x0b25          ; DPTR = 0xFA18
0x0209  74 01      mov a,#0x1            ; response: bAlternateSetting = 1
0x020b  f0         movx @dptr,a          ; write to EP0 IN buffer
0x020c  80 17      sjmp 0x0225           ; join
0x020e  90 ff 2c   mov dptr,#0xff2c      ; reload wIndexL
0x0211  e0         movx a,@dptr          ; A = wIndexL
0x0212  b4 02 0b   cjne a,#0x2,0x0220    ; interface 2?
0x0215  30 09 08   jnb 0x09,0x0220       ; and its alt flag set?
0x0218  12 0b 25   lcall 0x0b25          ; DPTR = 0xFA18
0x021b  74 02      mov a,#0x2            ; response byte = 2 (sic — not 1; mirrors class path 0x0077)
0x021d  f0         movx @dptr,a          ; write to EP0 IN buffer
0x021e  80 05      sjmp 0x0225           ; join
0x0220  12 0b 25   lcall 0x0b25          ; DPTR = 0xFA18
0x0223  e4         clr a                 ; response: alt setting 0
0x0224  f0         movx @dptr,a          ; write to EP0 IN buffer
0x0225  90 ff 6b   mov dptr,#0xff6b      ; IEPDCNTX0
0x0228  74 01      mov a,#0x1            ; 1-byte response
0x022a  80 1b      sjmp 0x0247           ; -> ep0_arm_in_and_done
0x022c  02 02 ef   ljmp 0x02ef           ; -> ep0_stall_both
; --- std_get_status (bRequest=0) — returns 0x0000 for ALL recipients ---
0x022f  12 0b 37   lcall 0x0b37          ; ep0_cursor = 0xFA18
0x0232  12 0b 25   lcall 0x0b25          ; DPTR = 0xFA18
0x0235  e4         clr a                 ; status low byte = 0
0x0236  f0         movx @dptr,a          ; write byte 0
0x0237  05 1e      inc 0x1e              ; ep0_cursor.lo++
0x0239  e5 1e      mov a,0x1e            ; A = new cursor low
0x023b  70 02      jnz 0x023f            ; carry into high byte on wrap
0x023d  05 1d      inc 0x1d              ; ep0_cursor.hi++
0x023f  12 0b 5b   lcall 0x0b5b          ; fcn_0b5b: status high byte = 0
0x0242  90 ff 6b   mov dptr,#0xff6b      ; IEPDCNTX0
0x0245  74 02      mov a,#0x2            ; 2-byte response
; --- ep0_arm_in_and_done (common tail; also entered from fcn_0103 with A=3) ---
0x0247  f0         movx @dptr,a          ; SFR WRITE: IEPDCNTX0 = A (1/2/3), NAK clear = EP0 IN armed
0x0248  c2 0b      clr 0x0b              ; ep0_more_data = 0 (single-packet response)
0x024a  d2 0c      setb 0x0c             ; ep0_data_loaded = 1
0x024c  22         ret                   ; back to SETUP dispatcher's caller
; --- std_set_address (bRequest=5) ---
0x024d  75 0d 05   mov 0x0d,#0x5         ; ctl_state = 5 = "address pending"
0x0250  90 ff 2a   mov dptr,#0xff2a      ; SETPACK+2 = wValueL = new device address
0x0253  e0         movx a,@dptr          ; A = new address
0x0254  f5 0e      mov 0x0e,a            ; pending_addr = wValueL
0x0256  02 02 e8   ljmp 0x02e8           ; -> ep0_done_no_data
; --- std_set_configuration (bRequest=9) ---
0x0259  90 ff 2a   mov dptr,#0xff2a      ; wValueL = configuration value
0x025c  e0         movx a,@dptr          ; A = config value
0x025d  d3         setb cy               ; prepare A - 1 - 1
0x025e  94 01      subb a,#0x1           ; A = config - 2; borrow iff config <= 1
0x0260  40 03      jc 0x0265             ; config 0 or 1 -> accept
0x0262  02 02 ef   ljmp 0x02ef           ; config >= 2 -> stall
0x0265  90 ff 2a   mov dptr,#0xff2a      ; reload config value
0x0268  e0         movx a,@dptr          ; A = config
0x0269  70 08      jnz 0x0273            ; config 0?
0x026b  c2 0a      clr 0x0a              ; clear dead flag 0x0a
0x026d  c2 0e      clr 0x0e              ; configured = 0
0x026f  c2 08      clr 0x08              ; if1_alt = 0
0x0271  c2 09      clr 0x09              ; if2_alt = 0
0x0273  90 ff 2a   mov dptr,#0xff2a      ; reload config value
0x0276  e0         movx a,@dptr          ; A = config
0x0277  b4 01 08   cjne a,#0x1,0x0282    ; config 1?
0x027a  c2 0a      clr 0x0a              ; clear dead flag
0x027c  d2 0e      setb 0x0e             ; configured = 1
0x027e  c2 08      clr 0x08              ; alt settings reset to 0 per spec
0x0280  c2 09      clr 0x09              ;
0x0282  90 ff 2a   mov dptr,#0xff2a      ; DEAD CODE from here to 0x028f: config==2 unreachable (gate at 0x0260)
0x0285  e0         movx a,@dptr          ; (dead) A = config
0x0286  b4 02 08   cjne a,#0x2,0x0291    ; (dead) config 2?
0x0289  c2 0a      clr 0x0a              ; (dead)
0x028b  d2 0e      setb 0x0e             ; (dead)
0x028d  c2 08      clr 0x08              ; (dead)
0x028f  c2 09      clr 0x09              ; (dead)
0x0291  75 0a 01   mov 0x0a,#0x1         ; pending_action = 1: main loop applies the configuration
0x0294  74 80      mov a,#0x80           ; NAK bit (EPDCNT bit7)
0x0296  12 0b 2e   lcall 0x0b2e          ; SFR WRITES via helper: IEPDCNTX0 = 0x80 and OEPDCNTX0 = 0x80
0x0299  80 4d      sjmp 0x02e8           ; -> ep0_done_no_data
; --- std_stall_unsupported (bRequest 3 / 7 / 12) ---
0x029b  80 52      sjmp 0x02ef           ; SET_FEATURE / SET_DESCRIPTOR / SYNCH_FRAME -> stall EP0
; --- std_set_interface (bRequest=11) ---
0x029d  90 ff 2c   mov dptr,#0xff2c      ; wIndexL = interface number
0x02a0  e0         movx a,@dptr          ; A = wIndexL
0x02a1  d3         setb cy               ; prepare A - 2 - 1
0x02a2  94 02      subb a,#0x2           ; A = wIndexL - 3; borrow iff <= 2
0x02a4  50 47      jnc 0x02ed            ; wIndexL >= 3 -> stall
0x02a6  90 ff 2a   mov dptr,#0xff2a      ; wValueL = alternate setting
0x02a9  e0         movx a,@dptr          ; A = alt
0x02aa  94 01      subb a,#0x1           ; CY still 1 (borrow from 0x02a2): A = alt - 2; borrow iff alt <= 1
0x02ac  50 3f      jnc 0x02ed            ; alt >= 2 -> stall (only alts 0/1 exist)
0x02ae  20 0a 03   jb 0x0a,0x02b4        ; dead flag 0x0a (never set) — always falls through
0x02b1  30 0e 39   jnb 0x0e,0x02ed       ; must be configured, else stall
0x02b4  90 ff 2c   mov dptr,#0xff2c      ; reload wIndexL
0x02b7  e0         movx a,@dptr          ; A = interface number
0x02b8  b4 01 0d   cjne a,#0x1,0x02c8    ; interface 1 (audio streaming)?
0x02bb  90 ff 2a   mov dptr,#0xff2a      ; reload alt
0x02be  e0         movx a,@dptr          ; A = alt (0 or 1)
0x02bf  24 ff      add a,#0xff           ; CY = (alt >= 1)
0x02c1  92 08      mov 0x08,cy           ; if1_alt = (alt == 1)
0x02c3  75 0a 02   mov 0x0a,#0x2         ; pending_action = 2: main loop applies iface1 alt setting
0x02c6  80 16      sjmp 0x02de           ; -> NAK EP0 and finish
0x02c8  90 ff 2c   mov dptr,#0xff2c      ; reload wIndexL
0x02cb  e0         movx a,@dptr          ; A = interface number
0x02cc  b4 02 0d   cjne a,#0x2,0x02dc    ; interface 2?
0x02cf  90 ff 2a   mov dptr,#0xff2a      ; reload alt
0x02d2  e0         movx a,@dptr          ; A = alt
0x02d3  24 ff      add a,#0xff           ; CY = (alt >= 1)
0x02d5  92 09      mov 0x09,cy           ; if2_alt = (alt == 1)
0x02d7  75 0a 03   mov 0x0a,#0x3         ; pending_action = 3: main loop applies iface2 alt setting
0x02da  80 02      sjmp 0x02de           ; -> NAK EP0 and finish
0x02dc  80 11      sjmp 0x02ef           ; interface 0 (or unmatched) -> stall
0x02de  90 ff 6b   mov dptr,#0xff6b      ; IEPDCNTX0
0x02e1  74 80      mov a,#0x80           ; NAK bit
0x02e3  f0         movx @dptr,a          ; SFR WRITE: IEPDCNTX0 = 0x80 — EP0 IN held NAKing
0x02e4  90 ff ab   mov dptr,#0xffab      ; OEPDCNTX0
0x02e7  f0         movx @dptr,a          ; SFR WRITE: OEPDCNTX0 = 0x80 — status stage waits for main loop
; --- ep0_done_no_data (shared tail; also from 0x005e/0x0156/0x0256/0x0299) ---
0x02e8  c2 0b      clr 0x0b              ; ep0_more_data = 0
0x02ea  c2 0c      clr 0x0c              ; ep0_data_loaded = 0 (no data stage)
0x02ec  22         ret                   ;
0x02ed  80 00      sjmp 0x02ef           ; stall trampoline for the JNC branches above
; --- fcn_02ef = ep0_stall_both ---
0x02ef  12 10 01   lcall 0x1001          ; fcn_1001: IEPCNF0 |= 0x08, OEPCNF0 |= 0x08 — set STALL on EP0 IN and OUT
0x02f2  22         ret                   ; end of range
```

### 5.4 — 0x02f3–0x0566 : usb_deferred_action_dispatch

Reminder (§2.5.2): cases 4/5 input-select polarity — case 4 (`clr 0x2c`) = analog,
case 5 (`setb 0x2c`) = S/PDIF; the "likely S/PDIF/analog" comments below are the
annotator's reversed guess, superseded by the reconciliation note.

```
; --- entry + jump table ---
0x02f3  e50a      mov a,0x0a         ; A = pending-action code (IRAM byte 0x0a)
0x02f5  14        dec a              ; convert 1..14 to table index 0..13
0x02f6  b40e00    cjne a,#0xe,0x02f9 ; compare index with 14: sets CY if index < 14
0x02f9  4003      jc 0x02fe          ; index 0..13 -> valid, go dispatch
0x02fb  020563    ljmp 0x0563        ; code 0 or >14: no valid action -> clear pending byte and return
0x02fe  90030c    mov dptr,#0x30c    ; DPTR = base of LJMP jump table at 0x030c
0x0301  75f003    mov b,#0x3         ; each table entry is a 3-byte LJMP
0x0304  a4        mul ab             ; A:B = index*3
0x0305  c583      xch a,dph          ; 16-bit table-offset idiom:
0x0307  25f0      add a,b            ;   DPH += high byte of index*3
0x0309  c583      xch a,dph          ;   A = low byte again
0x030b  73        jmp @a+dptr        ; jump into LJMP row below
0x030c  020336    ljmp 0x0336        ; case 1  -> deferred SET_CONFIGURATION
0x030f  02038a    ljmp 0x038a        ; case 2  -> deferred SET_INTERFACE, interface 1 (record path)
0x0312  0203fd    ljmp 0x03fd        ; case 3  -> deferred SET_INTERFACE, interface 2 (playback path)
0x0315  02045a    ljmp 0x045a        ; case 4  -> input-select: clear 0x25.4 (analog — see §2.5.2)
0x0318  020469    ljmp 0x0469        ; case 5  -> input-select: set 0x25.4 (S/PDIF — see §2.5.2)
0x031b  020478    ljmp 0x0478        ; case 6  -> apply audio mode 1
0x031e  02047d    ljmp 0x047d        ; case 7  -> apply audio mode 2 + CS8427 reg writes
0x0321  02049f    ljmp 0x049f        ; case 8  -> apply audio mode 3 + CS8427 reg writes
0x0324  0204c0    ljmp 0x04c0        ; case 9  -> apply audio mode 4
0x0327  0204c4    ljmp 0x04c4        ; case 10 -> apply audio mode 5
0x032a  0204c8    ljmp 0x04c8        ; case 11 -> mode 3 + CS8427 reg4=0x41 + EEPROM 0x1FFF write-probe
0x032d  020478    ljmp 0x0478        ; case 12 -> apply audio mode 1 (same target as case 6)
0x0330  020517    ljmp 0x0517        ; case 13 -> EEPROM[0x0000]=0 (likely DFU-trigger) + re-arm EP0 OUT
0x0333  020525    ljmp 0x0525        ; case 14 -> USB suspend (PCON.IDL) + reconnect on resume
; --- case 1 @ 0x0336 — deferred SET_CONFIGURATION ---
0x0336  120ff2    lcall 0x0ff2       ; -> fcn_0ff2: DMACTL0 &= 0x7F — stop DMA ch0 (playback)
0x0339  90ffee    mov dptr,#0xffee   ; DPTR -> DMACTL1
0x033c  e0        movx a,@dptr       ; read DMACTL1
0x033d  547f      anl a,#0x7f        ; clear bit7 DMAEN
0x033f  f0        movx @dptr,a       ; stop DMA ch1 (record stream)
0x0340  90ffb1    mov dptr,#0xffb1   ; DPTR -> GLOBCTL
0x0343  e0        movx a,@dptr       ; read GLOBCTL
0x0344  54fe      anl a,#0xfe        ; clear bit0 CPTEN
0x0346  f0        movx @dptr,a       ; C-port off during reconfig
0x0347  200a03    jb 0x0a,0x034d     ; config 1 active? -> check interface alts
0x034a  300e1f    jnb 0x0e,0x036c    ; neither config active -> 0x036c
0x034d  20081c    jb 0x08,0x036c     ; iface1 alt != 0 -> skip C-port reprogram
0x0350  200919    jb 0x09,0x036c     ; iface2 alt != 0 -> skip
0x0353  300a05    jnb 0x0a,0x035b    ; config 1 not active -> skip its C-port value
0x0356  74ac      mov a,#0xac        ; CPTCNF3 value for config 1
0x0358  120fe2    lcall 0x0fe2       ; -> fcn_0fe2: CPTCNF3 = 0xAC, CPTRXCNF3 = 0xAC
0x035b  300e05    jnb 0x0e,0x0363    ; config 2 not active -> skip
0x035e  74a8      mov a,#0xa8        ; CPTCNF3 value for config 2
0x0360  120fe2    lcall 0x0fe2       ; -> fcn_0fe2: CPTCNF3 = 0xA8, CPTRXCNF3 = 0xA8
0x0363  202e21    jb 0x2e,0x0387     ; teardown already done (0x25.6 latch)? -> skip
0x0366  1209b6    lcall 0x09b6       ; -> fcn_09b6 (audio_hw_bringup; §2.5.3)
0x0369  02044e    ljmp 0x044e        ; -> shared tail: ACK EP0 status stage
0x036c  200a03    jb 0x0a,0x0372     ; config1 active?
0x036f  300e06    jnb 0x0e,0x0378    ; neither config -> R6:R7 = 0
0x0372  7e00      mov r6,#0x0        ; some config active:
0x0374  7f01      mov r7,#0x1        ;   R6:R7 = 0:1 (configured boolean)
0x0376  8004      sjmp 0x037c        ; join
0x0378  7e00      mov r6,#0x0        ; not configured:
0x037a  7f00      mov r7,#0x0        ;   R6:R7 = 0:0
0x037c  ef        mov a,r7           ; test (R6:R7) == 1
0x037d  6401      xrl a,#0x1         ; A = R7 ^ 1
0x037f  4e        orl a,r6           ; A |= R6 -> zero iff R6:R7 == 0:1
0x0380  6005      jz 0x0387          ; configured -> leave teardown latch alone
0x0382  c22e      clr 0x2e           ; unconfigured: clear teardown-done latch
0x0384  120e56    lcall 0x0e56       ; -> fcn_0e56: clock image byte 0x23 out
0x0387  02044e    ljmp 0x044e        ; -> shared tail: ACK EP0 status stage
; --- case 2 @ 0x038a — deferred SET_INTERFACE, interface 1 (record) ---
0x038a  200a03    jb 0x0a,0x0390     ; config 1 active? -> continue
0x038d  300e4a    jnb 0x0e,0x03da    ; neither config -> alt-0/teardown branch
0x0390  300847    jnb 0x08,0x03da    ; iface1 alt == 0 -> teardown branch
0x0393  202e03    jb 0x2e,0x0399     ; teardown latch already set? -> skip
0x0396  1209b6    lcall 0x09b6       ; -> fcn_09b6 (audio_hw_bringup)
0x0399  c22d      clr 0x2d           ; clear status bit 0x25.5 — purpose UNKNOWN
0x039b  7522ff    mov 0x22,#0xff     ; shift image A := 0xFF
0x039e  c210      clr 0x10           ; clear line 0x22.0 — wiring UNKNOWN
0x03a0  c213      clr 0x13           ; clear line 0x22.3 — wiring UNKNOWN
0x03a2  c21e      clr 0x1e           ; clear line 0x23.6 — wiring UNKNOWN
0x03a4  c217      clr 0x17           ; clear line 0x22.7 (raised in alt-0 branch below)
0x03a6  120efc    lcall 0x0efc       ; -> fcn_0efc: clock image byte 0x22 out
0x03a9  c228      clr 0x28           ; clear status 0x25.0
0x03ab  c229      clr 0x29           ; clear status 0x25.1
0x03ad  c22a      clr 0x2a           ; clear status 0x25.2
0x03af  c22b      clr 0x2b           ; clear status 0x25.3
0x03b1  c22c      clr 0x2c           ; clear input-select 0x25.4 (default source)
0x03b3  120e56    lcall 0x0e56       ; -> fcn_0e56: clock image byte 0x23 out
0x03b6  90ff60    mov dptr,#0xff60   ; DPTR -> IEPCNF1 (IN endpoint 1 config)
0x03b9  74c5      mov a,#0xc5        ; 0xC5 = IEPEN|ISO, 6 bytes/sample (24-bit stereo)
0x03bb  f0        movx @dptr,a       ; enable EP1 IN isochronous — the record endpoint
0x03bc  7f03      mov r7,#0x3        ; mode argument 3
0x03be  12070f    lcall 0x070f       ; -> fcn_070f(3): apply audio clock/path mode 3
0x03c1  90ffee    mov dptr,#0xffee   ; DPTR -> DMACTL1
0x03c4  e0        movx a,@dptr       ; read
0x03c5  4480      orl a,#0x80        ; set DMAEN
0x03c7  f0        movx @dptr,a       ; start DMA channel 1 -> EP1 IN (record)
0x03c8  300e2a    jnb 0x0e,0x03f5    ; config 2 not active -> skip playback side
0x03cb  90ff98    mov dptr,#0xff98   ; DPTR -> OEPCNF2 (OUT endpoint 2 config)
0x03ce  74c5      mov a,#0xc5        ; OEPEN|ISO, 6 bytes/sample
0x03d0  f0        movx @dptr,a       ; enable EP2 OUT isochronous — playback endpoint
0x03d1  90ffe8    mov dptr,#0xffe8   ; DPTR -> DMACTL0
0x03d4  e0        movx a,@dptr       ; read
0x03d5  4480      orl a,#0x80        ; set DMAEN
0x03d7  f0        movx @dptr,a       ; start DMA channel 0 -> EP2 OUT (playback)
0x03d8  801b      sjmp 0x03f5        ; join: unmask USB interrupts
0x03da  200a03    jb 0x0a,0x03e0     ; (alt-0 branch) config1 active? -> teardown
0x03dd  300e15    jnb 0x0e,0x03f5    ; unconfigured -> just unmask + ACK
0x03e0  200812    jb 0x08,0x03f5     ; alt actually 1 -> skip
0x03e3  90ffee    mov dptr,#0xffee   ; DPTR -> DMACTL1
0x03e6  e0        movx a,@dptr       ; read
0x03e7  547f      anl a,#0x7f        ; clear DMAEN
0x03e9  f0        movx @dptr,a       ; stop DMA channel 1 (record off)
0x03ea  d217      setb 0x17          ; raise control line 0x22.7
0x03ec  120efc    lcall 0x0efc       ; clock image 0x22 out
0x03ef  300e03    jnb 0x0e,0x03f5    ; only under config 2:
0x03f2  120ff2    lcall 0x0ff2       ;   -> fcn_0ff2: DMACTL0 &= 0x7F — stop playback DMA too
0x03f5  90fffd    mov dptr,#0xfffd   ; DPTR -> USBIMSK
0x03f8  74ff      mov a,#0xff        ; all sources
0x03fa  f0        movx @dptr,a       ; USBIMSK = 0xFF — enable every USB interrupt incl. SOF/PSOF
0x03fb  8051      sjmp 0x044e        ; -> shared tail: ACK EP0 status stage
; --- case 3 @ 0x03fd — deferred SET_INTERFACE, interface 2 (playback) ---
0x03fd  300e04    jnb 0x0e,0x0404    ; R7 = (config2 active) ? 1 : 0
0x0400  7f01      mov r7,#0x1        ;   config2 -> 1
0x0402  8002      sjmp 0x0406        ;   join
0x0404  7f00      mov r7,#0x0        ;   else 0
0x0406  300a04    jnb 0x0a,0x040d    ; R6 = (config1 active) ? 1 : 0
0x0409  7e01      mov r6,#0x1        ;   config1 -> 1
0x040b  8002      sjmp 0x040f        ;   join
0x040d  7e00      mov r6,#0x0        ;   else 0
0x040f  ee        mov a,r6           ; A = R6
0x0410  4f        orl a,r7           ; A = configured-at-all boolean
0x0411  601f      jz 0x0432          ; not configured -> alt-0/teardown check
0x0413  30091c    jnb 0x09,0x0432    ; iface2 alt == 0 -> teardown check
0x0416  202e03    jb 0x2e,0x041c     ; teardown latch set? -> skip
0x0419  1209b6    lcall 0x09b6       ; -> fcn_09b6 (audio_hw_bringup)
0x041c  c22d      clr 0x2d           ; clear status bit 0x25.5 — purpose UNKNOWN
0x041e  90ff98    mov dptr,#0xff98   ; DPTR -> OEPCNF2
0x0421  74c5      mov a,#0xc5        ; OEPEN|ISO, 6 bytes/sample
0x0423  f0        movx @dptr,a       ; enable EP2 OUT isochronous (playback)
0x0424  7f03      mov r7,#0x3        ; mode argument 3
0x0426  12070f    lcall 0x070f       ; -> fcn_070f(3): apply audio clock/path mode 3
0x0429  90ffe8    mov dptr,#0xffe8   ; DPTR -> DMACTL0
0x042c  e0        movx a,@dptr       ; read
0x042d  4480      orl a,#0x80        ; set DMAEN
0x042f  f0        movx @dptr,a       ; start DMA channel 0 (playback)
0x0430  801c      sjmp 0x044e        ; -> shared tail: ACK EP0
0x0432  300e04    jnb 0x0e,0x0439    ; (repeat configured-boolean computation)
0x0435  7f01      mov r7,#0x1        ;   R7 = config2 ? 1 : 0
0x0437  8002      sjmp 0x043b        ;   join
0x0439  7f00      mov r7,#0x0        ;   else 0
0x043b  300a04    jnb 0x0a,0x0442    ;   R6 = config1 ? 1 : 0
0x043e  7e01      mov r6,#0x1        ;   config1 -> 1
0x0440  8002      sjmp 0x0444        ;   join
0x0442  7e00      mov r6,#0x0        ;   else 0
0x0444  ee        mov a,r6           ; A = R6
0x0445  4f        orl a,r7           ; configured?
0x0446  6006      jz 0x044e          ; unconfigured -> just ACK
0x0448  200903    jb 0x09,0x044e     ; alt is 1 -> nothing to stop, ACK
0x044b  120ff2    lcall 0x0ff2       ; alt 0 while configured: DMACTL0 &= 0x7F — stop playback DMA
; --- shared tail @ 0x044e — ACK the EP0 status stage ---
0x044e  90ff6b    mov dptr,#0xff6b   ; DPTR -> IEPDCNTX0
0x0451  e4        clr a              ; A = 0
0x0452  f0        movx @dptr,a       ; IEPDCNTX0 = 0: ZLP = status-stage ACK
0x0453  90ffab    mov dptr,#0xffab   ; DPTR -> OEPDCNTX0
0x0456  f0        movx @dptr,a       ; OEPDCNTX0 = 0: re-arm EP0 OUT
0x0457  020563    ljmp 0x0563        ; -> epilogue
; --- cases 4/5 @ 0x045a / 0x0469 — input-source select (see §2.5.2) ---
0x045a  c22c      clr 0x2c           ; clear input-select 0x25.4 (analog)
0x045c  d216      setb 0x16          ; raise control line 0x22.6
0x045e  120e56    lcall 0x0e56       ; clock image byte 0x23 out
0x0461  120efc    lcall 0x0efc       ; clock image byte 0x22 out
0x0464  af08      mov r7,0x08        ; R7 = IRAM byte 0x08 = current audio mode index
0x0466  020512    ljmp 0x0512        ; -> apply-mode tail
0x0469  d22c      setb 0x2c          ; set input-select 0x25.4 (S/PDIF)
0x046b  c216      clr 0x16           ; drop control line 0x22.6
0x046d  120e56    lcall 0x0e56       ; clock image byte 0x23 out
0x0470  120efc    lcall 0x0efc       ; clock image byte 0x22 out
0x0473  7f01      mov r7,#0x1        ; mode 1 (analog default mode)
0x0475  020512    ljmp 0x0512        ; -> apply-mode tail
; --- cases 6/12 @ 0x0478, 7 @ 0x047d, 8 @ 0x049f, 9 @ 0x04c0, 10 @ 0x04c4 ---
0x0478  7f01      mov r7,#0x1        ; cases 6 & 12: mode 1
0x047a  020512    ljmp 0x0512        ; -> apply-mode tail (fcn_070f(1))
0x047d  7f02      mov r7,#0x2        ; case 7: mode 2
0x047f  12070f    lcall 0x070f       ; -> fcn_070f(2)
0x0482  302c09    jnb 0x2c,0x048e    ; input-select 0x25.4 clear -> other CS8427 values
0x0485  120567    lcall 0x0567       ; -> fcn_0567: CS8427[0x04] = 0x41
0x0488  120ffa    lcall 0x0ffa       ; -> fcn_0ffa: (0x2c,0x2d) = (0x12, 0x00)
0x048b  020509    ljmp 0x0509        ; -> CS8427-write tail: CS8427[0x12] = 0x00
0x048e  752c23    mov 0x2c,#0x23     ; param: CS8427 register 0x23
0x0491  e4        clr a              ; A = 0
0x0492  f52d      mov 0x2d,a         ; param value 0x00
0x0494  120575    lcall 0x0575       ; -> fcn_0575: CS8427[0x23] = 0x00
0x0497  752c24    mov 0x2c,#0x24     ; param: CS8427 register 0x24
0x049a  752d80    mov 0x2d,#0x80     ; param value 0x80
0x049d  806a      sjmp 0x0509        ; -> CS8427-write tail: CS8427[0x24] = 0x80
0x049f  7f03      mov r7,#0x3        ; case 8: mode 3
0x04a1  12070f    lcall 0x070f       ; -> fcn_070f(3)
0x04a4  302c08    jnb 0x2c,0x04af    ; input-select clear -> S/PDIF-side values
0x04a7  120567    lcall 0x0567       ; CS8427[0x04] = 0x41
0x04aa  120ffa    lcall 0x0ffa       ; param pair := (0x12, 0x00)
0x04ad  800f      sjmp 0x04be        ; -> join -> CS8427-write tail
0x04af  752c23    mov 0x2c,#0x23     ; param: CS8427 register 0x23
0x04b2  752d40    mov 0x2d,#0x40     ; param value 0x40
0x04b5  120575    lcall 0x0575       ; CS8427[0x23] = 0x40
0x04b8  752c24    mov 0x2c,#0x24     ; param: CS8427 register 0x24
0x04bb  752d80    mov 0x2d,#0x80     ; param value 0x80
0x04be  8049      sjmp 0x0509        ; -> CS8427-write tail: CS8427[0x24] = 0x80
0x04c0  7f04      mov r7,#0x4        ; case 9: mode 4
0x04c2  804e      sjmp 0x0512        ; -> apply-mode tail (fcn_070f(4))
0x04c4  7f05      mov r7,#0x5        ; case 10: mode 5
0x04c6  804a      sjmp 0x0512        ; -> apply-mode tail (fcn_070f(5))
; --- case 11 @ 0x04c8 — mode 3 + CS8427 kick + EEPROM write-probe ---
0x04c8  202e03    jb 0x2e,0x04ce     ; teardown latch set? -> skip
0x04cb  1209b6    lcall 0x09b6       ; -> fcn_09b6 (audio_hw_bringup)
0x04ce  d22d      setb 0x2d          ; set status bit 0x25.5 — purpose UNKNOWN
0x04d0  7f03      mov r7,#0x3        ; mode 3
0x04d2  12070f    lcall 0x070f       ; -> fcn_070f(3)
0x04d5  752c04    mov 0x2c,#0x4      ; param: CS8427 register 0x04
0x04d8  752d41    mov 0x2d,#0x41     ; param value 0x41
0x04db  ad2d      mov r5,0x2d        ; R5 = value 0x41
0x04dd  af2c      mov r7,0x2c        ; R7 = register 0x04
0x04df  120c31    lcall 0x0c31       ; -> fcn_0c31: CS8427[0x04] = 0x41
0x04e2  7dff      mov r5,#0xff       ; EEPROM address low = 0xFF
0x04e4  7f1f      mov r7,#0x1f       ; EEPROM address high = 0x1F -> address 0x1FFF
0x04e6  120d11    lcall 0x0d11       ; -> fcn_0d11: R7 = eeprom_read(0x1FFF)
0x04e9  8f2c      mov 0x2c,r7        ; save original byte
0x04eb  632cff    xrl 0x2c,#0xff     ; complement it in place: 0x2c = ~orig
0x04ee  ab2c      mov r3,0x2c        ; R3 = data to write = ~orig
0x04f0  7f1f      mov r7,#0x1f       ; address high 0x1F again
0x04f2  120bda    lcall 0x0bda       ; -> fcn_0bda: eeprom_write(0x1FFF, ~orig)
0x04f5  7dff      mov r5,#0xff       ; address low = 0xFF (reloaded for readback)
0x04f7  120d11    lcall 0x0d11       ; R7 = eeprom_read(0x1FFF) — read back
0x04fa  8f2d      mov 0x2d,r7        ; save readback
0x04fc  e52d      mov a,0x2d         ; A = readback
0x04fe  b52c02    cjne a,0x2c,0x0503 ; readback != written value (write blocked) -> keep 0x22.6
0x0501  c216      clr 0x16           ; write took -> clear control line 0x22.6 — downstream meaning UNKNOWN
0x0503  120efc    lcall 0x0efc       ; clock image byte 0x22 out
0x0506  120ffa    lcall 0x0ffa       ; param pair := (0x12, 0x00)
; --- shared tails @ 0x0509 / 0x0512 ---
0x0509  ad2d      mov r5,0x2d        ; R5 = value (byte 0x2d)
0x050b  af2c      mov r7,0x2c        ; R7 = register (byte 0x2c)
0x050d  120c31    lcall 0x0c31       ; -> fcn_0c31: CS8427[R7] = R5
0x0510  8051      sjmp 0x0563        ; -> epilogue
0x0512  12070f    lcall 0x070f       ; -> fcn_070f(R7): apply audio clock/path mode R7
0x0515  804c      sjmp 0x0563        ; -> epilogue
; --- case 13 @ 0x0517 — EEPROM[0x0000] := 0 (likely DFU trigger) ---
0x0517  e4        clr a              ; A = 0
0x0518  fb        mov r3,a           ; data = 0x00
0x0519  fd        mov r5,a           ; address low = 0x00
0x051a  ff        mov r7,a           ; address high = 0x00
0x051b  120bda    lcall 0x0bda       ; -> fcn_0bda: eeprom_write(0x0000, 0x00) — clobber header byte 0
0x051e  90ffab    mov dptr,#0xffab   ; DPTR -> OEPDCNTX0
0x0521  e4        clr a              ; A = 0
0x0522  f0        movx @dptr,a       ; OEPDCNTX0 = 0: clear NAK, re-arm EP0 OUT
0x0523  803e      sjmp 0x0563        ; -> epilogue
; --- case 14 @ 0x0525 — USB suspend, then reconnect on resume ---
0x0525  a20e      mov cy,0x0e        ; CY = config2-active flag
0x0527  720a      orl cy,0x0a        ; CY |= config1-active
0x0529  5038      jnc 0x0563         ; not configured -> nothing to suspend
0x052b  90ffe1    mov dptr,#0xffe1   ; DPTR -> ACGCTL
0x052e  e0        movx a,@dptr       ; read
0x052f  543f      anl a,#0x3f        ; clear bit7 MCLKO2EN and bit6 MCLKO1EN
0x0531  f0        movx @dptr,a       ; both MCLK outputs off (suspend power cut)
0x0532  e4        clr a              ; A = 0
0x0533  f525      mov 0x25,a         ; zero audio-path status byte 0x25
0x0535  f523      mov 0x23,a         ; zero shift image B
0x0537  120e56    lcall 0x0e56       ; clock all-zero image 0x23 out
0x053a  7522ff    mov 0x22,#0xff     ; shift image A := 0xFF
0x053d  c21e      clr 0x1e           ; clear line 0x23.6 — UNKNOWN why after 0x23 already zeroed
0x053f  120efc    lcall 0x0efc       ; clock image 0x22 (=0xFF) out
0x0542  438701    orl 0x87,#0x1      ; PCON |= IDL: MCU clock stops; resumes at 0x0545 after wake
0x0545  90fffc    mov dptr,#0xfffc   ; (post-wake) DPTR -> USBCTL
0x0548  e0        movx a,@dptr       ; read USBCTL
0x0549  547f      anl a,#0x7f        ; clear bit7 CONT (pull-up connect)
0x054b  f0        movx @dptr,a       ; disconnect from the bus — begin forced re-enumeration
0x054c  a3        inc dptr           ; DPTR -> 0xFFFD = USBIMSK
0x054d  749f      mov a,#0x9f        ; 0x9F = RSTR|SOF|PSOF|SETUP|resv|STPOW; SUSR/RESR masked
0x054f  f0        movx @dptr,a       ; USBIMSK = 0x9F
0x0550  1207ec    lcall 0x07ec       ; -> fcn_07ec: USB engine / hardware re-init
0x0553  120891    lcall 0x0891       ; -> fcn_0891: EP0 buffer bases
0x0556  d28c      setb 0x8c          ; TCON.4 = TR0: restart Timer 0
0x0558  d2a8      setb 0xa8          ; IE.0 = EX0: re-enable INT0 (USB)
0x055a  d2af      setb 0xaf          ; IE.7 = EA: global interrupt enable
0x055c  90fffc    mov dptr,#0xfffc   ; DPTR -> USBCTL
0x055f  e0        movx a,@dptr       ; read
0x0560  4480      orl a,#0x80        ; set CONT
0x0562  f0        movx @dptr,a       ; reconnect pull-up — host re-enumerates
; --- epilogue @ 0x0563 ---
0x0563  e4        clr a              ; A = 0
0x0564  f50a      mov 0x0a,a         ; pending-action byte := 0 — action consumed
0x0566  22        ret                ; back to main loop
```

### 5.5 — 0x0567–0x057c : CS8427 register-write helpers

```
; --- fcn_0567 = cs8427_write_reg04_val41 ---
0x0567  75 2c 04     mov 0x2c,#0x4         ; cs_reg_shadow := 0x04 (target register)
0x056a  75 2d 41     mov 0x2d,#0x41        ; cs_val_shadow := 0x41 (register meaning UNVERIFIED)
0x056d  ad 2d        mov r5,0x2d           ; R5 := value (0x41) — byte 3 of the bit-banged frame
0x056f  af 2c        mov r7,0x2c           ; R7 := register (0x04) — byte 2 of the frame
0x0571  12 0c 31     lcall 0x0c31          ; -> fcn_0c31: clocks {0x20, R7, R5} MSB-first on P1.4/P1.3
0x0574  22           ret                   ; done
; --- fcn_0575 = cs8427_write_shadowed ---
0x0575  ad 2d        mov r5,0x2d           ; R5 := cs_val_shadow (caller-preloaded value)
0x0577  af 2c        mov r7,0x2c           ; R7 := cs_reg_shadow (caller-preloaded register)
0x0579  12 0c 31     lcall 0x0c31          ; -> fcn_0c31 (same 3-byte frame: 0x20, reg, value)
0x057c  22           ret                   ; done
```

**0x057d–0x070e is DATA** — the USB descriptor block. Decoded field-by-field in §6.1.

### 5.6 — 0x070f–0x07eb : audio_clock_set_mode (fcn_070f, R7 = mode 1/2/3/5)

```
; ---- entry / common preamble ----
0x070f  8f 2e        mov 0x2e,r7           ; save requested clock mode (1/2/3/5) in IRAM 0x2e
0x0711  e4           clr a                 ; A=0 (XREF from 0x06b9 is spurious — string data)
0x0712  f5 2f        mov 0x2f,a            ; delay-counter hi (IRAM 0x2f) = 0
0x0714  f5 30        mov 0x30,a            ; delay-counter lo (IRAM 0x30) = 0
0x0716  c2 1a        clr 0x1a              ; latch byte 0x23 bit2 = 0 — dropped during clock switch (restored 0x07cf)
0x0718  c2 1b        clr 0x1b              ; latch byte 0x23 bit3 = 0 (restored 0x07d1)
0x071a  12 0e 56     lcall 0x0e56          ; -> fcn_0e56: push the two cleared bits to hardware
0x071d  90 ff e2     mov dptr,#0xffe2      ; DPTR = ACG1DCTL
0x0720  12 0e f4     lcall 0x0ef4          ; -> fcn_0ef4: ACG1DCTL=0x10 (÷2) and ACG2DCTL=0x10
; ---- mode dispatch on IRAM 0x2e ----
0x0723  e5 2e        mov a,0x2e            ; A = requested mode
0x0725  24 fe        add a,#0xfe           ; A = mode-2
0x0727  60 1d        jz 0x0746             ; mode==2 -> 44.1 kHz branch
0x0729  14           dec a                 ; A = mode-3
0x072a  60 43        jz 0x076f             ; mode==3 -> 48 kHz branch
0x072c  24 fe        add a,#0xfe           ; A = mode-5
0x072e  60 47        jz 0x0777             ; mode==5 -> external/CPT branch
0x0730  24 04        add a,#0x4            ; A = mode-1
0x0732  70 72        jnz 0x07a6            ; any other mode: skip to common tail
; ---- mode 1: both MCLKOs from external MCLKI ----
0x0734  90 ff e1     mov dptr,#0xffe1      ; DPTR = ACGCTL
0x0737  74 0d        mov a,#0xd            ; 0x0d = MCLKO1S0, DIVEN, MCLKO2S0; outputs disabled
0x0739  f0           movx @dptr,a          ; ACGCTL=0x0d: external-clock mode
0x073a  75 08 01     mov 0x08,#0x1         ; global clock-mode ID = 1
0x073d  75 31 04     mov 0x31,#0x4         ; serial-device register number = 4
0x0740  75 32 41     mov 0x32,#0x41        ; register-4 value = 0x41 (external-clock variant)
0x0743  02 07 a6     ljmp 0x07a6           ; join common tail
; ---- mode 2: internal 44.1 kHz ----
0x0746  90 ff e6     mov dptr,#0xffe6      ; DPTR = ACG1FRQ1
0x0749  74 4b        mov a,#0x4b           ; middle byte of frequency word
0x074b  f0           movx @dptr,a          ; ACG1FRQ1=0x4B
0x074c  90 ff e5     mov dptr,#0xffe5      ; DPTR = ACG1FRQ2 (MSB)
0x074f  74 6a        mov a,#0x6a           ; 0x6A4B20 = 22.579 MHz
0x0751  f0           movx @dptr,a          ; ACG1FRQ2=0x6A
0x0752  90 ff e7     mov dptr,#0xffe7      ; DPTR = ACG1FRQ0 (write latches value)
0x0755  74 20        mov a,#0x20           ; LSB
0x0757  f0           movx @dptr,a          ; ACG1FRQ0=0x20 -> ACG1 = 22.5792 MHz
0x0758  90 ff f8     mov dptr,#0xfff8      ; DPTR = ACG2FRQ1
0x075b  74 4b        mov a,#0x4b           ; same value for second synthesizer
0x075d  f0           movx @dptr,a          ; ACG2FRQ1=0x4B
0x075e  90 ff f7     mov dptr,#0xfff7      ; DPTR = ACG2FRQ2
0x0761  74 6a        mov a,#0x6a           ; MSB
0x0763  f0           movx @dptr,a          ; ACG2FRQ2=0x6A
0x0764  74 20        mov a,#0x20           ; LSB for ACG2 in A (helper writes it)
0x0766  12 0e e8     lcall 0x0ee8          ; -> fcn_0ee8: ACG2FRQ0=0x20 (latch), ACGCTL=0x06
0x0769  75 08 02     mov 0x08,#0x2         ; global clock-mode ID = 2 (44.1 kHz)
0x076c  02 07 a0     ljmp 0x07a0           ; set 0x31=4/0x32=0x40 then common tail
; ---- mode 3: internal 48 kHz ----
0x076f  12 0e c8     lcall 0x0ec8          ; -> fcn_0ec8: ACG1FRQ=ACG2FRQ=0x61A80F (24.576 MHz), ACGCTL=0x06
0x0772  75 08 03     mov 0x08,#0x3         ; global clock-mode ID = 3 (48 kHz)
0x0775  80 29        sjmp 0x07a0           ; set 0x31=4/0x32=0x40 then common tail
; ---- mode 5: reprogram codec-port RX divider + 24.576 MHz ACG ----
0x0777  90 ff b1     mov dptr,#0xffb1      ; DPTR = GLOBCTL
0x077a  e0           movx a,@dptr          ; read GLOBCTL
0x077b  54 fe        anl a,#0xfe           ; clear bit0 CPTEN
0x077d  f0           movx @dptr,a          ; GLOBCTL &= ~CPTEN — codec port disabled
0x077e  90 ff d4     mov dptr,#0xffd4      ; DPTR = CPTRXCNF4
0x0781  74 01        mov a,#0x1            ; DIVB2=1
0x0783  f0           movx @dptr,a          ; CPTRXCNF4=0x01: DIVB2 divide ratio for SCLK2 from MCLKO2
0x0784  90 ff b1     mov dptr,#0xffb1      ; DPTR = GLOBCTL again
0x0787  e0           movx a,@dptr          ; read GLOBCTL
0x0788  44 01        orl a,#0x1            ; set CPTEN
0x078a  12 0e c7     lcall 0x0ec7          ; -> fcn_0ec7: GLOBCTL |= CPTEN, then ACG = 24.576 MHz, ACGCTL=0x06
0x078d  a3           inc dptr              ; DPTR = 0xFFE2 = ACG1DCTL
0x078e  e4           clr a                 ; A=0
0x078f  f0           movx @dptr,a          ; ACG1DCTL=0x00: DIVM=÷1 -> MCLKO1 = full 24.576 MHz
0x0790  90 ff f6     mov dptr,#0xfff6      ; DPTR = ACG2DCTL
0x0793  74 10        mov a,#0x10           ; DIVM=÷2
0x0795  f0           movx @dptr,a          ; ACG2DCTL=0x10 -> MCLKO2 = 12.288 MHz
0x0796  d2 18        setb 0x18             ; latch byte 0x23 bit0 = 1 — mode 5 only; UNKNOWN
0x0798  d2 19        setb 0x19             ; latch byte 0x23 bit1 = 1 — mode 5 only; UNKNOWN
0x079a  12 0e 56     lcall 0x0e56          ; -> fcn_0e56: push latch bytes to shift register
0x079d  75 08 05     mov 0x08,#0x5         ; global clock-mode ID = 5
; ---- shared: stage serial-device write reg4=0x40 (modes 2,3,5) ----
0x07a0  75 31 04     mov 0x31,#0x4         ; serial-device register number = 4
0x07a3  75 32 40     mov 0x32,#0x40        ; register-4 value = 0x40 (internal-clock variant)
; ---- common tail: device write, enable MCLK outs, re-arm audio EPs ----
0x07a6  ad 32        mov r5,0x32           ; R5 = register value
0x07a8  af 31        mov r7,0x31           ; R7 = register number (4)
0x07aa  12 0c 31     lcall 0x0c31          ; -> fcn_0c31: bit-bang reg R7 := R5 to the external device
0x07ad  90 ff e1     mov dptr,#0xffe1      ; DPTR = ACGCTL
0x07b0  e0           movx a,@dptr          ; read ACGCTL
0x07b1  44 c0        orl a,#0xc0           ; set b7 MCLKO2EN + b6 MCLKO1EN
0x07b3  f0           movx @dptr,a          ; ACGCTL |= 0xC0: enable both master-clock output pins
0x07b4  90 ff 63     mov dptr,#0xff63      ; DPTR = IEPDCNTX1
0x07b7  e4           clr a                 ; A=0
0x07b8  f0           movx @dptr,a          ; IEPDCNTX1=0: clear EP1-IN X buffer count/NAK
0x07b9  90 ff 67     mov dptr,#0xff67      ; DPTR = IEPDCNTY1
0x07bc  f0           movx @dptr,a          ; IEPDCNTY1=0
0x07bd  90 ff 9b     mov dptr,#0xff9b      ; DPTR = OEPDCNTX2
0x07c0  f0           movx @dptr,a          ; OEPDCNTX2=0
0x07c1  90 ff 9f     mov dptr,#0xff9f      ; DPTR = OEPDCNTY2
0x07c4  f0           movx @dptr,a          ; OEPDCNTY2=0
0x07c5  90 ff 60     mov dptr,#0xff60      ; DPTR = IEPCNF1
0x07c8  74 c5        mov a,#0xc5           ; EPEN|ISO|BPS=5 (6 bytes/sample)
0x07ca  f0           movx @dptr,a          ; IEPCNF1=0xC5: enable EP1 IN iso (record)
0x07cb  90 ff 98     mov dptr,#0xff98      ; DPTR = OEPCNF2
0x07ce  f0           movx @dptr,a          ; OEPCNF2=0xC5: enable EP2 OUT iso (playback)
0x07cf  d2 1a        setb 0x1a             ; latch byte 0x23 bit2 back to 1 (undo 0x0716)
0x07d1  d2 1b        setb 0x1b             ; latch byte 0x23 bit3 back to 1 (undo 0x0718)
0x07d3  12 0e 56     lcall 0x0e56          ; -> fcn_0e56: push restored latch state
; ---- settle delay: busy-count 0x0000 -> 0x0FFF ----
0x07d6  e4           clr a                 ; A=0
0x07d7  f5 2f        mov 0x2f,a            ; counter hi = 0
0x07d9  f5 30        mov 0x30,a            ; counter lo = 0
0x07db  05 30        inc 0x30              ; ++lo (loop head)
0x07dd  e5 30        mov a,0x30            ; A = lo
0x07df  70 02        jnz 0x07e3            ; no byte carry -> skip hi increment
0x07e1  05 2f        inc 0x2f              ; carry into hi byte
0x07e3  b4 ff f5     cjne a,#0xff,0x07db   ; keep looping until lo==0xFF
0x07e6  e5 2f        mov a,0x2f            ; A = hi
0x07e8  b4 0f f0     cjne a,#0xf,0x07db    ; ...and hi==0x0F -> ~4095 iterations settle delay
0x07eb  22           ret                   ; done — clocks stable, endpoints re-armed
```

### 5.7 — 0x07ec–0x0890 : hw_clock_codec_init (fcn_07ec)

```
0x07ec  e4           clr a                  ; A=0
0x07ed  f52e         mov 0x2e,a             ; delay counter hi = 0
0x07ef  f52f         mov 0x2f,a             ; delay counter lo = 0
0x07f1  90 ff fc     mov dptr,#0xfffc       ; DPTR -> USBCTL
0x07f4  f0           movx @dptr,a           ; USBCTL=0: CONT=0 (disconnect), FEN=0
0x07f5  90 ff b0     mov dptr,#0xffb0       ; DPTR -> MEMCFG
0x07f8  04           inc a                  ; A=1
0x07f9  f0           movx @dptr,a           ; MEMCFG=1: SDW=1 (code stays in shadow RAM)
0x07fa  e4           clr a                  ; A=0 again
0x07fb  f5 90        mov p1,a               ; P1=0x00 (all shift-chain lines low)
0x07fd  75 b0 ff     mov p3,#0xff           ; P3=0xFF (inputs/idle high)
0x0800  75 8c ce     mov th0,#0xce          ; TH0=0xCE: Timer0 reload for the ms tick
0x0803  f5 8a        mov tl0,a              ; TL0=0
0x0805  f5 8d        mov th1,a              ; TH1=0
0x0807  f5 8b        mov tl1,a              ; TL1=0
0x0809  75 89 11     mov tmod,#0x11         ; TMOD=0x11: timer0 and timer1 both 16-bit mode 1
0x080c  f5 88        mov tcon,a             ; TCON=0: timers stopped
0x080e  c2 af        clr ea                 ; IE.7: global interrupt enable OFF during init
0x0810  c2 ac        clr es                 ; IE.4: serial int disabled
0x0812  c2 aa        clr ex1                ; IE.2: external int 1 disabled
0x0814  d2 a9        setb et0               ; IE.1: Timer0 overflow int enabled (ms tick)
0x0816  c2 ab        clr et1                ; IE.3: Timer1 int disabled
0x0818  d2 a8        setb ex0               ; IE.0: INT0 = USB interrupt enabled (masked until EA set)
0x081a  f5 b8        mov ip,a               ; IP=0: all interrupts low priority
0x081c  a3           inc dptr               ; DPTR 0xFFB0 -> 0xFFB1 = GLOBCTL
0x081d  74 06        mov a,#0x6             ; LPWR(0x04) | P3PUDIS(0x02)
0x081f  f0           movx @dptr,a           ; GLOBCTL=0x06: USB blocks powered, P3 pullups off, CPTEN=0
0x0820  90 ff e0     mov dptr,#0xffe0       ; DPTR -> CPTCNF1
0x0823  74 0d        mov a,#0xd             ; NTSL=00001 (2 slots), MODE=101 (I2S mode 5)
0x0825  f0           movx @dptr,a           ; CPTCNF1=0x0D
0x0826  90 ff df     mov dptr,#0xffdf       ; DPTR -> CPTCNF2
0x0829  74 e5        mov a,#0xe5            ; TSL0L=11 (32 clk), BPTSL=100 (24 bits), TSLL=101 (32 clk)
0x082b  f0           movx @dptr,a           ; CPTCNF2=0xE5: 24-bit data in 32-clock slots (64fs)
0x082c  90 ff de     mov dptr,#0xffde       ; DPTR -> CPTCNF3
0x082f  74 ac        mov a,#0xac            ; DDLY|CSCLKP|CSYNCL|BYOR
0x0831  f0           movx @dptr,a           ; CPTCNF3=0xAC: I2S timing, CSCLK/CSYNC master
0x0832  90 ff dd     mov dptr,#0xffdd       ; DPTR -> CPTCNF4
0x0835  74 03        mov a,#0x3             ; DIVB=011 = divide-by-4
0x0837  f0           movx @dptr,a           ; CPTCNF4=0x03: CSCLK = MCLKO/4 (3.072 MHz = 64x48k)
0x0838  90 ff dc     mov dptr,#0xffdc       ; DPTR -> CPTCTL
0x083b  74 50        mov a,#0x50            ; RXIE|TXIE
0x083d  f0           movx @dptr,a           ; CPTCTL=0x50: enable C-port RX-full and TX-empty interrupts
0x083e  90 ff d6     mov dptr,#0xffd6       ; DPTR -> CPTRXCNF2
0x0841  74 25        mov a,#0x25            ; TSL0L=00, BPTSL=100 (24 bits), TSLL=101 (32 clk)
0x0843  f0           movx @dptr,a           ; CPTRXCNF2=0x25
0x0844  90 ff d5     mov dptr,#0xffd5       ; DPTR -> CPTRXCNF3
0x0847  74 ac        mov a,#0xac            ; same framing polarity as TX
0x0849  f0           movx @dptr,a           ; CPTRXCNF3=0xAC
0x084a  90 ff d4     mov dptr,#0xffd4       ; DPTR -> CPTRXCNF4 (write happens inside helper)
0x084d  74 03        mov a,#0x3             ; RX DIVB = divide-by-4
0x084f  12 0e c7     lcall 0x0ec7           ; -> fcn_0ec7: CPTRXCNF4=3, then ACG=24.576 MHz, ACGCTL=0x06
0x0852  12 0e f3     lcall 0x0ef3           ; -> fcn_0ef3: ACG1DCTL=0x10, ACG2DCTL=0x10 (÷2 -> 12.288 MHz)
0x0855  90 ff b1     mov dptr,#0xffb1       ; DPTR -> GLOBCTL
0x0858  e0           movx a,@dptr           ; read GLOBCTL (0x06)
0x0859  44 01        orl a,#0x1             ; set CPTEN
0x085b  f0           movx @dptr,a           ; GLOBCTL=0x07: codec port enabled
0x085c  75 08 03     mov 0x08,#0x3          ; IRAM byte 0x08 := 3 (clock-mode ID init)
0x085f  e4           clr a                  ; A=0
0x0860  f5 22        mov 0x22,a             ; shift-chain-A shadow := 0x00
0x0862  d2 1e        setb 0x1e              ; set flag bit 0x23.6: fcn_0efc ends with P1.7|P1.6 high
0x0864  12 0e fc     lcall 0x0efc           ; -> fcn_0efc: bit-bang 0x22 (=0x00) out P1.7/P1.5
; ---- busy delay: count 0x2e:0x2f from 0 up to 0x0FFF ----
0x0867  e5 2f        mov a,0x2f             ; loop top: A = counter lo
0x0869  f4           cpl a                  ; A = ~lo (0 iff lo==0xFF)
0x086a  70 04        jnz 0x0870             ; lo != 0xFF -> skip hi test
0x086c  e5 2e        mov a,0x2e             ; lo==0xFF: A = counter hi
0x086e  64 0f        xrl a,#0xf             ; A==0 iff hi==0x0F
0x0870  60 0a        jz 0x087c              ; counter reached 0x0FFF -> exit delay
0x0872  05 2f        inc 0x2f               ; ++lo
0x0874  e5 2f        mov a,0x2f             ; test lo wrap
0x0876  70 02        jnz 0x087a             ; no wrap
0x0878  05 2e        inc 0x2e               ; lo wrapped -> ++hi
0x087a  80 eb        sjmp 0x0867            ; loop
0x087c  75 22 ff     mov 0x22,#0xff         ; shift-chain-A shadow := 0xFF ...
0x087f  c2 10        clr 0x10               ; ... clear bit 0x22.0 ...
0x0881  c2 13        clr 0x13               ; ... and bit 0x22.3 -> shadow = 0xF6
0x0883  c2 1e        clr 0x1e               ; flag 0x23.6=0: fcn_0efc ends with P1.7 low + P1.6 strobe
0x0885  12 0e fc     lcall 0x0efc           ; shift out 0xF6 on the P1.7/P1.5 chain
0x0888  e4           clr a                  ; A=0
0x0889  f5 25        mov 0x25,a             ; shift-chain-B shadow byte 2 := 0
0x088b  f5 23        mov 0x23,a             ; shift-chain-B shadow byte 1 := 0
0x088d  12 0e 56     lcall 0x0e56           ; -> fcn_0e56: bit-bang 16 bits (0x23 then 0x25) out P1.0/P1.2
0x0890  22           ret                    ; hardware clock/codec init done
```

### 5.8 — 0x0891–0x0929 : usb_ep_dma_init (fcn_0891)

```
0x0891  90 ff a9     mov dptr,#0xffa9       ; DPTR -> OEPBBAX0
0x0894  74 42        mov a,#0x42            ; 0xF800 + 0x42*8 = 0xFA10
0x0896  f0           movx @dptr,a           ; OEPBBAX0=0x42: EP0 OUT X buffer at 0xFA10
0x0897  90 ff 69     mov dptr,#0xff69       ; DPTR -> IEPBBAX0
0x089a  04           inc a                  ; A=0x43 -> 0xFA18
0x089b  f0           movx @dptr,a           ; IEPBBAX0=0x43: EP0 IN X buffer at 0xFA18
0x089c  90 ff ab     mov dptr,#0xffab       ; DPTR -> OEPDCNTX0
0x089f  e4           clr a                  ; A=0
0x08a0  f0           movx @dptr,a           ; OEPDCNTX0=0
0x08a1  90 ff 6b     mov dptr,#0xff6b       ; DPTR -> IEPDCNTX0
0x08a4  f0           movx @dptr,a           ; IEPDCNTX0=0
0x08a5  90 ff af     mov dptr,#0xffaf       ; DPTR -> OEPDCNTY0
0x08a8  f0           movx @dptr,a           ; OEPDCNTY0=0
0x08a9  90 ff 6f     mov dptr,#0xff6f       ; DPTR -> IEPDCNTY0
0x08ac  f0           movx @dptr,a           ; IEPDCNTY0=0
0x08ad  90 ff aa     mov dptr,#0xffaa       ; DPTR -> OEPBSIZ0
0x08b0  04           inc a                  ; A=1 (1*8 = 8-byte buffer)
0x08b1  f0           movx @dptr,a           ; OEPBSIZ0=1: EP0 max packet 8
0x08b2  90 ff 6a     mov dptr,#0xff6a       ; DPTR -> IEPBSIZ0
0x08b5  f0           movx @dptr,a           ; IEPBSIZ0=1: EP0 IN buffer 8 bytes
0x08b6  90 ff a8     mov dptr,#0xffa8       ; DPTR -> OEPCNF0
0x08b9  74 84        mov a,#0x84            ; OEPEN(0x80) | OEPIE(0x04)
0x08bb  f0           movx @dptr,a           ; OEPCNF0=0x84: EP0 OUT enabled + interrupt
0x08bc  90 ff 68     mov dptr,#0xff68       ; DPTR -> IEPCNF0
0x08bf  f0           movx @dptr,a           ; IEPCNF0=0x84: EP0 IN enabled + interrupt
0x08c0  90 ff 99     mov dptr,#0xff99       ; DPTR -> OEPBBAX2 (audio playback endpoint)
0x08c3  74 44        mov a,#0x44            ; 0xF800 + 0x44*8 = 0xFA20
0x08c5  f0           movx @dptr,a           ; OEPBBAX2=0x44: OUT EP2 buffer at 0xFA20
0x08c6  90 ff 61     mov dptr,#0xff61       ; DPTR -> IEPBBAX1 (audio record endpoint)
0x08c9  74 94        mov a,#0x94            ; 0xF800 + 0x94*8 = 0xFCA0
0x08cb  f0           movx @dptr,a           ; IEPBBAX1=0x94: IN EP1 buffer at 0xFCA0
0x08cc  90 ff 9a     mov dptr,#0xff9a       ; DPTR -> OEPBSIZ2
0x08cf  74 50        mov a,#0x50            ; 0x50*8 = 640 bytes
0x08d1  f0           movx @dptr,a           ; OEPBSIZ2=0x50: 640-byte iso OUT buffer
0x08d2  90 ff 62     mov dptr,#0xff62       ; DPTR -> IEPBSIZ1
0x08d5  f0           movx @dptr,a           ; IEPBSIZ1=0x50: 640 bytes for IN EP1 (nominal end overlaps EP-config space)
0x08d6  90 ff 9b     mov dptr,#0xff9b       ; DPTR -> OEPDCNTX2
0x08d9  e4           clr a                  ; A=0
0x08da  f0           movx @dptr,a           ; OEPDCNTX2=0
0x08db  90 ff 63     mov dptr,#0xff63       ; DPTR -> IEPDCNTX1
0x08de  f0           movx @dptr,a           ; IEPDCNTX1=0
0x08df  90 ff 98     mov dptr,#0xff98       ; DPTR -> OEPCNF2
0x08e2  74 c5        mov a,#0xc5            ; OEPEN|ISO|BPS=5 (6 bytes/sample = 2ch x 24-bit)
0x08e4  f0           movx @dptr,a           ; OEPCNF2=0xC5: iso playback EP enabled
0x08e5  90 ff 60     mov dptr,#0xff60       ; DPTR -> IEPCNF1
0x08e8  f0           movx @dptr,a           ; IEPCNF1=0xC5: iso record EP enabled
0x08e9  90 ff ea     mov dptr,#0xffea       ; DPTR -> DMATSL0
0x08ec  74 03        mov a,#0x3             ; TSL0|TSL1
0x08ee  f0           movx @dptr,a           ; DMATSL0=0x03: DMA ch0 covers time slots 0+1 (L+R)
0x08ef  90 ff e9     mov dptr,#0xffe9       ; DPTR -> DMATSH0
0x08f2  74 80        mov a,#0x80            ; BPTS=10b = 3 bytes per time slot
0x08f4  f0           movx @dptr,a           ; DMATSH0=0x80: 24-bit samples on ch0
0x08f5  90 ff f0     mov dptr,#0xfff0       ; DPTR -> DMATSL1
0x08f8  74 03        mov a,#0x3             ; TSL0|TSL1
0x08fa  f0           movx @dptr,a           ; DMATSL1=0x03: ch1 slots 0+1
0x08fb  90 ff ef     mov dptr,#0xffef       ; DPTR -> DMATSH1
0x08fe  74 80        mov a,#0x80            ; BPTS = 3 bytes/slot
0x0900  f0           movx @dptr,a           ; DMATSH1=0x80
0x0901  90 ff e8     mov dptr,#0xffe8       ; DPTR -> DMACTL0
0x0904  74 02        mov a,#0x2             ; DMAEN=0, EPDIR=0(OUT), EPNUM=2 [secondary entry: LJMP from 0x061f]
0x0906  f0           movx @dptr,a           ; DMACTL0=0x02: ch0 bound to OUT EP2, not yet enabled
0x0907  90 ff ee     mov dptr,#0xffee       ; DPTR -> DMACTL1
0x090a  74 09        mov a,#0x9             ; DMAEN=0, EPDIR=1(IN), EPNUM=1
0x090c  f0           movx @dptr,a           ; DMACTL1=0x09: ch1 bound to IN EP1, not enabled
0x090d  90 ff fd     mov dptr,#0xfffd       ; DPTR -> USBIMSK
0x0910  74 9f        mov a,#0x9f            ; [secondary entry: ACALL from 0x0cbc — bus-reset reinit]
0x0912  f0           movx @dptr,a           ; USBIMSK=0x9F: unmask RSTR|SOF|PSOF|SETUP|resv|STPOW; SUSR/RESR masked
0x0913  90 ff ff     mov dptr,#0xffff       ; DPTR -> USBFADR
0x0916  e4           clr a                  ; A=0
0x0917  f0           movx @dptr,a           ; USBFADR=0: back to default address
0x0918  c2 0a        clr 0x0a               ; clear flag bit 0x21.2 (USB state flag)
0x091a  c2 0e        clr 0x0e               ; clear flag bit 0x21.6
0x091c  c2 08        clr 0x08               ; clear flag bit 0x21.0
0x091e  c2 09        clr 0x09               ; clear flag bit 0x21.1
0x0920  f5 09        mov 0x09,a             ; IRAM byte 0x09 := 0
0x0922  f5 0b        mov 0x0b,a             ; IRAM byte 0x0b := 0
0x0924  75 0c fe     mov 0x0c,#0xfe         ; IRAM byte 0x0c := 0xFE (sentinel-looking init)
0x0927  f5 0a        mov 0x0a,a             ; IRAM byte 0x0a := 0
0x0929  22           ret                    ; endpoints + DMA configured, USB int sources unmasked
```

### 5.9 — 0x092a–0x0938 : keil_c51_startup (?C_STARTUP)

```
0x092a  78 7f        mov r0,#0x7f           ; top of IRAM clear region
0x092c  e4           clr a                  ; A=0
0x092d  f6           mov @r0,a              ; clear IRAM[R0]
0x092e  d8 fd        djnz r0,0x092d         ; loop: zeroes IRAM 0x7F down to 0x01
0x0930  75 81 32     mov sp,#0x32           ; stack starts at 0x33
0x0933  02 09 71     ljmp 0x0971            ; enter INIT-table interpreter
0x0936  02 0a 3f     ljmp 0x0a3f            ; INIT table done -> main() at 0x0a3f (jumped from 0x0978)
```

### 5.10 — 0x0939–0x09b5 : keil_c_init_interpreter (?C_INIT)

```
; --- IDATA/PDATA copy path (entered from 0x098f with CY = header bit7) ---
0x0939  e4           clr a                  ; index 0 for MOVC
0x093a  93           movc a,@a+dptr         ; fetch target address byte from table
0x093b  a3           inc dptr               ; advance table pointer
0x093c  f8           mov r0,a               ; R0 = 8-bit target address
0x093d  e4           clr a                  ; byte-copy loop: index 0
0x093e  93           movc a,@a+dptr         ; fetch data byte
0x093f  a3           inc dptr               ; advance table pointer
0x0940  40 03        jc 0x0945              ; CY (type 10) -> PDATA store
0x0942  f6           mov @r0,a              ; type 00: store to IDATA
0x0943  80 01        sjmp 0x0946            ; skip PDATA store
0x0945  f2           movx @r0,a             ; type 10: store to PDATA
0x0946  08           inc r0                 ; next target byte
0x0947  df f4        djnz r7,0x093d         ; R7 = block length, loop
0x0949  80 29        sjmp 0x0974            ; next record
; --- BIT-init path (type 11, entered from 0x0991) ---
0x094b  e4           clr a                  ; index 0
0x094c  93           movc a,@a+dptr         ; fetch bit-descriptor byte
0x094d  a3           inc dptr               ; advance table pointer
0x094e  f8           mov r0,a               ; save descriptor
0x094f  54 07        anl a,#0x7             ; A = bit number 0-7
0x0951  24 0c        add a,#0xc             ; A = bit# + 0x0C (MOVC offset for mask table 0x0969)
0x0953  c8           xch a,r0               ; A = descriptor, R0 = table index
0x0954  c3           clr cy                 ; prepare RLC
0x0955  33           rlc a                  ; CY = descriptor bit7 (bit VALUE); A = descriptor<<1
0x0956  c4           swap a                 ; nibble swap
0x0957  54 0f        anl a,#0xf             ; isolate byte offset
0x0959  44 20        orl a,#0x20            ; A = bit-addressable byte address 0x20-0x2F
0x095b  c8           xch a,r0               ; R0 = byte address, A = mask-table index
0x095c  83           movc a,@a+pc           ; A = mask = table[bit#] at 0x0969+bit#
0x095d  40 04        jc 0x0963              ; bit value 1 -> set path
0x095f  f4           cpl a                  ; ~mask
0x0960  56           anl a,@r0              ; clear the bit in target byte
0x0961  80 01        sjmp 0x0964            ; join
0x0963  46           orl a,@r0              ; set the bit in target byte
0x0964  f6           mov @r0,a              ; write back
0x0965  df e4        djnz r7,0x094b         ; R7 = number of bit records, loop
0x0967  80 0b        sjmp 0x0974            ; next record
; --- (DATA 0x0969-0x0970: bit mask table 01 02 04 08 10 20 40 80 — see §6.3) ---
; --- record dispatcher ---
0x0971  90 0f ba     mov dptr,#0xfba        ; DPTR = INIT table base at CODE 0x0FBA
0x0974  e4           clr a                  ; record loop: index 0
0x0975  7e 01        mov r6,#0x1            ; default outer repeat count = 1
0x0977  93           movc a,@a+dptr         ; fetch record header byte
0x0978  60 bc        jz 0x0936             ; header 0x00 = table end -> LJMP main (0x0a3f)
0x097a  a3           inc dptr               ; advance past header
0x097b  ff           mov r7,a               ; R7 = header (keep type bits)
0x097c  54 3f        anl a,#0x3f            ; A = length field (low 6 bits)
0x097e  30 e5 09     jnb acc.5,0x098a       ; small block (len<32)
0x0981  54 1f        anl a,#0x1f            ; big block: low 5 bits = high part of 13-bit length
0x0983  fe           mov r6,a               ; R6 = high length byte
0x0984  e4           clr a                  ; index 0
0x0985  93           movc a,@a+dptr         ; fetch low length byte
0x0986  a3           inc dptr               ; advance
0x0987  60 01        jz 0x098a              ; low byte 0 -> R6 stays as-is
0x0989  0e           inc r6                 ; low byte nonzero -> one extra outer pass
0x098a  cf           xch a,r7               ; A = header, R7 = low length count
0x098b  54 c0        anl a,#0xc0            ; isolate type bits 7:6
0x098d  25 e0        add a,acc              ; A<<=1: CY = type bit7, A bit7 = type bit6
0x098f  60 a8        jz 0x0939              ; type 00/10 -> copy path (CY selects mov/movx)
0x0991  40 b8        jc 0x094b              ; type 11 -> BIT-init path
; --- fall through: type 01 = XDATA block with 16-bit target address ---
0x0993  e4           clr a                  ; index 0
0x0994  93           movc a,@a+dptr         ; fetch target address high byte
0x0995  a3           inc dptr               ; advance
0x0996  fa           mov r2,a               ; R2 = target high
0x0997  e4           clr a                  ; index 0
0x0998  93           movc a,@a+dptr         ; fetch target address low byte
0x0999  a3           inc dptr               ; advance
0x099a  f8           mov r0,a               ; R0 = target low
0x099b  e4           clr a                  ; XDATA copy loop: index 0
0x099c  93           movc a,@a+dptr         ; fetch data byte
0x099d  a3           inc dptr               ; advance table pointer
0x099e  c8           xch a,r0               ; begin swapping DPTR <-> target address
0x099f  c5 82        xch a,dpl              ; DPL <-> target low
0x09a1  c8           xch a,r0               ; R0 = saved table-pointer low
0x09a2  ca           xch a,r2               ; continue pointer swap
0x09a3  c5 83        xch a,dph              ; DPH <-> target high
0x09a5  ca           xch a,r2               ; swap complete: DPTR = XDATA target, R2:R0 = table pointer
0x09a6  f0           movx @dptr,a           ; store init byte to XDATA target
0x09a7  a3           inc dptr               ; ++target address
0x09a8  c8           xch a,r0               ; begin swapping back ...
0x09a9  c5 82        xch a,dpl              ; DPL <-> saved table low
0x09ab  c8           xch a,r0               ; ...
0x09ac  ca           xch a,r2               ; ...
0x09ad  c5 83        xch a,dph              ; DPH <-> saved table high
0x09af  ca           xch a,r2               ; swap back: DPTR = table pointer, R2:R0 = target address
0x09b0  df e9        djnz r7,0x099b         ; inner length counter
0x09b2  de e7        djnz r6,0x099b         ; outer (big-block) counter
0x09b4  80 be        sjmp 0x0974            ; next record
```

### 5.11 — 0x09b6–0x0a3e : audio_hw_bringup (fcn_09b6)

See §2.5.3: this is the full-body reading (bring-up), superseding the "teardown" label
used by callers in §5.4.

```
0x09b6  e4        clr a                 ; A=0 (entry; XREF 0x0366/0x0396/0x0419/0x04cb)
0x09b7  f5 25     mov 0x25,a            ; control-word byte2 (IRAM 0x25) := 0
0x09b9  f5 23     mov 0x23,a            ; control-word byte1 (IRAM 0x23) := 0
0x09bb  d2 2e     setb 0x2e             ; set bit 0x25.6 (teardown-done latch) — hardware role UNKNOWN
0x09bd  7f ff     mov r7,#0xff          ; delay counter = 255
0x09bf  df fe     djnz r7,0x09bf        ; busy-wait ~510 cycles
0x09c1  12 0e 56  lcall 0x0e56          ; -> fcn_0e56: shift 16-bit word {0x23,0x25} out
0x09c4  12 0e c8  lcall 0x0ec8          ; -> fcn_0ec8: program ACG1+ACG2 (24.576 MHz), ACGCTL=0x06
0x09c7  12 0e f3  lcall 0x0ef3          ; -> fcn_0ef3: ACG1DCTL=0x10, ACG2DCTL=0x10
0x09ca  75 08 03  mov 0x08,#0x3         ; state code 0x08 := 3
0x09cd  90 ff e1  mov dptr,#0xffe1      ; DPTR = ACGCTL
0x09d0  e0        movx a,@dptr          ; read ACGCTL
0x09d1  44 c0     orl a,#0xc0           ; set bit7 MCLKO2EN + bit6 MCLKO1EN
0x09d3  f0        movx @dptr,a          ; ACGCTL |= 0xC0: enable both MCLK output pins
0x09d4  7f ff     mov r7,#0xff          ; delay counter
0x09d6  df fe     djnz r7,0x09d6        ; busy-wait (let clocks settle)
0x09d8  d2 1a     setb 0x1a             ; set control-word bit 0x23.2
0x09da  d2 1b     setb 0x1b             ; set control-word bit 0x23.3 (bits .2/.3 set as a pair)
0x09dc  12 0e 56  lcall 0x0e56          ; shift updated word to the control latch
0x09df  7f ff     mov r7,#0xff          ; delay counter
0x09e1  df fe     djnz r7,0x09e1        ; busy-wait
0x09e3  d2 2f     setb 0x2f             ; set control-word bit 0x25.7
0x09e5  d2 1c     setb 0x1c             ; set control-word bit 0x23.4
0x09e7  12 0e 56  lcall 0x0e56          ; shift word (0x25.7 + 0x23.4 now high)
0x09ea  7f ff     mov r7,#0xff          ; delay counter
0x09ec  df fe     djnz r7,0x09ec        ; busy-wait
0x09ee  c2 2f     clr 0x2f              ; drop bit 0x25.7 ...
0x09f0  12 0e 56  lcall 0x0e56          ; ... shift (0x25.7 low)
0x09f3  d2 2f     setb 0x2f             ; ... raise it again
0x09f5  12 0e 56  lcall 0x0e56          ; ... shift: net = low pulse on latch output 0x25.7
0x09f8  7f 04     mov r7,#0x4           ; codec reg addr = 0x04
0x09fa  e4        clr a                 ; A=0
0x09fb  fd        mov r5,a              ; codec value = 0x00
0x09fc  12 0c 31  lcall 0x0c31          ; -> fcn_0c31: codec reg 0x04 := 0x00
0x09ff  7f 13     mov r7,#0x13          ; reg addr = 0x13
0x0a01  7d 10     mov r5,#0x10          ; value = 0x10
0x0a03  12 0c 31  lcall 0x0c31          ; codec reg 0x13 := 0x10
0x0a06  7f 04     mov r7,#0x4           ; reg addr = 0x04
0x0a08  e4        clr a                 ; A=0
0x0a09  fd        mov r5,a              ; value = 0x00
0x0a0a  12 0c 31  lcall 0x0c31          ; codec reg 0x04 := 0x00
0x0a0d  7f 04     mov r7,#0x4           ; reg addr = 0x04
0x0a0f  7d 40     mov r5,#0x40          ; value = 0x40
0x0a11  12 0c 31  lcall 0x0c31          ; codec reg 0x04 := 0x40
0x0a14  7f 01     mov r7,#0x1           ; reg addr = 0x01
0x0a16  7d 01     mov r5,#0x1           ; value = 0x01
0x0a18  12 0c 31  lcall 0x0c31          ; codec reg 0x01 := 0x01
0x0a1b  7f 02     mov r7,#0x2           ; reg addr = 0x02
0x0a1d  7d 20     mov r5,#0x20          ; value = 0x20
0x0a1f  12 0c 31  lcall 0x0c31          ; codec reg 0x02 := 0x20
0x0a22  7f 03     mov r7,#0x3           ; reg addr = 0x03
0x0a24  7d 0c     mov r5,#0xc           ; value = 0x0C
0x0a26  12 0c 31  lcall 0x0c31          ; codec reg 0x03 := 0x0C
0x0a29  7f 05     mov r7,#0x5           ; reg addr = 0x05
0x0a2b  7d 05     mov r5,#0x5           ; value = 0x05
0x0a2d  12 0c 31  lcall 0x0c31          ; codec reg 0x05 := 0x05
0x0a30  7f 06     mov r7,#0x6           ; reg addr = 0x06
0x0a32  7d 05     mov r5,#0x5           ; value = 0x05
0x0a34  12 0c 31  lcall 0x0c31          ; codec reg 0x06 := 0x05
0x0a37  7f 11     mov r7,#0x11          ; reg addr = 0x11
0x0a39  7d ff     mov r5,#0xff          ; value = 0xFF
0x0a3b  12 0c 31  lcall 0x0c31          ; codec reg 0x11 := 0xFF (register map UNVERIFIED)
0x0a3e  22        ret                   ; done: clocks running, latch staged, codec programmed
```

### 5.12 — 0x0a3f–0x0aba : main_loop (fcn_0a3f)

```
0x0a3f  e4        clr a                 ; A=0 (entry from 0x0936)
0x0a40  f5 27     mov 0x27,a            ; P3.1 edge latch := 0
0x0a42  74 ff     mov a,#0xff           ; A=0xFF
0x0a44  f5 28     mov 0x28,a            ; startup delay counter HIGH := 0xFF
0x0a46  f5 29     mov 0x29,a            ; startup delay counter LOW := 0xFF (16-bit 0xFFFF)
0x0a48  75 2a 00  mov 0x2a,#0x0         ; byte 0x2a := 0 (not read in annotated code)
0x0a4b  75 2b 10  mov 0x2b,#0x10        ; byte 0x2b := 0x10 (not read in annotated code)
0x0a4e  c2 af     clr 0xaf              ; EA=0 (IE.7): global interrupt disable during USB setup
0x0a50  90 ff fd  mov dptr,#0xfffd      ; DPTR = USBIMSK
0x0a53  e4        clr a                 ; A=0
0x0a54  f0        movx @dptr,a          ; USBIMSK = 0x00: mask ALL USB interrupt sources
0x0a55  c2 22     clr 0x22              ; clear flag bit 0x24.2 — purpose UNKNOWN
0x0a57  12 07 ec  lcall 0x07ec          ; -> fcn_07ec: hw_clock_codec_init
0x0a5a  12 08 91  lcall 0x0891          ; -> fcn_0891: usb_ep_dma_init
0x0a5d  d3        setb cy               ; top of 16-bit countdown
0x0a5e  e5 29     mov a,0x29            ; A = counter low
0x0a60  94 00     subb a,#0x0           ; A - 0 - 1: borrow iff low byte == 0
0x0a62  e5 28     mov a,0x28            ; A = counter high
0x0a64  94 00     subb a,#0x0           ; propagate borrow: CY set iff 0x28:0x29 == 0
0x0a66  40 0a     jc 0x0a72             ; counter exhausted -> enable timer/ints/connect
0x0a68  e5 29     mov a,0x29            ; A = low byte
0x0a6a  15 29     dec 0x29              ; low byte -= 1
0x0a6c  70 ef     jnz 0x0a5d            ; if old low != 0, loop
0x0a6e  15 28     dec 0x28              ; borrow into high byte
0x0a70  80 eb     sjmp 0x0a5d           ; loop: 0xFFFF iterations settle delay before connecting
0x0a72  d2 8c     setb 0x8c             ; TCON.4 = TR0 = 1: start Timer 0 (ms tick)
0x0a74  d2 af     setb 0xaf             ; EA=1: global interrupt enable
0x0a76  90 ff fc  mov dptr,#0xfffc      ; DPTR = USBCTL
0x0a79  e0        movx a,@dptr          ; read USBCTL (RMW keeps boot-ROM-set bits)
0x0a7a  44 80     orl a,#0x80           ; set bit7 CONT
0x0a7c  f0        movx @dptr,a          ; USBCTL |= CONT: enable DP pull-up, device appears on bus
0x0a7d  20 20 09  jb 0x20,0x0a89        ; TOP OF FOREVER LOOP: if tick flag (bit 0x24.0) -> per-tick work
0x0a80  e5 0a     mov a,0x0a            ; idle path: A = pending event code
0x0a82  60 f9     jz 0x0a7d             ; no event -> spin on tick flag
0x0a84  12 02 f3  lcall 0x02f3          ; -> fcn_02f3: dispatch event code 0x0a
0x0a87  80 f4     sjmp 0x0a7d           ; back to loop top
0x0a89  12 0f 31  lcall 0x0f31          ; per-tick: -> fcn_0f31 poll P3, run button handlers
0x0a8c  ef        mov a,r7              ; A = poller result
0x0a8d  30 e0 06  jnb 0xe0,0x0a96       ; ACC.0 clear (no button change) -> skip re-shift
0x0a90  12 0e fc  lcall 0x0efc          ; -> fcn_0efc: shift mux byte (IRAM 0x22)
0x0a93  12 0e 56  lcall 0x0e56          ; -> fcn_0e56: shift control word {0x23,0x25}
0x0a96  20 01 0d  jb 0x01,0x0aa6        ; P3.1 shadow high -> check release edge
0x0a99  e5 27     mov a,0x27            ; P3.1 currently LOW: A = edge latch
0x0a9b  70 09     jnz 0x0aa6            ; already reported this low level -> skip
0x0a9d  75 27 01  mov 0x27,#0x1         ; latch := 1 (low level reported)
0x0aa0  75 0a 0b  mov 0x0a,#0xb         ; queue event 0x0b = "P3.1 went low"
0x0aa3  12 02 f3  lcall 0x02f3          ; dispatch it now
0x0aa6  30 01 0e  jnb 0x01,0x0ab7       ; P3.1 shadow low -> done with edge logic
0x0aa9  e5 27     mov a,0x27            ; P3.1 currently HIGH: A = edge latch
0x0aab  b4 01 09  cjne a,#0x1,0x0ab7    ; only if we previously reported the low level...
0x0aae  e4        clr a                 ; A=0
0x0aaf  f5 27     mov 0x27,a            ; latch := 0
0x0ab1  75 0a 0c  mov 0x0a,#0xc         ; queue event 0x0c = "P3.1 back high"
0x0ab4  12 02 f3  lcall 0x02f3          ; dispatch it now
0x0ab7  c2 20     clr 0x20              ; consume tick flag (bit 0x24.0)
0x0ab9  80 c2     sjmp 0x0a7d           ; forever
```

### 5.13 — 0x0abb–0x0b1e : ep0_in_send_chunk (fcn_0abb)

Buffer note (§2.5.1): the EP0 IN buffer is 0xFA18; `fcn_0b37` sets the cursor to it.

```
0x0abb  e4        clr a                 ; A=0 (entry; XREF from wrapper fcn_0b63)
0x0abc  f5 18     mov 0x18,a            ; packet byte counter (IRAM 0x18) := 0
0x0abe  12 0b 37  lcall 0x0b37          ; -> fcn_0b37: dest pointer 0x1d:0x1e := 0xFA18
0x0ac1  e5 09     mov a,0x09            ; COPY LOOP: A = remaining low byte
0x0ac3  d3        setb cy               ; CY=1 for compare-vs-1
0x0ac4  94 00     subb a,#0x0           ; A-0-1: borrow iff remaining low == 0
0x0ac6  40 2f     jc 0x0af7             ; nothing (more) to copy -> arm endpoint (also ZLP path)
0x0ac8  12 0b 6e  lcall 0x0b6e          ; -> fcn_0b6e: DPTR := source 0x19:0x1a
0x0acb  e4        clr a                 ; A=0
0x0acc  93        movc a,@a+dptr        ; fetch source byte from CODE space
0x0acd  12 0b 25  lcall 0x0b25          ; -> fcn_0b25: DPTR := dest 0x1d:0x1e
0x0ad0  f0        movx @dptr,a          ; store byte into EP0 IN buffer
0x0ad1  05 1e     inc 0x1e              ; dest low++
0x0ad3  e5 1e     mov a,0x1e            ; A = dest low
0x0ad5  70 02     jnz 0x0ad9            ; no wrap -> skip
0x0ad7  05 1d     inc 0x1d              ; carry into dest high
0x0ad9  05 1a     inc 0x1a              ; source low++
0x0adb  e5 1a     mov a,0x1a            ; A = source low
0x0add  70 02     jnz 0x0ae1            ; no wrap -> skip
0x0adf  05 19     inc 0x19              ; carry into source high
0x0ae1  d5 09 0c  djnz 0x09,0x0af0      ; remaining low--; if nonzero, continue
0x0ae4  e5 0b     mov a,0x0b            ; low byte hit 0: A = remaining HIGH byte
0x0ae6  d3        setb cy               ; CY=1
0x0ae7  94 00     subb a,#0x0           ; borrow iff high == 0
0x0ae9  40 05     jc 0x0af0             ; high == 0 too -> transfer data exhausted
0x0aeb  75 09 ff  mov 0x09,#0xff        ; borrow from high byte: low := 0xFF ...
0x0aee  15 0b     dec 0x0b             ; ... high -= 1
0x0af0  05 18     inc 0x18              ; packet byte counter++
0x0af2  e5 18     mov a,0x18            ; A = bytes in packet
0x0af4  b4 08 ca  cjne a,#0x8,0x0ac1    ; loop until 8 bytes copied or count exhausted
0x0af7  90 ff 6b  mov dptr,#0xff6b      ; DPTR = IEPDCNTX0
0x0afa  74 80     mov a,#0x80           ; NAK bit
0x0afc  f0        movx @dptr,a          ; IEPDCNTX0 = 0x80: hold EP0 IN NAKing while count staged
0x0afd  e0        movx a,@dptr          ; read back (0x80)
0x0afe  45 18     orl a,0x18            ; OR in this packet's byte count (0..8)
0x0b00  f0        movx @dptr,a          ; IEPDCNTX0 = 0x80|count: count valid, still NAK
0x0b01  d2 0b     setb 0x0b             ; BIT 0x0b (0x21.3) := 1: EP0 IN transfer in progress
0x0b03  c2 0c     clr 0x0c             ; BIT 0x0c (0x21.4) := 0: data stage not complete
0x0b05  e5 09     mov a,0x09            ; A = remaining low
0x0b07  70 15     jnz 0x0b1e            ; bytes still pending -> return
0x0b09  e5 0b     mov a,0x0b            ; A = remaining high (BYTE 0x0b)
0x0b0b  70 11     jnz 0x0b1e            ; bytes still pending -> return
0x0b0d  e5 18     mov a,0x18            ; remaining == 0: A = bytes just packed
0x0b0f  b4 08 08  cjne a,#0x8,0x0b1a    ; short packet (<8) -> transfer self-terminates
0x0b12  30 0d 05  jnb 0x0d,0x0b1a       ; full 8-byte final packet: if ZLP flag clear -> done
0x0b15  d2 0b     setb 0x0b             ; ZLP needed: stay in-progress
0x0b17  c2 0c     clr 0x0c             ; ... next pass copies 0 bytes -> ZLP
0x0b19  22        ret                   ; return, ZLP pending
0x0b1a  c2 0b     clr 0x0b             ; transfer finished: clear in-progress flag
0x0b1c  d2 0c     setb 0x0c             ; set data-stage-complete flag (status stage next)
0x0b1e  22        ret                   ; return
```

### 5.14 — 0x0b1f–0x0c30 : EP0 / I2C / divide / dispatch leaf helpers

Buffer-label note (§2.5.1): `fcn_0b1f` sets 0xFA10 = EP0 **OUT**, `fcn_0b37` sets 0xFA18
= EP0 **IN**. The comments below reflect the annotator's original (swapped) labels;
read with the reconciliation note.

```
; --- fcn_0b1f = ep0_in_setptr (per §2.5.1: actually EP0 OUT buffer 0xFA10) ---
0x0b1f  75 1d fa    mov 0x1d,#0xfa       ; ptr high = 0xFA (USB buffer RAM page)
0x0b22  75 1e 10    mov 0x1e,#0x10       ; ptr low = 0x10 -> 0xFA10 = EP0 IN buffer [OUT per §2.5.1]
; --- fcn_0b25 = ep0_load_dptr ---
0x0b25  85 1e 82    mov dpl,0x1e         ; DPL = ptr low
0x0b28  85 1d 83    mov dph,0x1d         ; DPH = ptr high -> DPTR = EP0 buffer addr
0x0b2b  22          ret                  ; return with DPTR ready for MOVX
; --- fcn_0b2c = ep0_store_byte_and_arm_zlp (2nd entry at 0x0b2e) ---
0x0b2c  f0          movx @dptr,a         ; store byte A into EP0 buffer at @DPTR
0x0b2d  e4          clr a                ; A = 0 (count value for the ZLP arm)
0x0b2e  90 ff 6b    mov dptr,#0xff6b     ; DPTR -> IEPDCNTX0 [2nd entry: A = caller's count]
0x0b31  f0          movx @dptr,a         ; IEPDCNTX0 = A: NAK=0 -> EP0 IN armed with A-byte payload (0 = ZLP)
0x0b32  90 ff ab    mov dptr,#0xffab     ; DPTR -> OEPDCNTX0
0x0b35  f0          movx @dptr,a         ; OEPDCNTX0 = A: clears OUT NAK, EP0 OUT ready
0x0b36  22          ret                  ; done
; --- fcn_0b37 = ep0_out_setptr (per §2.5.1: actually EP0 IN buffer 0xFA18) ---
0x0b37  75 1d fa    mov 0x1d,#0xfa       ; ptr high = 0xFA
0x0b3a  75 1e 18    mov 0x1e,#0x18       ; ptr low = 0x18 -> 0xFA18 = EP0 OUT buffer [IN per §2.5.1]
0x0b3d  22          ret                  ; pointer set only; DPTR untouched
; --- fcn_0b3e = ep0_clear_stall_both ---
0x0b3e  90 ff 68    mov dptr,#0xff68     ; DPTR -> IEPCNF0
0x0b41  e0          movx a,@dptr         ; read IEPCNF0
0x0b42  54 f7       anl a,#0xf7          ; clear bit3 = STALL (STALLClrInEp0)
0x0b44  f0          movx @dptr,a         ; write back -> EP0 IN un-stalled
0x0b45  90 ff a8    mov dptr,#0xffa8     ; DPTR -> OEPCNF0
0x0b48  e0          movx a,@dptr         ; read OEPCNF0
0x0b49  54 f7       anl a,#0xf7          ; clear bit3 = STALL (STALLClrOutEp0)
0x0b4b  f0          movx @dptr,a         ; write back -> EP0 OUT un-stalled
0x0b4c  22          ret                  ; both EP0 halves un-stalled
; --- fcn_0b4d = ep0_clear_stall_toggle — OUT half never written back (§2.5.5) ---
0x0b4d  90 ff 68    mov dptr,#0xff68     ; DPTR -> IEPCNF0
0x0b50  e0          movx a,@dptr         ; read IEPCNF0
0x0b51  54 d7       anl a,#0xd7          ; clear bit5 (data toggle) + bit3 (STALL)
0x0b53  f0          movx @dptr,a         ; write back: EP0 IN toggle cleared + un-stalled
0x0b54  90 ff a8    mov dptr,#0xffa8     ; DPTR -> OEPCNF0
0x0b57  e0          movx a,@dptr         ; read OEPCNF0
0x0b58  54 d7       anl a,#0xd7          ; mask computed in A ...
0x0b5a  22          ret                  ; ... but NEVER stored — OEPCNF0 unchanged. Likely bug.
; --- fcn_0b5b = ep0_buf_store_zero ---
0x0b5b  f5 82       mov dpl,a            ; DPL = caller's offset (low byte of target)
0x0b5d  85 1d 83    mov dph,0x1d         ; DPH = ptr high (0xFA when EP0 ptrs in use)
0x0b60  e4          clr a                ; A = 0
0x0b61  f0          movx @dptr,a         ; store 0x00 at 0x1d:A (EP0 buffer byte)
0x0b62  22          ret                  ;
; --- fcn_0b63 = ep0_in_stage_and_go ---
0x0b63  12 0a bb    lcall 0x0abb         ; -> fcn_0abb: stage next EP0 IN chunk, leaves IEPDCNTX0=0x80|count
0x0b66  90 ff 6b    mov dptr,#0xff6b     ; DPTR -> IEPDCNTX0
0x0b69  e0          movx a,@dptr         ; read count (bit7=NAK set by stager)
0x0b6a  54 7f       anl a,#0x7f          ; clear bit7 (NAK)
0x0b6c  f0          movx @dptr,a         ; write back -> EP0 IN transfer armed with staged count
0x0b6d  22          ret                  ;
; --- fcn_0b6e = load_dptr_from_ptr19 ---
0x0b6e  85 1a 82    mov dpl,0x1a         ; DPL = source ptr low
0x0b71  85 19 83    mov dph,0x19         ; DPH = source ptr high
0x0b74  22          ret                  ; DPTR = descriptor/source read pointer
; --- fcn_0b75 = ep0_flush_arm ---
0x0b75  90 ff ab    mov dptr,#0xffab     ; DPTR -> OEPDCNTX0
0x0b78  e4          clr a                ; A = 0
0x0b79  f0          movx @dptr,a         ; OEPDCNTX0 = 0: clear OUT NAK (EMPTYOutEp0)
0x0b7a  90 ff 6b    mov dptr,#0xff6b     ; DPTR -> IEPDCNTX0
0x0b7d  f0          movx @dptr,a         ; IEPDCNTX0 = 0: arm zero-length IN (ZEROPACKInEp0)
0x0b7e  22          ret                  ;
; --- fcn_0b7f = udiv16 (unsigned 16/16 divide) ---
0x0b7f  bc 00 0b    cjne r4,#0x0,0x0b8d  ; divisor high != 0? -> 16-bit-divisor path
0x0b82  be 00 29    cjne r6,#0x0,0x0bae  ; dividend high != 0? -> 16/8 path
0x0b85  ef          mov a,r7             ; 8/8 case: A = dividend low
0x0b86  8d f0       mov b,r5             ; B = divisor low
0x0b88  84          div ab               ; A = quotient, B = remainder
0x0b89  ff          mov r7,a             ; quotient -> R7
0x0b8a  ad f0       mov r5,b             ; remainder -> R5
0x0b8c  22          ret                  ; done
0x0b8d  e4          clr a                ; A = 0
0x0b8e  cc          xch a,r4             ; A = divisor high, R4 = 0
0x0b8f  f8          mov r0,a             ; R0 = divisor high (saved)
0x0b90  75 f0 08    mov b,#0x8           ; loop counter = 8 bits
0x0b93  ef          mov a,r7             ; loop: shift dividend/remainder left 1 bit
0x0b94  2f          add a,r7             ; R7 << 1 (bit7 -> CY)
0x0b95  ff          mov r7,a             ;
0x0b96  ee          mov a,r6             ;
0x0b97  33          rlc a                ; R6 << 1 with carry in
0x0b98  fe          mov r6,a             ;
0x0b99  ec          mov a,r4             ;
0x0b9a  33          rlc a                ; R4 << 1 with carry in (remainder high)
0x0b9b  fc          mov r4,a             ;
0x0b9c  ee          mov a,r6             ; trial subtract: (R4:R6) - (R0:R5)
0x0b9d  9d          subb a,r5            ; low compare
0x0b9e  ec          mov a,r4             ;
0x0b9f  98          subb a,r0            ; high compare
0x0ba0  40 05       jc 0x0ba7            ; borrow -> quotient bit 0
0x0ba2  fc          mov r4,a             ; fits: commit high difference
0x0ba3  ee          mov a,r6             ;
0x0ba4  9d          subb a,r5            ; recompute+commit low difference
0x0ba5  fe          mov r6,a             ;
0x0ba6  0f          inc r7               ; set quotient bit
0x0ba7  d5 f0 e9    djnz b,0x0b93        ; 8 iterations
0x0baa  e4          clr a                ; A = 0
0x0bab  ce          xch a,r6             ; R6 = 0, A = remainder-mid
0x0bac  fd          mov r5,a             ; remainder low -> R5
0x0bad  22          ret                  ; done
0x0bae  ed          mov a,r5             ; A = divisor
0x0baf  f8          mov r0,a             ; R0 = divisor (saved)
0x0bb0  f5 f0       mov b,a              ; B = divisor
0x0bb2  ee          mov a,r6             ; A = dividend high
0x0bb3  84          div ab               ; quotient-high = A, partial remainder = B; OV if divisor==0
0x0bb4  20 d2 1c    jb 0xd2,0x0bd3       ; PSW.2 (OV) set -> divide-by-zero, bail out
0x0bb7  fe          mov r6,a             ; quotient high -> R6
0x0bb8  ad f0       mov r5,b             ; partial remainder -> R5
0x0bba  75 f0 08    mov b,#0x8           ; loop counter = 8 bits
0x0bbd  ef          mov a,r7             ; loop: shift dividend low left
0x0bbe  2f          add a,r7             ; R7 << 1 (msb -> CY)
0x0bbf  ff          mov r7,a             ;
0x0bc0  ed          mov a,r5             ;
0x0bc1  33          rlc a                ; remainder << 1 with new bit
0x0bc2  fd          mov r5,a             ;
0x0bc3  40 07       jc 0x0bcc            ; remainder overflowed 8 bits
0x0bc5  98          subb a,r0            ; trial: remainder - divisor
0x0bc6  50 06       jnc 0x0bce           ; no borrow -> commit, set quotient bit
0x0bc8  d5 f0 f2    djnz b,0x0bbd        ; borrow -> quotient bit 0, next bit
0x0bcb  22          ret                  ; done
0x0bcc  c3          clr cy               ; 9-bit remainder case: clear borrow
0x0bcd  98          subb a,r0            ; remainder - divisor (always fits)
0x0bce  fd          mov r5,a             ; commit new remainder
0x0bcf  0f          inc r7               ; set quotient bit
0x0bd0  d5 f0 ea    djnz b,0x0bbd        ; 8 iterations
0x0bd3  22          ret                  ; done (also divide-by-zero exit)
; --- fcn_0bd4 = jmp_via_r2r1 ---
0x0bd4  8a 83       mov dph,r2           ; DPH = target high
0x0bd6  89 82       mov dpl,r1           ; DPL = target low
0x0bd8  e4          clr a                ; A = 0 (no offset)
0x0bd9  73          jmp @a+dptr          ; jump to R2:R1
; --- fcn_0bda = i2c_eeprom_write3 ---
0x0bda  ae 05       mov r6,0x05          ; R6 = IRAM 0x05 = bank-0 R5 (2nd byte param)
0x0bdc  90 ff c0    mov dptr,#0xffc0     ; DPTR -> I2CCTL/I2CSTA
0x0bdf  e0          movx a,@dptr         ; read I2C control/status
0x0be0  54 fc       anl a,#0xfc          ; clear bit0 STOP_WRITE + bit1 STOP_READ
0x0be2  f0          movx @dptr,a         ; no auto-STOP during the multi-byte write
0x0be3  90 ff c3    mov dptr,#0xffc3     ; DPTR -> I2CADR
0x0be6  74 a0       mov a,#0xa0          ; 0xA0 = EEPROM device addr, write direction
0x0be8  f0          movx @dptr,a         ; I2CADR = 0xA0
0x0be9  7d ff       mov r5,#0xff         ; settle-delay counter low = 0xFF
0x0beb  7c 00       mov r4,#0x0          ; counter high = 0
0x0bed  ed          mov a,r5             ; delay loop: A = counter low
0x0bee  1d          dec r5               ; counter low--
0x0bef  70 01       jnz 0x0bf2           ; skip high-byte borrow unless low was 0
0x0bf1  1c          dec r4               ; borrow into high byte
0x0bf2  ed          mov a,r5             ; test counter == 0
0x0bf3  4c          orl a,r4             ;
0x0bf4  70 f7       jnz 0x0bed           ; ~255-iteration busy wait
0x0bf6  90 ff c1    mov dptr,#0xffc1     ; DPTR -> I2CDATO
0x0bf9  ef          mov a,r7             ; byte 1 = R7 (EEPROM word-address high)
0x0bfa  f0          movx @dptr,a         ; I2CDATO = byte 1 -> starts the transfer
0x0bfb  90 ff c0    mov dptr,#0xffc0     ; poll loop: DPTR -> I2CSTA
0x0bfe  e0          movx a,@dptr         ; read status
0x0bff  30 e3 f9    jnb 0xe3,0x0bfb      ; wait ACC.3 = XMIT_DATA_EMPTY
0x0c02  90 ff c1    mov dptr,#0xffc1     ; DPTR -> I2CDATO
0x0c05  ee          mov a,r6             ; byte 2 = R6 (word-address low)
0x0c06  f0          movx @dptr,a         ; I2CDATO = byte 2
0x0c07  90 ff c0    mov dptr,#0xffc0     ; poll loop: DPTR -> I2CSTA
0x0c0a  e0          movx a,@dptr         ; read status
0x0c0b  30 e3 f9    jnb 0xe3,0x0c07      ; wait XMIT_DATA_EMPTY
0x0c0e  90 ff c0    mov dptr,#0xffc0     ; DPTR -> I2CCTL
0x0c11  e0          movx a,@dptr         ; read control
0x0c12  44 01       orl a,#0x1           ; set bit0 STOP_WRITE: STOP after next byte
0x0c14  f0          movx @dptr,a         ; write back
0x0c15  a3          inc dptr             ; DPTR -> 0xFFC1 = I2CDATO
0x0c16  eb          mov a,r3             ; byte 3 = R3 (data byte)
0x0c17  f0          movx @dptr,a         ; I2CDATO = byte 3 (followed by STOP)
0x0c18  90 ff c0    mov dptr,#0xffc0     ; final wait: DPTR -> I2CSTA
0x0c1b  e0          movx a,@dptr         ; read status
0x0c1c  20 e3 06    jb 0xe3,0x0c25       ; transmitter empty -> run post-write delay
0x0c1f  74 ff       mov a,#0xff          ; not empty yet: preload delay counter
0x0c21  fc          mov r4,a             ; R4 = 0xFF
0x0c22  fd          mov r5,a             ; R5 = 0xFF -> full ~65K delay once empty
0x0c23  80 f3       sjmp 0x0c18          ; keep polling
0x0c25  ed          mov a,r5             ; countdown loop: A = low
0x0c26  4c          orl a,r4             ; counter == 0?
0x0c27  60 07       jz 0x0c30            ; yes -> return
0x0c29  ed          mov a,r5             ; A = low (pre-decrement)
0x0c2a  1d          dec r5               ; low--
0x0c2b  70 f8       jnz 0x0c25           ; no borrow -> loop
0x0c2d  1c          dec r4               ; borrow into high
0x0c2e  80 f5       sjmp 0x0c25          ; ~65535 iterations = EEPROM write-cycle settle delay
0x0c30  22          ret                  ; write complete (delay skipped if TXE already set at first poll)
```

### 5.15 — 0x0c31–0x0c7c : fcn_0c31 (spi3w_write_reg — likely CS8427)

```
0x0c31  a9 07      mov r1,0x07        ; R1 := R7 (register-number arg saved)
0x0c33  7c 08      mov r4,#0x08       ; R4 = 8 bits to shift for current byte
0x0c35  7b 20      mov r3,#0x20       ; R3 = first byte = 0x20 (chip-address/WRITE byte)
0x0c37  7a 01      mov r2,#0x01       ; R2 = byte-stage counter (1=addr, 2=reg, 3=data)
0x0c39  c2 2f      clr 0x2f           ; bit 0x25.7 = 0: drive serial device chip-select LOW
0x0c3b  12 0e 56   lcall 0x0e56       ; -> fcn_0e56: shift out (applies CS low)
0x0c3e  ec         mov a,r4           ; A = bits remaining in current byte
0x0c3f  60 20      jz 0x0c61          ; byte done -> advance to next byte stage
0x0c41  78 01      mov r0,#0x01       ; rotate-count idiom: R0=1
0x0c43  af 03      mov r7,0x03        ; R7 := R3 (current shift byte)
0x0c45  ef         mov a,r7           ; A = shift byte
0x0c46  08         inc r0             ; R0=2 (one RL below)
0x0c47  80 01      sjmp 0x0c4a        ; enter rotate loop at the DJNZ
0x0c49  23         rl a               ; rotate left once (old bit7 -> bit0)
0x0c4a  d8 fd      djnz r0,0x0c49     ; executes RL exactly once
0x0c4c  fb         mov r3,a           ; store rotated byte back
0x0c4d  30 e0 05   jnb 0xe0,0x0c55    ; test ACC.0 = old MSB: 0 -> clear data pin
0x0c50  43 90 10   orl 0x90,#0x10     ; P1.4 = 1 (serial data high)
0x0c53  80 03      sjmp 0x0c58        ; go clock it
0x0c55  53 90 ef   anl 0x90,#0xef     ; P1.4 = 0 (serial data low)
0x0c58  43 90 08   orl 0x90,#0x08     ; P1.3 = 1: clock high (device samples data)
0x0c5b  53 90 f7   anl 0x90,#0xf7     ; P1.3 = 0: clock low (one full clock pulse per bit)
0x0c5e  1c         dec r4             ; one bit sent
0x0c5f  80 dd      sjmp 0x0c3e        ; next bit
0x0c61  ba 01 08   cjne r2,#0x01,0x0c6c ; finished stage 1 (0x20 address byte)?
0x0c64  7a 02      mov r2,#0x02       ; stage 2
0x0c66  ab 01      mov r3,0x01        ; R3 := R1 = register number
0x0c68  7c 08      mov r4,#0x08       ; 8 bits again
0x0c6a  80 d2      sjmp 0x0c3e        ; send register byte
0x0c6c  ba 02 08   cjne r2,#0x02,0x0c77 ; finished stage 2 (register)?
0x0c6f  7a 03      mov r2,#0x03       ; stage 3
0x0c71  ab 05      mov r3,0x05        ; R3 := R5 = data byte arg
0x0c73  7c 08      mov r4,#0x08       ; 8 bits again
0x0c75  80 c7      sjmp 0x0c3e        ; send data byte
0x0c77  d2 2f      setb 0x2f          ; all 3 bytes sent: bit 0x25.7 = 1, chip-select HIGH
0x0c79  12 0e 56   lcall 0x0e56       ; -> fcn_0e56: push shift-reg image again (releases CS)
0x0c7c  22         ret                ; done
```

**0x0c7d–0x0cc6 is DATA** — the VECINT dispatch table. Decoded in §6.2.

### 5.16 — 0x0cc7–0x0d10 : oep0_int_handler (OEP0_INT)

Reminder (§2.5.2): payload 0x01 → action 4 = analog; else → action 5 = S/PDIF.

```
0x0cc7  30 0b 40   jnb 0x0b,0x0d0a    ; no control-OUT data stage pending (bit 0x21.3 clear)? -> re-arm path
0x0cca  e5 0d      mov a,0x0d         ; A = pending-request state: 1=SET clock source, 2=SET input source
0x0ccc  b4 01 18   cjne a,#0x01,0x0ce7 ; not the clock-source request -> check state 2
0x0ccf  12 0b 1f   lcall 0x0b1f       ; -> fcn_0b1f: DPTR = 0xFA10 (EP0 OUT payload)
0x0cd2  e0         movx a,@dptr       ; A = payload byte 0
0x0cd3  ff         mov r7,a           ; keep a copy
0x0cd4  b4 44 03   cjne a,#0x44,0x0cda ; 0x44 = low byte of 44100?
0x0cd7  75 0a 07   mov 0x0a,#0x07     ; pending action 7 = 44.1 kHz
0x0cda  ef         mov a,r7           ; reload payload byte
0x0cdb  b4 80 03   cjne a,#0x80,0x0ce1 ; 0x80 = low byte of 48000?
0x0cde  75 0a 08   mov 0x0a,#0x08     ; pending action 8 = 48 kHz
0x0ce1  ef         mov a,r7           ; reload payload byte
0x0ce2  70 03      jnz 0x0ce7         ; nonzero and not 44/48 -> ignore
0x0ce4  75 0a 06   mov 0x0a,#0x06     ; payload 0 = S/PDIF sync -> action 6
0x0ce7  e5 0d      mov a,0x0d         ; re-check state byte
0x0ce9  b4 02 0f   cjne a,#0x02,0x0cfb ; not the input-source request -> finish up
0x0cec  12 0b 1f   lcall 0x0b1f       ; DPTR = 0xFA10 again (1-byte payload)
0x0cef  e0         movx a,@dptr       ; A = input-source byte (1=ANALOG, 2=S/PDIF)
0x0cf0  b4 01 05   cjne a,#0x01,0x0cf8 ; not 1 ->
0x0cf3  75 0a 04   mov 0x0a,#0x04     ; action 4 = select ANALOG input
0x0cf6  80 03      sjmp 0x0cfb        ; done classifying
0x0cf8  75 0a 05   mov 0x0a,#0x05     ; action 5 = select S/PDIF input
0x0cfb  c2 0b      clr 0x0b           ; data stage consumed: clear "OUT data expected" flag
0x0cfd  c2 0c      clr 0x0c           ; clear companion EP0 phase flag (bit 0x21.4)
0x0cff  90 ff 68   mov dptr,#0xff68   ; DPTR = IEPCNF0
0x0d02  e0         movx a,@dptr       ; read IEPCNF0
0x0d03  44 20      orl a,#0x20        ; set bit5 TOGGLE: status-stage IN will be DATA1
0x0d05  f0         movx @dptr,a       ; write IEPCNF0
0x0d06  12 0b 75   lcall 0x0b75       ; -> fcn_0b75: re-arm EP0 OUT; arm zero-length IN = status-stage ACK
0x0d09  22         ret                ; back to ISR epilogue
; --- tail: unexpected OEP0 interrupt (status stage of a control-IN, or abort) ---
0x0d0a  12 0b 4d   lcall 0x0b4d       ; -> fcn_0b4d: IEPCNF0 &= 0xD7 (clear TOGGLE|STALL); returns DPTR=OEPCNF0
0x0d0d  12 0b 2c   lcall 0x0b2c       ; -> fcn_0b2c: OEPCNF0 &= 0xD7 (store A), then IEPDCNTX0=0, OEPDCNTX0=0
0x0d10  22         ret                ; done
```

### 5.17 — 0x0d11–0x0d57 : i2c_eeprom_read_byte (fcn_0d11)

```
0x0d11  90 ff c0   mov dptr,#0xffc0   ; DPTR = I2CCTL/I2CSTA
0x0d14  e0         movx a,@dptr       ; read I2CCTL
0x0d15  54 fc      anl a,#0xfc        ; clear STPRD(bit1) and STPWR(bit0) — no auto-stop
0x0d17  f0         movx @dptr,a       ; write back I2CCTL
0x0d18  7e a0      mov r6,#0xa0       ; R6 = EEPROM device address, write direction (0xA0)
0x0d1a  90 ff c3   mov dptr,#0xffc3   ; DPTR = I2CADR
0x0d1d  ee         mov a,r6           ; A = 0xA0
0x0d1e  f0         movx @dptr,a       ; I2CADR = 0xA0 (address for write)
0x0d1f  90 ff c1   mov dptr,#0xffc1   ; DPTR = I2CDATO
0x0d22  ef         mov a,r7           ; A = sub-address high byte (arg)
0x0d23  f0         movx @dptr,a       ; send sub-address high (starts transfer)
0x0d24  90 ff c0   mov dptr,#0xffc0   ; DPTR = I2CCTL
0x0d27  e0         movx a,@dptr       ; read status
0x0d28  30 e3 f9   jnb 0xe3,0x0d24    ; spin until TXE (bit3) = 1
0x0d2b  90 ff c1   mov dptr,#0xffc1   ; DPTR = I2CDATO
0x0d2e  ed         mov a,r5           ; A = sub-address low byte (arg)
0x0d2f  f0         movx @dptr,a       ; send sub-address low
0x0d30  90 ff c0   mov dptr,#0xffc0   ; DPTR = I2CCTL
0x0d33  e0         movx a,@dptr       ; read status
0x0d34  30 e3 f9   jnb 0xe3,0x0d30    ; spin until TXE again
0x0d37  43 06 01   orl 0x06,#0x01     ; R6 |= 1 -> 0xA1 = EEPROM read direction
0x0d3a  90 ff c3   mov dptr,#0xffc3   ; DPTR = I2CADR
0x0d3d  ee         mov a,r6           ; A = 0xA1
0x0d3e  f0         movx @dptr,a       ; I2CADR = 0xA1 (repeated-start read)
0x0d3f  90 ff c1   mov dptr,#0xffc1   ; DPTR = I2CDATO
0x0d42  e4         clr a              ; A = 0 (dummy data)
0x0d43  f0         movx @dptr,a       ; dummy write clocks the read transaction
0x0d44  90 ff c0   mov dptr,#0xffc0   ; DPTR = I2CCTL
0x0d47  e0         movx a,@dptr       ; read I2CCTL
0x0d48  44 02      orl a,#0x02        ; set STPRD (bit1): stop after this read byte
0x0d4a  f0         movx @dptr,a       ; write I2CCTL
0x0d4b  90 ff c0   mov dptr,#0xffc0   ; DPTR = I2CCTL
0x0d4e  e0         movx a,@dptr       ; read status
0x0d4f  30 e7 f9   jnb 0xe7,0x0d4b    ; spin until RXF (bit7) = 1
0x0d52  90 ff c2   mov dptr,#0xffc2   ; DPTR = I2CDATI
0x0d55  e0         movx a,@dptr       ; read the EEPROM byte (clears RXF)
0x0d56  ff         mov r7,a           ; return value in R7
0x0d57  22         ret                ; done
```
Note: these waits are unbounded — a hung I2C bus would hang the firmware here.

### 5.18 — 0x0d58–0x0d9d : sof_int_handler (SOF_INT)

```
0x0d58  90 ff ec   mov dptr,#0xffec   ; DPTR = DMABCNT0H — DMA ch0 buffer content, high byte
0x0d5b  e0         movx a,@dptr       ; read high byte
0x0d5c  fe         mov r6,a           ; R6 = count high
0x0d5d  90 ff eb   mov dptr,#0xffeb   ; DPTR = DMABCNT0L
0x0d60  e0         movx a,@dptr       ; read low byte
0x0d61  7c 00      mov r4,#0x00       ; R4 = 0 (16-bit extend for divide)
0x0d63  24 00      add a,#0x00        ; +0: clears CY
0x0d65  ff         mov r7,a           ; R7 = count low
0x0d66  ec         mov a,r4           ; A = 0
0x0d67  3e         addc a,r6          ; A = count high + carry
0x0d68  fe         mov r6,a           ; R6:R7 = 16-bit DMABCNT0
0x0d69  ef         mov a,r7           ; compare against previous snapshot:
0x0d6a  65 1c      xrl a,0x1c         ; low byte vs IRAM 0x1c
0x0d6c  70 03      jnz 0x0d71         ; differs -> changed
0x0d6e  ee         mov a,r6           ; high byte
0x0d6f  65 1b      xrl a,0x1b         ; vs IRAM 0x1b
0x0d71  60 2a      jz 0x0d9d          ; unchanged since last SOF -> nothing to do
0x0d73  8e 1b      mov 0x1b,r6        ; snapshot new count high
0x0d75  8f 1c      mov 0x1c,r7        ; snapshot new count low
0x0d77  7d 06      mov r5,#0x06       ; divisor = 6 = audio OUT frame size (2ch x 24-bit)
0x0d79  12 0b 7f   lcall 0x0b7f       ; -> fcn_0b7f: R6:R7 / R5 -> quotient R7, remainder R5
0x0d7c  ed         mov a,r5           ; remainder low
0x0d7d  4c         orl a,r4           ; | remainder high
0x0d7e  60 1d      jz 0x0d9d          ; count is a multiple of 6 -> aligned, done
; --- misaligned: reset OUT EP2 + restart DMA channel 0 ---
0x0d80  90 ff e8   mov dptr,#0xffe8   ; DPTR = DMACTL0
0x0d83  e0         movx a,@dptr       ; read DMACTL0
0x0d84  54 7f      anl a,#0x7f        ; clear bit7 DMAEN: disable DMA channel 0
0x0d86  f0         movx @dptr,a       ; write DMACTL0
0x0d87  90 ff 9b   mov dptr,#0xff9b   ; DPTR = OEPDCNTX2
0x0d8a  e4         clr a              ; A = 0
0x0d8b  f0         movx @dptr,a       ; OEPDCNTX2 = 0: clear X-buffer count/NAK
0x0d8c  90 ff 9f   mov dptr,#0xff9f   ; DPTR = OEPDCNTY2
0x0d8f  f0         movx @dptr,a       ; OEPDCNTY2 = 0
0x0d90  90 ff 98   mov dptr,#0xff98   ; DPTR = OEPCNF2
0x0d93  74 c5      mov a,#0xc5        ; 0xC5 = OEPEN|ISO, BPS=5 -> 6 bytes/sample
0x0d95  f0         movx @dptr,a       ; rewrite OEPCNF2 (re-init iso OUT EP2)
0x0d96  90 ff e8   mov dptr,#0xffe8   ; DPTR = DMACTL0
0x0d99  e0         movx a,@dptr       ; read
0x0d9a  44 80      orl a,#0x80        ; set DMAEN: restart DMA channel 0
0x0d9c  f0         movx @dptr,a       ; write DMACTL0
0x0d9d  22         ret                ; back to ISR epilogue
```

### 5.19 — 0x0d9e–0x0dde : ep0_clamp_len_to_wlength (fcn_0d9e)

```
0x0d9e  90 ff 2f   mov dptr,#0xff2f   ; DPTR = SETPACK+7 = wLength high byte
0x0da1  e0         movx a,@dptr       ; read wLengthH
0x0da2  ff         mov r7,a           ; R7 = wLengthH
0x0da3  e5 0b      mov a,0x0b         ; A = our length high (IRAM 0x0b)
0x0da5  d3         setb cy            ; borrow-in for strict > compare
0x0da6  9f         subb a,r7          ; lenH - wLenH - 1
0x0da7  50 0f      jnc 0x0db8         ; no borrow -> lenH > wLenH -> must clamp
0x0da9  e0         movx a,@dptr       ; reload wLengthH
0x0daa  b5 0b 17   cjne a,0x0b,0x0dc4 ; lenH < wLenH -> no clamp; if equal, compare lows:
0x0dad  90 ff 2e   mov dptr,#0xff2e   ; DPTR = SETPACK+6 = wLength low byte
0x0db0  e0         movx a,@dptr       ; read wLengthL
0x0db1  ff         mov r7,a           ; R7 = wLengthL
0x0db2  e5 09      mov a,0x09         ; A = our length low (IRAM 0x09)
0x0db4  d3         setb cy            ; strict > compare again
0x0db5  9f         subb a,r7          ; lenL - wLenL - 1
0x0db6  40 0c      jc 0x0dc4          ; borrow -> lenL <= wLenL -> no clamp
; --- clamp: response longer than host asked for ---
0x0db8  90 ff 2e   mov dptr,#0xff2e   ; DPTR = wLength low
0x0dbb  e0         movx a,@dptr       ; read wLengthL
0x0dbc  f5 09      mov 0x09,a         ; length low := wLengthL
0x0dbe  a3         inc dptr           ; DPTR = 0xFF2F (wLength high)
0x0dbf  e0         movx a,@dptr       ; read wLengthH
0x0dc0  f5 0b      mov 0x0b,a         ; length high := wLengthH (len = wLength exactly)
0x0dc2  c2 0d      clr 0x0d           ; bit 0x21.5 = 0: response not shorter than requested
; --- second pass: set flag if len < wLength ---
0x0dc4  90 ff 2e   mov dptr,#0xff2e   ; DPTR = wLength low
0x0dc7  e0         movx a,@dptr       ; read wLengthL
0x0dc8  ff         mov r7,a           ; R7 = wLengthL
0x0dc9  e5 09      mov a,0x09         ; A = length low
0x0dcb  c3         clr cy             ; plain compare
0x0dcc  9f         subb a,r7          ; lenL - wLenL
0x0dcd  40 0d      jc 0x0ddc          ; lenL < wLenL -> shorter -> set flag
0x0dcf  e0         movx a,@dptr       ; reload wLengthL
0x0dd0  b5 09 0b   cjne a,0x09,0x0dde ; lows differ (lenL > wLenL): return without flag
0x0dd3  a3         inc dptr           ; lows equal: DPTR = wLength high
0x0dd4  e0         movx a,@dptr       ; read wLengthH
0x0dd5  ff         mov r7,a           ; R7 = wLengthH
0x0dd6  e5 0b      mov a,0x0b         ; A = length high
0x0dd8  c3         clr cy             ; compare
0x0dd9  9f         subb a,r7          ; lenH - wLenH
0x0dda  50 02      jnc 0x0dde         ; lenH >= wLenH -> equal lengths -> no flag
0x0ddc  d2 0d      setb 0x0d          ; bit 0x21.5 = 1: response shorter than wLength (short packet)
0x0dde  22         ret                ; end of range
```

### 5.20 — 0x0ddf–0x0e1a : usb_isr_int0_vecdispatch (INT0 USB ISR)

```
0x0ddf  c0 e0        push acc              ; ISR prologue: save ACC
0x0de1  c0 f0        push b                ; save B
0x0de3  c0 83        push dph              ; save DPTR high
0x0de5  c0 82        push dpl              ; save DPTR low
0x0de7  c0 d0        push psw              ; save PSW (incl. current register bank)
0x0de9  75 d0 10     mov psw,#0x10         ; select register bank 2 (Keil 'using 2')
0x0dec  c2 af        clr 0xaf              ; EA=0, global interrupt disable
0x0dee  90 ff b2     mov dptr,#0xffb2      ; DPTR -> VECINT = USB interrupt vector register
0x0df1  e0           movx a,@dptr          ; A = VECINT (which USB source fired: 0x00..0x24)
0x0df2  25 e0        add a,acc             ; A = 2*VECINT
0x0df4  24 7d        add a,#0x7d           ; low byte of table base 0x0C7D
0x0df6  f5 82        mov dpl,a             ; DPL = low(0x0C7D + 2*VECINT)
0x0df8  e4           clr a                 ; A = 0
0x0df9  34 0c        addc a,#0xc           ; A = 0x0C + carry
0x0dfb  f5 83        mov dph,a             ; DPTR = 0x0C7D + 2*VECINT
0x0dfd  e4           clr a                 ; offset 0
0x0dfe  93           movc a,@a+dptr        ; A = table[0] = handler address HIGH byte
0x0dff  fe           mov r6,a              ; R6 = handler high byte
0x0e00  74 01        mov a,#0x1            ; offset 1
0x0e02  93           movc a,@a+dptr        ; A = table[1] = handler address LOW byte
0x0e03  aa 16        mov r2,0x16           ; R2 = bank-2 R6 = handler HIGH (Keil idiom)
0x0e05  f9           mov r1,a              ; R1 = handler LOW
0x0e06  12 0b d4     lcall 0x0bd4          ; -> fcn_0bd4: call handler R2:R1; handler RETs back here
0x0e09  90 ff b2     mov dptr,#0xffb2      ; DPTR -> VECINT again
0x0e0c  e4           clr a                 ; A = 0
0x0e0d  f0           movx @dptr,a          ; VECINT = 0: clear/acknowledge the interrupt vector
0x0e0e  d2 af        setb 0xaf             ; EA=1, re-enable interrupts
0x0e10  d0 d0        pop psw               ; restore PSW / register bank
0x0e12  d0 82        pop dpl               ; restore DPTR low
0x0e14  d0 83        pop dph               ; restore DPTR high
0x0e16  d0 f0        pop b                 ; restore B
0x0e18  d0 e0        pop acc               ; restore ACC
0x0e1a  32           reti                  ; return from interrupt
```

### 5.21 — 0x0e1b–0x0e55 : panel_state_cycle_A (fcn_0e1b)

```
0x0e1b  20 28 0c     jb 0x28,0x0e2a        ; if state bit 0x28 (0x25.0) set, check second state bit
0x0e1e  d2 28        setb 0x28             ; state was (0,x): enter state (1,1)
0x0e20  d2 2a        setb 0x2a             ; set 0x25.2
0x0e22  d2 10        setb 0x10             ; output bits 0x22.0-2 := 1,0,1
0x0e24  c2 11        clr 0x11              ; 0x22.1 = 0
0x0e26  d2 12        setb 0x12             ; 0x22.2 = 1
0x0e28  80 1c        sjmp 0x0e46           ; to common bit-0x16 tail
0x0e2a  30 28 0f     jnb 0x28,0x0e3c       ; recheck 0x28
0x0e2d  30 2a 0c     jnb 0x2a,0x0e3c       ; state (1,0)? -> wrap to state (0,0)
0x0e30  d2 28        setb 0x28             ; state was (1,1): enter state (1,0)
0x0e32  c2 2a        clr 0x2a              ; clear 0x25.2
0x0e34  d2 10        setb 0x10             ; output bits := 1,1,0
0x0e36  d2 11        setb 0x11             ; 0x22.1 = 1
0x0e38  c2 12        clr 0x12              ; 0x22.2 = 0
0x0e3a  80 0a        sjmp 0x0e46           ; to common tail
0x0e3c  c2 28        clr 0x28             ; state was (1,0): wrap to state (0,0)
0x0e3e  c2 2a        clr 0x2a             ; clear 0x25.2
0x0e40  c2 10        clr 0x10             ; output bits := 0,1,1
0x0e42  d2 11        setb 0x11             ; 0x22.1 = 1
0x0e44  d2 12        setb 0x12             ; 0x22.2 = 1
0x0e46  20 2c 02     jb 0x2c,0x0e4b        ; common tail: if flag 0x2c (0x25.4) clear...
0x0e49  d2 16        setb 0x16             ; ...set output bit 0x16 (0x22.6). Semantics UNKNOWN
0x0e4b  30 2c 02     jnb 0x2c,0x0e50       ; if 0x2c set...
0x0e4e  c2 16        clr 0x16             ; ...clear 0x16
0x0e50  30 2d 02     jnb 0x2d,0x0e55       ; if flag 0x2d (0x25.5) set...
0x0e53  c2 16        clr 0x16             ; ...also clear 0x16. Net: 0x16 = !0x2c && !0x2d
0x0e55  22           ret                   ; done
```

### 5.22 — 0x0e56–0x0e8e : shiftreg_out16_p1 (fcn_0e56)

```
0x0e56  7e 08        mov r6,#0x8           ; bit counter = 8
0x0e58  af 23        mov r7,0x23           ; R7 = first byte to shift (IRAM 0x23)
0x0e5a  d2 30        setb 0x30             ; flag: second byte (IRAM 0x25) still pending
0x0e5c  ee           mov a,r6              ; loop head: A = bits remaining
0x0e5d  60 1e        jz 0x0e7d             ; all 8 bits sent -> byte-done path
0x0e5f  78 01        mov r0,#0x1           ; rotate-count seed
0x0e61  ef           mov a,r7              ; A = current shift byte
0x0e62  08           inc r0                ; R0 = 2 -> one RL
0x0e63  80 01        sjmp 0x0e66           ; enter rotate loop at DJNZ
0x0e65  23           rl a                  ; rotate left 1
0x0e66  d8 fd        djnz r0,0x0e65        ; executes RL once
0x0e68  ff           mov r7,a              ; save rotated byte back
0x0e69  30 e0 05     jnb 0xe0,0x0e71       ; test ACC.0 = just-rotated-out MSB
0x0e6c  43 90 01     orl 0x90,#0x1         ; data bit 1: P1.0 = 1
0x0e6f  80 03        sjmp 0x0e74           ; to clock pulse
0x0e71  53 90 fe     anl 0x90,#0xfe        ; data bit 0: P1.0 = 0
0x0e74  43 90 04     orl 0x90,#0x4         ; clock high: P1.2 = 1
0x0e77  53 90 fb     anl 0x90,#0xfb        ; clock low: P1.2 = 0
0x0e7a  1e           dec r6                ; one bit done
0x0e7b  80 df        sjmp 0x0e5c           ; next bit
0x0e7d  30 30 08     jnb 0x30,0x0e88       ; byte done: if second byte already sent -> latch
0x0e80  c2 30        clr 0x30              ; mark second byte in progress
0x0e82  af 25        mov r7,0x25           ; R7 = second byte (IRAM 0x25)
0x0e84  7e 08        mov r6,#0x8           ; 8 more bits
0x0e86  80 d4        sjmp 0x0e5c           ; shift second byte
0x0e88  43 90 02     orl 0x90,#0x2         ; latch high: P1.1 = 1
0x0e8b  53 90 fd     anl 0x90,#0xfd        ; latch low: P1.1 = 0 (16 bits latched)
0x0e8e  22           ret                   ; done
```

### 5.23 — 0x0e8f–0x0ec6 : panel_state_cycle_B (fcn_0e8f)

```
0x0e8f  20 29 0c     jb 0x29,0x0e9e        ; if state bit 0x29 (0x25.1) set, check 0x2b
0x0e92  d2 29        setb 0x29             ; state (0,x) -> (1,1)
0x0e94  d2 2b        setb 0x2b             ; set 0x25.3
0x0e96  d2 13        setb 0x13             ; outputs 0x22.3-5 := 1,0,1
0x0e98  c2 14        clr 0x14              ; 0x22.4 = 0
0x0e9a  d2 15        setb 0x15             ; 0x22.5 = 1
0x0e9c  80 19        sjmp 0x0eb7           ; to common tail
0x0e9e  30 2b 0c     jnb 0x2b,0x0ead       ; state (1,0)? -> wrap to (0,0)
0x0ea1  d2 29        setb 0x29             ; state (1,1) -> (1,0)
0x0ea3  c2 2b        clr 0x2b              ; clear 0x25.3
0x0ea5  d2 13        setb 0x13             ; outputs := 1,1,0
0x0ea7  d2 14        setb 0x14             ; 0x22.4 = 1
0x0ea9  c2 15        clr 0x15              ; 0x22.5 = 0
0x0eab  80 0a        sjmp 0x0eb7           ; to common tail
0x0ead  c2 29        clr 0x29             ; state (1,0) -> (0,0)
0x0eaf  c2 2b        clr 0x2b             ; clear 0x25.3
0x0eb1  c2 13        clr 0x13             ; outputs := 0,1,1
0x0eb3  d2 14        setb 0x14             ; 0x22.4 = 1
0x0eb5  d2 15        setb 0x15             ; 0x22.5 = 1
0x0eb7  20 2c 02     jb 0x2c,0x0ebc        ; common tail (same as fcn_0e1b):
0x0eba  d2 16        setb 0x16             ; 0x16 = 1 if 0x2c clear (semantics UNKNOWN)
0x0ebc  30 2c 02     jnb 0x2c,0x0ec1       ; if 0x2c set...
0x0ebf  c2 16        clr 0x16             ; ...clear 0x16
0x0ec1  30 2d 02     jnb 0x2d,0x0ec6       ; if 0x2d set...
0x0ec4  c2 16        clr 0x16             ; ...clear 0x16. Net: 0x16 = !0x2c && !0x2d
0x0ec6  22           ret                   ; done
```

### 5.24 — 0x0ec7–0x0ef2 : ACG synthesizer programming chain

```
0x0ec7  f0           movx @dptr,a          ; entry A: write caller's value to caller's SFR, then fall through
0x0ec8  90 ff e6     mov dptr,#0xffe6      ; entry B: DPTR -> ACGFRQ1
0x0ecb  74 a8        mov a,#0xa8           ; middle byte of N
0x0ecd  f0           movx @dptr,a          ; ACGFRQ1 = 0xA8
0x0ece  90 ff e5     mov dptr,#0xffe5      ; DPTR -> ACGFRQ2 (MSB)
0x0ed1  74 61        mov a,#0x61           ; integer part
0x0ed3  f0           movx @dptr,a          ; ACGFRQ2 = 0x61
0x0ed4  90 ff e7     mov dptr,#0xffe7      ; DPTR -> ACGFRQ0 (LSB); write loads synth 1
0x0ed7  74 0f        mov a,#0xf            ; fractional trim
0x0ed9  f0           movx @dptr,a          ; ACGFRQ0 = 0x0F -> synth 1 = 24.576 MHz
0x0eda  90 ff f8     mov dptr,#0xfff8      ; DPTR -> ACG2FRQ1
0x0edd  74 a8        mov a,#0xa8           ; same N for synthesizer 2
0x0edf  f0           movx @dptr,a          ; ACG2FRQ1 = 0xA8
0x0ee0  90 ff f7     mov dptr,#0xfff7      ; DPTR -> ACG2FRQ2
0x0ee3  74 61        mov a,#0x61           ; integer part
0x0ee5  f0           movx @dptr,a          ; ACG2FRQ2 = 0x61
0x0ee6  74 0f        mov a,#0xf            ; A = 0x0F for ACG2FRQ0, fall into fcn_0ee8
0x0ee8  90 ff f9     mov dptr,#0xfff9      ; entry C (caller 0x0766 arrives with A=0x20): DPTR -> ACG2FRQ0
0x0eeb  f0           movx @dptr,a          ; ACG2FRQ0 = A -> loads synthesizer 2
0x0eec  90 ff e1     mov dptr,#0xffe1      ; DPTR -> ACGCTL
0x0eef  74 06        mov a,#0x6            ; DIVEN=1, MCLKO2S=10 (acg2_clk/M2), outputs disabled
0x0ef1  f0           movx @dptr,a          ; ACGCTL = 0x06 (DPTR left at 0xFFE1 for callers)
0x0ef2  22           ret                   ; return
```

### 5.25 — 0x0ef3–0x0efb : acg_dividers_div2 (fcn_0ef3 / fcn_0ef4)

```
0x0ef3  a3           inc dptr              ; entry A: DPTR 0xFFE1 -> 0xFFE2 = ACG1DCTL
0x0ef4  74 10        mov a,#0x10           ; entry B: DIVM=0001b (÷2), DIVI=000b (÷1)
0x0ef6  f0           movx @dptr,a          ; ACG1DCTL = 0x10: ÷2 -> 12.288 MHz
0x0ef7  90 ff f6     mov dptr,#0xfff6      ; DPTR -> ACG2DCTL
0x0efa  f0           movx @dptr,a          ; ACG2DCTL = 0x10: ÷2 as well
0x0efb  22           ret                   ; done
```

### 5.26 — 0x0efc–0x0f30 : shiftreg_out8_p1hi (fcn_0efc)

```
0x0efc  7e 08        mov r6,#0x8           ; bit counter = 8
0x0efe  af 22        mov r7,0x22           ; R7 = byte to shift = IRAM 0x22
0x0f00  53 90 bf     anl 0x90,#0xbf        ; P1.6 = 0: latch/strobe line low before shifting
0x0f03  78 01        mov r0,#0x1           ; rotate-count seed
0x0f05  ef           mov a,r7              ; A = current shift byte
0x0f06  08           inc r0                ; R0 = 2 -> one RL
0x0f07  80 01        sjmp 0x0f0a           ; enter rotate loop at DJNZ
0x0f09  23           rl a                  ; rotate left 1
0x0f0a  d8 fd        djnz r0,0x0f09        ; executes RL exactly once
0x0f0c  ff           mov r7,a              ; save rotated byte
0x0f0d  30 e0 05     jnb 0xe0,0x0f15       ; test ACC.0 = rotated-out MSB
0x0f10  43 90 80     orl 0x90,#0x80        ; data bit 1: P1.7 = 1
0x0f13  80 03        sjmp 0x0f18           ; to clock pulse
0x0f15  53 90 7f     anl 0x90,#0x7f        ; data bit 0: P1.7 = 0
0x0f18  43 90 20     orl 0x90,#0x20        ; clock high: P1.5 = 1
0x0f1b  53 90 df     anl 0x90,#0xdf        ; clock low: P1.5 = 0
0x0f1e  de e3        djnz r6,0x0f03        ; next of 8 bits
0x0f20  30 1e 04     jnb 0x1e,0x0f27       ; flag 0x1e (0x23.6) clear -> normal latch pulse
0x0f23  43 90 c0     orl 0x90,#0xc0        ; 0x1e set: P1.7=1 AND P1.6=1, left high on exit — UNKNOWN
0x0f26  22           ret                   ; done (latch left high)
0x0f27  53 90 7f     anl 0x90,#0x7f        ; normal path: P1.7 = 0 (data line idle low)
0x0f2a  43 90 40     orl 0x90,#0x40        ; latch high: P1.6 = 1
0x0f2d  53 90 bf     anl 0x90,#0xbf        ; latch low: P1.6 = 0 (8 bits latched)
0x0f30  22           ret                   ; done
```

### 5.27 — 0x0f31–0x0f63 : p3_edge_poll_dispatch (fcn_0f31)

```
0x0f31  e4        clr a                 ; A = 0
0x0f32  ff        mov r7,a              ; R7 = 0: return flag "no event yet"
0x0f33  ae b0     mov r6,0xb0           ; R6 = P3 (SFR 0xB0): sample GPIO port 3
0x0f35  ee        mov a,r6              ; A = new P3 sample
0x0f36  b5 20 01  cjne a,0x20,0x0f3a    ; compare with byte 0x20 = last P3 sample
0x0f39  22        ret                   ; unchanged -> return, R7=0
0x0f3a  20 05 0a  jb 0x05,0x0f47        ; prev P3.5 (byte 0x20 bit5) already 1 -> skip
0x0f3d  ee        mov a,r6              ; A = new sample
0x0f3e  30 e5 06  jnb acc.5,0x0f47      ; new P3.5 still 0 -> no edge, skip
0x0f41  12 10 20  lcall 0x1020          ; P3.5 rising edge -> fcn_1020: toggle bit 0x1e
0x0f44  43 07 01  orl 0x07,#0x1         ; set return flag: R7 |= 1
0x0f47  20 03 0a  jb 0x03,0x0f54        ; prev P3.3 was 1 -> skip
0x0f4a  ee        mov a,r6              ; A = new sample
0x0f4b  30 e3 06  jnb acc.3,0x0f54      ; new P3.3 = 0 -> skip
0x0f4e  12 0e 1b  lcall 0x0e1b          ; P3.3 rising edge -> fcn_0e1b (panel_state_cycle_A)
0x0f51  43 07 01  orl 0x07,#0x1         ; R7 |= 1
0x0f54  20 04 0a  jb 0x04,0x0f61        ; prev P3.4 was 1 -> skip
0x0f57  ee        mov a,r6              ; A = new sample
0x0f58  30 e4 06  jnb acc.4,0x0f61      ; new P3.4 = 0 -> skip
0x0f5b  12 0e 8f  lcall 0x0e8f          ; P3.4 rising edge -> fcn_0e8f (panel_state_cycle_B)
0x0f5e  43 07 01  orl 0x07,#0x1         ; R7 |= 1
0x0f61  8e 20     mov 0x20,r6           ; byte 0x20 = new sample becomes "last P3"
0x0f63  22        ret                   ; return R7 flag to main loop
```

### 5.28 — 0x0f64–0x0f90 : usb_rstr_handler (RSTR_INT)

```
0x0f64  12 0b 75  lcall 0x0b75          ; helper: OEPDCNTX0=0, IEPDCNTX0=0; returns A=0
0x0f67  90 ff 9b  mov dptr,#0xff9b      ; DPTR -> OEPDCNTX2
0x0f6a  f0        movx @dptr,a          ; OEPDCNTX2 = 0: clear OUT-EP2 count/NAK
0x0f6b  90 ff 63  mov dptr,#0xff63      ; DPTR -> IEPDCNTX1
0x0f6e  f0        movx @dptr,a          ; IEPDCNTX1 = 0: clear IN-EP1 count/NAK
0x0f6f  90 ff ff  mov dptr,#0xffff      ; DPTR -> USBFADR
0x0f72  f0        movx @dptr,a          ; USBFADR = 0: revert to default address
0x0f73  90 ff a8  mov dptr,#0xffa8      ; DPTR -> OEPCNF0
0x0f76  74 84     mov a,#0x84           ; 0x84 = OEPEN | OEPIE
0x0f78  f0        movx @dptr,a          ; OEPCNF0 = 0x84
0x0f79  90 ff 68  mov dptr,#0xff68      ; DPTR -> IEPCNF0
0x0f7c  f0        movx @dptr,a          ; IEPCNF0 = 0x84
0x0f7d  90 ff fc  mov dptr,#0xfffc      ; DPTR -> USBCTL
0x0f80  e0        movx a,@dptr          ; read USBCTL (RMW)
0x0f81  44 c0     orl a,#0xc0           ; set CONT (bit7) | FEN (bit6)
0x0f83  f0        movx @dptr,a          ; write back USBCTL
0x0f84  c2 0a     clr 0x0a              ; clear flag bit 0x0a (0x21.2) — UNKNOWN precise meaning
0x0f86  c2 0e     clr 0x0e              ; clear flag bit 0x0e (0x21.6)
0x0f88  c2 08     clr 0x08              ; clear flag bit 0x08 (0x21.0)
0x0f8a  c2 09     clr 0x09              ; clear flag bit 0x09 (0x21.1)
0x0f8c  a3        inc dptr              ; DPTR: 0xFFFC -> 0xFFFD = USBIMSK
0x0f8d  74 9f     mov a,#0x9f           ; 0x9F = RSTR|SOF|PSOF|SETUP|rsvd|STPOW; SUSR/RESR masked
0x0f8f  f0        movx @dptr,a          ; USBIMSK = 0x9F: re-arm interrupt mask after reset
0x0f90  22        ret                   ; back to INT0 ISR
```

### 5.29 — 0x0f91–0x0fb9 : ep0_in_done_handler (IEP0_INT)

```
0x0f91  30 0b 04  jnb 0x0b,0x0f98       ; bit 0x0b = more EP0 IN data pending?
0x0f94  12 0b 63  lcall 0x0b63          ; yes -> helper 0x0b63: queue next <=8-byte chunk, clear NAK
0x0f97  22        ret                   ; done — chunk queued
0x0f98  30 0c 0a  jnb 0x0c,0x0fa5       ; bit 0x0c = completion pending?
0x0f9b  c2 0c     clr 0x0c              ; consume the completion flag
0x0f9d  90 ff a8  mov dptr,#0xffa8      ; DPTR -> OEPCNF0
0x0fa0  e0        movx a,@dptr          ; read OEPCNF0
0x0fa1  44 20     orl a,#0x20           ; set TOGGLE (bit5): status-stage OUT must be DATA1
0x0fa3  80 11     sjmp 0x0fb6           ; -> shared tail: 0x0b2c writes A to OEPCNF0
0x0fa5  e5 0d     mov a,0x0d            ; byte 0x0d = deferred-request code (5 = SET_ADDRESS)
0x0fa7  b4 05 09  cjne a,#0x5,0x0fb3    ; is it SET_ADDRESS?
0x0faa  90 ff ff  mov dptr,#0xffff      ; DPTR -> USBFADR
0x0fad  e5 0e     mov a,0x0e            ; byte 0x0e = new device address (saved at 0x0254)
0x0faf  f0        movx @dptr,a          ; USBFADR = new address — after the status IN completed
0x0fb0  e4        clr a                 ; A = 0
0x0fb1  f5 0d     mov 0x0d,a            ; clear deferred-request code
0x0fb3  12 0b 4d  lcall 0x0b4d          ; helper: IEPCNF0 &= 0xD7, stages OEPCNF0 write (no write yet)
0x0fb6  12 0b 2c  lcall 0x0b2c          ; helper: MOVX @DPTR,A — completes the OEPCNF0 write
0x0fb9  22        ret                   ; back to INT0 ISR
```

**0x0fba–0x0fe1 is DATA** — the Keil ?C_INIT table. Decoded in §6.4.

### 5.30 — 0x0fe2–0x1000 : codec-port / DMA / control-pair helpers

```
; --- fcn_0fe2 = cport_cnf3_write_enable (A = 0xAC from 0x0356, 0xA8 from 0x035e) ---
0x0fe2  90 ff de  mov dptr,#0xffde      ; DPTR -> CPTCNF3
0x0fe5  f0        movx @dptr,a          ; CPTCNF3 = caller's A
0x0fe6  90 ff d5  mov dptr,#0xffd5      ; DPTR -> CPTRXCNF3
0x0fe9  f0        movx @dptr,a          ; CPTRXCNF3 = same value
0x0fea  90 ff b1  mov dptr,#0xffb1      ; DPTR -> GLOBCTL
0x0fed  e0        movx a,@dptr          ; read GLOBCTL (RMW)
0x0fee  44 01     orl a,#0x1            ; set CPTEN (bit0)
0x0ff0  f0        movx @dptr,a          ; GLOBCTL write-back: codec port enabled
0x0ff1  22        ret                   ; done
; --- fcn_0ff2 = dma0_disable ---
0x0ff2  90 ff e8  mov dptr,#0xffe8      ; DPTR -> DMACTL0
0x0ff5  e0        movx a,@dptr          ; read DMACTL0 (RMW keeps EPDIR/EPNUM)
0x0ff6  54 7f     anl a,#0x7f           ; clear DMAEN (bit7)
0x0ff8  f0        movx @dptr,a          ; DMA channel 0 halted
0x0ff9  22        ret                   ; done
; --- fcn_0ffa = stage_ctrl_pair_12_00 ---
0x0ffa  75 2c 12  mov 0x2c,#0x12        ; IDATA byte 0x2c = 0x12 (register half of a (reg,val) pair)
0x0ffd  e4        clr a                 ; A = 0
0x0ffe  f5 2d     mov 0x2d,a            ; IDATA byte 0x2d = 0x00 (value half) — target device/register UNKNOWN
0x1000  22        ret                   ;
```

### 5.31 — 0x1001–0x1015 : ep0_stall_both (fcn_1001)

```
0x1001  90 ff 68  mov dptr,#0xff68      ; DPTR -> IEPCNF0
0x1004  e0        movx a,@dptr          ; read (RMW)
0x1005  44 08     orl a,#0x8            ; set STALL (bit3)
0x1007  f0        movx @dptr,a          ; EP0 IN now returns STALL handshake
0x1008  90 ff a8  mov dptr,#0xffa8      ; DPTR -> OEPCNF0
0x100b  e0        movx a,@dptr          ; read (RMW)
0x100c  44 08     orl a,#0x8            ; set STALL on EP0 OUT too
0x100e  12 0b 2c  lcall 0x0b2c          ; shared tail helper performs the MOVX @DPTR,A write
0x1011  c2 0b     clr 0x0b              ; abandon any in-progress EP0 IN data phase
0x1013  c2 0c     clr 0x0c              ; and any pending completion flag
0x1015  22        ret                   ; done
```

### 5.32 — 0x1016–0x101f : timer0_tick_isr (TF0 ISR)

```
0x1016  c2 af     clr 0xaf              ; EA = 0: mask interrupts while touching shared state
0x1018  d2 20     setb 0x20             ; set tick flag, bit 0x20 (IRAM byte 0x24 bit0)
0x101a  75 8c ce  mov 0x8c,#0xce        ; TH0 = 0xCE: reload timer 0 high byte
0x101d  d2 af     setb 0xaf             ; EA = 1
0x101f  32        reti                  ; end of ISR
```

### 5.33 — 0x1020–0x1028 : toggle_bit1E_state (fcn_1020)

```
0x1020  30 1e 03     jnb 0x1e,0x1026       ; if state bit 0x1E (IRAM 0x23.6) clear, jump to set-branch
0x1023  c2 1e        clr 0x1e              ; bit was set: clear it (toggle -> 0)
0x1025  22           ret                   ; return with bit 0x1E = 0
0x1026  d2 1e        setb 0x1e             ; bit was clear: set it (toggle -> 1)
0x1028  22           ret                   ; return with bit 0x1E = 1
```

### 5.34 — 0x1029–0x1035 : padding (13× 0x22 = RET, unreachable)

```
0x1029  22           ret                   ; padding byte 0x22, unreachable (no xref)
0x102a  22           ret                   ; padding byte 0x22, unreachable
0x102b  22           ret                   ; padding byte 0x22, unreachable
0x102c  22           ret                   ; padding byte 0x22, unreachable
0x102d  22           ret                   ; padding byte 0x22, unreachable
0x102e  22           ret                   ; padding byte 0x22, unreachable
0x102f  22           ret                   ; padding byte 0x22, unreachable
0x1030  22           ret                   ; padding byte 0x22, unreachable
0x1031  22           ret                   ; padding byte 0x22, unreachable
0x1032  22           ret                   ; padding byte 0x22, unreachable
0x1033  22           ret                   ; padding byte 0x22, unreachable
0x1034  22           ret                   ; padding byte 0x22, unreachable
0x1035  22           ret                   ; padding byte 0x22, unreachable
```

### 5.35 — 0x1036–0x1FED : erased fill

All bytes 0xFF (unprogrammed flash). Not code. `set(bytes[0x1036:]) == {0xFF}` verified.

---

## 6. Data Section Decodes

### 6.1 USB descriptor block (0x057d–0x070e)

Ghidra force-disassembled this region as instructions; it is data, verified byte-by-byte
with `xxd` (file-offset == CPU-address). Length fields sum exactly to the observed layout.

#### 6.1.1 Device descriptor (0x057d, 18 bytes)

```
0x057d  12          ; bLength = 18
0x057e  01          ; bDescriptorType = DEVICE (1)
0x057f  10 01       ; bcdUSB = 0x0110 (USB 1.1)
0x0581  00          ; bDeviceClass = 0 (per-interface)
0x0582  00          ; bDeviceSubClass = 0
0x0583  00          ; bDeviceProtocol = 0
0x0584  08          ; bMaxPacketSize0 = 8
0x0585  ba 0d       ; idVendor = 0x0DBA (Digidesign)
0x0587  00 10       ; idProduct = 0x1000 (Mbox 1)
0x0589  22 00       ; bcdDevice = 0x0022 — REV 22
0x058b  01          ; iManufacturer = string index 1 ("Digidesign Inc", 0x0691)
0x058c  02          ; iProduct = string index 2 ("Mbox USB Audio Device ...", 0x06af)
0x058d  00          ; iSerialNumber = 0 (none)
0x058e  01          ; bNumConfigurations = 1
```

#### 6.1.2 Configuration descriptor set #1 — USB Audio Class (0x058f, wTotalLength = 200)

```
; --- Configuration descriptor @0x058f (9) ---
0x058f  09 02       ; bLength 9, CONFIGURATION (2)
0x0591  c8 00       ; wTotalLength = 200 (0x058f..0x0656)
0x0593  03          ; bNumInterfaces = 3 (AC + 2x AS)
0x0594  01          ; bConfigurationValue = 1
0x0595  00          ; iConfiguration = 0
0x0596  80          ; bmAttributes = 0x80 (bus-powered)
0x0597  f0          ; bMaxPower = 0xF0 = 240 -> 480 mA
; --- Interface 0 alt 0 @0x0598 (9): AudioControl ---
0x0598  09 04 00 00 00 01 01 00 00
        ; iface 0, alt 0, 0 endpoints, class 01 Audio, subclass 01 AudioControl, proto 0, iInterface 0
; --- CS AC HEADER @0x05a1 (10) ---
0x05a1  0a 24 01    ; bLength 10, CS_INTERFACE (0x24), HEADER (1)
0x05a4  00 01       ; bcdADC = 0x0100
0x05a6  48 00       ; wTotalLength = 0x0048 = 72
0x05a8  02          ; bInCollection = 2
0x05a9  01 02       ; baInterfaceNr = {1, 2}
; --- Input Terminal @0x05ab (12): ID 1 ---
0x05ab  0c 24 02 01 ; INPUT_TERMINAL, bTerminalID = 1
0x05af  01 01       ; wTerminalType = 0x0101 USB Streaming (playback path from host)
0x05b1  05          ; bAssocTerminal = 5 — ANOMALY: 5 is the Selector UNIT (byte-verified)
0x05b2  02          ; bNrChannels = 2
0x05b3  03 00       ; wChannelConfig = 0x0003 (Left Front + Right Front)
0x05b5  00 00       ; iChannelNames 0, iTerminal 0
; --- Input Terminal @0x05b7 (12): ID 2 ---
0x05b7  0c 24 02 02 ; INPUT_TERMINAL, bTerminalID = 2
0x05bb  01 06       ; wTerminalType = 0x0601 Analog Connector (analog inputs)
0x05bd  03          ; bAssocTerminal = 3 (paired with OT 3)
0x05be  02          ; bNrChannels = 2
0x05bf  03 00       ; wChannelConfig = 0x0003 (L+R)
0x05c1  00 00       ; iChannelNames 0, iTerminal 0
; --- Input Terminal @0x05c3 (12): ID 6 ---
0x05c3  0c 24 02 06 ; INPUT_TERMINAL, bTerminalID = 6
0x05c7  05 06       ; wTerminalType = 0x0605 S/PDIF Interface (digital input)
0x05c9  00          ; bAssocTerminal = 0
0x05ca  02          ; bNrChannels = 2
0x05cb  03 00       ; wChannelConfig = 0x0003 (L+R)
0x05cd  00 00       ; iChannelNames 0, iTerminal 0
; --- Output Terminal @0x05cf (9): ID 3 ---
0x05cf  09 24 03 03 ; OUTPUT_TERMINAL, bTerminalID = 3
0x05d3  03 06       ; wTerminalType = 0x0603 Line Connector (analog line out)
0x05d5  02          ; bAssocTerminal = 2 (paired with IT 2)
0x05d6  01          ; bSourceID = 1 (fed by USB streaming IT 1 -> playback path)
0x05d7  00          ; iTerminal = 0
; --- Output Terminal @0x05d8 (9): ID 4 ---
0x05d8  09 24 03 04 ; OUTPUT_TERMINAL, bTerminalID = 4
0x05dc  01 01       ; wTerminalType = 0x0101 USB Streaming (capture path to host)
0x05de  01          ; bAssocTerminal = 1
0x05df  05          ; bSourceID = 5 (fed by Selector Unit 5)
0x05e0  00          ; iTerminal = 0
; --- Selector Unit @0x05e1 (8): ID 5 ---
0x05e1  08 24 05 05 ; SELECTOR_UNIT, bUnitID = 5
0x05e5  02          ; bNrInPins = 2
0x05e6  02 06       ; baSourceID = {2 analog, 6 S/PDIF} — the capture-source selector
0x05e8  00          ; iSelector = 0
; Topology: IT1(USB) -> OT3(line out);  IT2(analog)+IT6(SPDIF) -> SU5 -> OT4(USB in)
; --- Interface 1 alt 0 @0x05e9 (9): AudioStreaming, zero-bandwidth ---
0x05e9  09 04 01 00 00 01 02 00 00
        ; iface 1, alt 0, 0 EPs, class 01, subclass 02 AudioStreaming, proto 0
; --- Interface 1 alt 1 @0x05f2 (9): AudioStreaming, capture ---
0x05f2  09 04 01 01 01 01 02 00 00
        ; iface 1, alt 1, 1 endpoint (EP 0x81 IN below), Audio/AudioStreaming
; --- CS AS GENERAL @0x05fb (7) ---
0x05fb  07 24 01    ; AS_GENERAL
0x05fe  09          ; bTerminalLink = 9 — ANOMALY: no unit/terminal with ID 9 (dangling; capture iface "should" link OT 4)
0x05ff  01          ; bDelay = 1 frame
0x0600  01 00       ; wFormatTag = 0x0001 PCM
; --- CS FORMAT_TYPE I @0x0602 (14) ---
0x0602  0e 24 02    ; FORMAT_TYPE
0x0605  01          ; bFormatType = FORMAT_TYPE_I
0x0606  02          ; bNrChannels = 2
0x0607  03          ; bSubframeSize = 3 bytes
0x0608  18          ; bBitResolution = 24 bits
0x0609  02          ; bSamFreqType = 2 discrete rates
0x060a  44 ac 00    ; tSamFreq[0] = 44100 Hz
0x060d  80 bb 00    ; tSamFreq[1] = 48000 Hz
; --- Endpoint @0x0610 (9): EP1 IN iso ---
0x0610  09 05       ; ENDPOINT
0x0612  81          ; bEndpointAddress = 0x81 (EP 1 IN — capture data to host)
0x0613  05          ; bmAttributes = 0x05 isochronous, asynchronous
0x0614  30 01       ; wMaxPacketSize = 0x0130 = 304 bytes
0x0616  01          ; bInterval = 1 ms
0x0617  00          ; bRefresh = 0
0x0618  00          ; bSynchAddress = 0 (no explicit feedback EP)
; --- CS ENDPOINT @0x0619 (7): EP_GENERAL ---
0x0619  07 25 01    ; CS_ENDPOINT, EP_GENERAL
0x061c  01          ; bmAttributes = 0x01 Sampling Frequency control supported
0x061d  01          ; bLockDelayUnits = 1 (milliseconds)
0x061e  00 02       ; wLockDelay = 0x0200 = 512 (byte-verified)
; --- Interface 2 alt 0 @0x0620 (9): AudioStreaming, zero-bandwidth ---
0x0620  09 04 02 00 00 01 02 00 00
        ; iface 2, alt 0, 0 EPs, Audio/AudioStreaming
; --- Interface 2 alt 1 @0x0629 (9): AudioStreaming, playback ---
0x0629  09 04 02 01 01 01 02 00 00
        ; iface 2, alt 1, 1 endpoint (EP 0x02 OUT below)
; --- CS AS GENERAL @0x0632 (7) ---
0x0632  07 24 01    ; AS_GENERAL
0x0635  08          ; bTerminalLink = 8 — ANOMALY: dangling, no ID 8 (playback iface "should" link IT 1)
0x0636  01          ; bDelay = 1 frame
0x0637  01 00       ; wFormatTag = 0x0001 PCM
; --- CS FORMAT_TYPE I @0x0639 (14) ---
0x0639  0e 24 02 01 02 03 18 02 44 ac 00 80 bb 00
        ; identical to 0x0602: Type I, 2ch, 3-byte subframe, 24-bit, {44100, 48000} Hz
; --- Endpoint @0x0647 (9): EP2 OUT iso ---
0x0647  09 05       ; ENDPOINT
0x0649  02          ; bEndpointAddress = 0x02 (EP 2 OUT — playback data from host)
0x064a  05          ; bmAttributes = 0x05 isochronous, asynchronous
0x064b  30 01       ; wMaxPacketSize = 304
0x064d  01          ; bInterval = 1 ms
0x064e  00 00       ; bRefresh 0, bSynchAddress 0
; --- CS ENDPOINT @0x0650 (7) ---
0x0650  07 25 01 01 01 00 02
        ; EP_GENERAL: sampling-freq control, lock-delay units ms, wLockDelay 0x0200
; --- end of config #1: 0x0650+7 = 0x0657, total 200 bytes = wTotalLength ---
```

#### 6.1.3 Configuration descriptor set #2 — vendor class (0x0657, wTotalLength = 54)

Alternate configuration: same interface/endpoint topology but all interfaces class 0xFF
(vendor-specific), no class-specific descriptors. Matches Phase-0 live enumeration
(vendor-class interfaces; iface 1 alt 1 carries the EPs). Which set is served, and when,
is decided by code outside the annotated ranges.

```
0x0657  09 02 36 00 02 01 00 80 f0
        ; CONFIGURATION: wTotalLength 0x0036 = 54, bNumInterfaces = 2, value 1, bus-powered, 480 mA
0x0660  09 04 00 00 00 ff 00 00 00
        ; iface 0 alt 0, 0 EPs, class 0xFF vendor-specific
0x0669  09 04 01 00 00 ff 00 00 00
        ; iface 1 alt 0, 0 EPs, vendor-specific (zero-bandwidth idle)
0x0672  09 04 01 01 02 ff 00 00 00
        ; iface 1 alt 1, 2 EPs, vendor-specific — the streaming alt Phase 0 found
0x067b  09 05 81 05 30 01 01 00 00
        ; EP 0x81 IN, iso async, wMaxPacketSize 304, interval 1 ms
0x0684  09 05 02 05 30 01 01 00 00
        ; EP 0x02 OUT, iso async, 304, 1 ms
; --- end of config #2: 0x0684+9 = 0x068d, total 54 = wTotalLength ---
```

#### 6.1.4 String descriptors (0x068d–0x070e)

```
0x068d  04 03       ; String 0: bLength 4, STRING (3)
0x068f  09 04       ;   wLANGID[0] = 0x0409 English (US)
0x0691  1e 03       ; String 1: bLength 0x1E = 30, STRING (iManufacturer)
0x0693  ...         ;   UTF-16LE, 14 chars: "Digidesign Inc"
0x06af  60 03       ; String 2: bLength 0x60 = 96, STRING (iProduct)
0x06b1  ...         ;   UTF-16LE, 47 chars: "Mbox USB Audio Device copyright Digidesign 2001"
; ends at 0x070e — flush against fcn_070f. No padding.
```

### 6.2 VECINT interrupt dispatch table (0x0c7d–0x0cc6, 74 bytes = 37 big-endian entries)

Indexed by the INT0 ISR as `entry = 0x0c7d + 2·VECINT`. Vector names per Reg_stc1.h
234-270. Bytes verified with `xxd -s 0x0c7d`.

```
0x0c7d  0c c7   [0x00 OEP0_INT ] -> 0x0cc7  oep0_int_handler
0x0c7f  00 10   [0x01 OEP1_INT ] -> 0x0010  RET stub
0x0c81  00 11   [0x02 OEP2_INT ] -> 0x0011  RET stub
0x0c83  00 12   [0x03 OEP3_INT ] -> 0x0012  RET stub
0x0c85  00 16   [0x04 OEP4_INT ] -> 0x0016  RET stub
0x0c87  00 17   [0x05 OEP5_INT ] -> 0x0017  RET stub
0x0c89  00 18   [0x06 OEP6_INT ] -> 0x0018  RET stub
0x0c8b  00 19   [0x07 OEP7_INT ] -> 0x0019  RET stub
0x0c8d  0f 91   [0x08 IEP0_INT ] -> 0x0f91  ep0_in_done_handler
0x0c8f  00 1a   [0x09 IEP1_INT ] -> 0x001a  RET stub
0x0c91  00 1e   [0x0a IEP2_INT ] -> 0x001e  RET stub
0x0c93  00 1f   [0x0b IEP3_INT ] -> 0x001f  RET stub
0x0c95  00 20   [0x0c IEP4_INT ] -> 0x0020  RET stub
0x0c97  00 21   [0x0d IEP5_INT ] -> 0x0021  RET stub
0x0c99  00 22   [0x0e IEP6_INT ] -> 0x0022  RET stub
0x0c9b  10 29   [0x0f IEP7_INT ] -> 0x1029  RET (padding region)
0x0c9d  10 2a   [0x10 STPOW_INT] -> 0x102a  RET
0x0c9f  10 35   [0x11 reserved ] -> 0x1035  RET (default/no-op)
0x0ca1  00 26   [0x12 SETUP_INT] -> 0x0026  usb_setup_handler
0x0ca3  10 2b   [0x13 PSOF_INT ] -> 0x102b  RET
0x0ca5  0d 58   [0x14 SOF_INT  ] -> 0x0d58  sof_int_handler
0x0ca7  10 2c   [0x15 RESR_INT ] -> 0x102c  RET
0x0ca9  00 06   [0x16 SUSR_INT ] -> 0x0006  usb_susr_handler (sets pending_action 0x0e)
0x0cab  0f 64   [0x17 RSTR_INT ] -> 0x0f64  usb_rstr_handler
0x0cad  10 2d   [0x18 CPRX_INT ] -> 0x102d  RET
0x0caf  10 2e   [0x19 CPTX_INT ] -> 0x102e  RET
0x0cb1  10 35   [0x1a DPRX_INT ] -> 0x1035  RET (default)
0x0cb3  10 35   [0x1b DPTX_INT ] -> 0x1035  RET (default)
0x0cb5  10 2f   [0x1c I2CRX_INT] -> 0x102f  RET
0x0cb7  10 30   [0x1d I2CTX_INT] -> 0x1030  RET
0x0cb9  10 35   [0x1e reserved ] -> 0x1035  RET (default)
0x0cbb  10 31   [0x1f XINT_INT ] -> 0x1031  RET
0x0cbd  10 32   [0x20 (undef)  ] -> 0x1032  RET
0x0cbf  10 33   [0x21 (undef)  ] -> 0x1033  RET
0x0cc1  10 35   [0x22 (undef)  ] -> 0x1035  RET (default)
0x0cc3  10 35   [0x23 (undef)  ] -> 0x1035  RET (default)
0x0cc5  10 34   [0x24 NO_INT   ] -> 0x1034  RET (no-interrupt entry)
```

### 6.3 Keil ?C_INIT bit-mask table (0x0969–0x0970, 8 bytes)

```
0x0969  01 02 04 08 10 20 40 80   ; mask for bit 0..7, indexed by MOVC A,@A+PC at 0x095c
```

### 6.4 Keil ?C_INIT initialization table (0x0fba–0x0fe1, 40 bytes)

Interpreted by the ?C_INIT decoder at 0x0971/0x0974. Record format: (lead 0x01 = IDATA,
length 1), (address), (value); 0x00 terminates. Raw bytes verified with `xxd`.

```
0x0fba  01 22 00   ; IDATA[0x22] = 0x00  (ctrl_img_A / bits 0x10-0x17)
0x0fbd  01 20 00   ; IDATA[0x20] = 0x00  (p3_shadow / bits 0x00-0x07)
0x0fc0  01 25 00   ; IDATA[0x25] = 0x00  (ctrl_img_B1 / bits 0x28-0x2f)
0x0fc3  01 23 00   ; IDATA[0x23] = 0x00  (ctrl_img_B0 / bits 0x18-0x1f incl. shifter toggle 0x1e)
0x0fc6  01 24 00   ; IDATA[0x24] = 0x00  (bits 0x20-0x27 incl. tick flag 0x20)
0x0fc9  01 21 00   ; IDATA[0x21] = 0x00  (usb_flags / bits 0x08-0x0f)
0x0fcc  01 09 00   ; IDATA[0x09] = 0x00  (xfer_len_lo)
0x0fcf  01 0c 00   ; IDATA[0x0c] = 0x00
0x0fd2  01 0b 00   ; IDATA[0x0b] = 0x00  (xfer_len_hi)
0x0fd5  01 0e 00   ; IDATA[0x0e] = 0x00  (pending_addr)
0x0fd8  01 0a 00   ; IDATA[0x0a] = 0x00  (pending_action)
0x0fdb  01 0d 00   ; IDATA[0x0d] = 0x00  (ctl_state / pending_cmd)
0x0fde  01 08 03   ; IDATA[0x08] = 0x03  (clock_mode_id, initial value 3)
0x0fe1  00         ; terminator
```

---

## 7. Open Questions

Everything the annotators marked UNKNOWN or uncertain, collected with what would resolve
each.

### 7.1 External hardware / board wiring (needs a schematic or live probing)

1. **CS8427 chip identity and register map.** `fcn_0c31`, `fcn_0567`, `fcn_0575` and the
   bring-up sequence write registers 0x01–0x24 to a 3-wire device at write-address 0x20.
   The chip is *likely* the CS8427 S/PDIF transceiver (0x20 = CS8427 SPI write address
   with AD0..2=0; consistent with NOTES.md pin map), but no CS8427 datasheet is in the
   repo, so no register meaning is verified. *Resolve:* obtain the CS8427 datasheet and
   map the written registers (0x01=0x01, 0x02=0x20, 0x03=0x0C, 0x04=0x00/0x40/0x41,
   0x05=0x05, 0x06=0x05, 0x11=0xFF, 0x13=0x10, 0x23=0x00/0x40, 0x24=0x80, 0x12=0x00), or
   scope the P1.4/P1.3 lines against a known CS8427.

2. **External codec identity and register map** written by `audio_hw_bringup`
   (0x09fc–0x0a3b) — same 3-wire path. Register meanings unverified. *Resolve:* identify
   the codec part on the board.

3. **Control/LED shift-register bit meanings.** IRAM 0x22 (bits 0x10–0x17) via `fcn_0efc`
   and IRAM 0x23/0x25 (bits 0x18–0x1f, 0x28–0x2f) via `fcn_0e56` drive two external latch
   chains. Individual line meanings are UNKNOWN, specifically: bit 0x16 (= !0x2c && !0x2d),
   bit 0x17 (run/stop-like), bits 0x18/0x19 (mode-5 only), bits 0x1a/0x1b (clock-switch
   pair), bit 0x1c, bit 0x1e (shifter-mode toggle / `fcn_0efc` final drive pattern), bit
   0x2d (probe flag), bit 0x2e (teardown latch). The one-cold 3-bit patterns from the two
   `panel_state_cycle` state machines suggest per-channel source-select or LED groups.
   *Resolve:* board schematic / trace the latch outputs.

4. **P3 front-panel switch identities.** `fcn_0f31` edge-detects P3.5 (→ toggle bit 0x1e),
   P3.3 (→ `panel_state_cycle_A`), P3.4 (→ `panel_state_cycle_B`), and the main loop
   edge-detects P3.1 (→ events 0x0b/0x0c). Which physical control each pin is has NOT been
   hardware-verified. *Resolve:* press buttons while tracing P3.

5. **IN EP1 iso buffer bound.** `usb_ep_dma_init` sets IEPBBAX1=0x94 (0xFCA0) with
   IEPBSIZ1=0x50 (640 bytes); nominal end 0xFF20 overlaps the EP-config SFR space above
   0xFF07. How much the UBM actually uses for iso DMA is UNKNOWN. *Resolve:* TAS1020B UBM
   buffer-RAM documentation or hardware test.

### 7.2 Firmware state variables with unresolved meaning

6. **Bit 0x0a (0x21.2) — the "dead flag."** Tested as an OR-alternative to `configured`
   in GET/SET_INTERFACE (0x01ed/0x02ae) and used as the "config-1-active" flag in
   `usb_deferred_action_dispatch`. Range 2 found no `setb 0x0a` in the Chapter-9 paths and
   called it always-false dead code; range 3 treats it as a live config-active flag set at
   SETUP time. *Resolve:* confirm whether any path outside the standard-request range sets
   bit 0x0a (audit all `d2 0a`); reconcile the two readings against the config/interface
   state model.

7. **Bit 0x2d (0x25.5).** Set in deferred-action case 11 (0x04ce), cleared in cases 2/3;
   also feeds the bit-0x16 computation. Purpose UNKNOWN. *Resolve:* trace what consumes
   0x2d besides the 0x16 tail.

8. **Byte 0x0c.** Zeroed at ?C_INIT and used in the 0x00b3–0x00d8 sampling-freq path;
   standalone role not pinned. `usb_ep_dma_init` sets it to a sentinel 0xFE (0x0924).
   *Resolve:* find all readers of byte 0x0c.

9. **Bytes 0x2a / 0x2b.** Initialized (0x00 / 0x10) in `main_loop` (0x0a48/0x0a4b) but
   never read in the annotated code. UNKNOWN use. *Resolve:* search the whole image for
   reads of 0x2a/0x2b.

10. **Bit 0x22 (0x24.2).** Cleared once at 0x0a55; no other access. Purpose UNKNOWN.

11. **`stage_ctrl_pair_12_00` target (0x0ffa).** Stages (0x2c=0x12, 0x2d=0x00) as a
    (register, value) message; which external device/register 0x12 addresses is UNKNOWN.
    *Resolve:* trace the consumer of the 0x2c/0x2d pair after 0x04db.

12. **`pending_action`=0x0d semantics.** The SETUP handler queues action 0x0d for a
    class OUT to interface with bRequest==0 (0x005b), but `usb_deferred_action_dispatch`
    only has explicit cases 1–14 with no distinct 0x0d handler documented beyond the table
    row → 0x0517 (case 13, EEPROM[0]=0). Wait: 0x0d = 13 = case 13. So bRequest==0 class
    request triggers the DFU-trigger EEPROM write. *Resolve:* confirm the host actually
    sends this and that case 13 is the intended firmware-update entry (see #14).

### 7.3 Behavioral / intent questions

13. **CS8427 register 0x04 = 0x41 vs 0x40.** `audio_clock_set_mode` writes reg 4 = 0x41
    in external-clock mode 1, 0x40 in internal modes 2/3/5. Meaning unverified (see #1).

14. **EEPROM DFU-trigger interpretation.** Case 13 (0x0517) writes EEPROM[0x0000]=0; case
    11 (0x04c8) does a complement-write-readback probe on EEPROM[0x1FFF]. The "zero the
    boot header → next reset enters boot-ROM DFU" reading is *likely* (based on the TI boot
    ROM header check) but not confirmed against the actual boot ROM in this image. The
    0x1FFF probe's overall purpose (write-protect / revision detect?) is UNKNOWN. *Resolve:*
    dump/analyze the TAS1020B boot ROM header-validation path; correlate with the Digidesign
    flasher's observed DFU-entry sequence.

15. **Which configuration set (audio-class #1 vs vendor-class #2) is served when.** Two
    complete config descriptor sets exist (§6.1.2, §6.1.3). The selection logic is outside
    the annotated ranges. *Resolve:* find the GET_DESCRIPTOR config-index / bNumConfigurations
    handling that chooses between 0x058f and 0x0657 (device descriptor says
    bNumConfigurations=1, yet two sets are present — investigate).

16. **Descriptor anomalies (byte-verified, intent unknown).** IT1 bAssocTerminal=5 (points
    at the Selector Unit, not a terminal); AS_GENERAL bTerminalLink=9 (capture) and 8
    (playback) are dangling (no such unit IDs). These are firmware descriptor bugs in the
    stock image; harmless to the class driver but noted. *Resolve:* n/a (stock behavior);
    do not "fix" in a byte-compatible reflash.

17. **`fcn_0b4d` OEPCNF0 no-op (§2.5.5).** Confirmed the OUT-half AND result is never
    written back — bug or vestigial. *Resolve:* compare against rev20 to see if the write
    existed there (regression) or was always absent.

18. **Unbounded I2C spins in `fcn_0d11`.** The EEPROM read waits (TXE/RXF) have no timeout,
    unlike the write path (`fcn_0bda`), so a hung bus hangs the firmware. Noted as a
    robustness gap, not a decode question.

### 7.4 Out-of-annotated-scope regions referenced but not disassembled here

- The two `usb_ep_dma_init` secondary entry points (0x0904 from 0x061f, 0x0910 from
  0x0cbc) imply callers inside regions that Ghidra rendered as descriptor/table data; the
  0x061f caller in particular falls inside the descriptor block (0x0657 config set) and is
  almost certainly a spurious force-disassembly XREF. *Resolve:* confirm no real code lives
  in 0x057d–0x070e (all evidence says it is pure descriptor data).

---

*End of master annotated disassembly.*
