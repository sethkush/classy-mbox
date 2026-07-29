// MATCH: image=rev22 addr=0x0177 len=118 func=std_get_descriptor cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_clamp_len_to_wlength(void);  /* rev22 0x0D9E, rev20 0x0D6B */
extern void ep0_in_stage_and_go(void);       /* rev22 0x0B63; rev20 calls the
                                              * equivalent ep0_in_start_transfer
                                              * at 0x0B77 */
extern void ep0_stall_both(void);            /* rev22 0x02EF, rev20 0x1009 */

/* CODE source pointer: 0x19 high, 0x1A low — the same IRAM pair as Rev 20
 * (cand/std_get_descriptor.c).  Transfer length low at 0x09, high at 0x0B,
 * also unchanged.  These are BYTE addresses; bit addresses 0x09/0x0B are
 * unrelated flags in IRAM 0x21. */
__data __at (0x19) unsigned char g_src_hi;
__data __at (0x1A) unsigned char g_src_lo;
__data __at (0x09) unsigned char g_xfer_len_lo;
__data __at (0x0B) unsigned char g_xfer_len_hi;

/* GET_DESCRIPTOR, Rev 22.  Same three types as Rev 20 and only index 0 for
 * CONFIGURATION; everything else stalls.
 *
 * Descriptor addresses moved with the image:
 *
 * (cited at the MOV 0x1A,#lo that loads the low half of each pointer)
 *
 *     descriptor        rev20        rev22      rev20 site  rev22 site
 *     DEVICE            0x0596       0x057D       0x017D      0x0181
 *     CONFIGURATION     0x0670       0x0657       0x019B      0x0197
 *     STRING 0 (LANGID) 0x06A6       0x068D       0x01C1      0x01BA
 *     STRING 1          0x06AA       0x0691       0x01CE      0x01C7
 *     STRING 2          0x06C8       0x06AF       0x01DB      0x01D4
 *
 * Every one shifted by exactly -0x19, which is consistent with the whole
 * descriptor block having moved as a unit rather than any descriptor changing
 * size.  I have not decoded the Rev 22 descriptor bytes, so I am not claiming
 * their contents are identical, only that the pointers moved uniformly.
 *
 * As in Rev 20 the CONFIGURATION pointer is the vendor-class configuration
 * (its wTotalLength is read from offsets +2/+3, rev22 0x019D..0x01A5), and no
 * code path ever serves the UAC configuration block.
 *
 * Keil's type tests are mixed the same way in both images: `== 1` compiled to
 * CJNE (rev20 0x0177, rev22 0x017B) while types 2 and 3 compiled to XRL + JNZ
 * (rev22 0x018A, 0x01AD).  The C mirrors that distinction, which is why the
 * first test is written `== 1` and the others `(x ^ n) == 0`.
 *
 * TWO REAL DIFFERENCES FROM THE REV 20 SOURCE, both structural:
 *
 * 1. The helper was split.  Rev 20's code_read_byte_at_srcptr (0x0B6E) is
 *    MOV DPL,0x1A / MOV DPH,0x19 / CLR A / MOVC / RET — DPTR load and first
 *    byte fetch in one.  Rev 22's load_dptr_from_ptr19 sits at the same
 *    address, 0x0B6E, but is only MOV DPL,0x1A / MOV DPH,0x19 / RET; the
 *    CLR A / MOVC moved out to the caller (0x01DA).  That is why the Rev 20
 *    CONFIGURATION arm had to open-code its own DPL/DPH load (rev20 0x019E)
 *    while the Rev 22 one can call the helper (0x019A).  Same address, two
 *    different functions — worth flagging for anyone cross-reading listings.
 *
 * 2. Rev 20 emitted that "read bLength from offset 0" sequence twice, once in
 *    the DEVICE arm and once in the STRING arm.  Rev 22 emits it once at
 *    0x01D7 and the DEVICE arm jumps into it with SJMP (0x0184).  That is a
 *    tail shared between two arms, so the C needs an explicit goto into the
 *    STRING arm's tail; writing the two arms independently, as the Rev 20
 *    candidate does, produces the sequence twice and is 10 bytes longer.
 *
 * The exit tail (clamp + start transfer) is likewise shared by two arms at
 * 0x01E6 via SJMP, which the `goto send` expresses.  Ghidra names it
 * ep0_clamp_and_send; it is not a separate function, it is the last two
 * statements of this one, so it is inside this candidate rather than being
 * declared as an entry point.
 *
 * The DPTR load is inline asm for the same reason as in Rev 20: SDCC has no
 * way to spell "call something that leaves DPTR live", and the callee is
 * declared with a hand-written .globl inside the asm block because SDCC
 * rejects __asm at file scope. */
void std_get_descriptor(void) {
    if (SETUP_wValueH == 1) {                     /* DEVICE */
        g_src_hi = 0x05; g_src_lo = 0x7D;
        goto read_blength;                        /* shared tail at 0x01D7 */
    }
    if (((SETUP_wValueH ^ 2) == 0) && (SETUP_wValueL == 0)) {
        g_src_hi = 0x06; g_src_lo = 0x57;         /* vendor-class config */
        __asm
            .globl _load_dptr_from_ptr19
            lcall _load_dptr_from_ptr19
            mov  a,#0x02
            movc a,@a+dptr          ; wTotalLength low
            mov  0x09,a
            mov  a,#0x03
            movc a,@a+dptr          ; wTotalLength high
            mov  0x0b,a
        __endasm;
        goto send;
    }
    if ((SETUP_wValueH ^ 3) == 0) {               /* STRING */
        if (SETUP_wValueL == 0) { g_src_hi = 0x06; g_src_lo = 0x8D; }
        if (SETUP_wValueL == 1) { g_src_hi = 0x06; g_src_lo = 0x91; }
        if (SETUP_wValueL == 2) { g_src_hi = 0x06; g_src_lo = 0xAF; }
    read_blength:
        /* bLength is offset 0 of every descriptor; the high byte of the
         * transfer length is always zero for these. */
        __asm
            lcall _load_dptr_from_ptr19
            clr  a
            movc a,@a+dptr
            mov  0x09,a
            clr  a
            mov  0x0b,a
        __endasm;
        goto send;
    }
    ep0_stall_both();
    return;
send:
    ep0_clamp_len_to_wlength();
    /* Stock ends LCALL 0x0B63 / RET (0x01E9, 0x01EC).  Written as a plain C
     * call SDCC collapses it to LJMP and the function comes out one byte
     * short, so the call is spelled in asm to keep the RET.  Note Rev 20 went
     * the other way: its equivalent tail is LJMP 0x0B77 (rev20 0x01EE), which
     * is exactly what SDCC produces unaided.  So this one byte flipped
     * direction between the images. */
    __asm
        .globl _ep0_in_stage_and_go
        lcall _ep0_in_stage_and_go
    __endasm;
}
