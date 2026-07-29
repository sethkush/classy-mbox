// MATCH: image=rev22 addr=0x015C len=27 func=std_get_configuration cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_in_buf_ptr_load(void);   /* rev22 0x0B37; rev20 ep0_ptr_set_in_buf 0x0B3E */

__bit __at (0x0E) f_configured;          /* IRAM 0x21.6 -- SET_CONFIGURATION saw a
                                          * non-zero bConfigurationValue */

/* GET_CONFIGURATION: one byte, 1 if configured and 0 if not.  Identical
 * policy to Rev 20 (cand/std_get_configuration.c, rev20 0x015D, 22 B); the
 * zero arm still uses CLR A rather than MOV A,#0, one byte instead of two.
 *
 * REV 20 -> REV 22, three changes and none of them behavioural:
 *
 *  1. The EP0 working pointer moved in IRAM, 0x1B:0x1C -> 0x1D:0x1E, so the
 *     two helpers moved with it: ep0_ptr_set_in_buf 0x0B3E -> 0x0B37 and
 *     dptr_from_ep0_ptr 0x0B17 -> ep0_load_dptr 0x0B25.  Both still point at
 *     the same EP0 IN buffer, 0xFA18 (read off rev22 0x0B37).
 *
 *  2. Rev 20 finished with `LJMP ep0_send_1byte` (rev20 0x0B45), a standalone
 *     three-operation helper.  Rev 22 has no such helper: it open-codes
 *     `MOV DPTR,#IEPDCNTX0 / MOV A,#1` here and falls through -- via a 3-byte
 *     LJMP -- into ep0_arm_in_and_done at 0x0247, which does the MOVX and the
 *     two stage flags.  Same three operations, split one instruction earlier.
 *
 *  3. That split created a NEW SHARED ENTRY POINT at 0x016F.  The class
 *     GET_CUR input-source arm inside usb_setup_handler reaches it with
 *     `LJMP 0x016F` from both of its branches (rev22 0x007A and 0x0083),
 *     where Rev 20's equivalent arm called ep0_send_1byte instead.  So the
 *     helper Rev 20 kept as a function became, in Rev 22, the tail of this
 *     function that another function jumps into.  0x016F has no Ghidra symbol;
 *     it is proposed as `rev22 ep0_arm_in_1byte 0x016F`.
 *
 * WRITTEN AS ASSEMBLY, for one reason: the function ENDS in an LJMP with DPTR
 * and A live into the callee, so there must be no RET.  SDCC emits the
 * epilogue RET after any trailing inline asm, which makes the function 28
 * bytes instead of 27.  The two store arms are otherwise ordinary C -- the
 * Rev 20 candidate writes exactly this shape as an if/else -- and the DPTR
 * load through a helper is the usual "call that leaves DPTR live" problem
 * (see decomp/README.md, the helper-call idiom).
 */
void std_get_configuration(void) __naked {
    __asm
        .globl _ep0_in_buf_ptr_load   ; rev22 0x0B37
        .globl _ep0_load_dptr         ; rev22 0x0B25
        .globl _ep0_arm_in_and_done   ; rev22 0x0247

        lcall _ep0_in_buf_ptr_load ; IRAM 0x1D:0x1E = 0xFA18, EP0 IN buffer
        jnb   0x0e,10$             ; f_configured (IRAM 0x21.6)
        lcall _ep0_load_dptr
        mov   a,#0x01              ; configured: bConfigurationValue 1
        movx  @dptr,a
        sjmp  20$
    10$:                           ; 0x016A
        lcall _ep0_load_dptr
        clr   a                    ; not configured: 0
        movx  @dptr,a
    20$:                           ; 0x016F -- ep0_arm_in_1byte, also entered
                                   ; by LJMP from rev22 0x007A and 0x0083
        mov   dptr,#0xff6b         ; IEPDCNT X0: byte count for the IN stage
        mov   a,#0x01
        ljmp  _ep0_arm_in_and_done ; stores A through DPTR, then f_stage_out=0,
                                   ;   f_stage_in=1, RET
    __endasm;
}
