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
#define TLM_NUM_BLOCKS   11

/* Vendor requests. DEVICE recipient, NOT interface: snd-usb-audio claims
 * the audio interfaces, and an interface-recipient request then fails with
 * EBUSY in the host stack before it ever reaches us — that is exactly how
 * the enter-DFU request silently never arrived on 2026-07-27. */
#define TLM_REQ_READ     0x10   /* bmRequestType 0xC0, wValue = block index */
#define TLM_REQ_RESET    0x11   /* bmRequestType 0x40, clears the counters  */
/* Enter-DFU, DEVICE recipient. The Digi class request at interface
 * recipient CANNOT be delivered once snd-usb-audio has claimed the audio
 * interfaces — the host stack rejects it with EBUSY before it reaches the
 * device (observed 2026-07-28 against a bound card). The escape hatch is
 * the last thing that should stop working when a driver is attached, so it
 * gets a device-recipient alias for exactly the reason stated above. */
#define TLM_REQ_ENTER_DFU 0x12  /* bmRequestType 0x40, invalidate + halt   */

/* Set the source mux from the host. bmRequestType 0x40, DEVICE recipient.
 *
 *   wValue low  bits [2:0] = channel 1 source pattern
 *               bits [5:3] = channel 2 source pattern
 *   wIndex low  0 = mono off, 1 = mono on, anything else = leave unchanged
 *
 * NOVEL — reason: stock reaches these states only through the front-panel
 * buttons, so there is no request to port.
 *
 * KEPT when the UAC Selector Units were removed on 2026-08-03, and it is the
 * only remaining way to set the mux without physical access. The mux resets to
 * MIC on every power cycle while both bench loopbacks are wired to the LINE
 * inputs, so without this every flash would need someone at the unit pressing
 * buttons before any capture measurement means anything -- and the hosts are
 * ~1 km away. That mismatch already voided a full session on 2026-07-29.
 *
 * Device recipient on purpose: snd-usb-audio claims the audio interfaces, and
 * an interface-recipient request is rejected with EBUSY by the host stack
 * before it reaches us. This one keeps working when the class binding is
 * broken, which is exactly when bench control is most needed.
 *
 * Only the six source bits are taken from the host. Bit 0x22.6 is derived by
 * codec_source_changed() and bit 0x22.7 is a control line no stock source
 * handler ever writes, so both are preserved. Illegal patterns are rejected
 * rather than published: g_mux_state = 0x00 is exactly what voided that
 * earlier measurement, and a request that can reproduce it is a trap. */
#define TLM_REQ_SET_MUX  0x13

/* The three legal source patterns, one-cold, from the stock cycle handlers
 * (Rev 20 fcn.0x0E27 / fcn.0x0E9D, Rev 22 fcn.0x0E1B / fcn.0x0E8F). */
#define MUX_PAT_MIC      0x06   /* boot state */
#define MUX_PAT_LINE     0x05   /* what the bench loopbacks are wired to */
#define MUX_PAT_INST     0x03

/* Build identity. Bump when flashing a new image so a read of block 0
 * proves WHICH build is running rather than assuming. */
#define TLM_BUILD_ID     0x001B   /* 001B: #170 -- the codec control word's
                                   *       source nibble (0x25.0-.3) is now
                                   *       driven. It was write-zero-only, so
                                   *       the codec chain said MIC on both
                                   *       channels whatever the relay chain
                                   *       said. Block 9 byte 3 shows it.
                                   * 001A: software source control removed
                                   *       (UAC Selector Units out, setmux
                                   *       kept); blocks 8 and 10 retired;
                                   *       5994 -> 5281 bytes.
                                   * 0019: Selector Unit control selector
                                   *       is 0 in UAC1, not 1 -- 0x0018
                                   *       stalled every host read.
                                   * 0018: per-channel Selector Units --
                                   *       class-compliant source select
                                   *       from the host (#159).
                                   * 0017: DFU escape hoisted ahead of
                                   *       usb_init, with the two writes
                                   *       it depends on (#172).
                                   * 0016: buttons are ACTIVE HIGH -- GLOBCTL
                                   *       P3PUDIS restored, boot-DFU button
                                   *       read un-inverted and moved after
                                   *       hw_init (#150/#169).
                                   * 0015: block 10 = CS8427 readback probe (#165)
                                   * 0014: CS8427 SPI framing + chip select +
                                   *       external RESET released + bring-up
                                   *       order fixed (#157/#166/#167), mono
                                   *       moved into the codec word.
                                   * 0013: 0012 + host mux control (block 9) */

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
extern volatile __data unsigned char tlm_vec_susr;
extern volatile __data unsigned char tlm_vec_resr;

/* Playback frame-alignment resyncs performed by streaming_sof(). Non-zero
 * means the playback DMA buffer was found holding a partial sample frame and
 * the path was torn down and restarted -- Rev 22's watchdog firing. A steadily
 * climbing count means something upstream keeps misaligning the stream. */
extern volatile __data unsigned char tlm_playback_resyncs;

/* Completed suspend cycles — incremented in do_suspend() just before PCON
 * idle, so reading a non-zero value proves the device both entered and left
 * idle (a read is only possible once it is answering EP0 again). */
extern volatile __data unsigned char tlm_suspends;


/* Host mux-set request outcomes (block 9). Two counters rather than one, so a
 * read distinguishes "the request never arrived" from "it arrived and was
 * rejected as an illegal pattern" -- indistinguishable from the mux word
 * alone, since a rejected request leaves it unchanged. */
extern volatile __data unsigned char tlm_mux_sets;
extern volatile __data unsigned char tlm_mux_rejects;

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

/* Isochronous streaming (block 5).
 *
 * NOVEL — reason: streaming.c assumes the TAS1020B DMA engine shuttles audio
 * between the C-port and USB packet memory autonomously, which is why the
 * IEP1/OEP2 vectors are unhandled and streaming_sof() is a no-op. That
 * assumption has never been tested and arecord fails with -EIO. These
 * counters separate the three candidates: no SOF means we are not seeing
 * frames at all; SOF but no IEP1 means the host is not transacting or the
 * endpoint is not armed; IEP1 firing means the endpoint IS transacting and
 * the problem is upstream in the I2S/codec path. No reference firmware
 * records this, so there is nothing to copy. */
extern volatile __data unsigned int  tlm_sof_count;
extern volatile __data unsigned char tlm_vec_iep1;
extern volatile __data unsigned char tlm_vec_oep2;

/* SET_INTERFACE forensics. Sticky, because a host-side read always races
 * arecord's teardown back to alt 0. */
#define TLM_ALT_PLAYBACK_ON 0x01
#define TLM_ALT_CAPTURE_ON  0x02
extern volatile __data unsigned char tlm_last_iface;
extern volatile __data unsigned char tlm_last_alt;
extern volatile __data unsigned char tlm_alt_seen;

/* Saturating increments — a counter that wraps mid-experiment reads as a
 * smaller number than reality and would silently corrupt a measurement. */
#define TLM_INC8(c)   do { if ((c) < 0xFF)   (c)++; } while (0)
#define TLM_INC16(c)  do { if ((c) < 0xFFFF) (c)++; } while (0)

/* Fill an 8-byte block. Returns 0 and fills 0xFF for an unknown index so
 * a host reading past the end gets a clean sentinel instead of a stall. */
/* `out` is __data-qualified deliberately. Unqualified, SDCC builds a 3-byte
 * generic pointer and routes every one of this function's ~88 byte stores
 * through the __gptrput library helper: 56 such calls in the emitted object,
 * and telemetry.c weighing 1690 bytes -- 30% of the whole firmware. The only
 * caller passes a local array (usb.c stage_immediate path), which under
 * --model-small lives in internal RAM, so a 1-byte __data pointer is both
 * correct and what the hardware wants. Do not drop the qualifier: it is worth
 * hundreds of bytes against a 6016-byte program RAM. See BRICK_LOG.md #3. */
unsigned char tlm_read_block(unsigned char index, unsigned char __data *out);
void tlm_reset_counters(void);

#endif /* MBOXFW_TELEMETRY_H */
