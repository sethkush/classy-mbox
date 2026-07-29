// MATCH: image=rev22 addr=0x03FD len=93 func=cmd3_apply_iface2_alt cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Event 3, Rev 22 — make the playback stream follow SET_INTERFACE on
 * interface 2.
 *
 * std_set_interface records the requested alternate setting as the single bit
 * f_iface2_alt (IRAM 0x21.1) and queues event 3; this handler is where that bit
 * turns into hardware. Interface 2 is the OUT/playback stream: OEPCNF2 at
 * 0xFF98 and DMA channel 0 at 0xFFE8, per the endpoint assignment
 * usb_ep_dma_init makes (rev22 0x0901: DMACTL0 = 0x02 = EPDIR 0, EPNUM 2).
 * Event 2 (rev22 0x038A) is the same handler for interface 1, the IN/capture
 * stream, and touches IEPCNF1 at 0xFF60 and DMACTL1 at 0xFFEE instead.
 *
 *   alt != 0 : bring the external chips up if they never have been, clear
 *              f_force, re-enable OEPCNF2 (0xC5), apply clock mode 3 (48 kHz),
 *              and set DMAEN in DMACTL0.
 *   alt == 0 : clear DMAEN (dma0_disable, rev22 0x0FF2).
 *   neither  : if the device is not configured at all, do nothing.
 *
 * Either way it finishes in the shared event tail at 0x044E, which arms a
 * zero-length EP0 reply — the status stage of the SET_INTERFACE that started
 * this — and clears g_event.
 *
 * ===================================================================== *
 * REV 20 -> REV 22 DELTA: NO BEHAVIOURAL CHANGE, AND NO RELOCATION EITHER.
 *
 * The function is at the SAME address in both images, rev20 0x03FD and rev22
 * 0x03FD, and the first 81 bytes (0x03FD..0x044D) are identical byte for byte
 * apart from three LCALL operands:
 *
 *     callee                     rev20    rev22
 *     ext-chip bring-up          0x080B   0x09B6   (site 0x0419)
 *     clock-mode apply           0x0728   0x070F   (site 0x0426)
 *     dma0_disable               0x1001   0x0FF2   (site 0x044B)
 *
 * Same bit addresses 0x0E/0x0A/0x09/0x2E/0x2D, same doubled materialise-and-OR
 * guard, same OEPCNF2 = 0xC5, same R7 = 3, same DMACTL0 |= 0x80, same branch
 * displacements. Verified by diffing rev20_firmware_code.bin[0x03FD:0x044E]
 * against rev22_firmware_code.bin[0x03FD:0x044E]: exactly five bytes differ,
 * and every one of them is an LCALL operand byte — 0x041A/0x041B, 0x0428 and
 * 0x044C/0x044D. (Five, not six: 0x0728 and 0x070F share the high byte 0x07,
 * so that call's operand differs in one byte only.)
 *
 * The tail is where the two diverge, and it is a code-size change only:
 *
 *     rev20 0x044E  12 0F EA   LCALL ep0_arm_zlp
 *           0x0451  02 05 64   LJMP  evt_dispatch_epilogue        (6 B, 87 total)
 *     rev22 0x044E  90 FF 6B / E4 / F0 / 90 FF AB / F0   inlined ZLP arming
 *           0x0457  02 05 63   LJMP  evt_dispatch_epilogue       (12 B, 93 total)
 *
 * Rev 22 has no standalone ep0_arm_zlp function; it inlined the body here and
 * made this one copy the shared exit for cmd1 (LJMP from 0x0369 and 0x0387) and
 * cmd2 (SJMP from 0x03FB) as well as for this function's own three exits
 * (0x0430, 0x0446, 0x0448). That is why Ghidra's extent for cmd3 GREW by six
 * bytes while cmd1 and cmd2 shrank by eight and four: the same twelve bytes now
 * belong to cmd3 instead of being copied into all three.
 *
 * So the Rev 22 fix is not in the interface-2 SET_INTERFACE handler either.
 * ===================================================================== *
 *
 * WHY THIS IS ASSEMBLY. The guard `(f_configured || f_cfg_alt)` is compiled by
 * Keil WITHOUT short-circuiting: each bit is materialised into a whole register
 * with a jump-over pair (`JNB b,L / MOV R7,#1 / SJMP / L: MOV R7,#0`), the two
 * are combined with `MOV A,R6 / ORL A,R7`, and the result tested with `JZ`.
 * That is seven bytes per bit where a short-circuit `JB`/`JNB` pair would have
 * been three — and stock does use the short-circuit form for the same two bits
 * three functions earlier, at rev22 0x038A/0x038D in cmd2. So it is not a fixed
 * rule of Keil's, it is what Keil did here, and reproducing it would need a
 * peephole rule encoding one specific instruction adjacency. Per the rule of
 * thumb in decomp/README.md, it is written out instead. The whole
 * materialise-and-OR sequence appears twice, because the else-arm re-evaluates
 * the same guard from scratch.
 *
 * Two further reasons it could not have been C anyway: the clock mode is Keil's
 * R7 register parameter, and the exit is the dispatcher switch's `break` (an
 * LJMP, not a RET).
 *
 * BIT ADDRESSES USED HERE (bit B = IRAM 0x20 + (B >> 3), bit B & 7):
 *   0x09 f_iface2_alt (IRAM 0x21.1)   0x0A f_cfg_alt    (IRAM 0x21.2)
 *   0x0E f_configured (IRAM 0x21.6)   0x2D f_force      (IRAM 0x25.5)
 *   0x2E ext-chips-done (IRAM 0x25.6)
 * Bit 0x2E is the run-once guard audio_hw_bringup sets at rev22 0x09BB — and
 * note that the same function immediately uses BYTE 0x2E as a DJNZ delay
 * counter. Different storage, identical spelling.
 *
 * THIS CANDIDATE PLACES THE SHARED TAIL. Bytes 0x044E..0x0459 are physically
 * part of this function's extent, so they are emitted here. The same twelve
 * bytes are proved standalone, and given a linkable name for cmd1/cmd2 to jump
 * to, by cand/rev22_evt_arm_zlp_and_finish.c (entry=1). */
void cmd3_apply_iface2_alt(void) __naked {
    __asm
        .globl _audio_hw_bringup          ; rev22 0x09B6
        .globl _audio_clock_set_mode      ; rev22 0x070F
        .globl _dma0_disable              ; rev22 0x0FF2
        .globl _evt_dispatch_epilogue     ; rev22 0x0563 (merged tail; see
                                          ; proposed/cmd123.symbols)

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
        lcall _audio_hw_bringup
    00005$:
        clr   0x2d                 ; f_force = 0
        mov   dptr,#0xff98         ; OEPCNF2 (EP2 OUT, playback)
        mov   a,#0xc5              ;   endpoint enable | ISO | buffer config
        movx  @dptr,a
        mov   r7,#0x03             ; clock mode 3 = 48000 Hz
        lcall _audio_clock_set_mode
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

        ; ---- 0x044E: the shared event tail, inlined here in Rev 22 -----------
        ; Reached from six sites: cmd1 0x0369 and 0x0387 (LJMP), cmd2 0x03FB
        ; (SJMP), and this function's 0x0430, 0x0446, 0x0448.
    00011$:
        mov   dptr,#0xff6b         ; IEPDCNTX0 — EP0 IN X-buffer byte count
        clr   a
        movx  @dptr,a              ;   0 = zero-length packet (status stage)
        mov   dptr,#0xffab         ; OEPDCNTX0 — EP0 OUT X-buffer byte count
        movx  @dptr,a              ;   0 = ready for the next data phase
        ljmp  _evt_dispatch_epilogue   ; g_event = 0
    __endasm;
}
