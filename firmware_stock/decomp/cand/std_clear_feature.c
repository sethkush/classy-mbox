// MATCH: image=rev20 addr=0x0144 len=25 func=std_clear_feature cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
extern void ep0_clear_stall_both(void);
extern void ep0_stall_both(void);
/* Only ENDPOINT_HALT on endpoint 0 is honoured: bmRequestType must be 0x02
 * (standard, host->device, endpoint) and wIndex must select EP0.
 * The `^ 2` mirrors Keil's XRL A,#2 / JNZ rather than a CJNE. */
void std_clear_feature(void) {
    if (((SETUP_bmRequestType ^ 2) == 0) && (SETUP_wIndexL == 0)) {
        ep0_clear_stall_both();
        f_stage_out = 0;
        f_stage_in = 0;
        return;
    }
    ep0_stall_both();
}
