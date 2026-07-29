// MATCH: image=rev22 addr=0x0B6E len=7 func=load_dptr_from_ptr19 cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Load DPTR from the CODE source pointer held in IRAM 0x19:0x1A -- 0x19 HIGH,
 * 0x1A low, Keil's byte order for pointers and the opposite of SDCC's. That
 * pointer is set by std_get_descriptor to the descriptor being sent, and
 * walked by ep0_in_send_chunk.
 *
 * Factored out because LCALL costs 3 bytes against 6 for the inlined pair, and
 * it has three call sites (0x019A, 0x01D7, 0x0AC8). Setting DPTR as a side
 * effect is not expressible in C, so this is naked assembly with the IRAM
 * addresses written numerically.
 *
 * REV 20 -> REV 22 DELTA: A REAL SPLIT, and a trap for anyone cross-reading
 * the two listings, because the address did not move. Rev 20 0x0B6E is
 * code_read_byte_at_srcptr, 9 bytes, MOV DPL,0x1A / MOV DPH,0x19 / CLR A /
 * MOVC A,@A+DPTR / RET -- pointer load AND first-byte fetch. Rev 22 0x0B6E is
 * these 7 bytes only; the CLR A / MOVC moved out to the three call sites.
 * Same address, two different functions. Note the IRAM pair itself did NOT
 * move: 0x19:0x1A in both. It is the EP0 *buffer* pointer that was renumbered
 * (0x1B:0x1C -> 0x1D:0x1E) -- see ep0_load_dptr. */
void load_dptr_from_ptr19(void) __naked {
    __asm
        mov  dpl,0x1a      ; low  byte of the CODE source pointer
        mov  dph,0x19      ; high byte
        ret
    __endasm;
}
