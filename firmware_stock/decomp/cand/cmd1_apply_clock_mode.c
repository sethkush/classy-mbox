// MATCH: image=rev20 addr=0x032A len=92 func=cmd1_apply_clock_mode cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void dma0_disable(void);
extern void audio_path_reconfig_ext_chips(void);
extern void shiftreg16_commit(void);
extern void ep0_arm_zlp(void);
extern void evt_dispatch_epilogue(void);

/* Interface-state bits. std_set_interface (0x029F) writes 0x08 and 0x09,
 * std_set_configuration writes 0x0A and 0x0E; this handler only reads them. */
__bit __at (0x08) f_iface1_alt;   /* IRAM 0x21.0 — interface 1 on a non-zero alt */
__bit __at (0x09) f_iface2_alt;   /* IRAM 0x21.1 — interface 2 on a non-zero alt */
__bit __at (0x0A) f_cfg_alt;      /* IRAM 0x21.2 */
__bit __at (0x0E) f_configured;   /* IRAM 0x21.6 */
/* IRAM 0x25.6. Set in exactly one place in each image -- rev20 0x0810, rev22
 * 0x09BB -- four bytes into audio_path_reconfig_ext_chips (rev22 Ghidra name:
 * audio_hw_bringup), right after that routine zeroes IRAM 0x25 and 0x23. So it
 * is that routine's "already done" latch, and every call site is guarded by
 * `if (!f_ext_ready)`: rev20 0x035D, 0x038F, 0x0416, 0x04C4; rev22 0x0363,
 * 0x0393. cmd1 is the only place it is cleared (rev20 0x037B, rev22 0x0382).
 *
 * The two instructions at rev20 0x0810-0x0812 are the bit/byte trap in one
 * line: `SETB 0x2e` sets bit 0x2E = IRAM 0x25.6, and `MOV 0x2e,#0xff`
 * immediately after writes IRAM *byte* 0x2E, the DJNZ delay counter. */
__bit __at (0x2E) f_ext_ready;

/* Event 1 — dispatched from event_jump_table entry 0 (rev20 0x0300 LJMP 0x032A,
 * rev22 0x030C LJMP 0x0336). Queued whenever the clock source has to be
 * re-evaluated, and its job is to park the audio path: DMA off, codec port
 * disabled, then either re-arm the codec port for the current configuration or,
 * if the host has taken the device out of its configuration entirely, drop the
 * external-chip latch so the next start-up reprograms them.
 *
 * Note this handler never touches IRAM 0x08, the clock-mode number that
 * audio_clock_mode_apply (0x0728 / rev22 0x070F) writes and
 * setup_get_sample_freq reads back. It configures the *port*, not the rate;
 * the rate is set by cmd2 below, which calls audio_clock_mode_apply(3).
 *
 * The teardown order matters and is worth stating in datasheet terms:
 *   DMACTL0.7 (via dma0_disable, 0xFFE8) and DMACTL1.7 (0xFFEE) are the DMA
 *   channel enables; both are cleared before GLOBCTL.0 (CPTEN, 0xFFB1) is
 *   cleared. CPTEN gates the codec port, and clearing it is also what makes
 *   CPTCNF3/CPTRXCNF3 writable -- codec_port_cfg3_commit (0x0FF4) sets CPTEN
 *   back on as its last act, so each of the two writes below is a complete
 *   disable/program/enable cycle. */
void cmd1_apply_clock_mode(void) {
    dma0_disable();          /* DMACTL0 &= 0x7F */
    DMACTL1 &= 0x7F;
    GLOBCTL &= 0xFE;         /* CPTEN off */

    if ((f_cfg_alt || f_configured) && !f_iface1_alt && !f_iface2_alt) {
        /* Configured, but both streaming interfaces parked on alt 0.
         * Re-arm the codec port in its idle framing. codec_port_cfg3_commit
         * takes the byte in A and the first destination in DPTR: it stores A to
         * *DPTR (CPTCNF3, 0xFFDE), stores the same byte to CPTRXCNF3 (0xFFD5),
         * then ORs CPTEN back into GLOBCTL. Keil's register-parameter
         * convention, so these two calls are assembly.
         *
         * Rev 22 folded the DPTR load into the callee (0x0FE2 there), which is
         * why rev22 0x0356/0x035E are `MOV A,#imm; LCALL` with no MOV DPTR --
         * same two values, 0xAC and 0xA8, in the same order. */
        if (f_cfg_alt) {
            __asm
                .globl _codec_port_cfg3_commit
                mov   dptr,#0xffde      ; CPTCNF3
                mov   a,#0xac
                lcall _codec_port_cfg3_commit
            __endasm;
        }
        if (f_configured) {
            __asm
                mov   dptr,#0xffde      ; CPTCNF3
                mov   a,#0xa8
                lcall _codec_port_cfg3_commit
            __endasm;
        }
        if (!f_ext_ready) audio_path_reconfig_ext_chips();
    } else {
        /* Not configured, or a streaming interface is live. The only work is
         * the unconfigured case: forget that the external chips were set up and
         * push the front-panel shift registers out once more.
         *
         * Stock materialises `f_cfg_alt || f_configured` as an *int* in R6:R7
         * here and compares it against 1, where three instructions earlier
         * (0x033B) the same expression was short-circuited with bare bit tests.
         * That is Keil's int-valued `||` feeding an int comparison, and SDCC
         * will not reproduce it: its range analysis proves the value is 0 or 1
         * and narrows the whole thing to a single byte in R7, five bytes
         * shorter. No rewrite of the C avoids that, so the block is written as
         * assembly. It is exactly rev20 0x0365..0x037F / rev22 0x036C..0x0386,
         * which are byte-identical apart from the LCALL operand. */
        __asm
            .globl _shiftreg16_commit
            jb    _f_cfg_alt,00201$
            jnb   _f_configured,00202$
        00201$:
            mov   r6,#0x00              ; (unsigned int)1
            mov   r7,#0x01
            sjmp  00203$
        00202$:
            mov   r6,#0x00              ; (unsigned int)0
            mov   r7,#0x00
        00203$:
            mov   a,r7                  ; 16-bit compare against 1
            xrl   a,#0x01
            orl   a,r6
            jz    00204$
            clr   _f_ext_ready
            lcall _shiftreg16_commit
        00204$:
        __endasm;
    }

    /* Common epilogue: arm a zero-length reply on EP0 and clear the pending
     * event. The final call is a tail call, so it encodes as LJMP 0x0564.
     * Rev 22 merged both of these into a shared tail at 0x044E and reaches it
     * with LJMP/SJMP from the same two points. */
    ep0_arm_zlp();
    evt_dispatch_epilogue();
}
