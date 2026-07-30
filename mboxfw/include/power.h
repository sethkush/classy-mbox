#ifndef MBOXFW_POWER_H
#define MBOXFW_POWER_H

/* Deferred work codes, values taken from stock's RAM[0x0A] protocol.
 *
 * Stock's main-loop dispatcher is Rev 20 fcn.0x02EE (Rev 22 fcn.0x02F4) with a
 * jump table of 3-byte LJMP entries at 0x0300, indexed by code - 1. Only the
 * suspend code is implemented here; the rest of the table is stream setup and
 * S/PDIF clock slaving, which mboxfw handles in streaming.c or not yet at all
 * (task #145).
 *
 * 0x0E is suspend: Rev 20's SUSR vector handler at 0x0006 is `MOV 0x0A,#0x0E;
 * RET` and table entry 13 lands at 0x0526. Rev 22 identical, 0x0006 -> 0x0525.
 */
#define WORK_NONE       0x00
#define WORK_SUSPEND    0x0E

extern volatile __data unsigned char g_work_code;

/*
 * Run whatever g_work_code asks for, then clear it. Call from the main loop
 * with interrupts enabled — never from an ISR. Stock's suspend path spends an
 * unbounded time in PCON idle, so it cannot run in interrupt context, which is
 * exactly why stock defers it through this queue instead of doing the work in
 * the SUSR handler.
 */
void work_dispatch(void);

#endif
