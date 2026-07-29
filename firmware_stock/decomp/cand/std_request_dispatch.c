// MATCH: image=rev20 addr=0x0118 len=44 span=1 func=std_request_dispatch cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Standard-request demultiplexer: `switch (SETUP_bRequest)` for
 * bmRequestType[6:5] == 00 (standard). Reached by LJMP from 0x0052, the fall-
 * through arm of the bmRequestType classifier in usb_ev_setup.
 *
 * Ghidra lists this as 7 bytes because the 37 bytes that follow are data and
 * it stops at the LCALL. They are one construct: the LCALL to 0x0F70 is Keil's
 * ?C?CCASE helper, which pops the return address to find its argument table
 * inline immediately after the call site. The table is the rest of this
 * function, so the candidate claims 0x0118..0x0143 (44 bytes) with span=1.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY. There is no C that produces this: the
 * whole construct is a call into a Keil runtime routine that reads its own
 * return address, and the table is raw data whose entries are addresses of
 * eleven other functions. SDCC's sparse-switch lowering is a chain of
 * CJNE/DJNZ or its own two-table MOVC form (see device_event_dispatch), never
 * a searched key list behind a helper call.
 *
 * Table format, decoded from switch_case_dispatch at rev20 0x0F70:
 *   repeat: .db targetHi, .db targetLo, .db caseKey
 *   end:    .db 0, 0, then .db defaultHi, .db defaultLo
 * The terminator is recognised by both address bytes being zero, which is safe
 * because 0x0000 is the reset vector and never a case target.
 *
 * That decode is from rev20 ONLY -- Rev 22 has no ?C?CCASE helper at all. The
 * routine's opening bytes D0 83 D0 82 (POP DPH / POP DPL) occur once in
 * rev20_firmware_code.bin, at 0x0F70, and zero times in rev22_firmware_code.bin;
 * rev22 0x0F70 is ff ff f0 90 ff a8 74 84 f0 90, the middle of its RSTR
 * handler. Rev 22 dispatches standard requests with a dense jump table instead:
 * at rev22 0x010B it reads SETUP_bRequest from 0xFF29, range-checks it with
 * CJNE A,#0x0D / JC at rev22 0x010F..0x0113 (out of range -> LJMP 0x02EF, the
 * default arm), triples it with MOV R0,A / ADD A,R0 / ADD A,R0 at rev22
 * 0x011A..0x011C, and does JMP @A+DPTR at rev22 0x011D into a table of
 * thirteen three-byte LJMPs based at rev22 0x011E -- one slot per bRequest
 * 0..12, with the reserved codes 2 and 4 given explicit LJMP 0x02EF entries.
 * cand/switch_case_dispatch.c says the same thing.
 *
 * bRequest coverage. Present: 0 GET_STATUS, 1 CLEAR_FEATURE, 3 SET_FEATURE,
 * 5 SET_ADDRESS, 6 GET_DESCRIPTOR, 7 SET_DESCRIPTOR, 8 GET_CONFIGURATION,
 * 9 SET_CONFIGURATION, 10 GET_INTERFACE, 11 SET_INTERFACE, 12 SYNCH_FRAME.
 * Absent: 2 and 4, which are reserved in USB 2.0 table 9-4, so they take the
 * default arm along with everything above 12. SET_DESCRIPTOR, SET_FEATURE and
 * SYNCH_FRAME have entries but their handlers are three-byte LJMPs into
 * ep0_stall_both -- the request is recognised and then refused, which is a
 * different wire behaviour from the default arm (see std_request_unknown_default).
 *
 * The addresses in the table are written as literals rather than as symbols.
 * ASxxxx has no byte-halves relocation that would let `.db (_sym >> 8)` be
 * resolved by the linker, so a symbolic form would assemble to 00 00 and could
 * not be checked at all. Every entry is annotated with the name it points at
 * and all twelve were read out of the Ghidra function table.
 */
void std_request_dispatch(void) __naked {
    __asm
        .globl _switch_case_dispatch

        mov   dptr,#0xff29         ; SETUP_bRequest, USB engine setup buffer
        movx  a,@dptr
        lcall _switch_case_dispatch   ; 0x0F70, Keil ?C?CCASE; table is inline

        ;; ---- searched-key table, 0x011F ------------------------------------
        .db   0x02, 0x2f, 0x00     ; 0x022F std_get_status          GET_STATUS
        .db   0x01, 0x44, 0x01     ; 0x0144 std_clear_feature       CLEAR_FEATURE
        .db   0x02, 0x9c, 0x03     ; 0x029C std_set_feature_stall   SET_FEATURE
        .db   0x02, 0x4d, 0x05     ; 0x024D std_set_address         SET_ADDRESS
        .db   0x01, 0x73, 0x06     ; 0x0173 std_get_descriptor      GET_DESCRIPTOR
        .db   0x02, 0x99, 0x07     ; 0x0299 std_set_descriptor_stall SET_DESCRIPTOR
        .db   0x01, 0x5d, 0x08     ; 0x015D std_get_configuration   GET_CONFIGURATION
        .db   0x02, 0x5b, 0x09     ; 0x025B std_set_configuration   SET_CONFIGURATION
        .db   0x01, 0xf1, 0x0a     ; 0x01F1 std_get_interface       GET_INTERFACE
        .db   0x02, 0x9f, 0x0b     ; 0x029F std_set_interface       SET_INTERFACE
        .db   0x02, 0xe7, 0x0c     ; 0x02E7 std_synch_frame_stall   SYNCH_FRAME
        .db   0x00, 0x00           ; terminator
        .db   0x02, 0xea           ; 0x02EA std_request_unknown_default
    __endasm;
}
