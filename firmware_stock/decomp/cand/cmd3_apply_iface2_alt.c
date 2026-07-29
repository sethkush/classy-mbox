// MATCH: image=rev20 addr=0x03FD len=87 func=cmd3_apply_iface2_alt cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 3: make the playback stream follow SET_INTERFACE on interface 2.
 *
 * std_set_interface (0x029F) records the requested alternate setting as the
 * single bit f_iface2_alt and queues event 3; this handler is where that bit
 * turns into hardware. Interface 2 is the OUT/playback stream: OEPCNF2 at
 * 0xFF98 and DMA channel 0 at 0xFFE8, per the endpoint assignment
 * usb_ep_dma_init makes at 0x0970 (DMACTL0 = 0x02 = EPDIR 0, EPNUM 2).
 * Event 2 (0x0386) is the same handler for interface 1, the IN/capture
 * stream, and touches IEPCNF1 at 0xFF60 and DMACTL1 at 0xFFEE instead.
 *
 *   alt != 0 : bring up the external chips if they have never been brought up,
 *              clear f_force, re-enable OEPCNF2 (0xC5 = enable | ISO |
 *              BPS 5), apply clock mode 3 (48 kHz), and set DMAEN in DMACTL0.
 *   alt == 0 : clear DMAEN (dma0_disable at 0x1001).
 *   neither  : if the device is not configured at all, do nothing.
 *
 * Either way it finishes with ep0_arm_zlp (0x0FEA), which is the status stage
 * of the SET_INTERFACE that started this.
 *
 * WHY THIS IS ASSEMBLY. The guard `(f_configured || f_cfg_alt)` is compiled by
 * Keil WITHOUT short-circuiting: each bit is materialised into a whole
 * register with a jump-over pair (`JNB b,L / MOV R7,#1 / SJMP / L: MOV R7,#0`),
 * the two are combined with `MOV A,R6 / ORL A,R7`, and the result is tested
 * with `JZ`. That is seven bytes per bit where a short-circuit `JB`/`JNB` pair
 * would have been three -- and stock does use the short-circuit form for the
 * same two bits in std_set_interface at 0x02B0. So this is not a fixed rule of
 * Keil's, it is what Keil did here, and reproducing it would need a peephole
 * rule encoding one specific instruction adjacency. Per the rule of thumb in
 * decomp/README.md ("if a fix requires a rule that encodes one specific
 * instruction adjacency, write the function as annotated __naked assembly"),
 * it is written out. The whole materialise-and-OR sequence then appears twice,
 * because the else-arm re-evaluates the same guard from scratch.
 *
 * Two further reasons it could not have been C anyway: the clock mode is
 * Keil's R7 register parameter, and the exit is the dispatcher switch's
 * `break` (an LJMP, not a RET).
 *
 * BIT ADDRESSES USED HERE (bit B = IRAM 0x20 + (B >> 3), bit B & 7):
 *   0x08 f_iface1_alt (IRAM 0x21.0)   0x09 f_iface2_alt (IRAM 0x21.1)
 *   0x0A f_cfg_alt    (IRAM 0x21.2)   0x0E f_configured (IRAM 0x21.6)
 *   0x2D f_force      (IRAM 0x25.5)   0x2E ext-chips-done (IRAM 0x25.6)
 * Bit 0x2E is the run-once guard audio_path_reconfig_ext_chips sets at 0x0810
 * -- and note that the same function immediately uses BYTE 0x2E as a DJNZ
 * delay counter at 0x0812. Different storage, identical spelling.
 *
 * REV 22 CROSS-CHECK: cmd3_apply_iface2_alt sits at the same address, rev22
 * 0x03FD, and the first 81 bytes are instruction-for-instruction the same --
 * same bit addresses 0x0E/0x0A/0x09/0x2E/0x2D, same doubled materialise-and-OR
 * guard, same OEPCNF2 = 0xC5, same DMACTL0 |= 0x80 -- with only the call
 * targets moved (0x09B6 for the ext-chip reconfig, 0x070F for the clock apply,
 * 0x0FF2 for the DMA disable). The tail differs: Rev 22 inlines
 * IEPDCNTX0 = 0 / OEPDCNTX0 = 0 at 0x044E instead of calling ep0_arm_zlp, and
 * that inlined tail is shared with three other handlers (entered from rev22
 * 0x0369, 0x0387 and 0x03FB), so Ghidra's extent for the Rev 22 function is 93
 * bytes against 87 here. */
void cmd3_apply_iface2_alt(void) __naked {
    __asm
        .globl _audio_path_reconfig_ext_chips
        .globl _audio_clock_mode_apply
        .globl _dma0_disable
        .globl _ep0_arm_zlp
        .globl _evt_dispatch_epilogue

        ; ---- (f_configured || f_cfg_alt) && f_iface2_alt --------------------
        jnb   0x0e,00001$          ; f_configured
        mov   r7,#0x01
        sjmp  00002$
    00001$:
        mov   r7,#0x00
    00002$:
        jnb   0x0a,00003$          ; f_cfg_alt
        mov   r6,#0x01
        sjmp  00004$
    00003$:
        mov   r6,#0x00
    00004$:
        mov   a,r6
        orl   a,r7                 ; neither bit set -> not configured at all
        jz    00006$
        jnb   0x09,00006$          ; f_iface2_alt clear -> the alt-0 arm

        ; ---- alt != 0: arm the playback path ---------------------------------
        jb    0x2e,00005$          ; external chips already programmed?
        lcall _audio_path_reconfig_ext_chips
    00005$:
        clr   0x2d                 ; f_force = 0
        mov   dptr,#0xff98         ; OEPCNF2 (EP2 OUT, playback)
        mov   a,#0xc5              ;   enable | ISO | BPS 5 = 6 bytes/sample
        movx  @dptr,a
        mov   r7,#0x03             ; clock mode 3 = 48000 Hz
        lcall _audio_clock_mode_apply
        mov   dptr,#0xffe8         ; DMACTL0
        movx  a,@dptr
        orl   a,#0x80              ;   DMAEN
        movx  @dptr,a
        sjmp  00011$

        ; ---- the guard again, this time for the alt-0 arm --------------------
    00006$:
        jnb   0x0e,00007$          ; f_configured
        mov   r7,#0x01
        sjmp  00008$
    00007$:
        mov   r7,#0x00
    00008$:
        jnb   0x0a,00009$          ; f_cfg_alt
        mov   r6,#0x01
        sjmp  00010$
    00009$:
        mov   r6,#0x00
    00010$:
        mov   a,r6
        orl   a,r7
        jz    00011$               ; not configured -> leave the DMA alone
        jb    0x09,00011$          ; f_iface2_alt set -> handled above
        lcall _dma0_disable        ; DMACTL0 &= 0x7F

    00011$:
        lcall _ep0_arm_zlp         ; status stage for the SET_INTERFACE
        ljmp  _evt_dispatch_epilogue
    __endasm;
}
