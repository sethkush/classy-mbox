/*
 * Telemetry — device-side counters, readable over EP0 in single packets.
 *
 * Rationale and block map: mboxfw/TELEMETRY.md.
 *
 * One power cycle buys exactly one image on this part, and a power cycle
 * costs a 2 km round trip, so the loaded image has to answer questions
 * over the wire instead of by reflashing a variant.
 *
 * Every read is EXACTLY 8 bytes — one EP0 packet. The defect under
 * investigation is the multi-packet continuation path, so telemetry must
 * never depend on it: 1- and 2-packet transfers measured 60/60 on
 * hardware, 3+ packets 53/60 or worse. Host selects a block via wValue.
 */

#ifndef MBOXFW_TELEMETRY_H
#define MBOXFW_TELEMETRY_H

#define TLM_BLOCK_SIZE   8
#define TLM_NUM_BLOCKS   5

/* Vendor requests. DEVICE recipient, NOT interface: snd-usb-audio claims
 * the audio interfaces, and an interface-recipient request then fails with
 * EBUSY in the host stack before it ever reaches us — that is exactly how
 * the enter-DFU request silently never arrived on 2026-07-27. */
#define TLM_REQ_READ     0x10   /* bmRequestType 0xC0, wValue = block index */
#define TLM_REQ_RESET    0x11   /* bmRequestType 0x40, clears the counters  */

/* Build identity. Bump when flashing a new image so a read of block 0
 * proves WHICH build is running rather than assuming. */
#define TLM_BUILD_ID     0x0003   /* 0003: SET_CUR sample rate read in the OUT data stage */

/* Phase bitmap bits (block 0 byte 3) */
#define TLM_PHASE_USB_INIT   0x01
#define TLM_PHASE_HW_INIT    0x02
#define TLM_PHASE_ATTACH     0x04
#define TLM_PHASE_CS8427     0x08
#define TLM_PHASE_CODEC      0x10
#define TLM_PHASE_MAIN_LOOP  0x20

/* Counters. Written from ISR context, read from the SETUP handler (also
 * ISR context), so no cross-context tearing — but keep them volatile so
 * SDCC cannot cache them across the increments. */
extern volatile __data unsigned int  tlm_setup_count;
extern volatile __data unsigned int  tlm_iep0_count;
extern volatile __data unsigned int  tlm_chunks;
extern volatile __data unsigned int  tlm_drains;
extern volatile __data unsigned int  tlm_rstr_count;
extern volatile __data unsigned int  tlm_loop_count;
extern volatile __data unsigned char tlm_stalls;
extern volatile __data unsigned char tlm_stage;
extern volatile __data unsigned char tlm_phases;

/* Last SETUP packet seen (block 2) */
extern volatile __data unsigned char tlm_last_bmreq;
extern volatile __data unsigned char tlm_last_breq;
extern volatile __data unsigned int  tlm_last_wvalue;
extern volatile __data unsigned int  tlm_last_windex;
extern volatile __data unsigned int  tlm_last_wlength;

/* VECINT histogram (block 3), saturating at 255 */
extern volatile __data unsigned char tlm_vec_setup;
extern volatile __data unsigned char tlm_vec_iep0;
extern volatile __data unsigned char tlm_vec_oep0;
extern volatile __data unsigned char tlm_vec_rstr;
extern volatile __data unsigned char tlm_vec_none;
extern volatile __data unsigned char tlm_vec_other;

/* Peripheral init results (block 4) */
extern volatile __data unsigned char tlm_eeprom_ok;
extern volatile __data unsigned char tlm_cs8427_status;
extern volatile __data unsigned char tlm_codec_status;

/* Port state sampled in main() before hw_init() touches the pins.
 *
 * NOVEL — reason: settles the boot-DFU button question empirically. The
 * claim that source-1 reads on P3.3 is an RE inference off Rev 20 and has
 * never been confirmed; check_boot_dfu_button() has never once fired. A
 * live read plus this boot-time sample tells us which bit actually moves
 * when the user holds the button, instead of another guess costing a
 * power cycle. Nothing in the boot ROM or Rev 20 records port state, so
 * there is no reference behaviour to copy here. */
extern volatile __data unsigned char tlm_p1_boot;
extern volatile __data unsigned char tlm_p3_boot;

/* Saturating increments — a counter that wraps mid-experiment reads as a
 * smaller number than reality and would silently corrupt a measurement. */
#define TLM_INC8(c)   do { if ((c) < 0xFF)   (c)++; } while (0)
#define TLM_INC16(c)  do { if ((c) < 0xFFFF) (c)++; } while (0)

/* Fill an 8-byte block. Returns 0 and fills 0xFF for an unknown index so
 * a host reading past the end gets a clean sentinel instead of a stall. */
unsigned char tlm_read_block(unsigned char index, unsigned char *out);
void tlm_reset_counters(void);

#endif /* MBOXFW_TELEMETRY_H */
