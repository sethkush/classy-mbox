#ifndef MBOXFW_CS8427_H
#define MBOXFW_CS8427_H

void cs8427_write(unsigned char reg, unsigned char value);
void cs8427_boot_init(void);

/*
 * #165 — read `reg` back and report WHICH PIN answered. `out` must have room
 * g_cs8427_probe[i] is P3 sampled after read clock i, MSB of the reply
 * first. The HOST transposes — bit p across the eight samples is the byte pin
 * p produced, and whichever matches the value written identifies CDOUT. Eight
 * identical samples means no pin answered. See cs8427.c for why this does not
 * simply pick a pin, and why the transpose is not done here.
 */
void cs8427_read_probe(unsigned char reg);

/* Where cs8427_read_probe() leaves its eight samples. In XDATA because
 * internal RAM is full — see cs8427.c. */
extern __xdata unsigned char g_cs8427_probe[8];

#endif
