// MATCH: image=rev20 addr=0x0FEA func=ep0_arm_zlp
#include "mbox.h"
void ep0_arm_zlp(void) { IEPDCNTX0 = 0; OEPDCNTX0 = 0; }
