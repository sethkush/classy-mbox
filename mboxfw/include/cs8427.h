#ifndef MBOXFW_CS8427_H
#define MBOXFW_CS8427_H

void cs8427_write(unsigned char reg, unsigned char value);
void cs8427_boot_init(void);


/* Where cs8427_read_probe() leaves its eight samples. In XDATA because
 * internal RAM is full — see cs8427.c. */

#endif
