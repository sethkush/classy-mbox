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
/*
 * 0x02 is stock's cmd2, the interface-1-alt handler (event_jump_table entry 2,
 * Rev 20 0x0303 -> 0x0386, Rev 22 0x0303 -> 0x038D). Its FIRST action is the
 * guarded external-chip bring-up:
 *
 *     038f  JB 0x2e,0x0395      ; IRAM 0x25.6 — has bring-up run?
 *     0392  LCALL 0x080b        ; if not, run it
 *
 * mboxfw posts this code on SET_INTERFACE(alt != 0) and dispatches it to
 * cs8427_boot_init(), which carries the same guard internally. Only the
 * bring-up half of stock's cmd2 is deferred here; the endpoint enables stay
 * inline in the SETUP handler, which is an existing divergence and is fine —
 * they are register writes with no delays.
 *
 * It has to be deferred rather than called from the SETUP handler because
 * cs8427_boot_init() bit-bangs SPI and spins several settle_delay()s. Stock
 * runs cmd2 from the main-loop dispatcher for the same reason; doing it in the
 * EP0 ISR would hold off every other interrupt for milliseconds.
 */
#define WORK_BRINGUP    0x02
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
