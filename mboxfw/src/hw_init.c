/*
 * Master hardware initialisation.
 * Ports Rev 20's fcn.0x08CB verbatim — see
 * firmware_stock/disasm/NOTES.md § "Master boot init".
 */

#include "regs.h"
#include "mux.h"
#include "codec.h"

extern __data unsigned char g_mux_state;  /* mirror of Rev 20 RAM[0x22] */


static void short_delay(void)
{
    /* Rev 20 spins RAM[0x2E] from 0 up to 0x0F00 (~4000 cycles).
     *
     * `i` MUST be volatile. Without it SDCC proves this function has no
     * observable effect and deletes the CALL SITE outright -- the body stays
     * in the image, unreferenced, so nothing looks wrong. Found 2026-07-29:
     * hw_init.c:204 emitted zero instructions, so the boot panel sequence
     * published 0x00 and 0xF6 back-to-back with no gap where stock delays
     * between them. Every delay helper in mboxfw had the same defect. */
    volatile unsigned int i;
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
    /* Both stock images set GLOBCTL bit 1 HERE -- before the codec-port block
     * and long before CPTEN -- and mboxfw did not, so mboxfw ran the whole
     * audio path with bit 1 clear where stock has it set.
     *
     * This write was missed by every scanner and then explicitly dismissed.
     * `rev20_diff_justifications.md` carried it as
     * "FALSE_POSITIVE -- no `mov a,#0x06; movx @dptr,a` to 0xffb1 exists ...
     * scanner artifact from a nearby `mov 0x06, a`". That search was correct
     * and its conclusion was not: DPTR is never loaded with 0xFFB1 here. It
     * arrives by INC DPTR from the MEMCFG write at 0xFFB0:
     *
     *   Rev 20 0x08D4  MOV DPTR,#0xffb0   ; MEMCFG
     *          0x08FB  INC DPTR           ; -> 0xFFB1 GLOBCTL
     *          0x08FC  MOV A,#0x06
     *          0x08FE  MOVX @DPTR,A
     *   Rev 22 0x081C  same, MOV A,#0x06 at 0x081D, store at 0x081F
     *
     * Byte-scanned both images: `a3 74 06 f0` occurs exactly once each, and
     * `90 ff b1 74 06 f0` occurs nowhere -- which is why looking for the
     * direct form found nothing. 27 instructions separate the DPTR load from
     * the INC, so a windowed scanner cannot connect them either. The
     * dismissal also cited rev20_flat.asm, which is the known-bad
     * disassembly. rev20_STARTUP_TRACE.md step 14 had it right all along.
     *
     * RMW rather than stock's outright `= 0x06`, per task #48: the boot ROM
     * leaves GLOBCTL = 0x04 (LPWR on), so |= 0x02 reaches the same 0x06
     * without blindly clearing bits the ROM may own.
     *
     * GLOBCTL bit 1 IS P3PUDIS. Datasheet §6.5.7.4, read 2026-07-31, gives the
     * full map: 7 MCUCLK, 6 XINTEN, 5 P1PUDIS, 4 VREN, 3 RESET, 2 LPWR,
     * 1 P3PUDIS, 0 CPTEN. P3PUDIS is "Pullup resistor disable. If set to 1,
     * disables on-chip pullup resistors on P3 GPIO pins." So stock's 0x06 is
     * LPWR | P3PUDIS -- run normally, and turn the P3 pull-ups off.
     *
     * The "UNKNOWN" that rev20_STARTUP_TRACE.md's open-items list carried since
     * it was written is retired. TI's ROM sources document only LPWR and MCUCLK,
     * which is why nobody found it there; it is in the datasheet's own register
     * section, which had been read for other bits but never for this one.
     *
     * ============================================================
     * DO NOT SET IT. MEASURED ON HARDWARE 2026-07-29: it makes the device
     * SILENT ON USB. The app never attaches, no VID/PID appears at all.
     *
     * Isolated by bisect between two images differing ONLY in this line, same
     * flasher, same procedure, same host, back to back:
     *
     *     build 0x0010  (GLOBCTL |= 0x02 present)  -> silent, never attaches
     *     build 0x0011  (this line removed)        -> attaches in 7 s
     *
     * WHY, corrected 2026-07-31. The explanation recorded here was "stock runs
     * hardware init BEFORE bringing USB up, mboxfw calls usb_init() first
     * (#47), so this write lands on a live USB engine and stops enumeration."
     * That was a guess with no mechanism behind it, and now that bit 1 has a
     * name there is a mechanism that fits exactly and involves no USB engine:
     *
     *   main.c:48  check_boot_dfu_button() spins 0x5000 times waiting for
     *              P3 & P3_BTN_CH1_MASK to read HIGH. If it never does, it
     *              calls eeprom_invalidate_signature() and then `for(;;){}`
     *              -- it never attaches, by design.
     *   main.c:255 "check_boot_dfu_button() runs after hw_init so P3 pull-ups
     *              are set before the button is sampled"
     *
     * The boot-button read depends on the internal P3 pull-ups. P3PUDIS turns
     * them off. With them off that pin never reads high, `held` stays 1, and
     * the firmware wipes its own EEPROM signature and spins -- which presents
     * as "silent on USB, never attaches" and matches the bisect exactly.
     *
     * NOTE FOR ANYONE RE-RUNNING THAT BISECT: build 0x0010 was not passively
     * silent. On this reading it invalidated the header signature, which is
     * the DFU trigger. See #169.
     *
     * So P3PUDIS is not inherently fatal -- stock sets it and reads its buttons
     * fine. Reinstating it means sampling the button before this write, or
     * fixing the read to not depend on internal pull-ups.
     *
     * The arithmetic was never the problem: telemetry block 8 byte 2 reads
     * GLOBCTL = 0x04 at boot-ROM handoff on this actual part, so |= 0x02 did
     * reach stock's 0x06 exactly as intended. The value was right; the timing
     * was fatal. This is why "both stock images do it" is a reason to
     * investigate and never on its own a reason to ship.
     *
     * Reinstating it requires moving the write before usb_init() AND a hardware
     * test -- not another re-read of the stock listings.
     * ============================================================ */

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
     * 0xAC IS NOT STOCK'S RUNNING VALUE. It is stock's BOOT value, and it
     * survives only until the host's first SET_CONFIGURATION. What was
     * written here before — "Rev 20 toggles CPTCNF3 at runtime by direction,
     * 0xAC when capture is requested (@0x035C) and 0xA8 when playback is
     * (@0x0367)" — is wrong in the addresses, wrong in the condition, wrong
     * that it is a per-direction toggle, and wrong that 0xAC is reachable at
     * runtime at all. See FINDING_147_cport_and_ep_buffer_divergences.md §2.
     *
     * What both images actually do. SET_CONFIGURATION posts work code 1
     * (Rev 20 @0x0293), and cmd1_apply_clock_mode stops both DMAs, drops
     * GLOBCTL CPTEN, and calls a helper that writes ONE value to BOTH
     * registers and raises CPTEN again:
     *
     *   Rev 20 codec_port_cfg3_commit @0x0FF4   (Rev 22 @0x0FE2)
     *     0FF4  MOVX @DPTR,A        ; caller's DPTR = 0xFFDE  CPTCNF3
     *     0FF5  MOV DPTR,#0xFFD5    ;                         CPTRXCNF3
     *     0FF8  MOVX @DPTR,A        ; ...the same value
     *     0FF9  GLOBCTL |= 0x01     ; CPTEN back on
     *
     * Its two call sites are Rev 20 @0x034A (A = 0xAC) and @0x0355
     * (A = 0xA8). The 0xAC site is gated on bit 0x0A = IRAM 0x21.2, which
     * the Keil init table zeroes (@0x0F9C) and which NO instruction in
     * either image ever sets — scanned for SETB/CLR/CPL/MOV-bit,C and for
     * every direct byte write to 0x21. That site is unreachable.
     *
     * So stock runs at 0xA8/0xA8 — BYOR CLEAR in both directions — the
     * moment any host configures the device. The datasheet's own I2S Mode 5
     * example, whose callouts match this part exactly (NTSL=2, TSL0L=11b,
     * TSLL=101b, BPTSL=100b, CSYNCL=1, CSYNCP=0, DDLY=1), also lists
     * BYOR = 0.
     *
     * Consequence for the endianness chain that used to live here: Linux
     * reports stock as SNDRV_PCM_FMTBIT_S24_3BE while stock is running
     * BYOR=0, so BYOR=0 cannot be the little-endian setting. Per §6.5.4.12,
     * BYOR=0 "preserves" the received order, which for MSB-first I2S is
     * big-endian. mboxfw declares S24_3LE, so on this reading it wants BYOR
     * SET, the opposite of what commit 7590af1 concluded. That is a wire-
     * format question with no playback measurement behind it either way, so
     * nothing is changed on it here — task #161.
     *
     * The value below is left at stock's boot value, unchanged, because
     * changing it is a behaviour change that costs a flash and belongs with
     * the rest of the #147 batch rather than in a documentation fix. */
    CPTCNF3   = 0xAC;   /* Rev 20 fcn.0x08CB @ 0x090B — stock BOOT value.
                         * Stock's RUNNING value is 0xA8 (Rev 20 @0x0355 via
                         * the helper at 0x0FF4). #161. */
    CPTCNF4   = 0x03;   /* 0xFFDD — stock writes 0x03 */
    CPTSTA    = 0x50;   /* 0xFFDC — stock writes 0x50 */
    CPTRXCNF2 = 0x25;   /* 0xFFD6 — stock writes 0x25 */
    /* Capture path, BYOR clear. Stock's BOOT init writes 0xAC here (Rev 20
     * fcn.0x08CB @ 0x0923, Rev 22 @0x0844), but its RUNNING value is 0xA8 —
     * the helper at 0x0FF4 writes the same byte to CPTCNF3 and CPTRXCNF3 on
     * every SET_CONFIGURATION, and the only reachable call site passes 0xA8.
     * So this line already agrees with stock's operating state, and the
     * "CPTRXCNF3 is the leading #147 suspect" reading in
     * FINDING_capture_8frame_artifact.md Addendum 4/5 is withdrawn. */
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
    MONO_ON();              /* Rev 20 @ 0x0941 SETB 0x1E */
    mux_write(g_mux_state);

    short_delay();

    g_mux_state = (unsigned char)(0xFF & ~0x01 & ~0x08);   /* = 0xF6, mic/mic */
    MONO_OFF();             /* Rev 20 @ 0x0962 CLR 0x1E */
    mux_write(g_mux_state);
}
