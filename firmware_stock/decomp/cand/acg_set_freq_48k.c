// MATCH: image=rev20 addr=0x0DEC len=34 func=acg_set_freq_48k
#include "regs.h"
void acg_set_freq_48k(void) {
    ACG1FRQ1 = 0xA8; ACG1FRQ2 = 0x61; ACG1FRQ0 = 0x0F;
    ACG2FRQ1 = 0xA8; ACG2FRQ2 = 0x61; ACG2FRQ0 = 0x0F;
}
