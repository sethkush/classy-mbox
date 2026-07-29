// MATCH: image=rev20 addr=0x0E9D len=56 func=button_b_cycle_3state cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Channel B selector, cycled by P3.4. Same machine as channel A on
 * IRAM 0x22 bits 5:3. */
void button_b_cycle_3state(void) {
    if (!sb0) {
        sb0 = 1; sb1 = 1;
        pb_src0 = 1; pb_src1 = 0; pb_src2 = 1;
    } else if (sb1) {
        sb0 = 1; sb1 = 0;
        pb_src0 = 1; pb_src1 = 1; pb_src2 = 0;
    } else {
        sb0 = 0; sb1 = 0;
        pb_src0 = 0; pb_src1 = 1; pb_src2 = 1;
    }
    if (!f_spdif) p_derived = 1;
    if (f_spdif)  p_derived = 0;
    if (f_force)  p_derived = 0;
}
