# Rev-20-vs-safety_net SFR-write justifications

Every diff surfaced by `tools/diff_vs_rev20_safety_net.py` must have
a row here or the gate fails. Analogous to
`rev20_diff_justifications.md` for mboxfw, but this file scopes only
to the safety_net recovery firmware.

safety_net is intentionally minimal — its ONLY job is to answer the
Digi enter-DFU class request. Every audio / streaming / codec /
DMA / mux register Rev 20 touches is deliberately absent, so the
REV20_ONLY set is expected to be large. Each REV20_ONLY row still
needs to state WHY safety_net is safe skipping that write.

Format:

    | 0xADDR | PATTERN  | IMMEDIATE | source              | reason |

Categories `diff_vs_rev20_safety_net.py` surfaces:
- **SAFETYNET_ONLY**  we write it, Rev 20 doesn't.
- **REV20_ONLY**      Rev 20 writes it, we don't.
- **CHANGED_SN**      same addr, safety_net has a tuple Rev 20 doesn't.
- **CHANGED_REV**     same addr, Rev 20 has a tuple safety_net doesn't.

## Diffs

| Addr | Pattern | Imm | Category | Reason |
|------|---------|-----|----------|--------|
| 0xff2e | rmw | - | SAFETYNET_ONLY | safety_net reads wLength via `(XDATA(0xFF2F)<<8) \| XDATA(0xFF2E)` in reply_desc(); RMW arises from SDCC codegen touching the byte on the read path (no functional write). Rev 20 uses a different clamp strategy. |
| 0xff29 | assign | 0x01 | REV20_ONLY | **WITHDRAWN 2026-08-05 (#180). Rev 20 does not write this address at all.** 0xFF28-0xFF2F is the setup data packet buffer: the UBM writes it, the MCU only reads it, and stock writing it is impossible. These entries were phantoms produced by the shared Rev 20 scanner carrying DPTR across `ret` and branch edges, so a MOVX in an unrelated function was attributed to the last address loaded. Fixed in diff_vs_rev20.py; the row is kept, struck, as the record of a justification written for a fact that was never true. See FINDING_rev20_flat_asm_is_offset_by_18.md. |
| 0xff29 | rmw | - | REV20_ONLY | **WITHDRAWN 2026-08-05 (#180). Rev 20 does not write this address at all.** 0xFF28-0xFF2F is the setup data packet buffer: the UBM writes it, the MCU only reads it, and stock writing it is impossible. These entries were phantoms produced by the shared Rev 20 scanner carrying DPTR across `ret` and branch edges, so a MOVX in an unrelated function was attributed to the last address loaded. Fixed in diff_vs_rev20.py; the row is kept, struck, as the record of a justification written for a fact that was never true. See FINDING_rev20_flat_asm_is_offset_by_18.md. |
| 0xff2b | assign | 0x44 | REV20_ONLY | **WITHDRAWN 2026-08-05 (#180). Rev 20 does not write this address at all.** 0xFF28-0xFF2F is the setup data packet buffer: the UBM writes it, the MCU only reads it, and stock writing it is impossible. These entries were phantoms produced by the shared Rev 20 scanner carrying DPTR across `ret` and branch edges, so a MOVX in an unrelated function was attributed to the last address loaded. Fixed in diff_vs_rev20.py; the row is kept, struck, as the record of a justification written for a fact that was never true. See FINDING_rev20_flat_asm_is_offset_by_18.md. |
| 0xff2b | assign | 0x80 | REV20_ONLY | **WITHDRAWN 2026-08-05 (#180). Rev 20 does not write this address at all.** 0xFF28-0xFF2F is the setup data packet buffer: the UBM writes it, the MCU only reads it, and stock writing it is impossible. These entries were phantoms produced by the shared Rev 20 scanner carrying DPTR across `ret` and branch edges, so a MOVX in an unrelated function was attributed to the last address loaded. Fixed in diff_vs_rev20.py; the row is kept, struck, as the record of a justification written for a fact that was never true. See FINDING_rev20_flat_asm_is_offset_by_18.md. |
| 0xff2b | assign | 0xac | REV20_ONLY | **WITHDRAWN 2026-08-05 (#180). Rev 20 does not write this address at all.** 0xFF28-0xFF2F is the setup data packet buffer: the UBM writes it, the MCU only reads it, and stock writing it is impossible. These entries were phantoms produced by the shared Rev 20 scanner carrying DPTR across `ret` and branch edges, so a MOVX in an unrelated function was attributed to the last address loaded. Fixed in diff_vs_rev20.py; the row is kept, struck, as the record of a justification written for a fact that was never true. See FINDING_rev20_flat_asm_is_offset_by_18.md. |
| 0xff2b | assign | 0xbb | REV20_ONLY | **WITHDRAWN 2026-08-05 (#180). Rev 20 does not write this address at all.** 0xFF28-0xFF2F is the setup data packet buffer: the UBM writes it, the MCU only reads it, and stock writing it is impossible. These entries were phantoms produced by the shared Rev 20 scanner carrying DPTR across `ret` and branch edges, so a MOVX in an unrelated function was attributed to the last address loaded. Fixed in diff_vs_rev20.py; the row is kept, struck, as the record of a justification written for a fact that was never true. See FINDING_rev20_flat_asm_is_offset_by_18.md. |
| 0xff2b | rmw | - | REV20_ONLY | **WITHDRAWN 2026-08-05 (#180). Rev 20 does not write this address at all.** 0xFF28-0xFF2F is the setup data packet buffer: the UBM writes it, the MCU only reads it, and stock writing it is impossible. These entries were phantoms produced by the shared Rev 20 scanner carrying DPTR across `ret` and branch edges, so a MOVX in an unrelated function was attributed to the last address loaded. Fixed in diff_vs_rev20.py; the row is kept, struck, as the record of a justification written for a fact that was never true. See FINDING_rev20_flat_asm_is_offset_by_18.md. |
| 0xff2c | assign | 0x00 | REV20_ONLY | **WITHDRAWN 2026-08-05 (#180). Rev 20 does not write this address at all.** 0xFF28-0xFF2F is the setup data packet buffer: the UBM writes it, the MCU only reads it, and stock writing it is impossible. These entries were phantoms produced by the shared Rev 20 scanner carrying DPTR across `ret` and branch edges, so a MOVX in an unrelated function was attributed to the last address loaded. Fixed in diff_vs_rev20.py; the row is kept, struck, as the record of a justification written for a fact that was never true. See FINDING_rev20_flat_asm_is_offset_by_18.md. |
| 0xff2c | rmw | - | REV20_ONLY | **WITHDRAWN 2026-08-05 (#180). Rev 20 does not write this address at all.** 0xFF28-0xFF2F is the setup data packet buffer: the UBM writes it, the MCU only reads it, and stock writing it is impossible. These entries were phantoms produced by the shared Rev 20 scanner carrying DPTR across `ret` and branch edges, so a MOVX in an unrelated function was attributed to the last address loaded. Fixed in diff_vs_rev20.py; the row is kept, struck, as the record of a justification written for a fact that was never true. See FINDING_rev20_flat_asm_is_offset_by_18.md. |
| 0xff60 | assign | 0xc5 | REV20_ONLY | OEP1 audio-in endpoint config (0xFF60..0xFF67); safety_net has no audio endpoints. |
| 0xff60 | runtime | - | REV20_ONLY | OEP1 audio-in endpoint config (0xFF60..0xFF67); safety_net has no audio endpoints. |
| 0xff61 | assign | 0x94 | REV20_ONLY | OEP1 audio-in endpoint config (0xFF60..0xFF67); safety_net has no audio endpoints. |
| 0xff62 | runtime | - | REV20_ONLY | OEP1 audio-in endpoint config (0xFF60..0xFF67); safety_net has no audio endpoints. |
| 0xff63 | assign | 0x00 | REV20_ONLY | OEP1 audio-in endpoint config (0xFF60..0xFF67); safety_net has no audio endpoints. |
| 0xff63 | runtime | - | REV20_ONLY | OEP1 audio-in endpoint config (0xFF60..0xFF67); safety_net has no audio endpoints. |
| 0xff67 | runtime | - | REV20_ONLY | OEP1 audio-in endpoint config (0xFF60..0xFF67); safety_net has no audio endpoints. |
| 0xff6f | runtime | - | REV20_ONLY | IEPBCNT0 / secondary EP0 buffer count; safety_net EP0 uses single-buffer path only. |
| 0xff98 | assign | 0xc5 | REV20_ONLY | OEP2 audio-out endpoint config (0xFF98..0xFF9F); safety_net has no audio endpoints. |
| 0xff98 | runtime | - | REV20_ONLY | OEP2 audio-out endpoint config (0xFF98..0xFF9F); safety_net has no audio endpoints. |
| 0xff99 | assign | 0x44 | REV20_ONLY | OEP2 audio-out endpoint config (0xFF98..0xFF9F); safety_net has no audio endpoints. |
| 0xff9a | assign | 0x50 | REV20_ONLY | OEP2 audio-out endpoint config (0xFF98..0xFF9F); safety_net has no audio endpoints. |
| 0xff9b | assign | 0x00 | REV20_ONLY | OEP2 audio-out endpoint config (0xFF98..0xFF9F); safety_net has no audio endpoints. |
| 0xff9b | runtime | - | REV20_ONLY | OEP2 audio-out endpoint config (0xFF98..0xFF9F); safety_net has no audio endpoints. |
| 0xff9f | runtime | - | REV20_ONLY | OEP2 audio-out endpoint config (0xFF98..0xFF9F); safety_net has no audio endpoints. |
| 0xffaf | runtime | - | REV20_ONLY | OEPBCNT0 secondary buffer count; safety_net uses only the primary EP0 OUT buffer. |
| 0xffd4 | assign | 0x01 | REV20_ONLY | Rev 20 codec/mux GPIO configuration (0xFFD4..0xFFD6); safety_net does not touch codec/mux. |
| 0xffd5 | assign | 0xac | REV20_ONLY | Rev 20 codec/mux GPIO configuration (0xFFD4..0xFFD6); safety_net does not touch codec/mux. |
| 0xffd5 | runtime | - | REV20_ONLY | Rev 20 codec/mux GPIO configuration (0xFFD4..0xFFD6); safety_net does not touch codec/mux. |
| 0xffd6 | assign | 0x25 | REV20_ONLY | Rev 20 codec/mux GPIO configuration (0xFFD4..0xFFD6); safety_net does not touch codec/mux. |
| 0xffdc | assign | 0x50 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffdd | assign | 0x03 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffde | assign | 0xac | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffdf | assign | 0xe5 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe0 | assign | 0x0d | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe1 | and_not | 0x3f | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe1 | assign | 0x06 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe1 | assign | 0x0d | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe1 | or | 0xc0 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe2 | assign | 0x10 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe5 | assign | 0x61 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe5 | assign | 0x6a | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe6 | assign | 0x4b | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe6 | assign | 0xa8 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe7 | assign | 0x0f | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe7 | assign | 0x20 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe8 | and_not | 0x7f | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe8 | assign | 0x02 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe8 | or | 0x80 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffe9 | assign | 0x80 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffea | assign | 0x03 | REV20_ONLY | Rev 20 DMA / audio streaming controller writes (DMACTL / DMASRC / DMADST family); safety_net does no DMA. |
| 0xffee | and_not | 0x7f | REV20_ONLY | Rev 20 second DMA channel setup; safety_net does no DMA. |
| 0xffee | assign | 0x09 | REV20_ONLY | Rev 20 second DMA channel setup; safety_net does no DMA. |
| 0xffee | or | 0x80 | REV20_ONLY | Rev 20 second DMA channel setup; safety_net does no DMA. |
| 0xffef | assign | 0x80 | REV20_ONLY | Rev 20 second DMA channel setup; safety_net does no DMA. |
| 0xfff0 | assign | 0x03 | REV20_ONLY | Rev 20 second DMA channel setup; safety_net does no DMA. |
| 0xfff6 | assign | 0x10 | REV20_ONLY | Rev 20 audio streaming tail writes (SOF-driven feedback / streaming state); safety_net does not stream. |
| 0xfff6 | runtime | - | REV20_ONLY | Rev 20 audio streaming tail writes (SOF-driven feedback / streaming state); safety_net does not stream. |
| 0xfff7 | assign | 0x61 | REV20_ONLY | Rev 20 audio streaming tail writes (SOF-driven feedback / streaming state); safety_net does not stream. |
| 0xfff7 | assign | 0x6a | REV20_ONLY | Rev 20 audio streaming tail writes (SOF-driven feedback / streaming state); safety_net does not stream. |
| 0xfff8 | assign | 0x4b | REV20_ONLY | Rev 20 audio streaming tail writes (SOF-driven feedback / streaming state); safety_net does not stream. |
| 0xfff8 | assign | 0xa8 | REV20_ONLY | Rev 20 audio streaming tail writes (SOF-driven feedback / streaming state); safety_net does not stream. |
| 0xfff9 | assign | 0x0f | REV20_ONLY | Rev 20 audio streaming tail writes (SOF-driven feedback / streaming state); safety_net does not stream. |
| 0xff68 | assign | 0x84 | CHANGED_SN | IEPCNF0 = 0x84 (UBME\|UBMIE) at init — TI UsbEng.c:609 engUsbInit line 620. Rev 20 uses same value via different write sequence; the (assign,0x84) pattern is intentional and documented in src/main.c:477. |
| 0xff69 | assign | 0x43 | CHANGED_SN | IEPBBAX0 = EP_BBAX(EP0_IN_BUF_ADDR) = 0x43 (EP0 IN buffer @ 0xFA18) — TI UsbEng.c engUsbInit line 613. Deterministic address; Rev 20 computes at runtime. |
| 0xff69 | runtime | - | CHANGED_REV | Rev 20 computes IEPBBAX0 at runtime; safety_net writes a fixed immediate — same target buffer, different codegen. |
| 0xff6a | assign | 0x01 | CHANGED_SN | IEPBSIZ0 = EP_BSIZE(8) = 0x01 (8-byte EP0 IN buffer). TI UsbEng.c line 614. Rev 20 computes at runtime; safety_net inlines the immediate. |
| 0xff6a | runtime | - | CHANGED_REV | Rev 20 computes IEPBSIZ0 at runtime; safety_net writes fixed 0x01 for 8-byte EP0 IN. |
| 0xff6b | and_not | 0x7f | CHANGED_REV | IEPBCTX0 is used across Rev 20's IN-transfer state machine (multiple immediates for chunked transfers, NAK, ACK-with-length). safety_net's simpler reply_zlp/reply_short/reply_desc all touch it too; the byte-level tuple diff is codegen shape, not semantics. |
| 0xff6b | assign | 0x01 | CHANGED_REV | IEPBCTX0 is used across Rev 20's IN-transfer state machine (multiple immediates for chunked transfers, NAK, ACK-with-length). safety_net's simpler reply_zlp/reply_short/reply_desc all touch it too; the byte-level tuple diff is codegen shape, not semantics. |
| 0xff6b | assign | 0x02 | CHANGED_REV | IEPBCTX0 is used across Rev 20's IN-transfer state machine (multiple immediates for chunked transfers, NAK, ACK-with-length). safety_net's simpler reply_zlp/reply_short/reply_desc all touch it too; the byte-level tuple diff is codegen shape, not semantics. |
| 0xff6b | assign | 0x03 | CHANGED_REV | IEPBCTX0 is used across Rev 20's IN-transfer state machine (multiple immediates for chunked transfers, NAK, ACK-with-length). safety_net's simpler reply_zlp/reply_short/reply_desc all touch it too; the byte-level tuple diff is codegen shape, not semantics. |
| 0xff6b | assign | 0x80 | CHANGED_REV | IEPBCTX0 is used across Rev 20's IN-transfer state machine (multiple immediates for chunked transfers, NAK, ACK-with-length). safety_net's simpler reply_zlp/reply_short/reply_desc all touch it too; the byte-level tuple diff is codegen shape, not semantics. |
| 0xff6b | rmw | - | CHANGED_REV | IEPBCTX0 is used across Rev 20's IN-transfer state machine (multiple immediates for chunked transfers, NAK, ACK-with-length). safety_net's simpler reply_zlp/reply_short/reply_desc all touch it too; the byte-level tuple diff is codegen shape, not semantics. |
| 0xffa8 | or | 0x08 | CHANGED_SN | OEPCNF0 STALL clear/set + TOGGLE — main.c:282-285 documents; matches Rev 20 semantics via RMW-OR sequence but hits distinct (or,0x08)/(or,0x20) tuples. |
| 0xffa8 | or | 0x20 | CHANGED_SN | OEPCNF0 STALL clear/set + TOGGLE — main.c:282-285 documents; matches Rev 20 semantics via RMW-OR sequence but hits distinct (or,0x08)/(or,0x20) tuples. |
| 0xffa8 | runtime | - | CHANGED_SN | OEPCNF0 STALL clear/set + TOGGLE — main.c:282-285 documents; matches Rev 20 semantics via RMW-OR sequence but hits distinct (or,0x08)/(or,0x20) tuples. |
| 0xffaa | assign | 0x01 | CHANGED_SN | OEPBSIZ0 = EP_BSIZE(8) = 0x01 (8-byte EP0 OUT buffer). Companion to IEPBSIZ0 (0xFF6A) above. |
| 0xffaa | runtime | - | CHANGED_REV | Rev 20 computes OEPBSIZ0 at runtime; safety_net writes fixed 0x01. |
| 0xffab | runtime | - | CHANGED_REV | Rev 20 sets OEPBCTX0 with runtime-computed value; safety_net writes 0 (accept next OUT) explicitly. |
| 0xffb0 | and_not | 0xfe | CHANGED_SN | MEMCFG &= ~0x01 in handle_dfu_trigger() to clear SDW before ljmp 0x8000 — the boot-ROM re-entry sequence. Rev 20's DFU path uses the boot-ROM-owned UtilResetBootCPU trampoline; safety_net inlines the equivalent asm. Documented src/main.c:242-246. |
| 0xffb1 | and_not | 0xfe | CHANGED_REV | Rev 20's boot-ROM GLOBCTL initial state (assign 0x06 by boot ROM before app code, then and_not 0xFE by TI reference path). safety_net starts from boot-ROM-set 0x04 and OR's bit 0 — RMW-only per POLICY §2. See main.c:446-455. |
| 0xffb1 | assign | 0x06 | CHANGED_REV | Rev 20's boot-ROM GLOBCTL initial state (assign 0x06 by boot ROM before app code, then and_not 0xFE by TI reference path). safety_net starts from boot-ROM-set 0x04 and OR's bit 0 — RMW-only per POLICY §2. See main.c:446-455. |
| 0xffb2 | rmw | - | CHANGED_REV | Rev 20 sometimes reads-modifies VECINT; safety_net always writes 0. Datasheet §6.5.7.3: any write clears — either approach is correct. |
| 0xffc0 | and_not | 0x54 | CHANGED_SN | I2C_STA &= I2C_CLEAR_ALL (0x54) in eeprom_wr; TI I2c.c reference and mboxfw/src/eeprom.c use the same mask. Rev 20's flasher uses a different I2C prep pattern. |
| 0xffc0 | and_not | 0xfc | CHANGED_REV | Rev 20 flasher uses different I2C_STA masks (0xFC clear, 0x02 OR); safety_net's eeprom_wr uses the mboxfw/TI I2c.c mask (0x54). Both are valid init sequences; ours is documented src/main.c:145 and 154. |
| 0xffc0 | or | 0x02 | CHANGED_REV | Rev 20 flasher uses different I2C_STA masks (0xFC clear, 0x02 OR); safety_net's eeprom_wr uses the mboxfw/TI I2c.c mask (0x54). Both are valid init sequences; ours is documented src/main.c:145 and 154. |
| 0xffc1 | assign | 0x00 | CHANGED_REV | Rev 20 flasher writes constants to I2C_TX during EEPROM erase paths not exercised by safety_net. |
| 0xffc3 | runtime | - | CHANGED_REV | Rev 20 sets I2C_SADDR at runtime; safety_net uses fixed 0xA0 (byte-EEPROM). Codegen difference only. |
| 0xfffc | and_not | 0x7f | CHANGED_REV | Rev 20 clears CONN via `and_not 0x7F` on shutdown paths not present in safety_net (safety_net's DFU trigger does the ljmp 0x8000 boot-ROM handoff instead). |
| 0xfffc | and_not | 0xfe | CHANGED_SN | USBCTL writes: (assign,0x00) explicit disconnect at main() entry per POLICY §2 carve-out (main.c:437-444); (or,0x01) is a false positive from RMW pattern classification of `USBCTL \|= 0x80` chain; (and_not,0xFE) is scan artifact of MEMCFG (0xFFB0) clear that reused dptr. See main.c:437-444 and USBCTL_ATTACH_BIT comment (main.c:64-74). |
| 0xfffc | assign | 0x00 | CHANGED_SN | USBCTL writes: (assign,0x00) explicit disconnect at main() entry per POLICY §2 carve-out (main.c:437-444); (or,0x01) is a false positive from RMW pattern classification of `USBCTL \|= 0x80` chain; (and_not,0xFE) is scan artifact of MEMCFG (0xFFB0) clear that reused dptr. See main.c:437-444 and USBCTL_ATTACH_BIT comment (main.c:64-74). |
| 0xfffc | or | 0x01 | CHANGED_SN | USBCTL writes: (assign,0x00) explicit disconnect at main() entry per POLICY §2 carve-out (main.c:437-444); (or,0x01) is a false positive from RMW pattern classification of `USBCTL \|= 0x80` chain; (and_not,0xFE) is scan artifact of MEMCFG (0xFFB0) clear that reused dptr. See main.c:437-444 and USBCTL_ATTACH_BIT comment (main.c:64-74). |
| 0xfffd | assign | 0x00 | CHANGED_REV | Rev 20 promotes USBIMSK to 0xFF and clears it in various error/reset paths. safety_net keeps a single 0xE5 mask — see main.c:483-494. |
| 0xfffd | assign | 0xe5 | CHANGED_SN | USBIMSK = 0xE5 — TI UsbEng.c:640. **#180 CORRECTION, 2026-08-05: the Rev 20 half of this row was FALSE and is withdrawn.** It read "TI UsbEng.c:640, Rev 20 rev20_flat.asm 0x0917 both use 0xE5". Rev 20 never writes USBIMSK = 0xE5 anywhere: its only USBIMSK values are 0xFF (@0x03F1), 0x9F (@0x09EC, @0x0550, @0x0F6E) and 0x00 (@0x0AA6), byte-scanned across the whole image. The 0xE5 that was seen at flat 0x0917 is **CPTCNF2** at true 0x0905 — a codec-port register, not USBIMSK. A value collision plus a wrong-register attribution, produced by reading an EEPROM-relative listing (+0x12). Only the TI reference supports 0xE5. mboxfw's diff justifications already cite this in row 326; safety_net matches. Rev 20's (assign,0xFF) and (assign,0x00) tuples are from VEC_RSTR mask promotion and boot-ROM path not present in safety_net. |
| 0xfffd | assign | 0xff | CHANGED_REV | Rev 20 promotes USBIMSK to 0xFF and clears it in various error/reset paths. safety_net keeps a single 0xE5 mask — see main.c:483-494. |

## #180 / safety_net scanner parity, 2026-08-05

These nine rows were added when `diff_vs_rev20_safety_net.py` was brought into
line with `diff_vs_rev20.py`. Six of them were ALREADY unjustified — this gate
is wired into nothing, so it had been failing silently. Three are newly visible
because the Rev 20 baseline now includes writes a callee performs against the
caller's DPTR, which a linear scan of the listing cannot see.

| addr | pattern | immediate | class | reason |
|---|---|---|---|---|
| 0xffb1 | or | 0x01 | REV20_ONLY | `GLOBCTL \|= 0x01` is CPTEN, the codec port enable (Rev 20 @ 0x0934 and @ 0x0FF9; Rev 22 @ 0x0855 / 0x0FEA). safety_net DELIBERATELY omits it: §6.5.7.4 requires the codec-port configuration registers to be fully programmed before CPTEN is set, and safety_net programs none of them. Setting it over an unconfigured C-port is exactly the defect fixed in #122. The boot ROM's GLOBCTL = 0x04 (LPWR on, CPTEN off) is already correct for a USB-only recovery image. |
| 0xffd4 | assign | 0x03 | REV20_ONLY | `CPTRXCNF4`, codec port receive config. Newly visible: stock reaches it through the helper at 0x0DEB (Rev 20 site 0x0929), invisible to a linear scan. Same reason as the existing `assign 0x01` row — safety_net touches no codec/mux register. |
| 0xffde | assign | 0xa8 | REV20_ONLY | `CPTCNF3` — stock's RUNNING value, written through helper 0x0FF4 (Rev 20 site 0x0355), where the existing `assign 0xac` row is stock's BOOT value. Newly visible for the same reason. safety_net does no codec setup, so it writes neither. (mboxfw deliberately keeps 0xAC; see #161 and the corresponding row in rev20_diff_justifications.md.) |
| 0xfff9 | assign | 0x20 | REV20_ONLY | `ACG2FRQ0` — the mode-2 (44.1 kHz) clock-generator constant, written through helper 0x0E0F (Rev 20 site 0x077D). Newly visible. The existing `assign 0x0f` row covers the mode-3 constant written directly at 0x0E0A. safety_net synthesises no audio clock. |
| 0xffb0 | or | 0x01 | CHANGED_SN | `MEMCFG` — safety_net's `MEMCFG &= ~0x01` (clear SDW before `ljmp 0x8000`) is emitted by SDCC as a read-modify-write that the pattern classifier records as `or 0x01` as well as `and_not 0xfe`. Same single source line as the existing `and_not 0xfe` row (main.c handle_dfu_trigger); this is the second tuple that one write produces, not a second write. |
| 0xffb0 | runtime | - | CHANGED_REV | `MEMCFG` — Rev 20's only write to this address is write-computed (@ 0x08D4, Rev 22 @ 0x07F5): the value is not a literal, so the scanner cannot name an immediate. safety_net's write is a static RMW. Different shape, same register, and safety_net's is the narrower operation. |
| 0xffb2 | runtime | - | CHANGED_SN | `VECINT` — safety_net writes 0 to acknowledge each vector; SDCC emits some of those through a computed path the classifier reports as `runtime`. Complements the existing `rmw` row: datasheet §6.5.7.3 says ANY write clears the vector, so value and shape are both immaterial. |
| 0xfffc | runtime | - | CHANGED_REV | `USBCTL` — Rev 20's write-computed site (@ 0x08D0, Rev 22 @ 0x07F1), the master-init zeroing. safety_net does the same thing as a literal `assign 0x00` one instruction earlier (POLICY §2 carve-out, already justified in its own row). Same effect, statically determinable. |
| 0xfffd | assign | 0x9f | CHANGED_REV | `USBIMSK` — Rev 20 uses 0x9F = RSTR\|SOF\|PSOF\|SETUP\|STPOW (Rev 20 @ 0x09EC, Rev 22 @ 0x090D). safety_net uses 0xE5, which drops SOF/PSOF and adds SUSR/RESR: it does not stream, so it needs no frame clock, and it does need suspend/resume. Both set SETUP and STPOW, so enumeration works either way. NOTE the correction in the `assign 0xe5` row below: 0xE5 is supported by the TI reference ONLY. The claim that Rev 20 also used 0xE5 was false — Rev 20 never writes USBIMSK = 0xE5 anywhere. |
