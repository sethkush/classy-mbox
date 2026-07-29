// MATCH: image=rev20 addr=0x0E10 len=7 func=acg_commit_and_ctl
#include "regs.h"
void acg_commit_and_ctl(void) { ACGCTL = 0x06; }
