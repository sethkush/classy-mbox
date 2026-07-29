// MATCH: image=rev20 addr=0x024D len=14 func=std_set_address cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* The address must not take effect until after the status stage, so it is
 * stashed and written to USBFADR by the EP0-IN completion handler. */
void std_set_address(void) {
    g_class_tag = 5;
    g_pending_addr = SETUP_wValueL;
    f_stage_out = 0;
    f_stage_in = 0;
}
