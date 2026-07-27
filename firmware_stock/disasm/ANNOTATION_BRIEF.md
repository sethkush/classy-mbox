# Annotation brief — Rev 20 / Rev 22 full-firmware annotation project

Goal: 100% understood firmware. Every instruction annotated, every data
byte identified. No byte a mystery.

## Input files (all paths relative to repo root)

- `firmware_stock/disasm/rev20_ghidra.txt` — Ghidra listing, Rev 20
- `firmware_stock/disasm/rev22_ghidra.txt` — Ghidra listing, Rev 22
- `firmware_stock/rev20_firmware_code.bin` / `rev22_firmware_code.bin` —
  flat code images, load address 0x0000, 8174 bytes each

## Address-space ground truth

- 8051 core inside TAS1020B. CODE 0x0000-0x1FED loaded to RAM from EEPROM
  by boot ROM; MEMCFG.SDW=1 routes code fetches to that RAM.
- Boot ROM lives at 0x8000+ (shadow); `ljmp 0x2f00`-style calls from app
  code land in boot ROM services (std-request handler etc.).
- XDATA 0xFF00-0xFFFF = memory-mapped SFRs (USB engine, DMA, codec port).
  AUTHORITATIVE SFR NAME MAP: `reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h`
  — always name SFRs from this file, never guess.
- XDATA 0xFA00-0xFCFF = USB packet buffer RAM. EP0 IN buffer 0xFA10 (Rev 20),
  EP0 OUT 0xFA18. SETUP packet block ("SETPACK") at 0xFF28-0xFF2F.
- Datasheet: `reference/tas1020a/sles025b_tas1020b_datasheet.pdf`
  (§6.5 = USB engine, §6.5.7.3 = VECINT semantics).
- TI reference firmware (the vendor SDK Digidesign built on):
  `reference/tas1020a/ti_uac_reference/` — ROM/ (boot ROM sources) and
  application sources. Rev 20/22 are believed DERIVED from this SDK, so
  matching function shapes against TI C code is the fastest route to
  understanding. UsbEng.c usbIntrHandler, engUsbInit, UsbDfu.c are the
  most productive comparisons.

## Known facts (hardware-verified or multiply-audited)

- Rev 20 entry: reset vector `ljmp 0x0a09`. Rev 22 entry: `ljmp 0x0dff`
  (differs! rev22 byte at 0: 02 09 2a → wait, verify against YOUR listing;
  do not trust this line).
- INT0 (vector 0x0003) is the USB interrupt: USB engine ORs unmasked
  USBIMSK sources into INT0; handler reads VECINT to dispatch.
- Timer 0 (vector 0x000B): millisecond tick, TH0 reload 0xCE.
- bcdDevice: Rev 20 = 0x0020, Rev 22 = 0x0022 (in device descriptor).
- Rev 20's SETUP dispatcher: near 0x0026-0x0118, ends `ljmp 0x2f00`
  (boot ROM std-request fallback).
- Master hw init in Rev 20: fcn near 0x08CB-0x0A00 region (TMOD/timers,
  CPT codec port config, DMA arming, mux defaults).
- WARNING on legacy citations: `rev20_flat.asm` was disassembled from the
  EEPROM image INCLUDING its 18-byte header — all its addresses are
  +0x12 relative to true code addresses. Citations like
  "rev20_flat.asm 0x0ADE" may be header-shifted. The Ghidra listings are
  header-free (true code addresses). When a legacy citation disagrees
  with the Ghidra listing by 0x12, the legacy one is shifted.

## Annotation output format

For your assigned address range, produce a markdown file with EVERY
instruction listed and annotated:

```
0x0a09  mov sp, #0x35        ; set stack top (SDCC-style init)
0x0a0c  lcall 0x0b2b         ; -> fcn_0b2b = <name you assign> (purpose)
```

Rules:
1. Every instruction in your range gets a comment. If purpose is genuinely
   unknowable from context, write `; UNKNOWN — <why>` (these get a second
   pass).
2. Name every function you can (`fcn_XXXX = usb_setup_dispatch` etc.) and
   keep a function table at the top of your file.
3. For XDATA accesses in 0xFF00-0xFFFF, ALWAYS resolve the SFR name from
   Reg_stc1.h and state read/write and the value's meaning.
4. For GAP regions (data): identify what the bytes ARE — descriptor sets
   (decode field-by-field), string descriptors (decode UTF-16), lookup
   tables (state what indexes them, from xrefs), padding (0xFF/0x00 runs).
5. Cross-reference TI SDK C code where the assembly matches a TI function
   (cite file:line).
6. NEVER fabricate: if you didn't verify a claim against the listing bytes,
   the datasheet, or TI source, don't write it. `UNKNOWN` is an acceptable
   and expected answer. Fabricated citations have repeatedly poisoned this
   project (see BRICK_LOG.md 2026-07-25).
7. Flow that leaves your range: note the target address and stop —
   another annotator owns it.
