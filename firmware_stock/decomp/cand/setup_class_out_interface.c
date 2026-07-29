// MATCH: image=rev20 addr=0x0055 len=22 func=setup_class_out_interface cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* bmRequestType 0x21: class, host->device, interface.
 * bRequest 0 is Digidesign's enter-DFU trigger — queue event 13, which
 * zeroes EEPROM byte 0 and so invalidates the boot signature.
 * Anything else is a class SET_CUR; tag it and expect an OUT data stage. */
void setup_class_out_interface(void) {
    if (SETUP_bRequest == 0) {
        g_event = 13;
        f_stage_out = 0;
        f_stage_in = 0;
        return;
    }
    g_class_tag = 2;
    f_stage_out = 1;
    f_stage_in = 0;
}
