// MATCH: image=rev20 addr=0x029F len=72 func=std_set_interface cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_nack_both(void);
extern void ep0_stall_both(void);
__bit __at (0x08) f_iface1_alt;
__bit __at (0x09) f_iface2_alt;
__bit __at (0x0A) f_cfg_alt;     /* written but never set — vestigial */
__bit __at (0x0E) f_configured;

/* SET_INTERFACE. Records the requested alternate setting as a single bit per
 * interface — alt 0 or "not alt 0" is the whole of the state — and queues the
 * event that makes the audio path follow.
 *
 * Note `0x0A` appears here as both a bit and a byte, and they are unrelated:
 * `JB 0x0a` tests bit 0x0A, which is IRAM 0x21.2 (f_cfg_alt), while
 * `MOV 0x0a,#2` writes IRAM byte 0x0A (g_event). The two print identically in
 * a disassembly listing.
 *
 * The two stalls are deliberately not shared. The three guards jump to one
 * LJMP at 0x02E4; the unreachable else arm has its own at 0x02DE. Stock keeps
 * them separate, so the C does too. */
void std_set_interface(void) {
    if (SETUP_wIndexL > 2)   goto stall;
    if (SETUP_wValueL >= 2)  goto stall;
    if (!f_cfg_alt && !f_configured) goto stall;

    if (SETUP_wIndexL == 1) {
        f_iface1_alt = (SETUP_wValueL > 0);
        g_event = 2;
    } else if (SETUP_wIndexL == 2) {
        f_iface2_alt = (SETUP_wValueL > 0);
        g_event = 3;
    } else {
        ep0_stall_both();       /* unreachable: wIndex > 2 already rejected */
        return;
    }
    ep0_nack_both();
    return;
stall:
    ep0_stall_both();
}
