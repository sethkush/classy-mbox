// MATCH: image=rev22 addr=0x0026 len=218 span=1 func=usb_setup_handler cflags=--peep-file,firmware_stock/decomp/keil.peep

/* VECINT 0x12 (SETUP): an 8-byte SETUP packet landed in the EP0 setup buffer.
 * Reached from the VECINT table (XREF rev22 0x0CA1), exactly as Rev 20's
 * usb_ev_setup is reached from rev20 0x0CB7.
 *
 * WHAT THIS CANDIDATE COVERS, AND WHY IT IS ONE UNIT.
 *
 * At source level this is FIVE Keil functions, the same five Rev 20 has, laid
 * out adjacently so the dispatcher can reach each with a 2-byte JZ:
 *
 *     rev20                              rev22        role
 *     usb_ev_setup             0x0026    0x0026  47B  prologue + bmRequestType
 *     setup_class_out_interface 0x0055   0x0055  25B  0x21 class OUT iface
 *     setup_class_out_endpoint  0x006B   0x0066   3B  0x22 class OUT endpoint
 *     setup_get_input_source    0x0073   0x006E  24B  0xA1 class IN  iface
 *     setup_get_sample_freq     0x008A   0x0086 122B  0xA2 class IN  endpoint
 *
 * Ghidra splits them in Rev 20 but merges them in Rev 22 into one 218-byte
 * usb_setup_handler, because Rev 22 gave the interface and endpoint arms a
 * SHARED TAIL at 0x0069 (`SETB 0x0b / CLR 0x0c / RET`): 0x0066 falls through
 * into it and 0x0064 jumps into it, so no function boundary can be drawn
 * between them.  Rev 20 emitted that tail twice (0x0066 and 0x006E) and the
 * arms were separable.  Since link51 derives its symbol set from the header's
 * func= name and the rev22 function table has exactly one function here, the
 * candidate claims the whole run 0x0026..0x00FF with span=1.
 *
 * WRITTEN AS ASSEMBLY.  Two independent reasons, both already established:
 *
 *  1. The bmRequestType switch is Keil's add-chain (see the annotation at the
 *     chain below).  It is a global lowering decision, not a peephole-sized
 *     substitution; cand/usb_ev_setup.c documents the same problem in Rev 20.
 *  2. Even with the chain in assembly, the four arms cannot be C in the same
 *     translation unit: they are reachable only from inside an __asm block, so
 *     SDCC sees them as dead code and deletes them.  Splitting them into their
 *     own candidates -- which is what Rev 20 does, and their C is known good
 *     (cand/setup_class_out_interface.c, cand/setup_get_sample_freq.c) -- is
 *     blocked by the merged tail and by the single Ghidra symbol.
 *
 * Branch targets inside the run are local labels.  Two targets fall OUTSIDE
 * it and are still relative branches, so they are written as offsets from the
 * end-of-function label: 0x0100 (thunk_stall_ep0, the 3-byte LJMP trampoline
 * Keil emitted because CJNE's displacement cannot reach ep0_stall_both at
 * 0x02EF) and 0x0103 (ep0_arm_in_3bytes).  That encodes the layout assumption
 * explicitly, and the layout is itself part of the match.
 *
 * ============================ REV 20 -> REV 22 ============================
 *
 * PROLOGUE (0x0026..0x0054): byte-identical in shape, and the add-chain is
 * literally the same bytes (24 de / 60 1f, 24 81 / 60 23, 14 / 60 38,
 * 24 81 / 60 03).  Only the call operands (0x0B50->0x0B3E, 0x0B2B->0x0B2C),
 * the four JZ displacements, and the default LJMP (0x0118 -> 0x010B) moved.
 * Dispatch POLICY is unchanged: same four bmRequestType values, same order.
 *
 * ARMS: three behavioural refactors, no change in wire behaviour that I can
 * see -- every one is Keil factoring a repeated tail into a call:
 *
 *   a. The "no data stage, we are done" tail.  Rev 20 open-coded
 *      CLR 0x0b / CLR 0x0c / RET at rev20 0x005E.  Rev 22 replaced it with
 *      LJMP 0x02E8 (ep0_done_no_data), a real function with four callers.
 *      Same three operations, one byte cheaper per site after the second.
 *
 *   b. The "one byte armed, IN stage" tail.  Rev 20's input-source arm ends
 *      LJMP 0x0B45 (ep0_send_1byte: IEPDCNTX0=1 / clr / setb).  Rev 22 ends
 *      LJMP 0x016F instead -- the TAIL OF std_get_configuration, which is the
 *      same three operations reached one instruction earlier (it loads
 *      DPTR=IEPDCNTX0 and A=1 there, then falls into ep0_arm_in_and_done at
 *      0x0247).  So Rev 22 merged what Rev 20 kept as a standalone helper.
 *      0x016F is a merged-tail entry point with no Ghidra symbol; it is in
 *      decomp/proposed/setup.symbols as ep0_arm_in_1byte.
 *
 *   c. The EP0 working pointer MOVED IN IRAM: 0x1B:0x1C (hi:lo) in Rev 20,
 *      0x1D:0x1E in Rev 22.  Everything that touches it moved with it, and
 *      the helpers were renumbered to match (0x0B17->0x0B25 load DPTR,
 *      0x0B36->0x0B5B store-zero-at-A, 0x0B3E->0x0B37 point at the IN buffer).
 *      The buffer addresses themselves are unchanged: EP0 IN 0xFA18, EP0 OUT
 *      0xFA10, read off rev22 0x0B37 and 0x0B1F.
 *
 * The three-byte reply payloads are IDENTICAL: mode 1 -> 00 00 00, mode 2 ->
 * 44 AC 00 (44100), mode 3 -> 80 BB 00 (48000), same clock-mode numbering in
 * IRAM 0x08.  So Rev 22 did NOT change the reported sample rates, and it did
 * not add 88.2/96k.  The wValueH != 1 stall and the "unknown clock mode"
 * stall are both still there.
 *
 * I did not find any behavioural difference in SETUP handling between the two
 * revisions.  Whatever Rev 22 fixed, it is not in this function.
 */
void usb_setup_handler(void) __naked {
    __asm
        .globl _ep0_clear_stall_both        ; rev22 0x0B3E
        .globl _ep0_store_byte_and_arm_zlp  ; rev22 0x0B2C
        .globl _usb_std_request_dispatch    ; rev22 0x010B
        .globl _ep0_done_no_data            ; rev22 0x02E8
        .globl _ep0_in_buf_ptr_load         ; rev22 0x0B37
        .globl _ep0_load_dptr               ; rev22 0x0B25
        .globl _ep0_buf_store_zero          ; rev22 0x0B5B
        .globl _ep0_stall_both              ; rev22 0x02EF
        .globl _ep0_arm_in_1byte            ; rev22 0x016F (merged tail)

        ;; ================= 0x0026  prologue (was usb_ev_setup) =============
        lcall _ep0_clear_stall_both ; a new SETUP unhalts EP0 (USB 2.0 8.5.3.4)

        mov   dptr,#0xff68         ; IEPCNF0
        movx  a,@dptr
        orl   a,#0x20              ; TOGGLE: data stage starts at DATA1
        movx  @dptr,a
        mov   dptr,#0xffa8         ; OEPCNF0
        movx  a,@dptr
        orl   a,#0x20
        lcall _ep0_store_byte_and_arm_zlp ; stores A, zeroes IEPDCNTX0/OEPDCNTX0
                                   ;   (arming EP0) and returns with A == 0

        clr   0x0d                 ; IRAM 0x21.5 -- no trailing ZLP pending
        mov   0x09,a               ; EP0 IN remaining length, low  (BYTE 0x09)
        mov   0x0b,a               ; EP0 IN remaining length, high (BYTE 0x0B)
                                   ; both reuse the A == 0 the callee left

        mov   dptr,#0xff28         ; SETUP_bmRequestType
        movx  a,@dptr

        ;; Keil's add-chain switch: each ADD leaves the running total in A, so
        ;; every later test is relative to the one before.  Eleven bytes for
        ;; four equality tests where a CJNE chain would cost twenty.  A is also
        ;; live across the LCALL above, which is the inter-procedural knowledge
        ;; SDCC does not have.
        add   a,#0xde              ; == 0 iff bmRequestType == 0x22
        jz    30$                ;   class, host->device, endpoint
        add   a,#0x81              ; == 0 iff 0xA1
        jz    50$                ;   class, device->host, interface
        dec   a                    ; == 0 iff 0xA2
        jz    70$                ;   class, device->host, endpoint
        add   a,#0x81              ; == 0 iff 0x21
        jz    10$                ;   class, host->device, interface
        ljmp  _usb_std_request_dispatch   ; everything else: standard requests

        ;; ========= 0x0055  bmRequestType 0x21 (class OUT, interface) =======
        ;; bRequest 0 is Digidesign's enter-DFU trigger: queue event 13, which
        ;; zeroes EEPROM byte 0 and so invalidates the boot signature.
        ;; Anything else is a class SET_CUR: tag it and expect an OUT stage.
    10$:  ; 0x0055
        mov   dptr,#0xff29         ; SETUP_bRequest
        movx  a,@dptr
        jnz   20$                             ; -> 0x0061
        mov   0x0a,#0x0d           ; g_event = 13, the DFU trigger
        ljmp  _ep0_done_no_data    ; (a) rev20 open-coded this tail at 0x005E
    20$:  ; 0x0061
        mov   0x0d,#0x02           ; g_class_tag = 2 (interface SET_CUR)
        sjmp  40$                             ; -> 0x0069

        ;; ========= 0x0066  bmRequestType 0x22 (class OUT, endpoint) ========
    30$:  ; 0x0066
        mov   0x0d,#0x01           ; g_class_tag = 1 (endpoint SET_CUR)
        ;; falls through into the tail it shares with the interface arm --
        ;; this fall-through is why Ghidra cannot split the two in rev22
    40$:  ; 0x0069
        setb  0x0b                 ; f_stage_out = 1: an OUT data stage follows
        clr   0x0c                 ; f_stage_in  = 0
        ret

        ;; ========= 0x006E  bmRequestType 0xA1 (class IN, interface) ========
        ;; Audio class GET_CUR on the Selector Unit (terminal ID 5): which
        ;; input pin is selected.  1 = analog (input terminal 2), 2 = S/PDIF
        ;; (input terminal 6).  One byte, stored through the EP0 pointer.
    50$:  ; 0x006E
        lcall _ep0_in_buf_ptr_load ; IRAM 0x1D:0x1E = 0xFA18, the EP0 IN buffer
        jnb   0x2c,60$           ; f_spdif (IRAM 0x25.4)
        lcall _ep0_load_dptr
        mov   a,#0x02              ; S/PDIF
        movx  @dptr,a
        ljmp  _ep0_arm_in_1byte    ; (b) rev20 used ep0_send_1byte at 0x0B45
    60$:  ; 0x007D
        lcall _ep0_load_dptr
        mov   a,#0x01              ; analog
        movx  @dptr,a
        ljmp  _ep0_arm_in_1byte

        ;; ========= 0x0086  bmRequestType 0xA2 (class IN, endpoint) =========
        ;; Audio class GET_CUR, SAMPLING_FREQ_CONTROL: the current rate as
        ;; three bytes, little endian, built by walking the EP0 IN buffer.
        ;; wValueH is the control selector and must be 1 (SAMPLING_FREQ).
    70$:  ; 0x0086
        mov   dptr,#0xff2b         ; SETUP_wValueH
        movx  a,@dptr
        xrl   a,#0x01
        jz    80$                             ; -> 0x0091
        ljmp  _ep0_stall_both      ; any other control selector: stall
    80$:  ; 0x0091
        lcall _ep0_in_buf_ptr_load
        mov   a,0x08               ; g_clock_mode, written by audio_clock_mode_apply
        cjne  a,#0x01,100$                    ; -> 0x00B3

        ;; ---- mode 1: clock idle, report 0 Hz (00 00 00) ----
        lcall _ep0_load_dptr
        clr   a
        movx  @dptr,a
        inc   0x1e                 ; advance the 16-bit EP0 pointer; the new
        mov   a,0x1e               ;   low byte is left in A, which is exactly
        jnz   90$                ;   the argument ep0_buf_store_zero takes
        inc   0x1d
    90$:  ; 0x00A6
        lcall _ep0_buf_store_zero
        inc   0x1e
        mov   a,0x1e
        jnz   150$                            ; -> 0x00FB
        inc   0x1d
        sjmp  150$                            ; -> 0x00FB

        ;; ---- mode 2: 44100 = 0x00AC44 ----
    100$:  ; 0x00B3
        mov   a,0x08
        cjne  a,#0x02,130$                    ; -> 0x00D8
        lcall _ep0_load_dptr
        mov   a,#0x44
        movx  @dptr,a
        inc   0x1e
        mov   a,0x1e
        jnz   110$                            ; -> 0x00C6
        inc   0x1d
    110$:  ; 0x00C6
        mov   dpl,a                ; A already holds the new low byte, so the
        mov   dph,0x1d             ;   pointer reload is two instructions
        mov   a,#0xac
        movx  @dptr,a
        inc   0x1e
        mov   a,0x1e
        jnz   120$                            ; -> 0x00D6
        inc   0x1d
    120$:  ; 0x00D6
        sjmp  150$                            ; -> 0x00FB

        ;; ---- mode 3: 48000 = 0x00BB80 ----
    130$:  ; 0x00D8
        mov   a,0x08
        cjne  a,#0x03,160$        ; unknown clock mode -> 0x0100, the LJMP
                                   ;   trampoline into ep0_stall_both
        lcall _ep0_load_dptr
        mov   a,#0x80
        movx  @dptr,a
        inc   0x1e
        mov   a,0x1e
        jnz   140$                            ; -> 0x00EB
        inc   0x1d
    140$:  ; 0x00EB
        mov   dpl,a
        mov   dph,0x1d
        mov   a,#0xbb
        movx  @dptr,a
        inc   0x1e
        mov   a,0x1e
        jnz   150$                            ; -> 0x00FB
        inc   0x1d

        ;; ---- shared third byte (always 0) and the 3-byte reply tail ----
    150$:  ; 0x00FB
        lcall _ep0_buf_store_zero
        sjmp  160$+3              ; 0x0103, ep0_arm_in_3bytes: IEPDCNTX0 = 3,
                                   ;   f_stage_out = 0, f_stage_in = 1.
                                   ;   Rev 20's equivalent merged tail is
                                   ;   send_3byte_ep0_reply at rev20 0x010D.
    160$:                         ; one past the last byte of this function
    __endasm;
}
