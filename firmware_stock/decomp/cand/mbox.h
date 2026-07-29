/* Shared declarations for Rev 20 match candidates.
 * Each candidate is its own translation unit, so definitions are fine here. */
#ifndef MBOX_H
#define MBOX_H
#define SFRX(nm, a) __xdata __at (a) volatile unsigned char nm
/* --- USB engine ------------------------------------------------------- */
SFRX(SETUP_bmRequestType, 0xFF28);
SFRX(SETUP_bRequest,      0xFF29);
SFRX(SETUP_wValueL,       0xFF2A);
SFRX(SETUP_wValueH,       0xFF2B);
SFRX(SETUP_wIndexL,       0xFF2C);
SFRX(IEPCNF0,   0xFF68);   SFRX(OEPCNF0,   0xFFA8);
SFRX(IEPDCNTX0, 0xFF6B);   SFRX(OEPDCNTX0, 0xFFAB);
SFRX(USBCTL,    0xFFFC);   SFRX(USBIMSK,   0xFFFD);
SFRX(USBFADR,   0xFFFF);
/* --- codec port / clock ----------------------------------------------- */
SFRX(GLOBCTL,   0xFFB1);
SFRX(CPTCNF3,   0xFFDE);   SFRX(CPTRXCNF3, 0xFFD5);
SFRX(ACGCTL,    0xFFE1);   SFRX(ACG1DCTL,  0xFFE2);  SFRX(ACG2DCTL, 0xFFF6);
SFRX(ACG1FRQ2,  0xFFE5);   SFRX(ACG1FRQ1,  0xFFE6);  SFRX(ACG1FRQ0, 0xFFE7);
SFRX(ACG2FRQ2,  0xFFF7);   SFRX(ACG2FRQ1,  0xFFF8);  SFRX(ACG2FRQ0, 0xFFF9);
SFRX(DMACTL0,   0xFFE8);   SFRX(DMACTL1,   0xFFEE);
/* --- IRAM state ------------------------------------------------------- */
__data __at (0x0A) unsigned char g_event;        /* pending event code 1..14 */
__data __at (0x0D) unsigned char g_class_tag;    /* pending class-request tag */
__data __at (0x0E) unsigned char g_pending_addr; /* deferred USB address     */
__data __at (0x1B) unsigned char g_ep0_ptr_hi;
__data __at (0x1C) unsigned char g_ep0_ptr_lo;
__data __at (0x31) unsigned char g_chip_reg;     /* queued chip register     */
__data __at (0x32) unsigned char g_chip_val;     /* queued chip value        */
/* --- bit flags (bit B lives in IRAM 0x20 + (B>>3), bit B&7) ------------ */
__bit __at (0x0B) f_stage_out;   /* IRAM 0x21.3 — expect an OUT data stage  */
__bit __at (0x0C) f_stage_in;    /* IRAM 0x21.4 — an IN data stage is armed */
#endif
