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
    /* P3 latch all-high. This is what makes the pins INPUTS -- it does not
     * decide what they read. With P3PUDIS set below, a latched-high pin is
     * released entirely and the board's own network drives it; the buttons
     * then rest LOW and a press pulls them HIGH. Reading this write as "button
     * inputs, pulled high" is what produced the active-low mistake. See
     * FINDING_buttons_are_active_high.md. */
    P3   = 0xFF;   /* Rev 20 fcn.0x08CB @ 0x08E9 `mov 0xb0,#0xff`,
                    * Rev 22 fcn.0x07EC @ 0x080A */

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
    /* GLOBCTL bit 1 = P3PUDIS, "Pullup resistor disable. If set to 1, disables
     * on-chip pullup resistors on P3 GPIO pins" (TAS1020B §6.5.7.4; full map:
     * 7 MCUCLK, 6 XINTEN, 5 P1PUDIS, 4 VREN, 3 RESET, 2 LPWR, 1 P3PUDIS,
     * 0 CPTEN).
     *
     * Both stock images set it here -- before the codec-port block and long
     * before CPTEN. The write is easy to miss: DPTR is never loaded with
     * 0xFFB1, it arrives by INC DPTR from the MEMCFG write at 0xFFB0, 27
     * instructions earlier.
     *
     *   Rev 20  0x08D4 MOV DPTR,#0xFFB0 -> 0x08FB INC DPTR, 0x08FC MOV A,#0x06,
     *                  0x08FE MOVX @DPTR,A
     *   Rev 22  0x07F5 MOV DPTR,#0xFFB0 -> 0x081C INC DPTR, 0x081D MOV A,#0x06,
     *                  0x081F MOVX @DPTR,A
     *
     * Byte-scanned both images: `a3 74 06 f0` occurs exactly once each and
     * `90 ff b1 74 06 f0` occurs nowhere, which is why searching for the direct
     * form found nothing and the write was filed as a scanner artifact. See
     * FINDING_globctl_bit1_missed.md.
     *
     * ---------------------------------------------------------------------
     * THIS BIT IS REQUIRED FOR THE FRONT-PANEL BUTTONS. It was removed on
     * 2026-07-29 on a bisect that was real and whose interpretation was
     * inverted; restored 2026-08-03. See
     * FINDING_buttons_are_active_high.md.
     *
     * The buttons are ACTIVE HIGH: the board pulls P3.3/P3.4/P3.5 low at rest
     * and a press drives them high. Proof from the image, not from a guess:
     * p3_button_scan fires on prev==0 && cur==1, and Keil's ?C_INITSEG table
     * zeroes the previous-sample shadow at IRAM 0x20 (record `01 20 00`), so if
     * those pins idled HIGH all three handlers would fire on the first scan of
     * every boot -- both channels would step MIC->LINE and mono would toggle
     * before the user touched anything. The hardware boots to MIC and stays
     * there. So they idle low.
     *
     * With P3PUDIS clear the internal pull-ups beat the board's pull-downs and
     * the pins sit at 1 permanently. Measured on Mbox A running build 0x0011:
     * P3 rests at 0xFA and bit 3 stays 1 across three live reads with the
     * button held down. Stock, on the same unit minutes later, cycles
     * mic -> line -> inst. That is the whole of why mboxfw's buttons are dead.
     *
     * WHY THE BISECT SAID OTHERWISE. Build 0x0010 (this line present) never
     * attached; 0x0011 (removed) attached in 7 s. check_boot_dfu_button() reads
     * `held = (pin LOW)`, the active-low premise. With P3PUDIS set the idle pin
     * reads LOW, so `held` was true with nothing pressed: the firmware
     * invalidated its own EEPROM header and spun forever without attaching.
     * "Silent on USB" was that, not a USB-engine effect -- and the build was
     * actively rewriting the header on the way down, not passively silent.
     * With P3PUDIS clear the same read can never fire, which is why holding the
     * button at boot has never worked in any position that call has occupied.
     * #169.
     *
     * ORDERING CONSTRAINT: check_boot_dfu_button() samples P3 and must run
     * AFTER this write, or it reads through the pull-ups and is meaningless.
     * main.c calls it immediately after hw_init() for that reason. Moving
     * either one without the other reintroduces the 0x0010 failure.
     * ---------------------------------------------------------------------
     *
     * RMW rather than stock's outright `= 0x06`, per task #48: the boot ROM
     * leaves GLOBCTL = 0x04 (LPWR on -- measured, telemetry block 8 byte 2), so
     * |= 0x02 reaches the same 0x06 without clearing bits the ROM may own. */
    GLOBCTL |= 0x02;    /* Rev 20 fcn.0x08CB @ 0x08FE, Rev 22 fcn.0x07EC @ 0x081F */


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
     * SET.
     *
     * #161 SETTLED BY MEASUREMENT 2026-08-03, build 0x001B. The value below
     * (0xAC, BYOR SET) is CORRECT for playback. 1 kHz through the out2->src2
     * analog loop, swept over 36 dB of input range:
     *
     *     in  -9 dBFS -> out -29.20    A/rms 1.41421   h2 -101.7 dB
     *     in -21 dBFS -> out -41.20    A/rms 1.41411   h2  -99.5 dB
     *     in -33 dBFS -> out -53.20    A/rms 1.41252   h2  -89.8 dB
     *     in -45 dBFS -> out -65.20    A/rms 1.38827   h2  -74.0 dB
     *
     * A constant 20.20 dB loss at every level, amplitude/rms = sqrt(2) to
     * five digits, harmonics ~100 dB down. That is a spectrally pure sine.
     * A byte swap maps LSBs into MSBs, so a quiet input would return near
     * full scale and noisy instead of tracking down linearly; these track.
     * The self-loop suffices to constrain PLAYBACK because capture is
     * independently settled (LE, FINDING_147_capture_works_analog_path_does_not.md)
     * and a playback swap corrupts the waveform in the ANALOG domain, where
     * no capture-side swap can cancel it.
     *
     * WHAT THIS DOES NOT SETTLE, and it is odd: mboxfw runs BYOR ASYMMETRIC
     * — 0xAC (set) here for playback, 0xA8 (clear) at CPTRXCNF3 for capture
     * — and BOTH directions measure correct for S24_3LE. The reading above
     * predicts the same polarity in both directions, so it does not explain
     * the asymmetry. Measurement outranks the reading, so the values stand,
     * but the datasheet story is incomplete. Note also that mboxfw never
     * runs stock's codec_port_cfg3_commit helper, which rewrites BOTH
     * registers with ONE value on every SET_CONFIGURATION; stock is
     * therefore always symmetric at 0xA8/0xA8 and mboxfw never is.
     *
     * These measurements prove the shipped setting is CORRECT; they do not
     * prove it is UNIQUELY correct, since 0xA8 playback was never flashed
     * and tried. That falsification costs one flash cycle if the asymmetry
     * ever needs explaining rather than just working. */
    /* #161 EXPERIMENT, build 0x001C: 0xAC -> 0xA8, i.e. BYOR CLEARED on the
     * playback path, making mboxfw symmetric with its own capture path
     * (CPTRXCNF3 = 0xA8 below) and with stock's running state.
     *
     * Why this is an experiment and not a cleanup. mboxfw was running
     * 0xAC/0xA8 — asymmetric — and stock NEVER is: its helper writes one
     * accumulator to BOTH registers (Rev 20 codec_port_cfg3_commit @0x0FF4,
     * Rev 22 cport_cnf3_write_enable @0x0FE2 — 0xFFDE then 0xFFD5), so boot
     * is 0xAC/0xAC and running is 0xA8/0xA8. Our asymmetry was the residue of
     * updating the capture line to stock's running value during #147 while
     * leaving this one at stock's boot value pending a measurement.
     *
     * The 2026-08-03 sweep proved 0xAC here is CORRECT, not that it is
     * UNIQUELY correct — only one value was ever tried. Two outcomes:
     *   audio still clean -> BYOR on the transmit path is inert in this
     *       configuration; keep 0xA8 and the divergence from stock is gone.
     *   audio breaks      -> BYOR-TX is real, the asymmetry is a physical
     *       fact, and 0xAC goes back with a measurement behind it.
     *
     * Reverting is one byte and one flash cycle. */
    CPTCNF3   = 0xA8;   /* Rev 20 fcn.0x08CB @ 0x090B (boot writes 0xAC here);
                         * 0xA8 is stock's RUNNING value — Rev 20 @0x0358 via
                         * fcn.0x0FF4, Rev 22 @0x035E via fcn.0x0FE2. #161. */
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
