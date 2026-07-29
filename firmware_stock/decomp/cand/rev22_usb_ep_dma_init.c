// MATCH: image=rev22 addr=0x0891 len=153 func=usb_ep_dma_init cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Endpoint buffer and DMA channel setup, Rev 22 at 0x0891.
 *
 * REV 20 -> REV 22 DELTA: NONE. All 153 bytes are IDENTICAL. Verified by
 * comparing rev20_firmware_code.bin[0x0970:0x0A09] against
 * rev22_firmware_code.bin[0x0891:0x092A] -- zero differing byte positions.
 * That is unsurprising for this function specifically: it contains no code
 * addresses at all (no LCALL, no LJMP), only SFR addresses and immediates, so
 * relocation could not perturb it even if the source had changed. The Rev 20
 * candidate cand/usb_ep_dma_init.c ported verbatim; only the MATCH header was
 * retargeted.
 *
 * Call sites also correspond one-for-one: rev20 0x0554 and 0x0AB0, rev22
 * 0x0553 and 0x0A5A -- the USB re-init path inside the vendor command block,
 * and main().  It runs immediately after hw_master_init and before USBCTL.CONN
 * is asserted (rev22 0x055C-0x0562 reads 0xFFFC and ORs in 0x80).
 *
 * Assembly for the same reason as hw_master_init: Keil keeps A live across the
 * whole run and uses INC A to step 0x42 -> 0x43 and 0 -> 1, which SDCC will
 * not reproduce without adjacency-specific peephole rules.
 *
 * Buffer base registers are in 8-byte units above 0xF800, so 0x42 means
 * 0x42 * 8 + 0xF800 = 0xFA10 and 0x43 means 0xFA18. Those are exactly the EP0
 * OUT and IN buffers that ep0_ptr_set_in_buf and dptr_to_ep0_out_buf point at,
 * and they are 8 bytes apart, matching bMaxPacketSize0 = 8. */
void usb_ep_dma_init(void) __naked {
    __asm
        ; ---- endpoint 0: 8-byte OUT and IN buffers at 0xFA10 / 0xFA18 ----
        mov   dptr,#0xffa9         ; OEPBBAX0
        mov   a,#0x42              ;   0x42 * 8 + 0xF800 = 0xFA10
        movx  @dptr,a
        mov   dptr,#0xff69         ; IEPBBAX0
        inc   a                    ;   0x43 -> 0xFA18
        movx  @dptr,a
        mov   dptr,#0xffab         ; OEPDCNTX0
        clr   a
        movx  @dptr,a
        mov   dptr,#0xff6b         ; IEPDCNTX0
        movx  @dptr,a
        mov   dptr,#0xffaf         ; OEPDCNTY0
        movx  @dptr,a
        mov   dptr,#0xff6f         ; IEPDCNTY0
        movx  @dptr,a
        mov   dptr,#0xffaa         ; OEPBSIZ0
        inc   a                    ;   1 unit = 8 bytes
        movx  @dptr,a
        mov   dptr,#0xff6a         ; IEPBSIZ0
        movx  @dptr,a
        mov   dptr,#0xffa8         ; OEPCNF0
        mov   a,#0x84
        movx  @dptr,a
        mov   dptr,#0xff68         ; IEPCNF0
        movx  @dptr,a

        ; ---- audio endpoints: EP2 OUT playback, EP1 IN capture ----
        mov   dptr,#0xff99         ; OEPBBAX2  0x44 -> 0xFA20
        mov   a,#0x44
        movx  @dptr,a
        mov   dptr,#0xff61         ; IEPBBAX1  0x94 -> 0xFCA0
        mov   a,#0x94
        movx  @dptr,a
        mov   dptr,#0xff9a         ; OEPBSIZ2
        mov   a,#0x50
        movx  @dptr,a
        mov   dptr,#0xff62         ; IEPBSIZ1
        movx  @dptr,a
        mov   dptr,#0xff9b         ; OEPDCNTX2
        clr   a
        movx  @dptr,a
        mov   dptr,#0xff63         ; IEPDCNTX1
        movx  @dptr,a
        mov   dptr,#0xff98         ; OEPCNF2
        mov   a,#0xc5              ;   enable | ISO | BPS 5 = 6 bytes/sample
        movx  @dptr,a
        mov   dptr,#0xff60         ; IEPCNF1
        movx  @dptr,a

        ; ---- DMA transfer sizes: 3 bytes/slot on slots 0 and 1 = 6 B/sample --
        mov   dptr,#0xffea         ; DMATSL0
        mov   a,#0x03              ;   time slots 0 and 1
        movx  @dptr,a
        mov   dptr,#0xffe9         ; DMATSH0
        mov   a,#0x80              ;   BPTS = 3 bytes per slot
        movx  @dptr,a
        mov   dptr,#0xfff0         ; DMATSL1
        mov   a,#0x03
        movx  @dptr,a
        mov   dptr,#0xffef         ; DMATSH1
        mov   a,#0x80
        movx  @dptr,a
        ; rev22 usb_ep_dma_init @ 0x0901 (rev20 0x09E0) — DMACTL0 = 0x02
        ; EPDIR=0 (OUT) + EPNUM=2. DMAEN (bit 7) deliberately NOT set here;
        ; the channels are armed per direction at SET_INTERFACE time.
        mov   dptr,#0xffe8         ; DMACTL0 = EP2 OUT, playback
        mov   a,#0x02
        movx  @dptr,a
        ; rev22 usb_ep_dma_init @ 0x0907 (rev20 0x09E6) — DMACTL1 = 0x09
        ; EPDIR=1 (IN) + EPNUM=1.
        mov   dptr,#0xffee         ; DMACTL1 = EP1 IN, capture
        mov   a,#0x09
        movx  @dptr,a

        mov   dptr,#0xfffd         ; USBIMSK
        mov   a,#0x9f              ;   note: cmd2 later raises this to 0xFF
        movx  @dptr,a
        mov   dptr,#0xffff         ; USBFADR
        clr   a
        movx  @dptr,a

        clr   0x0a                 ; bit 0x0A  (IRAM 0x21.2)
        clr   0x0e                 ; bit 0x0E  configured
        clr   0x08                 ; bit 0x08  interface 1 alt
        clr   0x09                 ; bit 0x09  interface 2 alt
        mov   0x09,a               ; BYTE 0x09 transfer length low  -- not the
        mov   0x0b,a               ; BYTE 0x0B transfer length high -- bits above
        mov   0x0c,#0xfe
        mov   0x0a,a               ; BYTE 0x0A pending event = none
        ret
    __endasm;
}
