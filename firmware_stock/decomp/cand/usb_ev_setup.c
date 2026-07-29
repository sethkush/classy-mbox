// MATCH: image=rev20 addr=0x0026 len=47 func=usb_ev_setup cflags=--peep-file,firmware_stock/decomp/keil.peep

/* VECINT 0x12 (SETUP) -- an 8-byte SETUP packet landed in the EP0 setup
 * buffer. Table entry 0x12 at 0x0CB7 reads 00 26, which is the XREF Ghidra
 * shows. Like usb_ev_suspend this leaf handler was packed into the unused
 * interrupt-vector space, here the gap after the serial vector at 0x0023.
 *
 * Prologue, in order:
 *   - ep0_clear_stall_both (0x0B50): a new SETUP always clears STALL on both
 *     halves of EP0, per USB 2.0 8.5.3.4 -- a control endpoint may not stay
 *     halted across a new SETUP;
 *   - IEPCNF0 |= 0x20 and OEPCNF0 |= 0x20 set TOGGLE, so the data stage that
 *     follows is expected as DATA1, which is what USB 2.0 8.5.3 mandates for
 *     the first data-stage packet of a control transfer. The OEPCNF0 half is
 *     the shared-tail idiom: the value is left in A and the store is done by
 *     the callee at 0x0B2B, which then also zeroes both data-count registers
 *     (arming EP0);
 *   - bit 0x0D (IRAM 0x21.5) cleared -- the "terminate this IN transfer with
 *     a short/zero-length packet" flag that ep0_in_fill_chunk consults at
 *     0x0BE1;
 *   - IRAM BYTES 0x09 and 0x0B zeroed. These are the 16-bit remaining-length
 *     counter of a pending EP0 IN transfer (0x09 low, decremented by the
 *     DJNZ at 0x0BB0; 0x0B high, decremented at 0x0BBD). Note this is the
 *     8051 bit/byte collision: `CLR 0x0D` above is a BIT and `MOV 0x0B,A`
 *     here is a BYTE, and 0x0B is used as both, for unrelated purposes.
 *     Both stores reuse the A == 0 that 0x0B2B left behind.
 *
 * Then the dispatch, on bmRequestType only:
 *
 *     0x21 -> setup_class_out_interface  (0x0055) class, host->device, iface
 *     0x22 -> setup_class_out_endpoint   (0x006B) class, host->device, EP
 *     0xA1 -> setup_get_input_source     (0x0073) class, device->host, iface
 *     0xA2 -> setup_get_sample_freq      (0x008A) class, device->host, EP
 *     else -> std_request_dispatch       (0x0118)
 *
 * WRITTEN AS ASSEMBLY, and the switch is why. Keil compiled these four
 * equality tests as a *chain of adds* on a single accumulator: A += 0xDE is
 * zero iff A was 0x22, then += 0x81 is zero iff 0xA1, then DEC A for 0xA2,
 * then += 0x81 for 0x21. Eleven bytes for four compares, where a CJNE chain
 * would cost twenty. SDCC has no such transform and no peephole can invent
 * one -- it is a global decision about how to lower a switch, not a local
 * substitution. A is also live across the LCALL at 0x0036, the same
 * inter-procedural knowledge that forced usb_rstr_handler into assembly.
 *
 * The branch targets are written as offsets from a label at the end of this
 * function, not as external symbols. JZ is a 2-byte relative branch, and the
 * assembler cannot compute a displacement to a symbol it will not resolve
 * until link time; anchoring on a local label makes the displacement correct
 * whether the function is assembled standalone at 0 or placed at 0x0026.
 * That works only because the four handlers are laid out immediately after
 * this function and in this order, which is itself part of the match -- the
 * same evidence that identified send_3byte_ep0_reply as a merged tail
 * (decomp/README.md).
 *
 * Rev 22 keeps this function at the identical address 0x0026 with the
 * identical 47-byte shape and the identical add chain (0x0043: 24 de / 60 1f,
 * 24 81 / 60 23, 14 / 60 38, 24 81 / 60 03), differing only in the call
 * operands (0x0B3E, 0x0B2C), the four displacements, and a default LJMP to
 * 0x010B instead of 0x0118. The dispatch policy is unchanged between the two
 * revisions. */
void usb_ev_setup(void) __naked {
    __asm
        .globl _ep0_clear_stall_both
        .globl _ep0_store_cnf_and_arm_both
        .globl _std_request_dispatch

        lcall _ep0_clear_stall_both ; a new SETUP unhalts EP0 (USB 2.0 8.5.3.4)

        mov   dptr,#0xff68         ; IEPCNF0
        movx  a,@dptr
        orl   a,#0x20              ; TOGGLE: data stage starts at DATA1
        movx  @dptr,a
        mov   dptr,#0xffa8         ; OEPCNF0
        movx  a,@dptr
        orl   a,#0x20
        lcall _ep0_store_cnf_and_arm_both ; stores A, then arms both; returns A=0

        clr   0x0d                 ; IRAM 0x21.5 -- no trailing ZLP pending
        mov   0x09,a               ; EP0 IN remaining length, low  (BYTE 0x09)
        mov   0x0b,a               ; EP0 IN remaining length, high (BYTE 0x0B)

        mov   dptr,#0xff28         ; SETUP_bmRequestType
        movx  a,@dptr

        ; The Keil add-chain switch. Each ADD leaves the running total in A, so
        ; every later test is relative to the one before it.
        add   a,#0xde              ; == 0 iff bmRequestType == 0x22
        jz    0055$+0x16           ;   -> setup_class_out_endpoint  (0x006B)
        add   a,#0x81              ; == 0 iff 0xA1
        jz    0055$+0x1e           ;   -> setup_get_input_source    (0x0073)
        dec   a                    ; == 0 iff 0xA2
        jz    0055$+0x35           ;   -> setup_get_sample_freq     (0x008A)
        add   a,#0x81              ; == 0 iff 0x21
        jz    0055$                ;   -> setup_class_out_interface (0x0055)
        ljmp  _std_request_dispatch
    0055$:
        ; one past the last byte of this function; the four class handlers
        ; follow immediately, in the order shown above
    __endasm;
}
