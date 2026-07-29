// MATCH: image=rev20 addr=0x025B len=62 func=std_set_configuration cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_nack_both(void);
extern void ep0_stall_both(void);
__bit __at (0x08) f_iface1_alt;
__bit __at (0x09) f_iface2_alt;
__bit __at (0x0A) f_cfg_alt;     /* written but never set — vestigial */
__bit __at (0x0E) f_configured;
/* Accepts configuration 0 or 1. The wValue==2 arm below is unreachable
 * because the range check above already rejected it; it survives in the
 * original as dead code and is reproduced here to match. */
void std_set_configuration(void) {
    if (SETUP_wValueL >= 2) { ep0_stall_both(); return; }
    if (SETUP_wValueL == 0) {
        f_cfg_alt = 0; f_configured = 0; f_iface1_alt = 0; f_iface2_alt = 0;
    }
    if (SETUP_wValueL == 1) {
        f_cfg_alt = 0; f_configured = 1; f_iface1_alt = 0; f_iface2_alt = 0;
    }
    if (SETUP_wValueL == 2) {
        f_cfg_alt = 0; f_configured = 1; f_iface1_alt = 0; f_iface2_alt = 0;
    }
    g_event = 1;
    ep0_nack_both();
}
