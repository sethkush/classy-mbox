/*
 * Master hardware initialisation.
 * Ports Rev 20's fcn.0x08CB verbatim — see
 * firmware_stock/disasm/NOTES.md § "Master boot init".
 */

#include "regs.h"
#include "mux.h"

extern __data unsigned char g_mux_state;  /* mirror of Rev 20 RAM[0x22] */
extern __bit g_mono;               /* mirror of Rev 20 RAM[0x23].6 */

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
    /* CPTCNF3 / CPTRXCNF3 bit 2 is BYOR, the byte-order bit: "when this bit
     * is set to a 1, the byte order of each audio sample is reversed when
     * the data is moved to/from the USB endpoint buffer" (datasheet
     * §6.5.4.3 and §6.5.4.12 — identical layouts).
     *
     * Stock is big-endian on the wire and sets BYOR. Chain of evidence,
     * entirely static:
     *   1. Linux declares stock 0dba:1000 as SNDRV_PCM_FMTBIT_S24_3BE, both
     *      directions (reference/mbox1_quirks-table.h.snippet).
     *   2. Stock boot init writes 0xAC (BYOR=1) to BOTH registers, in Rev 20
     *      (@0x090B, @0x0923) and Rev 22 (@0x082C, @0x0844).
     *   3. So BYOR=1 gives big-endian, and BYOR=0 gives little-endian.
     * mboxfw declares S24_3LE, which is the spec-compliant choice and the
     * whole point of the project, so it wants BYOR=0 where stock has 1.
     *
     * Copying stock's 0xAC is why the first successful capture looked like
     * full-scale noise: it was correct audio with the bytes in the other
     * order. `00 00 80` reads as -8388608 little-endian and as 128 big-.
     *
     * The two directions are NOT symmetric. Rev 20 toggles CPTCNF3 at
     * runtime by direction — 0xAC (BYOR=1) when capture is requested
     * (@0x035C) and 0xA8 (BYOR=0) when playback is (@0x0367) — while Linux
     * reports BOTH directions as big-endian. So the reversal is relative to
     * the direction the DMA moves bytes, and the two paths need opposite
     * BYOR values to produce the same wire order.
     *
     * CPTRXCNF3 governs the receive (capture) path in I2S mode 5, which is
     * the path we have measured, and clearing BYOR there is well supported
     * by the chain above. The playback value is a build-time switch because
     * we have no playback measurement yet; MBOX_PLAYBACK_BYOR selects it so
     * two units can carry opposite settings and be compared over a loopback
     * cable. Delete the switch once a loopback settles it. */
#ifndef MBOX_PLAYBACK_BYOR
#define MBOX_PLAYBACK_BYOR 1      /* 1 = 0xAC (stock boot value), 0 = 0xA8 */
#endif
#if MBOX_PLAYBACK_BYOR
    CPTCNF3   = 0xAC;   /* Rev 20 fcn.0x08CB @ 0x090B — BYOR set */
#else
    CPTCNF3   = 0xA8;   /* Rev 20 @ 0x0367 playback branch — BYOR clear */
#endif
    CPTCNF4   = 0x03;   /* 0xFFDD — stock writes 0x03 */
    CPTSTA    = 0x50;   /* 0xFFDC — stock writes 0x50 */
    CPTRXCNF2 = 0x25;   /* 0xFFD6 — stock writes 0x25 */
    /* Capture path: BYOR cleared so the wire order matches our declared
     * S24_3LE. Stock writes 0xAC here (Rev 20 fcn.0x08CB @ 0x0923). */
    CPTRXCNF3 = 0xA8;
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
    DMACTL0 = 0x02;   /* Rev 20 fcn.0x08CB @ 0x09E0 */
    DMATSH0 = 0x80;   /* Rev 20 fcn.0x08CB @ 0x09CE */
    DMATSL0 = 0x03;   /* Rev 20 fcn.0x08CB @ 0x09C8 */
    DMACTL1 = 0x09;   /* Rev 20 fcn.0x08CB @ 0x09E6 */
    DMATSH1 = 0x80;   /* Rev 20 fcn.0x08CB @ 0x09DA */
    DMATSL1 = 0x03;   /* Rev 20 fcn.0x08CB @ 0x09D4 */

    /* -------- Panel / mux boot sequence --------
     *
     * Two publishes with a delay between them, exactly as stock. Rev 20
     * fcn.0x08CB @ 0x093E-0x0964, Rev 22 fcn.0x07EC @ 0x085F-0x0885:
     *
     *   093e  CLR A ; MOV 0x22,A   ; panel word = 0x00
     *   0941  SETB 0x1e            ; mono = 1
     *   0943  LCALL 0x0F0C         ; publish
     *   0946  ...                  ; delay loop on RAM[0x2E]:RAM[0x2F]
     *   095b  MOV 0x22,#0xFF
     *   095e  CLR 0x10 ; CLR 0x13  ; clear .0 and .3 -> 0xF6
     *   0962  CLR 0x1e             ; mono = 0
     *   0964  LCALL 0x0F0C         ; publish
     *
     * 0xF6 decodes as source pattern 0x06 on BOTH channels, which is mic —
     * matching the observed boot state. The 0x00-then-mono-set first publish
     * is a deliberate all-on flash, not a bug: it is the only place either
     * image writes 0x00 to this byte, and it is immediately followed by the
     * settle delay and the real value.
     *
     * An earlier defect list claimed stock boots this byte to 0x76 and that
     * mboxfw's 0x00 was illegal. Both parts were wrong. 0x76 comes from
     * Rev 20 0x0397 / Rev 22 0x039B, which is inside the SET_INTERFACE
     * alt-setting handler (fcn.0x0386 / fcn.0x038A) — a stream-teardown
     * state, not boot. The sequence below already matched stock and is
     * unchanged apart from the mono rename. */
    g_mux_state = 0x00;
    g_mono      = 1;        /* Rev 20 @ 0x0941 SETB 0x1E */
    mux_write(g_mux_state);

    short_delay();

    g_mux_state = (unsigned char)(0xFF & ~0x01 & ~0x08);   /* = 0xF6, mic/mic */
    g_mono      = 0;        /* Rev 20 @ 0x0962 CLR 0x1E */
    mux_write(g_mux_state);
}
