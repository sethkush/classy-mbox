/*
 * Telemetry — device-side counters, readable over EP0 in single packets.
 *
 * Rationale and block map: mboxfw/TELEMETRY.md.
 *
 * One power cycle buys exactly one image on this part, and a power cycle
 * costs a 2 km round trip, so the loaded image has to answer questions
 * over the wire instead of by reflashing a variant.
 *
 * Every read is EXACTLY 8 bytes — one EP0 packet. Host selects a block via
 * wValue.
 *
 * This was originally forced: the multi-packet continuation path was losing
 * ~12% of IN packets past the first (3 packets 53/60, 23 packets 9/60 on
 * 2026-07-27), so telemetry was designed never to depend on it. THAT DEFECT IS
 * FIXED — re-measured 2026-08-05 at 300/300 on 36-packet transfers, on both
 * units and both host-controller families, 21,600 packets with no loss. See
 * FINDING_ep0_multipacket_loss_is_fixed.md.
 *
 * The constraint stays anyway, because it is simple and proven and costs
 * nothing. It is no longer a workaround, so anything new may use multi-packet
 * replies if there is a reason to.
 */

#ifndef MBOXFW_TELEMETRY_H
#define MBOXFW_TELEMETRY_H

#define TLM_BLOCK_SIZE   8
/* 12 since build 0x0032: #186 stage 1 added the ACG clock block at index
 * 11. Retired indices (3, 6, 7, 8, 10) are never reused -- see the
 * BLOCK_RETIRED note in tools/mboxtlm.py for why. */
#define TLM_NUM_BLOCKS   12

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

/* Clock source + Selector Unit, DEVICE recipient (#177).
 *
 *   bmRequestType 0x40
 *   wValue low   0 = slave to the incoming S/PDIF stream (clock mode 1)
 *                1 = internal 44.1 kHz   2 = internal 48 kHz
 *   wIndex low   0 = Selector -> analog  1 = Selector -> S/PDIF
 *                anything else = leave the Selector alone
 *
 * This is an ALIAS, not a new capability: the real controls are the UAC1
 * Selector Unit (0x21/0xA1, wIndex 0x0500) and the endpoint sampling-frequency
 * control (0x22/0xA2, wIndex 0x0081), both of which are implemented and both of
 * which stock also serves. It exists for the same reason TLM_REQ_SET_MUX and
 * the enter-DFU alias do: those two are INTERFACE and ENDPOINT recipient, so
 * once snd-usb-audio claims the interfaces the host stack rejects them with
 * EBUSY before they reach the device. At MBOX_PID=0x2000 the kernel's mbox1
 * quirk does not apply either, so nothing on a stock Linux host issues the
 * class requests at all — without this the S/PDIF path would be unreachable
 * from the bench, 1 km away.
 *
 * Read block 9 back afterwards: byte 3 carries the Selector bit (0x25.4) and
 * byte 7 the applied clock mode, so the request can be confirmed rather than
 * assumed from the absence of a stall. */
#define TLM_REQ_SET_CLOCK 0x14

/* 0x15 was TLM_REQ_SET_MUTE, the #189 bench control for the 0x23.2/0x23.3
 * pair. REMOVED in build 0x0036, when #190 declared the two UAC1 Feature Units
 * that carry the same control as a class request.
 *
 * It is not merely redundant, it has no remaining unique job. The reason the
 * other vendor aliases exist is that their class equivalents are INTERFACE or
 * ENDPOINT recipient and the host stack rejects those with EBUSY once
 * snd-usb-audio binds. That argument does not hold for mute: with the Feature
 * Units declared, ALSA surfaces mute as an ordinary mixer control that works
 * WITH the driver bound, which is the bench case. And with no driver bound
 * there is no streaming, so g_path_enabled is 0 and the pair is down anyway --
 * there is nothing to mute in the state the alias was meant to survive.
 *
 * What it actually bought at the end was 42 bytes, which is what the
 * per-unit serial descriptor costs. Without this removal #190 fits only in the
 * serial-less build, and the bench cannot tell two units apart without serials
 * (BENCH_WIRING.md, "Trust the serial"). The number is not reused: a request
 * code that changes meaning between builds is the same trap BLOCK_RETIRED
 * exists to prevent for telemetry indices. */

/* The three legal source patterns, one-cold, from the stock cycle handlers
 * (Rev 20 fcn.0x0E27 / fcn.0x0E9D, Rev 22 fcn.0x0E1B / fcn.0x0E8F). */
#define MUX_PAT_MIC      0x06   /* boot state */
#define MUX_PAT_LINE     0x05   /* what the bench loopbacks are wired to */
#define MUX_PAT_INST     0x03

/* Build identity. Bump when flashing a new image so a read of block 0
 * proves WHICH build is running rather than assuming. */
/* The Makefile may pre-define this for a one-off diagnostic image (see
 * MBOX_MUTE_PAIR_MASK). Keep exactly ONE `#define TLM_BUILD_ID <literal>` line in
 * this file: sim_telemetry_roundtrip.py parses it with a line regex and takes
 * the last match, so a second literal here makes the gate compare the image
 * against the wrong number. The guard, not a second #define, is what lets a
 * diagnostic build carry its own id. */
#ifndef TLM_BUILD_ID
#define TLM_BUILD_ID     0x003C   /* 003C: #197 v4 -- the clocks must have been
                                   *       running a WHILE, not merely be on.
                                   *       0x003B pulsed microseconds after
                                   *       ACGCTL and was inert; a host pulse
                                   *       with clocks on and NO stream running
                                   *       cleared the same unit. The pulse now
                                   *       waits 2500 SOFs in the MAIN LOOP --
                                   *       streaming_set_rate() runs in ISR
                                   *       context and cannot spin.
                                   * 003B: #197 v3 -- the pulse needs the codec's
                                   *       master clocks RUNNING, and mboxfw
                                   *       leaves ACGCTL alone until a stream
                                   *       opens, so at boot it was inert. It
                                   *       now fires once from
                                   *       streaming_set_rate(), which is what
                                   *       starts the clocks. Measured, not
                                   *       reasoned: playback alone raised the
                                   *       clocks and the pulse then worked
                                   *       with no capture ever opened.
                                   * 003A: #197 v2 -- the pulse polarity was
                                   *       WRONG in 0x0039 and measured wrong
                                   *       on hardware: it held the gate HIGH
                                   *       and ended LOW. Hold it LOW (>=50 ms
                                   *       measured) and end HIGH. Verify by
                                   *       capturing right after the replug --
                                   *       first 100 ms at the -101 dBFS floor,
                                   *       not -33. FINDING_197.
                                   * 0039: #197 -- pulse the capture gate once at
                                   *       boot, which clears the ADC start-up
                                   *       transient for the whole power-up.
                                   *       Verify by capturing immediately
                                   *       after the replug: the first 100 ms
                                   *       must sit at the -101 dBFS floor,
                                   *       not -39. FINDING_197.
                                   * 0038: #195 -- stall the standard requests
                                   *       that name what we do not declare:
                                   *       GET_DESCRIPTOR config index != 0,
                                   *       SET_CONFIGURATION > 1, and
                                   *       SET_INTERFACE with an undeclared
                                   *       iface or alt. Found by ch9_probe.py,
                                   *       which is also what verifies them.
                                   *
                                   *       0037: retires telemetry block 4 (stalls +
                                   *       live P1/P3) and the tlm.stalls
                                   *       counter with it -- 43 bytes. P3 is
                                   *       already on block 9 byte 4, and the
                                   *       counter had exactly one reader, so
                                   *       keeping it would have left it
                                   *       write-only. Index 4 is NOT reused.
                                   *
                                   *       0036: #190 -- two UAC1 Feature Units, one
                                   *       per path, each with a master Mute.
                                   *       0x23.3 is the playback gate and
                                   *       0x23.2 the capture gate, per #189.
                                   *       streaming_set_rate() no longer
                                   *       re-raises a host-set mute on every
                                   *       stream open, which was a real defect
                                   *       and not just the test artifact #189
                                   *       met it as. 6016 of 6016 bytes.
                                   *
                                   *       0035: #189 -- TLM_REQ_SET_MUTE, runtime
                                   *       control of the 0x23.2/0x23.3 pair.
                                   *       Replaces the MBOX_MUTE_PAIR_MASK
                                   *       compile-time variants: all four mask
                                   *       states on ONE power cycle, on one
                                   *       unit, repeatable in either
                                   *       direction, instead of two images and
                                   *       two round trips for a single
                                   *       one-shot A/B. Read block 9 byte 2
                                   *       back to confirm. Previously
                                   *
                                   *       0034: fixes the feedback value published
                                   *       across a stream start. usbmon on
                                   *       0x0033 caught the first 64-frame
                                   *       window straddling the ACG being
                                   *       reprogrammed and reporting 52.5
                                   *       samples/frame (+9.4%) for 16
                                   *       consecutive polls -- inside Linux's
                                   *       acceptance band, so the host took it.
                                   *       The window is now discarded on any
                                   *       rate change and any window more than
                                   *       ~3900 ppm from nominal is rejected,
                                   *       counted on block 11 byte 7.
                                   * 0033: #185 + #186 stage 2. The iso endpoints
                                   *       declare ASYNCHRONOUS (they were
                                   *       ADAPTIVE, which the 2026-08-05
                                   *       measurements showed to be false), and
                                   *       playback now publishes a real UAC
                                   *       feedback endpoint on EP2 IN carrying
                                   *       samples-per-frame in 10.14, measured
                                   *       from ACGCAP.
                                   * 0032: #186 stage 1 -- ACGCAP clock measurement
                                   *       on block 11, plus #187's S/PDIF output
                                   *       terminal and #188's feature-request
                                   *       stalls. MEASUREMENT ONLY on the clock:
                                   *       nothing here changes the ACG, the
                                   *       endpoints or the sync declarations.
                                   *       Block 11 must show a plausible
                                   *       MCLK-per-frame before a feedback
                                   *       endpoint is built on ACGCAP -- TI's own
                                   *       SoftPll.c hardcodes past that counter
                                   *       under the comment "debug test for
                                   *       capture counter malfunction".
                                   * 0031: first image flashed since the dead-field
                                   *       removal (tlm_cs8427_status /
                                   *       tlm_codec_status, block 4 rewritten
                                   *       to stalls/P1/P3). Bumped rather than
                                   *       reusing 0030 because 0030 was
                                   *       assigned one commit BEFORE that
                                   *       change, so it names two different
                                   *       source states. Neither was ever
                                   *       flashed -- the units ran 002C -- so
                                   *       nothing on-device is ambiguous, but
                                   *       a build id that maps to two trees is
                                   *       exactly the thing block 0 exists to
                                   *       prevent. Carries #181's drift test.
                                   * 0030: boot-button DFU trigger REMOVED, and
                                   *       with it eeprom_read_byte /
                                   *       eeprom_smoke_test (its only caller)
                                   *       and the #172 P3/GLOBCTL hoist.
                                   *       It never worked -- BRICK_LOG has it
                                   *       failing on three separate incidents
                                   *       -- and it was not the last resort it
                                   *       was taken for: the canonical
                                   *       SDA-short bootstrap needs no
                                   *       firmware cooperation at all. The
                                   *       WRITE path stays; TLM_REQ_ENTER_DFU
                                   *       is the trigger in daily use.
                                   *       Also: the per-experiment counters
                                   *       moved into one struct so the reset
                                   *       is a loop, and tlm_eeprom_ok went
                                   *       (its only writer was the smoke test).
                                   * 002F: telemetry block 6 retired -- its
                                   *       question (isoc IN returning
                                   *       zero-length) is answered and fixed.
                                   *       eeprom_smoke_test was considered and
                                   *       KEPT: it is not a health check, it
                                   *       gates the boot-button DFU trigger,
                                   *       so cutting it would attempt the
                                   *       signature invalidate blind.
                                   * 002E: #46 REMOVED. 88.2 and 96 kHz are gone
                                   *       from the descriptors AND from the
                                   *       code, and the endpoint buffers are
                                   *       back to stock's 640/640.
                                   *       Measured: the converter follows
                                   *       MCLK, and the ACG cannot double it
                                   *       (2.2.6.1 caps the synthesizer at
                                   *       25 MHz; 48 kHz already runs it at
                                   *       24.576 = 512 fs). So the doubled
                                   *       rates presented the codec with
                                   *       256 fs, it kept converting at the
                                   *       base rate, and every tone folded
                                   *       about that rate's Nyquist --
                                   *       30 kHz came back as 18, 40 kHz as 8.
                                   *       They cost double bandwidth, cost
                                   *       duplex entirely, and returned a
                                   *       folded spectrum. No firmware change
                                   *       reaches this. See
                                   *       FINDING_46_no_bandwidth_above_24k.md.
                                   * 002C: #46 -- the SOF playback watchdog now
                                   *       needs the misalignment to PERSIST
                                   *       across two consecutive SOFs before it
                                   *       tears the DMA down. 0x002B measured
                                   *       resyncs 0 at 48 kHz and SATURATED at
                                   *       96 on the same firmware: at 48 kHz
                                   *       DMABCNT0 is a steady 294 B so the
                                   *       watchdog takes its unchanged-exit
                                   *       every frame and never evaluates
                                   *       alignment at all, while at 96 kHz the
                                   *       fill level jitters and it evaluates
                                   *       every frame. The DMA moves 3 bytes
                                   *       per time slot, so an SOF snapshot can
                                   *       land mid-sample and read 3 (mod 6) on
                                   *       a healthy stream. A real misalignment
                                   *       persists; that one does not. Also
                                   *       drops block 6's duplicate IEPCNF1
                                   *       (block 5 byte 4 is the same read) to
                                   *       pay for it.
                                   * 002B: #46 -- asymmetric endpoint buffers,
                                   *       playback 696 B / capture 576 B, plus
                                   *       the instrument to tell two failure
                                   *       modes apart. 0x002A made capture at
                                   *       96 kHz correct and left playback
                                   *       silent with NOTHING visibly wrong
                                   *       from the host. The two directions do
                                   *       not want the same thing: capture
                                   *       needs the wrap frame-aligned, and
                                   *       playback needs slack the drain can
                                   *       live in. 576 B is exactly one frame
                                   *       at 96 kHz, i.e. zero slack, which is
                                   *       where playback fails. 696 = 116 whole
                                   *       samples (a multiple of 6 AND 8) and
                                   *       is the largest that fits beside
                                   *       capture's 576 in the 1288 B region.
                                   *       Block 3 now also reports DMABCNT0 and
                                   *       the playback resync count, so a null
                                   *       result says whether the buffer is
                                   *       starved or the SOF watchdog is
                                   *       thrashing. See regs.h.
                                   * 002A: #46 fix attempt + instrument.
                                   *       Endpoint buffers 640 -> 576 B: an
                                   *       ALIGNMENT fix, not a size cut. An
                                   *       isochronous BSIZ sizes a CIRCULAR
                                   *       buffer, and 640 is a whole number of
                                   *       frames at NEITHER rate (2.22 at 48
                                   *       kHz, 1.11 at 96) so the wrap point
                                   *       moves every frame. 576 is exactly 2
                                   *       frames at 48 kHz and exactly 1 at
                                   *       96. Bases 0xFA20 / 0xFC60.
                                   *       Block 3 revived as a live read of
                                   *       OEPBSIZ2/OEPBBAX2/IEPBBAX1, to tell
                                   *       a buffer bug from a register that
                                   *       never took.
                                   * 0029: #46 -- 88.2/96 kHz are now CLASS
                                   *       COMPLIANT. Each streaming interface
                                   *       gains an alt 2 advertising the two
                                   *       doubled rates at wMaxPacketSize 582,
                                   *       kept separate from alt 1's 294 so a
                                   *       host reserves the bandwidth it is
                                   *       about to use rather than the worst
                                   *       case. SET_CUR accepts them, and
                                   *       stalls a rate whose class disagrees
                                   *       with a running stream's alt -- the
                                   *       part has one clock for both
                                   *       directions and cannot hold 48 one
                                   *       way and 96 the other.
                                   * 0028: dead weight cut -- ten telemetry
                                   *       counters that were written on every
                                   *       interrupt and read by nobody once
                                   *       blocks 3 and 7 retired, the vestigial
                                   *       no-op switch at the top of
                                   *       usb_service() they were the only
                                   *       content of, and the settled #171
                                   *       MBOX_NO_MUTE_PAIR switch. 224 bytes
                                   *       free. NOT YET FLASHED -- 0027 is what
                                   *       runs on B; this rides along with the
                                   *       next image.
                                   * 0027: same firmware as 0024 with MBOX_UNIT=B,
                                   *       which 0024 was flashed WITHOUT -- so it
                                   *       served no iSerialNumber and B had to be
                                   *       addressed by bus:addr. 0025/0026 stay
                                   *       reserved for the MBOX_MUTE_PAIR_MASK
                                   *       variants (see the Makefile).
                                   * 0024: #46 -- 88.2 and 96 kHz reachable, via
                                   *       TLM_REQ_SET_CLOCK wValue 3/4 ONLY. The
                                   *       synthesizer cannot reach 2x (12-25 MHz
                                   *       range, and 48 kHz already runs it at
                                   *       24.576 MHz), so the doubling is in the
                                   *       C-port dividers: CPTCNF4/CPTRXCNF4 go
                                   *       /4 -> /2 and the frequency word is
                                   *       unchanged. Clock modes 6 and 7. NOT in
                                   *       the descriptors: whether the codec
                                   *       CONVERTS at 96 kHz is unmeasured, and
                                   *       until it is no host can select a rate
                                   *       whose analog behaviour is unknown.
                                   * 0023: #162 + #163 -- stock's endpoint buffer
                                   *       geometry. 640 B each, contiguous from
                                   *       0xFA20 (playback) and 0xFCA0 (capture),
                                   *       and base/size written ONCE in
                                   *       usb_ep0_setup() instead of on every
                                   *       SET_INTERFACE(alt=1). The emitted code
                                   *       is byte-identical to stock's 22-byte
                                   *       block at Rev 20 0x099F.
                                   * 0022: #179 -- selecting S/PDIF now HOLDS the
                                   *       slaved clock. A SET_CUR(rate) while the
                                   *       Selector is on S/PDIF re-runs mode 1
                                   *       instead of mode 2/3, so opening a
                                   *       stream no longer un-slaves the part
                                   *       and re-creates the 4.53 s slip. GET_CUR
                                   *       reports the host's own rate rather
                                   *       than 0,0,0.
                                   * 0021: #160 -- the Selector Unit is now
                                   *       ADVERTISED, not just answered: an
                                   *       S/PDIF input terminal (ID 6) and a
                                   *       Selector Unit (ID 5) on the path to
                                   *       the capture terminal, matching stock's
                                   *       own IDs and the kernel quirk.
                                   * 0020: #177 -- host-driven S/PDIF. Selector
                                   *       Unit 5 answers GET_CUR/SET_CUR,
                                   *       SET_CUR(rate=0) selects clock mode 1
                                   *       (ACGCTL=0x0D + CLOCKSOURCE=0x41),
                                   *       and the rate handlers write channel
                                   *       status. Mode 1 is NEVER the boot
                                   *       default -- see the note in
                                   *       streaming_set_rate().
                                   * 001F: #175 -- SET_INTERFACE(alt!=0) now
                                   *       posts WORK_BRINGUP, so a suspend no
                                   *       longer strands the CS8427 in reset.
                                   *       Plus optional per-unit iSerialNumber
                                   *       (make MBOX_UNIT=A|B).
                                   * 001E: #171 experiment, mute pair not raised
                                   *       (make MBOX_NO_MUTE_PAIR=1). Proved
                                   *       0x23.2/0x23.3 gate BOTH directions.
                                   * 001D: CPTCNF3 restored to 0xAC (0x001C
                                   *       destroyed playback -- BYOR SET is
                                   *       uniquely correct there). CPTRXCNF3
                                   *       0xA8 -> 0xAC: does the RX BYOR bit
                                   *       reach our capture path at all?
                                   * 001C: #161 experiment -- CPTCNF3 0xAC ->
                                   *       0xA8, clearing BYOR on the playback
                                   *       path so both directions match (and
                                   *       match stock's running state). Tests
                                   *       whether BYOR-TX does anything here.
                                   * 001B: #170 -- the codec control word's
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
#endif

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
/* The per-experiment counters, gathered into one struct so tlm_reset_counters()
 * can clear them with a single loop.
 *
 * Not cosmetic: these live in __data above 0x7F, where direct addressing
 * collides with SFR space, so SDCC reaches each one with `MOV R0,#addr;
 * MOV @R0,#0` -- four bytes and a reloaded pointer per store. Ten separate
 * assignments cost 111 bytes; one loop over a contiguous struct costs ~20.
 *
 * Only the per-EXPERIMENT counters belong here. tlm_stage, tlm_phases and
 * tlm_loop_count describe how this boot went rather than the current
 * experiment and are deliberately never cleared, so they stay separate --
 * being outside the struct is what makes that impossible to get wrong. */
struct tlm_ctrs {
    unsigned int  setup_count;
    unsigned int  iep0_count;
    unsigned int  chunks;
    unsigned int  drains;
    unsigned int  rstr_count;
    unsigned int  sof_count;
};
extern volatile __data struct tlm_ctrs tlm;

extern volatile __data unsigned int  tlm_loop_count;
extern volatile __data unsigned char tlm_stage;
extern volatile __data unsigned char tlm_phases;

/* Last SETUP packet seen (block 2) */
extern volatile __data unsigned char tlm_last_bmreq;
extern volatile __data unsigned char tlm_last_breq;
extern volatile __data unsigned int  tlm_last_wvalue;
extern volatile __data unsigned int  tlm_last_windex;
extern volatile __data unsigned int  tlm_last_wlength;

/* ACG clock measurement (block 3) — #186 stage 1.
 *
 * tlm_acg_window is the total MCLKO count over the last COMPLETED 1024-frame
 * window, so MCLK-per-frame is window/1024 and the device's clock error
 * against the host frame clock follows directly. Latched whole: a read cannot
 * catch a partial window and mistake it for a full one.
 *
 * tlm_acg_count proves the measurement is live and lets a host confirm two
 * reads span different windows. Saturates at 255 rather than wrapping, so a
 * long run cannot make it look freshly started. */
extern volatile __idata unsigned long tlm_acg_window;
extern volatile __idata unsigned int  tlm_acg_last;
extern volatile __idata unsigned char tlm_acg_count;
/* #186 stage 2 — feedback windows REJECTED as implausible. Non-zero after a
 * stream start is expected exactly once per rate change if the reset ever
 * regresses; steadily climbing means the clock is moving under the
 * accumulator and the reported rate is stale. */
extern volatile __idata unsigned char tlm_fb_rejects;

/* VECINT histogram (block 3), saturating at 255 */

/* Playback frame-alignment resyncs performed by streaming_sof(). Non-zero
 * means the playback DMA buffer was found holding a partial sample frame and
 * the path was torn down and restarted -- Rev 22's watchdog firing. A steadily
 * climbing count means something upstream keeps misaligning the stream. */

/* Completed suspend cycles — incremented in do_suspend() just before PCON
 * idle, so reading a non-zero value proves the device both entered and left
 * idle (a read is only possible once it is answering EP0 again). */


/* Host mux-set request outcomes (block 9). Two counters rather than one, so a
 * read distinguishes "the request never arrived" from "it arrived and was
 * rejected as an illegal pattern" -- indistinguishable from the mux word
 * alone, since a rejected request leaves it unchanged. */
extern volatile __data unsigned char tlm_mux_sets;
extern volatile __data unsigned char tlm_mux_rejects;

/* Peripheral init results (block 4) */

/* Port state sampled in main() before hw_init() touches the pins.
 *
 * NOVEL — reason: settles the boot-DFU button question empirically. The
 * claim that source-1 reads on P3.3 is an RE inference off Rev 20 and has
 * never been confirmed; check_boot_dfu_button() has never once fired. A
 * live read plus this boot-time sample tells us which bit actually moves
 * when the user holds the button, instead of another guess costing a
 * power cycle. Nothing in the boot ROM or Rev 20 records port state, so
 * there is no reference behaviour to copy here. */
/* tlm_p1_boot / tlm_p3_boot RETIRED in 0x002B. They existed to compare the
 * port latches at handoff against a live read, and that question is closed:
 * 48V is a mechanical switch that firmware never sees, mono is IRAM 0x23.6,
 * and the three source patterns are known (0x06 mic / 0x05 line / 0x03 inst).
 * Block 4 keeps the LIVE P1/P3 reads, which is what bench work uses. Retired
 * for the 18 bytes that put iSerialNumber back into an image that also has the
 * 96 kHz playback instrumentation -- being able to name WHICH unit a reading
 * came from outranks a settled comparison. */

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

/* SET_INTERFACE forensics. Sticky, because a host-side read always races
 * arecord's teardown back to alt 0. */
extern volatile __data unsigned char tlm_last_iface;
extern volatile __data unsigned char tlm_last_alt;

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
