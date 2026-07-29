// MATCH: image=rev22 addr=0x0336 len=84 func=cmd1_apply_clock_mode cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void dma0_disable(void);            /* rev22 0x0FF2, rev20 0x1001 */
extern void audio_hw_bringup(void);        /* rev22 0x09B6, rev20 0x080B */
extern void shiftreg_out16_p1(void);       /* rev22 0x0E56, rev20 0x0E62 */
extern void evt_arm_zlp_and_finish(void);  /* rev22 0x044E — shared event tail */

/* Interface-state bits. std_set_interface writes 0x08 and 0x09,
 * std_set_configuration writes 0x0A and 0x0E; this handler only reads them. */
__bit __at (0x08) f_iface1_alt;   /* IRAM 0x21.0 — interface 1 on a non-zero alt */
__bit __at (0x09) f_iface2_alt;   /* IRAM 0x21.1 — interface 2 on a non-zero alt */
__bit __at (0x0A) f_cfg_alt;      /* IRAM 0x21.2 */
__bit __at (0x0E) f_configured;   /* IRAM 0x21.6 */
/* IRAM 0x25.6, the "external chips are programmed" latch. Set in exactly one
 * place in each image — rev22 0x09BB, rev20 0x0810 — four bytes into
 * audio_hw_bringup / audio_path_reconfig_ext_chips, right after that routine
 * zeroes IRAM 0x25 and 0x23. Every call site is guarded by `if (!f_ext_ready)`
 * (rev22 0x0363, 0x0393, 0x0416, 0x04C8; rev20 0x035D, 0x038F, 0x0416,
 * 0x04C4 — four sites in each image, one-for-one), and
 * cmd1 is the only place it is cleared (rev22 0x0382, rev20 0x037B). */
__bit __at (0x2E) f_ext_ready;

/* Event 1 — dispatched from event_jump_table entry 0 (rev22 0x030C LJMP
 * 0x0336, rev20 0x0300 LJMP 0x032A). Queued whenever the clock source has to be
 * re-evaluated. Its job is to park the audio path: DMA off, codec port
 * disabled, then either re-arm the codec port for the current configuration or,
 * if the host has taken the device out of its configuration entirely, drop the
 * external-chip latch so the next start-up reprograms them.
 *
 * This handler never touches IRAM 0x08, the clock-mode number that
 * audio_clock_set_mode (rev22 0x070F, rev20 0x0728) writes and
 * setup_get_sample_freq reads back. It configures the PORT, not the rate; the
 * rate is set by cmd2, which calls audio_clock_set_mode(3).
 *
 * The teardown order matters and is worth stating in datasheet terms:
 * DMACTL0.7 (via dma0_disable, 0xFFE8) and DMACTL1.7 (0xFFEE) are the DMA
 * channel enables; both are cleared before GLOBCTL.0 (CPTEN, 0xFFB1). CPTEN
 * gates the codec port, and clearing it is also what makes CPTCNF3/CPTRXCNF3
 * writable — cport_cnf3_write_enable sets CPTEN back on as its last act, so
 * each of the two calls below is a complete disable/program/enable cycle.
 *
 * ===================================================================== *
 * REV 20 -> REV 22 DELTA: NO BEHAVIOURAL CHANGE. Two code-size changes
 * account for the whole 92 -> 84 byte difference.
 *
 * (1) THE CODEC-PORT HELPER ABSORBED ITS DPTR LOAD, saving 3 bytes x 2 sites.
 *
 *     rev20 codec_port_cfg3_commit @ 0x0FF4 starts with a bare MOVX @DPTR,A,
 *     so each caller had to set DPTR itself:
 *         0x034A  90 FF DE   MOV DPTR,#0xFFDE   (CPTCNF3)
 *         0x034D  74 AC      MOV A,#0xAC
 *         0x034F  12 0F F4   LCALL                             8 B
 *     rev22 cport_cnf3_write_enable @ 0x0FE2 begins MOV DPTR,#0xFFDE itself:
 *         0x0356  74 AC      MOV A,#0xAC
 *         0x0358  12 0F E2   LCALL                             5 B
 *     Same two values, 0xAC then 0xA8, in the same order, to the same two
 *     registers (the helper stores A to CPTCNF3 0xFFDE and CPTRXCNF3 0xFFD5,
 *     then ORs CPTEN into GLOBCTL). The helper grew by 3 and the two call
 *     sites shrank by 3 each: a net 3-byte win, taken out of cmd1.
 *
 * (2) THE EVENT EPILOGUE MOVED TO A SHARED COPY, saving 2 bytes.
 *
 *     rev20 had its own six-byte tail at 0x0380 (LCALL ep0_arm_zlp 0x0FEA,
 *     LJMP evt_dispatch_epilogue 0x0564) and reached it from the then-arm with
 *     a two-byte SJMP at 0x0363: 8 bytes of exit code.
 *     rev22 branches to the shared tail at 0x044E instead, and because that is
 *     out of SJMP range from here, both exits are three-byte LJMPs (0x0369 and
 *     0x0387): 6 bytes. See cand/rev22_evt_arm_zlp_and_finish.c.
 *
 *     6 (helper) + 2 (epilogue) = 8, and 92 - 8 = 84. Every other instruction
 *     in the function is the same opcode with the same operand; the only other
 *     changes are the two remaining LCALL targets (0x080B -> 0x09B6 for the
 *     bring-up, 0x0E62 -> 0x0E56 for the shift register) and branch
 *     displacements shifted by the removals.
 *
 * So Rev 22 did not change what event 1 does. Same bits tested in the same
 * order, same CPTCNF3 values, same latch clear, same shift-register push.
 * ===================================================================== */
/* SHAPE NOTE — why this is `if (...) { ... goto done; }` and not `if/else`.
 *
 * Rev 20's counterpart is a plain if/else and matched as one, because its two
 * arms converged on a six-byte tail that lived inside the function: the
 * then-arm reached it with a two-byte SJMP, which is exactly what SDCC emits
 * to jump over an else block. Rev 22's tail is OUTSIDE the function, at
 * 0x044E, so Keil ended the then-arm with a three-byte LJMP straight there
 * (0x0369) rather than jumping to the jump; and the `if (!f_ext_ready)` test at
 * 0x0363 branches past the whole else block to the second LJMP at 0x0387.
 *
 * Written as if/else, SDCC emits `SJMP 00109$` at that point — 2 bytes where
 * stock has 3, and the function comes out 83 bytes instead of 84 with every
 * later branch displacement off by one (measured: 33 differing bytes). SDCC
 * has no reason to duplicate a tail jump, and no peephole can create the
 * duplicate without encoding one specific instruction adjacency.
 *
 * Dropping the `else` and terminating the then-arm with an explicit LJMP
 * reproduces the control flow exactly: with no else block there is no
 * jump-over to emit, the `goto done` becomes stock's `JB 0x2e,0x0387`, and the
 * guard's failure branches land on the first instruction after the if-body,
 * which is where the else-arm code now sits. */
void cmd1_apply_clock_mode(void) __naked {
    dma0_disable();          /* DMACTL0 &= 0x7F */
    DMACTL1 &= 0x7F;
    GLOBCTL &= 0xFE;         /* CPTEN off */

    if ((f_cfg_alt || f_configured) && !f_iface1_alt && !f_iface2_alt) {
        /* Configured, but both streaming interfaces parked on alt 0. Re-arm
         * the codec port in its idle framing. cport_cnf3_write_enable takes the
         * byte in A — Keil's register-parameter convention, which SDCC has no
         * way to express — so these two calls are assembly. */
        if (f_cfg_alt) {
            __asm
                .globl _cport_cnf3_write_enable
                .globl _evt_arm_zlp_and_finish
                mov   a,#0xac
                lcall _cport_cnf3_write_enable
            __endasm;
        }
        if (f_configured) {
            __asm
                mov   a,#0xa8
                lcall _cport_cnf3_write_enable
            __endasm;
        }
        if (f_ext_ready) goto done;     /* 0x0363 JB 0x2E,0x0387 */
        audio_hw_bringup();
        __asm
            ljmp  _evt_arm_zlp_and_finish   ; 0x0369 -> 0x044E
        __endasm;
    }
    {
        /* Not configured, or a streaming interface is live. The only work is
         * the unconfigured case: forget that the external chips were set up and
         * push the front-panel shift registers out once more.
         *
         * Stock materialises `f_cfg_alt || f_configured` as an INT in R6:R7
         * here and compares it against 1, where three instructions earlier
         * (0x0347) the same expression was short-circuited with bare bit tests.
         * That is Keil's int-valued `||` feeding an int comparison, and SDCC
         * will not reproduce it: its range analysis proves the value is 0 or 1
         * and narrows the whole thing to a single byte in R7, five bytes
         * shorter. No rewrite of the C avoids that, so the block is written as
         * assembly. rev22 0x036C..0x0386 and rev20 0x0365..0x037F are identical
         * apart from the LCALL operand. */
        __asm
            .globl _shiftreg_out16_p1
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
            lcall _shiftreg_out16_p1
        00204$:
        __endasm;
    }

    /* Common exit: the shared tail at 0x044E arms a zero-length EP0 reply and
     * clears g_event. */
done:
    __asm
        ljmp  _evt_arm_zlp_and_finish   ; 0x0387 -> 0x044E
    __endasm;
}
