// MATCH: image=rev20 addr=0x0E27 len=59 func=button_a_cycle_3state cflags=--peep-file,firmware_stock/decomp/keil.peep
#include "mbox.h"
/* Channel A source selector, cycled by the P3.3 button.
 * Two hidden state bits walk a 3-cycle and emit the panel code in
 * IRAM 0x22 bits 2:0:  0b101 Mic -> 0b011 Line -> 0b110 Inst -> ... */
void button_a_cycle_3state(void) {
    if (!sa0) {                 /* (0,x) -> Mic  */
        sa0 = 1; sa1 = 1;
        pa_src0 = 1; pa_src1 = 0; pa_src2 = 1;
    } else if (sa0 && sa1) {    /* (1,1) -> Line.  Keil re-tests sa0 here;
                                   channel B's source omits it. */
        sa0 = 1; sa1 = 0;
        pa_src0 = 1; pa_src1 = 1; pa_src2 = 0;
    } else {                    /* (1,0) -> Inst */
        sa0 = 0; sa1 = 0;
        pa_src0 = 0; pa_src1 = 1; pa_src2 = 1;
    }
    /* Keil emitted these as three independent tests, not an if/else. */
    if (!f_spdif) p_derived = 1;
    if (f_spdif)  p_derived = 0;
    if (f_force)  p_derived = 0;
}
