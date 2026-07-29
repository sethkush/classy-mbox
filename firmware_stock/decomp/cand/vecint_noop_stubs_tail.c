// MATCH: image=rev20 addr=0x1031 len=14 span=1 func=vecint_iep7_noop cflags=--peep-file,firmware_stock/decomp/keil.peep
/* The fourteen VECINT no-op handlers that did not fit in the vector-table gaps
 * (see cand/vecint_noop_stubs.c). They sit at the very top of the image, right
 * after toggle_flag_bit1e ends at 0x1030; 0x103F onwards is 0xFF erase fill,
 * so these are the last code bytes in rev20.
 *
 * Ghidra lists fourteen one-byte functions. This candidate claims the run with
 * span=1 for the same reason as the low block: fourteen files each holding one
 * 0x22 byte would carry no information.
 *
 * Each is the target of one or more entries of the VECINT dispatch table at
 * 0x0C93 (37 big-endian code addresses, 0x0C93..0x0CDC). The entry index below
 * is (siteAddr - 0x0C93) / 2 and each was read out of the table bytes; Ghidra
 * records the same links as XREFs on each stub.
 *
 * 0x103E is shared by six table entries. That is the "definitely nothing to do
 * here" address, and it is what makes the block fourteen bytes rather than
 * nineteen.
 *
 * REV 22 COMPARISON. The same construct exists in rev22 with its table at
 * 0x0C7D and its stub block at 0x1029..0x1035 -- thirteen bytes, one fewer.
 * The missing one is entry 20: rev20 points it at the no-op 0x1034, rev22
 * points it at a real routine, sof_int_handler at rev22 0x0D58 (Ghidra's rev22
 * listing names it and records the XREF from CODE:0ca5, which is exactly entry
 * 20 of the rev22 table). So Rev 20 ships no start-of-frame handler at all.
 *
 * Note what that does NOT mean. SOF is *not* masked off: USBIMSK (0xFFFD) is
 * loaded with 0x9F at rev20 0x09EF / rev22 0x0910, and 0x9F is 1001 1111, so
 * bit 4 -- SOF -- is set, i.e. enabled. The bus-reset handler rewrites the same
 * 0x9F, and starting a stream raises the mask to 0xFF (rev20 0x03F4, rev22
 * 0x03F8), which also leaves SOF set. See cand/usb_rstr_handler.c for the full
 * decode of 0x9F. So in Rev 20 the SOF interrupt is UNMASKED and fires once per
 * USB frame, every frame: it enters usb_int0_isr, indexes the table, LCALLs
 * 0x1034, and immediately RETs. The frame clock is being delivered to firmware
 * and thrown away. Rev 22 is the image that puts a real routine in that slot;
 * anything in Rev 20 that wants a frame clock has the interrupt already but no
 * handler behind it.
 *
 * VECINT NAMES. The value-to-name mapping is TI's and is in this repo:
 * reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h, "interrupt source" block
 * -- OEP0_INT 0x00..IEP7_INT 0x0F, STPOW_INT 0x10, SETUP_INT 0x12, PSOF_INT
 * 0x13, SOF_INT 0x14, RESR_INT 0x15, SUSR_INT 0x16, RSTR_INT 0x17, CPRX_INT
 * 0x18, CPTX_INT 0x19, DPRX_INT 0x1A, DPTX_INT 0x1B, I2CRX_INT 0x1C, I2CTX_INT
 * 0x1D, XINT_INT 0x1F, NO_INT 0x24; 0x11 and 0x1E are marked reserved and
 * 0x20..0x23 are not defined at all. The table entry index IS the VECINT value
 * (cand/usb_int0_isr.c: VECINT is doubled and added to the base), so the index
 * and the name are the same fact. Both are given below. That mapping also
 * independently corroborates Ghidra's position-derived name for this block's
 * first byte: entry 15 = IEP7_INT, and Ghidra calls it vecint_iep7_noop.
 */
void vecint_iep7_noop(void) __naked {
    __asm
        ret                        ; 0x1031  <- entry 15 @ 0x0CB1  IEP7_INT
        ret                        ; 0x1032  <- entry 16 @ 0x0CB3  STPOW_INT
        ret                        ; 0x1033  <- entry 19 @ 0x0CB9  PSOF_INT
        ret                        ; 0x1034  <- entry 20 @ 0x0CBB  SOF_INT
                                   ;          unmasked but ignored here; a real
                                   ;          handler in rev22
        ret                        ; 0x1035  <- entry 21 @ 0x0CBD  RESR_INT
        ret                        ; 0x1036  <- entry 24 @ 0x0CC3  CPRX_INT
        ret                        ; 0x1037  <- entry 25 @ 0x0CC5  CPTX_INT
        ret                        ; 0x1038  <- entry 28 @ 0x0CCB  I2CRX_INT
        ret                        ; 0x1039  <- entry 29 @ 0x0CCD  I2CTX_INT
        ret                        ; 0x103A  <- entry 31 @ 0x0CD1  XINT_INT
        ret                        ; 0x103B  <- entry 32 @ 0x0CD3  (0x20, undefined)
        ret                        ; 0x103C  <- entry 33 @ 0x0CD5  (0x21, undefined)
        ret                        ; 0x103D  <- entry 36 @ 0x0CDB  NO_INT
        ret                        ; 0x103E  <- entries 17, 26, 27, 30, 34, 35
                                   ;            = 0x11 res, DPRX, DPTX, 0x1E res,
                                   ;              0x22, 0x23
                                   ;            @ 0x0CB5 0x0CC7 0x0CC9 0x0CCF
                                   ;              0x0CD7 0x0CD9
    __endasm;
}
