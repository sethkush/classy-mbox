// MATCH: image=rev22 addr=0x0B37 len=7 func=ep0_in_buf_ptr_load cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Point the EP0 working pointer at the IN buffer, XDATA 0xFA18. Six call
 * sites (0x006E, 0x0091, 0x022F, 0x0ABE, 0x015C, 0x01FC).
 *
 * REV 20 -> REV 22 DELTA: same length, same constant 0xFA18 and the same
 * instruction sequence as rev20 ep0_ptr_set_in_buf at 0x0B3E, but NOT the same
 * bytes -- two operand bytes differ, at +1 (1b -> 1d) and +4 (1c -> 1e),
 * because the EP0 working pointer moved from IRAM 0x1B:0x1C to 0x1D:0x1E. Behaviour unchanged. (Rev 20's 0x0B3E is this function;
 * Rev 22's 0x0B3E is ep0_clear_stall_both. Another same-address collision
 * between the images.) */
__data __at (0x1D) unsigned char g_ep0_ptr_hi;   /* HIGH byte -- Keil order */
__data __at (0x1E) unsigned char g_ep0_ptr_lo;
void ep0_in_buf_ptr_load(void) { g_ep0_ptr_hi = 0xFA; g_ep0_ptr_lo = 0x18; }
