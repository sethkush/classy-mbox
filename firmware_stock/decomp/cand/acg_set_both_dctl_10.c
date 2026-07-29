// MATCH: image=rev20 addr=0x0E18 func=acg_set_both_dctl_10
#include "regs.h"
void acg_set_both_dctl_10(void) { ACG1DCTL = 0x10; ACG2DCTL = 0x10; }
