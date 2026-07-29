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

/* --- panel / selector state (bit addresses) ---------------------------- */
__bit __at (0x10) pa_src0;   /* IRAM 0x22.0 — channel A source, bit 0 */
__bit __at (0x11) pa_src1;   /* IRAM 0x22.1 */
__bit __at (0x12) pa_src2;   /* IRAM 0x22.2 */
__bit __at (0x13) pb_src0;   /* IRAM 0x22.3 — channel B source, bit 0 */
__bit __at (0x14) pb_src1;   /* IRAM 0x22.4 */
__bit __at (0x15) pb_src2;   /* IRAM 0x22.5 */
__bit __at (0x16) p_derived; /* IRAM 0x22.6 — derived from f_spdif/f_force */
__bit __at (0x28) sa0;       /* IRAM 0x25.0 — channel A hidden state */
__bit __at (0x2A) sa1;       /* IRAM 0x25.2 */
__bit __at (0x29) sb0;       /* IRAM 0x25.1 — channel B hidden state */
__bit __at (0x2B) sb1;       /* IRAM 0x25.3 */
__bit __at (0x2C) f_spdif;   /* IRAM 0x25.4 — S/PDIF selected, not analog */
__bit __at (0x2D) f_force;   /* IRAM 0x25.5 — forces p_derived clear */

/* --- panel shift-register source bytes -------------------------------- */
__data __at (0x22) unsigned char g_mux_byte;   /* chain A payload  */
__data __at (0x23) unsigned char g_panel_lo;   /* chain B, first 8 bits out */
__data __at (0x25) unsigned char g_panel_hi;   /* chain B, second 8 bits    */
__bit  __at (0x1E) p_hold;                     /* IRAM 0x23.6, P3.5 toggle  */
