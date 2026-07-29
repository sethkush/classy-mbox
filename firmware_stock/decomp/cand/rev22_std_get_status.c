// MATCH: image=rev22 addr=0x022F len=24 func=std_get_status cflags=--peep-file,firmware_stock/decomp/keil.peep

/* GET_STATUS, for every recipient: always answers 0x0000.
 *
 * Nothing is examined -- not bmRequestType, not wIndex -- so the device
 * reports itself bus-powered, not remote-wakeup-capable, and no endpoint ever
 * halted, whatever was asked.  That is not merely lazy: std_set_feature and
 * std_clear_feature do maintain a halt state and this handler will not report
 * it, so a host that halts an endpoint and reads the status back sees zero.
 *
 * REV 20 -> REV 22: same policy, same two zero bytes, and the same
 * store/advance/store idiom.  The differences are all mechanical:
 *
 *   * the EP0 working pointer moved from IRAM 0x1B:0x1C to 0x1D:0x1E, and its
 *     two helpers with it (ep0_ptr_set_in_buf 0x0B3E -> ep0_in_buf_ptr_load
 *     0x0B37, dptr_from_ep0_ptr 0x0B17 -> ep0_load_dptr 0x0B25,
 *     ep0_buf_clear_byte 0x0B36 -> ep0_buf_store_zero 0x0B5B);
 *   * the length-and-flags tail was factored out.  Rev 20 open-coded
 *     `IEPDCNTX0 = 2; f_stage_out = 0; f_stage_in = 1;` and was 30 bytes.
 *     Rev 22 sets up DPTR and A here and FALLS THROUGH into
 *     ep0_arm_in_and_done at 0x0247, which is 24 bytes.  There is no jump at
 *     the end at all: 0x0246 is `MOV A,#2` and 0x0247 is the next function.
 *
 * WRITTEN AS ASSEMBLY because of that fall-through.  A C function always gets
 * an epilogue RET, and stock has none -- the last byte of this function is the
 * immediate operand of `MOV A,#2`.  Falling out of the bottom of a function
 * into the next one is not expressible in C.  The Rev 20 counterpart
 * (cand/std_get_status.c) is C precisely because it did not need to.
 *
 * The 16-bit pointer advance is Keil's: `INC lo / MOV A,lo / JNZ / INC hi`
 * leaves the new low byte in A, which is exactly the argument
 * ep0_buf_store_zero takes -- it does MOV DPL,A / MOV DPH,0x1D / CLR A / MOVX.
 * That register-passed argument is the other reason C cannot reach this.
 */
void std_get_status(void) __naked {
    __asm
        .globl _ep0_in_buf_ptr_load   ; rev22 0x0B37
        .globl _ep0_load_dptr         ; rev22 0x0B25
        .globl _ep0_buf_store_zero    ; rev22 0x0B5B, takes low addr byte in A

        lcall _ep0_in_buf_ptr_load ; IRAM 0x1D:0x1E = 0xFA18, EP0 IN buffer
        lcall _ep0_load_dptr
        clr   a
        movx  @dptr,a              ; wStatus low  = 0
        inc   0x1e                 ; advance the pointer; A ends up holding
        mov   a,0x1e               ;   the new low byte
        jnz   10$
        inc   0x1d
    10$:                           ; 0x023F
        lcall _ep0_buf_store_zero  ; wStatus high = 0
        mov   dptr,#0xff6b         ; IEPDCNTX0
        mov   a,#0x02              ; two bytes to send
        ;; falls through into ep0_arm_in_and_done at 0x0247
    __endasm;
}
