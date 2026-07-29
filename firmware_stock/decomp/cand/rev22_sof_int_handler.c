// MATCH: image=rev22 addr=0x0D58 len=70 func=sof_int_handler cflags=--peep-file,firmware_stock/decomp/keil.peep

/* =====================================================================
 * THE REV 22 FIX.
 *
 * VECINT 0x14 (SOF_INT, constant from
 * reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h line 255) -- a USB
 * Start-Of-Frame arrived, i.e. this runs once per millisecond at full speed.
 *
 * REV 20 -> REV 22 DELTA: THIS FUNCTION IS NEW. There is no Rev 20
 * counterpart to port. Rev 20's vector table (0x0C93) has entry 0x14 at
 * 0x0CBB reading 10 34, and 0x1034 is a one-byte RET. Rev 22's table (0x0C7D)
 * has entry 0x14 at 0x0CA5 reading 0D 58, this function. I also searched
 * rev20_firmware_code.bin for these 70 bytes and for the four-byte SFR loads
 * `90 FF EC` and `90 FF EB`: neither address appears anywhere in the Rev 20
 * image, so Rev 20 never reads DMABCNT0 at all, in any function.
 *
 * SOF WAS NOT MASKED OFF IN REV 20. USBIMSK is written 0x9F in both images --
 * at power-on inside usb_ep_dma_init (`MOV A,#0x9F` at rev22 0x0910, rev20
 * 0x09EF) and again on every bus reset in usb_rstr_handler (rev22 0x0F8D,
 * rev20 0x0F6C) -- and bit 4 of 0x9F is the SOF interrupt enable. Rev 20 was
 * therefore already taking this interrupt every millisecond, running the whole
 * dispatcher prologue, and landing on a one-byte RET.
 * So the entire cost of the Rev 22 fix is two bytes in the vector table plus
 * the 70 bytes below. Nothing else in the image had to change to enable it.
 *
 * =====================================================================
 * WHAT IT DOES, in one sentence: it is a sample-alignment watchdog on the
 * playback isochronous stream, and it re-arms the endpoint whenever the
 * amount of undrained audio in the EP2-OUT buffer stops being a whole number
 * of 6-byte stereo 24-bit sample frames.
 *
 * ---------------------------------------------------------------------
 * THE REGISTER IT WATCHES.
 *
 * DMABCNT0H = 0xFFEC, DMABCNT0L = 0xFFEB (Reg_stc1.h lines 80-81; TAS1020B
 * datasheet SLES025B sections 6.5.2.4 and 6.5.2.5). This is NOT the USB frame
 * number. The datasheet is explicit about what it is:
 *
 *   "This register shows the buffer content (bytes) for an ISO OUT endpoint.
 *    This register is updated every SOF and is stable for the following USB
 *    frame, during which the MCU can read it to implement USB audio
 *    synchronization."   (6.5.2.4, Size(7:0))
 *
 *   "For isochronous OUT transactions, the count in the register represents
 *    the number of bytes being transferred from the OUT endpoint buffer to
 *    the C-port during the current USB frame. A new count is derived at each
 *    USB SOF event, and is the value of the write pointer address setting
 *    minus the read pointer address setting at the time of the USB SOF
 *    event."   (section 2, DMA overview)
 *
 * So it is the fill level, in BYTES, of the EP2-OUT circular buffer at the
 * instant of SOF. Reading it inside the SOF ISR is the documented usage, and
 * "channel 0" is the playback channel: rev22_usb_ep_dma_init writes
 * DMACTL0 = 0x02 at 0x0901, which is EPDIR = 0 (OUT) with EPNUM = 2, and
 * DMACTL1 = 0x09, EPDIR = 1 (IN) with EPNUM = 1.
 *
 * ---------------------------------------------------------------------
 * WHY 6.
 *
 * rev22_usb_ep_dma_init writes OEPCNF2 = 0xC5 at 0x08DF. For an isochronous
 * OUT endpoint (datasheet 6.4.3.6.2) that byte is
 * OEPEN | ISO | OVF | BPS(4:0), and BPS is encoded as bytes-minus-one
 * ("00h = 1 byte, 01h = 2 bytes, ... 1Fh = 32 bytes"), so 0xC5 means
 * OEPEN = 1, ISO = 1, OVF = 0, BPS = 5 = SIX BYTES PER SAMPLE. Six bytes is
 * stereo 24-bit: three bytes left, three bytes right. It is corroborated on
 * the C-port side by the same function's DMATSH0 = 0x80 / DMATSL0 = 0x03 --
 * BPTS = 3 bytes per time slot across time slots 0 and 1, 3 + 3 = 6.
 *
 * A residue in the buffer that is not a multiple of 6 means the DMA's next
 * fetch will take the wrong three bytes as time slot 0. That is not a
 * transient glitch: the circular buffer never re-aligns itself, so from that
 * moment on every sample is split across the L and R slots with a byte
 * rotation inside each. Audibly it is not a dropout, it is permanent
 * loud garbage until the stream is torn down. Nothing in Rev 20 detects or
 * corrects it.
 *
 * ---------------------------------------------------------------------
 * THE ALGORITHM, and I checked every branch against the bytes.
 *
 *   fill = (DMABCNT0H << 8) + DMABCNT0L;      0x0D58..0x0D68
 *   if (fill == saved) return;                0x0D69..0x0D71  <-- edge test
 *   saved = fill;                             0x0D73..0x0D76
 *   if (fill % 6 == 0) return;                0x0D77..0x0D7E
 *   DMACTL0 &= ~0x80;                         0x0D80  stop DMA channel 0
 *   OEPDCNTX2 = 0;                            0x0D87
 *   OEPDCNTY2 = 0;                            0x0D8C
 *   OEPCNF2   = 0xC5;                         0x0D90
 *   DMACTL0 |= 0x80;                          0x0D96  restart DMA channel 0
 *
 * THREE CORRECTIONS TO THE UNDERSTANDING PREVIOUSLY ON RECORD, all of which
 * the bytes settle:
 *
 *  (a) The two registers zeroed are OEPDCNTX2 (0xFF9B) and OEPDCNTY2
 *      (0xFF9F) -- the X and Y buffer DATA COUNT bytes of OUT endpoint 2 --
 *      not "OEPDCNTX2/OEPDCNTY2". Reg_stc1.h lines 205 and 226 name them
 *      OEPDCNTX2 and OEPDCNTY2. (Some TI material calls the same byte
 *      OEPBCTx; cand/usb_ep_dma_init.c uses that spelling for 0xFF9B. Same
 *      register either way, but the datasheet section that describes the bit
 *      layout, 6.4.4.1 and its OUT twin, is filed under the DCNT name.)
 *
 *  (b) The test is `changed AND NOT a multiple of 6`, not `a multiple of 6`.
 *      0x0D7E is `JZ 0x0D9D`, which RETURNS when the remainder is zero, so
 *      the body runs on a NON-zero remainder. (A comment in
 *      cand/rev22_udiv16.c has this backwards, and also calls DMABCNT0 the
 *      frame number. Both are wrong; I have not edited that file, as it is
 *      not in my batch.)
 *
 *  (c) The `if (fill == saved)` early-out at 0x0D71 was not on record at all,
 *      and it is not incidental. Without it the handler would re-arm the
 *      endpoint every single millisecond for as long as the fill level sat at
 *      a misaligned value -- including while the stream is idle and the level
 *      is frozen. With it, the watchdog fires at most once per DISTINCT value
 *      of DMABCNT0. Note the save happens BEFORE the modulo test (0x0D73
 *      precedes the LCALL at 0x0D79), so `saved` tracks every change whether
 *      or not the body runs.
 *
 * ---------------------------------------------------------------------
 * WHERE THE TWO BYTES OF STATE CAME FROM.
 *
 * `saved` lives in IRAM 0x1B (high) and 0x1C (low) -- Keil's big-endian
 * convention, the same one Rev 20 used for its EP0 buffer pointer. That is
 * not a coincidence: in Rev 20, IRAM 0x1B:0x1C IS the EP0 buffer pointer
 * (cand/dptr_to_ep0_out_buf.c). Rev 22 moved that pointer to 0x1D:0x1E --
 * ep0_out_buf_ptr_load at 0x0B1F writes 0x1D/0x1E, ep0_load_dptr at 0x0B25
 * reads them -- which freed 0x1B:0x1C for this handler.
 *
 * 8051 TRAP, and it matters here. Scanning the Rev 22 listing for 0x1B and
 * 0x1C turns up `CLR 0x1B` at 0x0718, `SETB 0x1B` at 0x07D1 and 0x09DA, and
 * `SETB 0x1C` at 0x09E5. Those are BIT addresses -- IRAM 0x23.3 and 0x23.4 --
 * and have nothing to do with these bytes. The only BYTE accesses to IRAM
 * 0x1B and 0x1C in the entire image are the four instructions in this
 * function, at 0x0D6A, 0x0D6F, 0x0D73 and 0x0D75.
 *
 * Initial value: the Keil startup at 0x092A is
 * `MOV R0,#0x7F / CLR A / MOV @R0,A / DJNZ R0,-3`, which clears IRAM 0x7F down
 * to 0x01 -- the DJNZ exits when R0 reaches zero, so IRAM 0x00 is never
 * written. `saved` lives in that cleared range, so it starts at 0. Nothing resets it
 * afterwards -- usb_rstr_handler (0x0F64) does not touch it -- so across a
 * bus reset the first SOF compares the new fill level against the last one
 * from before the reset. That is harmless: a stale mismatch costs at most one
 * spurious re-arm of an endpoint that the reset has just re-armed anyway.
 *
 * ---------------------------------------------------------------------
 * THE RECOVERY SEQUENCE, register by register.
 *
 *   DMACTL0 &= ~0x80    Clears DMAEN (datasheet 6.5.2.3 bit 7). The datasheet
 *                       requires that "before enabling the DMA channel, all
 *                       other DMA channel configuration bits must be set to
 *                       the desired value", which is why the endpoint is
 *                       fixed up with the channel stopped and DMAEN is set
 *                       again last. Note this is a read-modify-write, so
 *                       EPDIR and EPNUM survive -- the channel stays bound to
 *                       EP2 OUT.
 *   OEPDCNTX2 = 0       Clears the X-buffer data count and, with it, bit 7
 *   OEPDCNTY2 = 0       NACK. The datasheet is explicit that for isochronous
 *                       endpoints "the MCU or DMA must clear this bit after
 *                       writing a data packet to the buffer". Both halves are
 *                       cleared. usb_ep_dma_init only ever clears X (0xFF9B),
 *                       never Y, so at power-on the Y count is whatever reset
 *                       left it.
 *
 *                       THE PAIR IS NOT NEW CODE, though: the identical
 *                       four-register sequence -- OEPDCNTX2 = 0,
 *                       OEPDCNTY2 = 0, then 0xC5 into IEPCNF1 and OEPCNF2 --
 *                       already exists inside audio_clock_set_mode, at rev22
 *                       0x07BD..0x07CE and rev20 0x07DC..0x07EE. Those are the
 *                       ONLY two writes to 0xFF9F in the Rev 22 image (0x07C1
 *                       and 0x0D8C) and the only one in Rev 20 (0x07E0). So
 *                       the SOF handler did not invent a recovery sequence; it
 *                       reuses the endpoint-restart idiom the clock-mode
 *                       change path was already using, minus the IEPCNF1 half
 *                       (capture is not what is misaligned) and plus the
 *                       DMACTL0 stop/start bracket.
 *   OEPCNF2 = 0xC5      Rewrites the endpoint configuration to exactly what
 *                       usb_ep_dma_init set at power-on: OEPEN | ISO |
 *                       BPS = 5. Because it is a whole-byte write and not an
 *                       OR, it also clears bit 5, which in isochronous mode is
 *                       OVF -- "set to a 1 by the UBM to indicate a buffer
 *                       overflow condition has occurred ... can only be
 *                       cleared to a 0 by the MCU" (6.4.3.6.2). So this write
 *                       does double duty: restore configuration, and clear the
 *                       latched overflow flag that a misaligned buffer is the
 *                       likely aftermath of.
 *   DMACTL0 |= 0x80     DMAEN back on.
 *
 * WHAT I HAVE NOT VERIFIED: that writing 0 to the two data-count bytes resets
 * the circular buffer's read and write pointers, which is what would actually
 * restore alignment. The datasheet describes the ISO endpoint buffer as "a
 * circular buffer rather than one or two linear buffers" but does not say what
 * the DCNTX/DCNTY writes do to its pointers. The intent of the sequence is
 * clear from its shape -- stop, clear counts, reassert config, restart -- and
 * it is empirically the fix Digidesign shipped, but I am not going to assert a
 * pointer-reset mechanism I cannot cite. Confirming it needs hardware.
 *
 * ---------------------------------------------------------------------
 * COST, since this runs 1000 times a second with EA = 0 (the dispatcher
 * clears it at 0x0DEC and restores it at 0x0E0E). The common path is the
 * two SFR reads, the 16-bit compare and a RET -- about 20 cycles. When the
 * level has changed it also pays a call to udiv16 (0x0B7F), whose 8-bit-
 * divisor path is a DIV AB (4 cycles) when fill < 256 and an 8-iteration
 * shift-subtract loop when it is not. Interrupts are off for all of it, and
 * it is why the ack ordering in usb_isr_int0_vecdispatch matters: VECINT is
 * written 0 only after the handler returns, so a SETUP that lands mid-divide
 * is deferred, not lost. (I have not measured this against the other USB
 * handlers to claim it is the worst case in the image; the point is only that
 * Rev 22 added work to a path that previously did none.)
 *
 * =====================================================================
 * WRITTEN AS ASSEMBLY. Not a stylistic choice -- C cannot reach it.
 *
 * The modulo is a call to the Keil library routine ?C?UIDIV at 0x0B7F, which
 * takes its dividend in R6:R7 and its divisor in R4:R5 and returns the
 * remainder in R4:R5. SDCC's `%` emits a call to its own _moduint, with a
 * different name, a different register convention and a different body, and
 * no peephole rule can turn one library into the other. On top of that, R4 is
 * loaded with 0 at 0x0D61 as the high byte of the 16-bit widening of
 * DMABCNT0L and is then REUSED, still 0, as the high byte of the divisor at
 * 0x0D77 and as the high half of the zero-test at 0x0D7D -- one register
 * serving three unrelated roles across a call boundary. That is Keil's global
 * register allocator, the hw_master_init class described in
 * decomp/README.md "When to stop using C".
 *
 * The C this corresponds to is, as near as it can be written:
 *
 *     static unsigned int saved;                 // IRAM 0x1B:0x1C
 *     void sof_int_handler(void) {
 *         unsigned int fill = ((unsigned int)DMABCNT0H << 8) + DMABCNT0L;
 *         if (fill == saved) return;
 *         saved = fill;
 *         if (fill % 6 == 0) return;
 *         DMACTL0 &= ~0x80;
 *         OEPDCNTX2 = 0;
 *         OEPDCNTY2 = 0;
 *         OEPCNF2   = 0xC5;
 *         DMACTL0 |= 0x80;
 *     }
 *
 * and the `+ DMABCNT0L` rather than `| DMABCNT0L` is visible in the bytes:
 * 0x0D63 is `ADD A,#0x00` and 0x0D67 is `ADDC A,R6`, a full 16-bit addition
 * of (H<<8) and the zero-extended L, with both constant-folded halves left in
 * place. An OR or a cast would not have emitted the carry propagation. */
void sof_int_handler(void) __naked {
    __asm
        .globl _udiv16

        ; ---- fill = (DMABCNT0H << 8) + DMABCNT0L -------------------------
        mov   dptr,#0xffec         ; DMABCNT0H -- ISO OUT buffer content, high
        movx  a,@dptr
        mov   r6,a                 ; R6:R7 = H:00 so far
        mov   dptr,#0xffeb         ; DMABCNT0L -- ... low
        movx  a,@dptr
        mov   r4,#0x00             ; high half of the widened L; stays 0 and is
                                   ; reused twice more below
        add   a,#0x00              ; 16-bit add: low halves
        mov   r7,a
        mov   a,r4
        addc  a,r6                 ; ... and high halves, with the carry
        mov   r6,a                 ; R6:R7 = fill, in bytes

        ; ---- unchanged since the last SOF? then nothing to judge ----------
        mov   a,r7
        xrl   a,0x1c               ; IRAM BYTE 0x1C = saved low
        jnz   00301$
        mov   a,r6
        xrl   a,0x1b               ; IRAM BYTE 0x1B = saved high
    00301$:
        jz    00302$               ; equal -> return, fire once per new value

        mov   0x1b,r6              ; saved = fill, BEFORE the modulo test
        mov   0x1c,r7

        ; ---- fill % 6 -- 6 bytes is one stereo 24-bit sample frame --------
        mov   r5,#0x06             ; divisor R4:R5 = 0x0006 (R4 still 0)
        lcall _udiv16              ; Keil ?C?UIDIV: R6:R7 / R4:R5
                                   ;   -> quotient R6:R7, remainder R4:R5
        mov   a,r5
        orl   a,r4                 ; 16-bit test of the remainder
        jz    00302$               ; aligned -> nothing to do

        ; ---- misaligned: stop the channel, re-arm EP2 OUT, restart --------
        mov   dptr,#0xffe8         ; DMACTL0
        movx  a,@dptr
        anl   a,#0x7f              ; clear DMAEN; EPDIR/EPNUM preserved
        movx  @dptr,a

        mov   dptr,#0xff9b         ; OEPDCNTX2
        clr   a
        movx  @dptr,a              ; count = 0, NACK = 0
        mov   dptr,#0xff9f         ; OEPDCNTY2 -- see the note on 0x07C1 above
        movx  @dptr,a

        mov   dptr,#0xff98         ; OEPCNF2
        mov   a,#0xc5              ; OEPEN | ISO | BPS=5 (6 B/sample); the
        movx  @dptr,a              ; whole-byte write also clears OVF (bit 5)

        mov   dptr,#0xffe8         ; DMACTL0
        movx  a,@dptr
        orl   a,#0x80              ; DMAEN back on, config now settled
        movx  @dptr,a
    00302$:
        ret                        ; RET, not RETI -- the dispatcher LCALLed us
    __endasm;
}
