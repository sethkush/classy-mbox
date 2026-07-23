/*
 * Master hardware initialisation.
 * Ports Rev 20's fcn.0x08CB verbatim — see
 * firmware_stock/disasm/NOTES.md § "Master boot init".
 */

#include "regs.h"
#include "mux.h"

extern __data unsigned char g_mux_state;  /* mirror of Rev 20 RAM[0x22] */
extern __bit g_phantom_48v;               /* mirror of Rev 20 RAM[0x23].6 */

static void short_delay(void)
{
    /* Rev 20 spins RAM[0x2E] from 0 up to 0x0F00 (~4000 cycles). */
    unsigned int i;
    for (i = 0; i < 0x0F00; i++) { }
}

void hw_init(void)
{
    /* -------- 8051 core SFRs -------- */
    TMOD = 0x11;   /* both timers 16-bit mode 1 */
    TH0  = 0xCE;   /* SOF-tick timer reload */
    TL0  = 0x00;
    TH1  = 0x00;
    TL1  = 0x00;
    TCON = 0x00;
    IE   = 0x03;   /* EX0 + ET0 enabled, EA still off (set later) */
    IP   = 0x00;
    P1   = 0x00;   /* all P1 pins low */
    P3   = 0xFF;   /* all P3 pins high (button inputs pull-up) */

    /* -------- TAS1020A UIFR init (order matters) --------
     *
     * DO NOT touch USBCTL here. Boot ROM's UtilResetCPU handoff and our
     * usb_init() (called BEFORE hw_init in main.c) both configure
     * USBCTL — clobbering it with `=` in the middle of boot leaves the
     * host un-attached and any hang below unreachable via DFU. If we
     * ever want to reset USB state, use RMW (& ~bits / |= bits), never
     * a raw assignment. See task #48. */
    MEMCFG  |= 0x01;    /* set SDW — code fetches route to RAM copy.
                         * RMW because boot ROM's UtilResetCPU already
                         * did MEMCFG |= SDW_BIT; we're just being
                         * idempotent. Reference: TI Utils.c UtilResetCPU. */
    DMACTL0   = 0x0D;
    CPTCNF4   = 0xE5;
    CPTCNF3   = 0xAC;
    CPTCNF2   = 0x03;
    CPTCNF1   = 0x50;
    CPTBRTX   = 0x25;
    CPTBRRX   = 0xAC;
    CPTCTL    = 0x03;
    GLOBCTL  |= 0x01;   /* enable USB last */

    /* -------- Initial mux state -------- */
    g_mux_state  = 0x00;
    g_phantom_48v = 1;      /* Rev 20 sets 0x23.6 = 1 initially */
    mux_write(g_mux_state);

    short_delay();

    /* Rev 20 default: source-select bit 0 clear on both channels, 48V off.
     * Bits: 0x22 = 0xFF then clear .0, .3, and 0x23.6.
     */
    g_mux_state  = (unsigned char)(0xFF & ~0x01 & ~0x08);
    g_phantom_48v = 0;
    mux_write(g_mux_state);
}
