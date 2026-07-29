// MATCH: image=rev22 addr=0x010B len=58 span=1 func=usb_std_request_dispatch cflags=--peep-file,firmware_stock/decomp/keil.peep

/* Standard-request demultiplexer: `switch (SETUP_bRequest)` for the requests
 * that are not class requests.  Reached by the fall-through LJMP at rev22
 * 0x0052, the default arm of the bmRequestType classifier in
 * usb_setup_handler.
 *
 * ==================== THE BIGGEST STRUCTURAL DELTA ====================
 *
 * This is where Rev 20 and Rev 22 differ in CONSTRUCT and not merely in
 * addresses.  Rev 20 (cand/std_request_dispatch.c, rev20 0x0118, 44 B) used
 * Keil's SEARCHED-KEY helper: `MOV A,bRequest` / `LCALL ?C?CCASE` with an
 * inline table of {targetHi, targetLo, caseKey} triples, terminated by a
 * 00 00 sentinel and a default address.  Keil picks that form when the case
 * values are sparse, and bRequest is sparse because USB 2.0 table 9-4
 * reserves 2 and 4.
 *
 * Rev 22 has NO ?C?CCASE at all.  The helper's opening bytes D0 83 D0 82
 * (POP DPH / POP DPL) occur once in rev20_firmware_code.bin, at 0x0F70, and
 * ZERO times in rev22_firmware_code.bin -- rev22 0x0F70 is
 * ff ff f0 90 ff a8 74 84 f0 90, the middle of its RSTR handler.  Instead
 * Rev 22 emits a DENSE jump table: range-check, multiply by three, JMP @A+DPTR
 * into thirteen consecutive three-byte LJMPs, one slot per bRequest 0..12,
 * with the reserved codes 2 and 4 given explicit slots pointing at the stall.
 *
 * Cost, counting only what each dispatcher owns:
 *     rev20   3 (load) + 3 (lcall) + 11*3 (entries) + 4 (sentinel+default) = 43
 *     rev22   3 + 1 + 3 + 2 + 3 + 3 + 1 + 1 + 1 + 13*3                     = 58
 * so Rev 22 spends 15 more bytes here -- but it no longer needs the 38-byte
 * ?C?CCASE routine anywhere in the image, and it drops the four separate
 * three-byte stall thunks Rev 20 had (see below), so the image is smaller
 * overall.  It is also O(1) instead of a linear scan over the key list.
 *
 * WHY THE LOWERING CHANGED is not something the bytes tell me.  Keil switches
 * between the two forms on a density heuristic; anything that changed the case
 * set or the optimiser settings between builds could flip it.  I am not
 * asserting a cause.
 *
 * ---- dispatch policy: unchanged in substance, refactored in encoding ----
 *
 *   bRequest   name                rev20 target          rev22 target
 *     0  GET_STATUS         0x022F std_get_status      0x022F std_get_status
 *     1  CLEAR_FEATURE      0x0144 std_clear_feature   0x0145 std_clear_feature
 *     2  (reserved)         default -> 0x02EA          0x02EF ep0_stall_both
 *     3  SET_FEATURE        0x029C stall thunk         0x029B std_stall_unsupported
 *     4  (reserved)         default -> 0x02EA          0x02EF ep0_stall_both
 *     5  SET_ADDRESS        0x024D std_set_address     0x024D std_set_address
 *     6  GET_DESCRIPTOR     0x0173 std_get_descriptor  0x0177 std_get_descriptor
 *     7  SET_DESCRIPTOR     0x0299 stall thunk         0x029B std_stall_unsupported
 *     8  GET_CONFIGURATION  0x015D                     0x015C
 *     9  SET_CONFIGURATION  0x025B                     0x0259
 *    10  GET_INTERFACE      0x01F1                     0x01ED
 *    11  SET_INTERFACE      0x029F                     0x029D
 *    12  SYNCH_FRAME        0x02E7 stall thunk         0x029B std_stall_unsupported
 *    >12 default            0x02EA (LCALL+RET)        0x02EF ep0_stall_both
 *
 * Same eleven handlers, same three refusals, same default.  Every request the
 * firmware recognises it recognised before, and every one it refused it still
 * refuses.  So the wire-visible standard-request behaviour is unchanged; what
 * changed is only how the branch is taken.
 *
 * Two encoding consequences worth noting:
 *
 *  * Rev 20 spent three separate three-byte LJMP thunks on SET_FEATURE,
 *    SET_DESCRIPTOR and SYNCH_FRAME (0x029C, 0x0299, 0x02E7), because the
 *    ?C?CCASE table stores an address and cannot store "the default".  Rev 22
 *    folded all three into ONE two-byte thunk, std_stall_unsupported at
 *    0x029B (`SJMP 0x02EF`).  Nine bytes down to two.
 *  * Rev 20's default arm was its own function, std_request_unknown_default
 *    at 0x02EA (LCALL stall / RET).  Rev 22 has no such function: out-of-range
 *    bRequest LJMPs straight to ep0_stall_both at 0x02EF, which is itself the
 *    LCALL 0x1001 / RET wrapper.  The distinction Rev 20 had -- recognised-
 *    then-refused versus unrecognised -- was never visible on the wire (both
 *    STALL), and Rev 22 stopped paying for it.
 *
 * ---- the range check ----
 *
 * `CJNE A,#0x0D,$+3` / `JC` is Keil's unsigned `< 13` idiom: CJNE is used
 * purely for the borrow it leaves in CY, and its own branch displacement is
 * zero, so it always "falls through" to the JC.  Note this admits bRequest 0
 * through 12 only; bRequest is a full byte, so 13..255 all take the LJMP to
 * ep0_stall_both.  cand/std_set_configuration.c documents the SETB C / SUBB
 * variant of the same comparison; here Keil chose the CJNE form because the
 * value is already in A and nothing else needs CY set first.
 *
 * `MOV R0,A / ADD A,R0 / ADD A,R0` is the multiply-by-three: three bytes and
 * three cycles, against a 4-byte MUL AB sequence.  The result indexes a table
 * of LJMPs rather than a table of addresses, so the dispatch is JMP @A+DPTR
 * into code, not a pointer load.
 *
 * WRITTEN AS ASSEMBLY.  The table is thirteen LJMP instructions that exist as
 * jump-table data at a fixed base address; there is no C for "take the address
 * of my own jump table", and SDCC's own dense-switch lowering uses a different
 * shape (two MOVC tables of address halves -- see cand/device_event_dispatch.c
 * for an instance of that in this very image family).  The table base 0x011E
 * is written as a LITERAL, not as a label: match51 compiles standalone at
 * address 0 and must never mask a `MOV DPTR,#imm16`, so a symbolic base would
 * assemble to 0x0013 and fail.  The literal is a layout claim and link51 is
 * what checks it, by resolving the thirteen LJMP targets for real.
 */
void usb_std_request_dispatch(void) __naked {
    __asm
        .globl _ep0_stall_both          ; 0x02EF
        .globl _std_get_status          ; 0x022F
        .globl _std_clear_feature       ; 0x0145
        .globl _std_stall_unsupported   ; 0x029B
        .globl _std_set_address         ; 0x024D
        .globl _std_get_descriptor      ; 0x0177
        .globl _std_get_configuration   ; 0x015C
        .globl _std_set_configuration   ; 0x0259
        .globl _std_get_interface       ; 0x01ED
        .globl _std_set_interface       ; 0x029D

        mov   dptr,#0xff29         ; SETUP_bRequest, USB engine setup buffer
        movx  a,@dptr
        cjne  a,#0x0d,10$          ; displacement 0: this is the borrow, not
    10$:                           ;   a branch.  CY = (bRequest < 13)
        jc    20$
        ljmp  _ep0_stall_both      ; bRequest 13..255: not a standard request
    20$:
        mov   dptr,#0x011e         ; base of the 13-entry LJMP table below
        mov   r0,a
        add   a,r0                 ; A = bRequest * 3, the LJMP slot size
        add   a,r0
        jmp   @a+dptr

        ;; ---- dense jump table, rev22 0x011E, one slot per bRequest ----
        ljmp  _std_get_status        ;  0 GET_STATUS
        ljmp  _std_clear_feature     ;  1 CLEAR_FEATURE
        ljmp  _ep0_stall_both        ;  2 reserved (USB 2.0 table 9-4)
        ljmp  _std_stall_unsupported ;  3 SET_FEATURE      -- recognised, refused
        ljmp  _ep0_stall_both        ;  4 reserved
        ljmp  _std_set_address       ;  5 SET_ADDRESS
        ljmp  _std_get_descriptor    ;  6 GET_DESCRIPTOR
        ljmp  _std_stall_unsupported ;  7 SET_DESCRIPTOR   -- recognised, refused
        ljmp  _std_get_configuration ;  8 GET_CONFIGURATION
        ljmp  _std_set_configuration ;  9 SET_CONFIGURATION
        ljmp  _std_get_interface     ; 10 GET_INTERFACE
        ljmp  _std_set_interface     ; 11 SET_INTERFACE
        ljmp  _std_stall_unsupported ; 12 SYNCH_FRAME      -- recognised, refused
    __endasm;
}
