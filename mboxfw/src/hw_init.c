/*
 * Master hardware initialisation.
 * Ports Rev 20's fcn.0x08CB verbatim — see
 * firmware_stock/disasm/NOTES.md § "Master boot init".
 */

#include "regs.h"
#include "mux.h"

extern __data unsigned char g_mux_state;  /* mirror of Rev 20 RAM[0x22] */
extern __bit g_phantom_48v;               /* mirror of Rev 20 RAM[0x23].6 */

static void short_delay(void)
{
    /* Rev 20 spins RAM[0x2E] from 0 up to 0x0F00 (~4000 cycles). */
    unsigned int i;
    for (i = 0; i < 0x0F00; i++) { }
}

void hw_init(void)
{
    /* -------- 8051 core SFRs -------- */
    TMOD = 0x11;   /* both timers 16-bit mode 1 */
    TH0  = 0xCE;   /* SOF-tick timer reload */
    TL0  = 0x00;
    TH1  = 0x00;
    TL1  = 0x00;
    TCON = 0x00;
    /* Explicit IT0 = 0 (level-triggered INT0). TCON = 0x00 above already
     * zeroes bit 0, but making it explicit documents the intent and
     * survives refactors of the TCON write. Reference: TI UsbEng.c:644
     * `IT0 = 0` in engUsbInit; Rev 20 clears TCON at 0x08FD. Level mode
     * is required because the USB engine ORs all unmasked USBIMSK sources
     * into INT0 — edge mode would only fire once and miss re-assertions. */
    IT0  = 0;
    IE   = 0x03;   /* EX0 (INT0/USB) + ET0 (Timer 0) */
    IP   = 0x00;
    P1   = 0x00;   /* all P1 pins low */
    P3   = 0xFF;   /* all P3 pins high (button inputs pull-up) */

    /* -------- TAS1020A UIFR init (order matters) --------
     *
     * DO NOT touch USBCTL here. Boot ROM's UtilResetCPU handoff and our
     * usb_init() (called BEFORE hw_init in main.c) both configure
     * USBCTL — clobbering it with `=` in the middle of boot leaves the
     * host un-attached and any hang below unreachable via DFU. If we
     * ever want to reset USB state, use RMW (& ~bits / |= bits), never
     * a raw assignment. See task #48. */
    MEMCFG  |= 0x01;    /* set SDW — code fetches route to RAM copy.
                         * RMW because boot ROM's UtilResetCPU already
                         * did MEMCFG |= SDW_BIT; we're just being
                         * idempotent. Reference: TI Utils.c UtilResetCPU. */
    /* Codec-port config. Addresses and values verified byte-for-byte
     * against both stock images by static scan (see the SFR tables in
     * firmware_stock/disasm/rev2{0,2}_STARTUP_TRACE.md). The register
     * names used here previously did not match TI's and have been
     * corrected; the addresses written are unchanged. */
    CPTCNF1   = 0x0D;   /* 0xFFE0 — stock writes 0x0D */
    CPTCNF2   = 0xE5;   /* 0xFFDF — stock writes 0xE5 */
    CPTCNF3   = 0xAC;   /* 0xFFDE — stock writes 0xAC */
    CPTCNF4   = 0x03;   /* 0xFFDD — stock writes 0x03 */
    CPTSTA    = 0x50;   /* 0xFFDC — stock writes 0x50 */
    CPTRXCNF2 = 0x25;   /* 0xFFD6 — stock writes 0x25 */
    CPTRXCNF3 = 0xAC;   /* 0xFFD5 — stock writes 0xAC */
    /* CPTRXCNF4 — DIVB2(2:0), the divider from MCLKO2 to SCLK2, which is
     * the I2S RECEIVE bit clock (datasheet §6.5.4.13; block diagram
     * Figure 2-1). Encoding: 001b = ÷2, 010b = ÷3, 011b = ÷4.
     *
     * This read 0x01 (÷2) between 2026-07-26 and 2026-07-28, changed from
     * 0x03 on the note "both stock images write 0x01 here. mboxfw wrote
     * 0x03 — the only address+value divergence in the whole codec-port
     * block." That note was true and incomplete. Both stock images write
     * this address TWICE, with different values in different contexts:
     *
     *   boot init  Rev 20 @0x0929, Rev 22 @0x084A:  0x03   (÷4)
     *   mode 5     Rev 20 @0x07A0, Rev 22 @0x077E:  0x01   (÷2)
     *
     * hw_init mirrors stock's BOOT init, so 0x03 is the value that belongs
     * here. 0x01 belongs to the mode-5 branch (I2S "1 OUT and 1 IN at
     * different frequencies"), which mboxfw does not implement.
     *
     * Halving this divider doubles the receive frame rate, and that is
     * precisely what hardware measured: IEPDCNTX1 read a steady DCNTX of 96
     * samples per USB frame where stock delivers 48, and 88 where stock
     * would deliver 44. Both exactly 2x. Restoring ÷4 restores 48 kHz and
     * 44.1 kHz. Verified by byte-scanning both stock images for every
     * `90 ff d4` (mov dptr,#0xFFD4) rather than trusting one disassembly. */
    CPTRXCNF4 = 0x03;   /* Rev 20 fcn.0x08CB @ 0x0929 */
    /* GLOBCTL bit 0 = CPTEN (codec port enable), NOT USB engine —
     * verified against TI RomBoot.c:33 "GLOBCTL = 0x04; // 12Mclk,
     * Ext int off, LPWR on, CODEC is off". The USB engine is
     * already up (boot ROM leaves LPWR bit 2 = 1). We set CPTEN
     * only AFTER the six CPTCNF/CPTBR/CPTCTL codec regs above are
     * programmed, matching Rev 20 fcn.0x08CB @0x0946 and v22. Do
     * NOT copy this write into any codec-less context (see
     * safety_net main.c:473-491 comment for the silent-USB bug
     * this pattern caused there). */
    GLOBCTL  |= 0x01;   /* enable codec port (CPTEN) */

    /* -------- DMA channel 0 + 1 boot init --------
     *
     * These are the TI-defined DMACTL0/1 + transfer-size registers at
     * 0xFFE8-0xFFF0, NOT the Rev-20-empirical DMACTL0/1/2 aliases at
     * 0xFFE0-0xFFE2. Rev 20 configures both address blocks; earlier
     * mboxfw drafts only knew about the Rev-20-empirical aliases and
     * dropped the TI-block writes, which meant the underlying DMA
     * channels were never armed — enumeration succeeded but no audio
     * bytes actually flowed to the codec.
     *
     * Rev 20 fcn.0x08CB boot init writes these during hw setup. Values
     * come from rev20_flat.asm boot-block plus RE cross-checks in
     * firmware_stock/disasm/rev20_audio_dispatch.md §3 ("three DMA
     * channels are configured, not two") and rev20_dynamic_reconfig.md
     * §3 "Common streaming tail". Names cited by address per the
     * regs.h naming caveat (Rev-20 vs TI Reg_stc1.h disagree). */
    /* Rev 20 fcn.0x08CB @ 0x0912-0x092A — six DMA-channel init bytes.
     * Same addresses and values as before; only the names changed, from
     * raw XDATA() to the TI/datasheet names now in regs.h.
     * DMACTL0 = 0x02: EPDIR=0 (OUT) + EPNUM=2  → EP2 OUT, playback.
     * DMACTL1 = 0x09: EPDIR=1 (IN)  + EPNUM=1  → EP1 IN,  capture.
     * DMATSH  = 0x80: BPTS=10b = 3 bytes per time slot.
     * DMATSL  = 0x03: time slots 0 and 1 → 2 channels × 3 B = 6 B/sample.
     * DMAEN (bit 7) is deliberately NOT set here — the channels are
     * enabled per direction at SET_INTERFACE time in streaming.c, which
     * is what Rev 20 does. */
    DMACTL0 = 0x02;   /* Rev 20 fcn.0x08CB @ 0x09F2 */
    DMATSH0 = 0x80;   /* Rev 20 fcn.0x08CB @ 0x09E0 */
    DMATSL0 = 0x03;   /* Rev 20 fcn.0x08CB @ 0x09DA */
    DMACTL1 = 0x09;   /* Rev 20 fcn.0x08CB @ 0x09F8 */
    DMATSH1 = 0x80;   /* Rev 20 fcn.0x08CB @ 0x09EC */
    DMATSL1 = 0x03;   /* Rev 20 fcn.0x08CB @ 0x09E6 */

    /* -------- Initial mux state -------- */
    g_mux_state  = 0x00;
    g_phantom_48v = 1;      /* Rev 20 sets 0x23.6 = 1 initially */
    mux_write(g_mux_state);

    short_delay();

    /* Rev 20 default: source-select bit 0 clear on both channels, 48V off.
     * Bits: 0x22 = 0xFF then clear .0, .3, and 0x23.6.
     */
    g_mux_state  = (unsigned char)(0xFF & ~0x01 & ~0x08);
    g_phantom_48v = 0;
    mux_write(g_mux_state);
}
