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
| 0xff29 | assign | 0x01 | REV20_ONLY | Rev 20 boot-ROM SETUP-packet scratch writes (0xFF28..0xFF2F); safety_net reads these but does not write them — its handle_setup is a smaller state machine that never needs to synthesize SETUP fields. |
| 0xff29 | rmw | - | REV20_ONLY | Rev 20 boot-ROM SETUP-packet scratch writes (0xFF28..0xFF2F); safety_net reads these but does not write them — its handle_setup is a smaller state machine that never needs to synthesize SETUP fields. |
| 0xff2b | assign | 0x44 | REV20_ONLY | Rev 20 boot-ROM SETUP-packet scratch writes (0xFF28..0xFF2F); safety_net reads these but does not write them — its handle_setup is a smaller state machine that never needs to synthesize SETUP fields. |
| 0xff2b | assign | 0x80 | REV20_ONLY | Rev 20 boot-ROM SETUP-packet scratch writes (0xFF28..0xFF2F); safety_net reads these but does not write them — its handle_setup is a smaller state machine that never needs to synthesize SETUP fields. |
| 0xff2b | assign | 0xac | REV20_ONLY | Rev 20 boot-ROM SETUP-packet scratch writes (0xFF28..0xFF2F); safety_net reads these but does not write them — its handle_setup is a smaller state machine that never needs to synthesize SETUP fields. |
| 0xff2b | assign | 0xbb | REV20_ONLY | Rev 20 boot-ROM SETUP-packet scratch writes (0xFF28..0xFF2F); safety_net reads these but does not write them — its handle_setup is a smaller state machine that never needs to synthesize SETUP fields. |
| 0xff2b | rmw | - | REV20_ONLY | Rev 20 boot-ROM SETUP-packet scratch writes (0xFF28..0xFF2F); safety_net reads these but does not write them — its handle_setup is a smaller state machine that never needs to synthesize SETUP fields. |
| 0xff2c | assign | 0x00 | REV20_ONLY | Rev 20 boot-ROM SETUP-packet scratch writes (0xFF28..0xFF2F); safety_net reads these but does not write them — its handle_setup is a smaller state machine that never needs to synthesize SETUP fields. |
| 0xff2c | rmw | - | REV20_ONLY | Rev 20 boot-ROM SETUP-packet scratch writes (0xFF28..0xFF2F); safety_net reads these but does not write them — its handle_setup is a smaller state machine that never needs to synthesize SETUP fields. |
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
| 0xffaa | assign | 0x01 | CHANGED_SN | OEPBSIZ0 = EP_BSIZE(8) = 0x01 (8-byte EP0 OUT buffer). Companion to IEPBSIZ0 above. |
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
| 0xfffc | and_not | 0xfe | CHANGED_SN | USBCTL writes: (assign,0x00) explicit disconnect at main() entry per POLICY §2 carve-out (main.c:437-444); (or,0x01) is a false positive from RMW pattern classification of `USBCTL \|= 0x80` chain; (and_not,0xFE) is scan artifact of MEMCFG clear that reused dptr. See main.c:437-444 and USBCTL_ATTACH_BIT comment (main.c:64-74). |
| 0xfffc | assign | 0x00 | CHANGED_SN | USBCTL writes: (assign,0x00) explicit disconnect at main() entry per POLICY §2 carve-out (main.c:437-444); (or,0x01) is a false positive from RMW pattern classification of `USBCTL \|= 0x80` chain; (and_not,0xFE) is scan artifact of MEMCFG clear that reused dptr. See main.c:437-444 and USBCTL_ATTACH_BIT comment (main.c:64-74). |
| 0xfffc | or | 0x01 | CHANGED_SN | USBCTL writes: (assign,0x00) explicit disconnect at main() entry per POLICY §2 carve-out (main.c:437-444); (or,0x01) is a false positive from RMW pattern classification of `USBCTL \|= 0x80` chain; (and_not,0xFE) is scan artifact of MEMCFG clear that reused dptr. See main.c:437-444 and USBCTL_ATTACH_BIT comment (main.c:64-74). |
| 0xfffd | assign | 0x00 | CHANGED_REV | Rev 20 promotes USBIMSK to 0xFF and clears it in various error/reset paths. safety_net keeps a single 0xE5 mask — see main.c:483-494. |
| 0xfffd | assign | 0xe5 | CHANGED_SN | USBIMSK = 0xE5 — TI UsbEng.c:640, Rev 20 rev20_flat.asm 0x0917 both use 0xE5. mboxfw's diff justifications already cite this in row 326; safety_net matches. Rev 20's (assign,0xFF) and (assign,0x00) tuples are from VEC_RSTR mask promotion and boot-ROM path not present in safety_net. |
| 0xfffd | assign | 0xff | CHANGED_REV | Rev 20 promotes USBIMSK to 0xFF and clears it in various error/reset paths. safety_net keeps a single 0xE5 mask — see main.c:483-494. |
