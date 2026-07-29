/* Absolute XDATA register declarations for match candidates.
 * Each candidate is its own translation unit, so plain definitions are fine. */
#ifndef MBOX_REGS_H
#define MBOX_REGS_H
#define SFRX(nm, a) __xdata __at (a) volatile unsigned char nm
SFRX(DMACTL0,   0xFFE8);
SFRX(DMACTL1,   0xFFEE);
SFRX(ACGCTL,    0xFFE1);
SFRX(ACG1DCTL,  0xFFE2);
SFRX(ACG2DCTL,  0xFFF6);
SFRX(ACG1FRQ2,  0xFFE5);
SFRX(ACG1FRQ1,  0xFFE6);
SFRX(ACG1FRQ0,  0xFFE7);
SFRX(ACG2FRQ2,  0xFFF7);
SFRX(ACG2FRQ1,  0xFFF8);
SFRX(ACG2FRQ0,  0xFFF9);
SFRX(OEPDCNTX0, 0xFFAB);
SFRX(IEPDCNTX0, 0xFF6B);
SFRX(IEPCNF0,   0xFF68);
SFRX(OEPCNF0,   0xFFA8);
#endif
