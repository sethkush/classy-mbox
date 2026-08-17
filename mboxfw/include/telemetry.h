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
/* #199's TLM_SET_CLOCK_NO_RST (wIndexH == 0xD1 on SET_CLOCK) is REMOVED, and
 * its number is not reused as a modifier. It self-cleared after the handler
 * returned, so it could never affect the streaming_set_rate() that arecord's own
 * SET_CUR drives at stream open -- the moment under investigation. It was
 * superseded by #200's latched mask, which is itself now retired.
 *
 * TLM_REQ_DIAG_MODE, current meaning: RECALIBRATE AT THE NEXT STREAM OPEN.
 * bmRequestType 0x40; wValue and wIndex are ignored.
 *
 * #200's form -- wValue selecting which pair bits streaming_set_rate() cleared,
 * wIndex carrying structural flags, both read back in block 12 -- is GONE along
 * with block 12. It existed to reproduce the pre-fix transient on demand, it did
 * that, and the questions are closed (FINDING_197_RESOLVED, FINDING_202).
 *
 * What survives is worth its ~10 bytes: it is the only way to force a
 * recalibration without a power cycle, and a power cycle is a 2 km round trip.
 * It is what measured stock's per-open calibration against #201's calibrate-once
 * -- 2.2 dB of opening transient for 183 ms on every capture -- which is the
 * measurement that justifies the shipping behaviour. */
#define TLM_REQ_DIAG_MODE 0x17

/* 0x18 was TLM_REQ_FB_TUNE, the #215 bench knob that set IEPDCNTX2/IEPDCNTY2 at
 * runtime. RETIRED at #219, in build 0x0058. Its number is recorded here so it
 * is not reused, and so a reader of the 0x0056/0x0057 measurements can still
 * find what produced them.
 *
 * IT EXISTED FOR ONE QUESTION: does IEPDCNTX2 govern the emitted length at all,
 * given the device sent 9 bytes while the register said 3? Answering that with
 * a compile-time constant costs one flash and one 2 km round trip per value, so
 * the knob bought the whole sweep for one flash. It did its job -- the sweep is
 * the six-point table in FINDING_211 -- and #219 then explained the result from
 * the datasheet: IEPDCNTX/Y counts SAMPLES on an isochronous endpoint, and the
 * sample width is the BPS field of IEPCNF2 (§6.4.4.3, §6.4.4.6.2). Arming 1 is
 * one 3-byte sample, which is correct by construction. There is no remaining
 * question for a sweep to answer.
 *
 * WHAT IT NEVER TOUCHED, AND WHY THAT STILL STANDS: IEPBSIZ2. The feedback
 * buffer is 8 bytes at 0xFF20, EP_BSIZE() works in 8-byte units, so the next
 * size up is 16 -- which runs 0xFF20-0xFF2F and OVERLAPS THE SETUP PACKET
 * BUFFER AT 0xFF28. Corrupting that breaks every control transfer, i.e. the
 * device stops answering and needs a bench visit. Anything that wants a wider
 * feedback packet must RELOCATE the buffer first, and must re-cut IEPCNF2's BPS
 * with it (#219). That is a change designed on its own, not bolted to a knob.
 *
 * tools/fbsweep.sh is kept, with a header saying what to restore to run it. */

/* #226 -- PROVISIONING. Present ONLY in `make MBOX_PROVISION=1` builds, which
 * are never shipped and never left on a unit. Both requests are DEVICE
 * recipient, like every other vendor request here and for the same reason.
 *
 * WHY THESE EXIST AT ALL. #221 wants each unit to serve the serial printed on
 * its case, from one image, so the record has to be written per-unit and has to
 * SURVIVE reflashing. It does: dfuDnloadData() seeds dataRemain from the
 * header's payloadSize and refuses the byte after it (errFILE), so a flash
 * touches offsets 18..payloadSize and nothing above -- 0x1F00 is unreachable
 * BY a flash, which is exactly the property provisioning needs. The route
 * mkserial.py took -- append the record to the image and let DFU write it --
 * is refuted by the same code; see FINDING_226.
 *
 * WHY THE HOST CANNOT NAME AN ADDRESS. wValue carries an OFFSET INTO THE
 * RECORD, not an EEPROM address. The firmware rejects offset >= the record
 * length and supplies the 0x1F00 base itself, so no value any host can send
 * reaches the header at 0x0000 or the payload below 0x1792. That check is here
 * and not in the host tool deliberately: the host tool is the component most
 * likely to be wrong, and tonight's errFILE is what that looks like when the
 * boot ROM is there to refuse. From the running app nothing would refuse it --
 * an unbounded write is a real brick, from software, with no bench visit
 * possible (FINDING_226, "The one thing that must not be got wrong").
 *
 * TLM_REQ_PROV_WRITE  bmRequestType 0x40
 *   wValue low  = offset into the record, 0 .. SERIAL_HDR_LEN+SERIAL_MAX_CHARS-1
 *   wIndex low  = byte to write
 *   Stalls on an out-of-range offset, and stalls if a write is still pending.
 *
 * The write itself runs in the MAIN LOOP, not in the SETUP handler, because
 * the 24C64 program cycle is a ~5 ms busy-wait and the handler runs in
 * interrupt context -- the same reason TLM_REQ_ENTER_DFU latches rather than
 * writing inline. The busy latch is what makes the host's pacing safe: a
 * second request arriving before the first has landed is STALLed, not
 * silently dropped, so the tool retries instead of losing a byte.
 *
 * TLM_REQ_PROV_READ   bmRequestType 0xC0, wValue low = offset, returns 8 bytes
 *   Reads back TLM_BLOCK_SIZE bytes from the record region so the tool can
 *   verify what actually landed without a power cycle. Bounded the same way,
 *   against offset + 8 rather than offset. */
#define TLM_REQ_PROV_WRITE 0x19
#define TLM_REQ_PROV_READ  0x1A
/* 0x1B — instrumented single-byte read, provisioning builds only. Returns
 * I2CSTA at each step of one transaction plus the byte, so a failing read
 * says WHERE it failed. wIndex low forces the bus frequency first. See
 * eeprom_read_diag() in eeprom.c. */
/* 0x1B/0x1C were TLM_REQ_PROV_DIAG / PROV_DIAGRD, the #226 instrument that
 * returned I2CSTA at every step of one EEPROM read. RETIRED in build 0x0060,
 * having answered. Numbers recorded so they are not reused.
 *
 * IT EXISTED FOR ONE QUESTION -- why did every EEPROM read return 0x00? -- and
 * the answer was not in the I2C code at all: the destination buffer was an
 * __xdata global, which SDCC places at XDATA 0x0001, an address this board does
 * not implement. With the buffer in internal RAM the existing TI-derived
 * sequence read the header correctly on the first attempt (0x0001..0x0004 ->
 * 12 12 34 0D). It also settled a second question: a byte-for-byte port of
 * stock Rev 20 i2c_eeprom_read_byte (CODE:0cdd) does NOT work here -- it never
 * gets RCV_DATA_FULL -- so TI's version is the right one to keep.
 *
 * Both answers are recorded in eeprom.c and serialno.c, so the instrument has
 * no remaining job, and its style-switch cost four SFR-write patterns that only
 * ever existed in a bench image. */

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

/* 0x16 was TLM_REQ_GATE_PROBE, the #197 diagnostic that drove the capture gate
 * from a chosen execution context so the device's own 48 kHz ADC could witness
 * whether the codec ACCEPTED the word -- the question telemetry structurally
 * cannot answer, since block 9 mirrors what firmware wrote and never what the
 * shift register latched.
 *
 * It shipped in build 0x0041 only, and it went in labelled DIAGNOSTIC, NOT A
 * FEATURE, to be removed once it had answered. It has: host, ISR-context and
 * main-loop publishes all land, the main-loop arm repeatable to +/-0.2 ms
 * across three runs and two units. See FINDING_197, "2026-08-07".
 *
 * Removed for two reasons. It is a trap -- it can mute capture from the host
 * with no ALSA control to show for it, which is exactly the state someone
 * debugging silence would waste a day on. And its 96 bytes are what the
 * per-unit serial descriptors cost: 0x0041 fit only by dropping them, which
 * left enter_dfu_serial.py unable to name a unit and BENCH_WIRING.md's "trust
 * the serial" rule unenforceable. Instruments that disable the bench's own
 * identification are not worth their measurement for longer than the
 * measurement takes.
 *
 * Not reused, per the rule above: a request code that changes meaning between
 * builds is the trap BLOCK_RETIRED exists to prevent for telemetry indices. */

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
#define TLM_BUILD_ID     0x0061   /* 0061: #228 -- the Selector Unit describes what
                                   *       the hardware makes GLOBAL and nothing
                                   *       else. Two positions, analog and
                                   *       S/PDIF; the front-panel buttons own
                                   *       which analog front end feeds the ADC,
                                   *       per channel, which is the only place
                                   *       that choice can honestly live.
                                   *
                                   *       #203's instrument and #224's
                                   *       microphone positions are GONE, with
                                   *       their Input Terminals and strings.
                                   *       Their measurements stand and are why
                                   *       the analog path is declared at all;
                                   *       what was wrong was letting a host
                                   *       SET them, which forced both channels
                                   *       to one front end and silently threw
                                   *       away a per-channel setting made at
                                   *       the unit.
                                   *
                                   *       The analog terminal is retyped 0x0601
                                   *       ANALOG CONNECTOR from 0x0603 LINE:
                                   *       the panel decides which connector is
                                   *       live and the host is not told, so
                                   *       naming one would be the same class of
                                   *       lie #225 fixed.
                                   *
                                   *       EP 0x83 RETIRED with it (#207/#214).
                                   *       Its only producer was the button
                                   *       notification, and no button can move
                                   *       a two-position analog/SPDIF control.
                                   *       #227's stale-Core-Audio problem is
                                   *       dissolved rather than fixed: nothing
                                   *       the panel does is host-visible, so
                                   *       there is nothing to go stale.
                                   *
                                   *       5865 -> 5596 bytes.
                                   *
                                   * 0060: #226 -- eeprom_read_seq() REWRITTEN 0060: #226 -- eeprom_read_seq() REWRITTEN
                                   *       against TI's I2c.c. Every sequential
                                   *       read returned 0x00, including from
                                   *       EEPROM that had never been written
                                   *       (a blank 24C64 reads 0xFF, which is
                                   *       what said the READ path was at fault
                                   *       and not the writes). Cause: the dummy
                                   *       I2CDATO write was issued once per
                                   *       iteration; TI issues it ONCE, before
                                   *       the loop, and it is READING I2CDATI
                                   *       that clocks each further byte. Also
                                   *       restores TI's CLEAR_ALL between the
                                   *       sub-address and the read address, and
                                   *       arms STOP_READ when TWO bytes remain
                                   *       rather than one.
                                   *
                                   *       This had never been exercised: #221
                                   *       shipped the read side before any
                                   *       record existed to read, so the
                                   *       boot-time serial read has never once
                                   *       worked on hardware.
                                   *
                                   *       Also adds the provisioning requests
                                   *       (0x19/0x1A) in MBOX_PROVISION builds.
                                   *
                                   *       NOTE 0x0059 -> 0x0060: the id must
                                   *       stay valid BCD, because bcdDevice
                                   *       carries it (#222). 0x005A is not.
                                   *
                                   * 0059: #224 declares the MICROPHONE input
                                   *       (Selector position 4, terminal type
                                   *       0x0201) after the XLR path was
                                   *       measured -- 69.9 dB at 1234 Hz
                                   *       against an un-miked control channel.
                                   *       #225 fixes a defect that fix exposed:
                                   *       the Selector answered POSITION 1
                                   *       (LINE) while hw_init leaves the mux
                                   *       on MIC, so every host was told the
                                   *       wrong capture source from power-on
                                   *       until something moved the selector.
                                   *       All four inputs are now declared and
                                   *       reported honestly.
                                   *       NOTE: bcdDevice carries this id
                                   *       (#222), so it must stay valid BCD --
                                   *       the id after 0x0059 is 0x0060.
                                   * 0058: #219 -- the 3x is EXPLAINED, and the
                                   *       scaffolding built to chase it is
                                   *       gone. IEPDCNTX/Y counts SAMPLES on an
                                   *       isochronous endpoint, not bytes, and
                                   *       IEPCNF2's BPS field sets the width:
                                   *       0xC2 is BPS 2 = 3 bytes per sample,
                                   *       so emitted = armed x 3. Confirmed on
                                   *       a second endpoint with a different
                                   *       BPS -- capture is 0xC5, BPS 5, and
                                   *       measures 48 x 6 = 288 B/frame.
                                   *       AUDIO_FEEDBACK_ARM stays 1, now
                                   *       correct by construction rather than
                                   *       by measurement. TLM_REQ_FB_TUNE and
                                   *       MBOX_FB_TUNE are RETIRED with the
                                   *       question they existed to answer.
                                   *       NO BEHAVIOUR CHANGE ON THE WIRE.
                                   * 0057: #211 FIXED -- the feedback endpoint
                                   *       delivers. AUDIO_FEEDBACK_ARM = 1,
                                   *       because the UBM emits THREE bytes per
                                   *       unit armed (measured 1/2/3/4/6/8 ->
                                   *       3/6/9/12/18/24). TI's SoftPll.c arms
                                   *       3, we copied it, and that is why the
                                   *       endpoint sent 9 bytes against a
                                   *       wMaxPacketSize of 3 and babbled on
                                   *       every packet since it was declared.
                                   *       Verified before the change by arming
                                   *       1 over the wire: 1005 packets, all
                                   *       status 0, exactly 3 bytes, and the
                                   *       host then VARIES its playback packet
                                   *       sizes instead of sending a fixed
                                   *       nominal -- the loop is closed.
                                   * 0056: instruments, no behaviour change.
                                   *       #215: TLM_REQ_FB_TUNE sets the
                                   *       feedback endpoint's byte count at
                                   *       runtime, so #211's open question --
                                   *       does IEPDCNTX2 govern the emitted
                                   *       length at all, given the device sends
                                   *       9 while it says 3 -- can be swept in
                                   *       ONE flash instead of one per value.
                                   *       IEPBSIZ2 is deliberately NOT exposed:
                                   *       the next size up overlaps the setup
                                   *       packet buffer at 0xFF28.
                                   *       #209: the stall + wLen0 counters, and
                                   *       a block 4 case in the RELEASE reader
                                   *       -- the release tier is the shipping
                                   *       tier now, so without that the
                                   *       counters would be write-only, which
                                   *       is what retired block 4 originally.
                                   *       Both behind MBOX_TLM_STALL=1.
                                   * 0055: TWO CONFORMANCE FIXES, #212 + #214.
                                   *       #212: SET_ADDRESS took any wValueL
                                   *       with no range check. ch9addr.ko sent
                                   *       200, the device ACCEPTED it (§9.4.6
                                   *       makes >127 a Request Error) and then
                                   *       stopped answering at its old address
                                   *       until a port cycle. Now stalls >127.
                                   *       #214: ENDPOINT_HALT is mandatory on
                                   *       an INTERRUPT endpoint and we stalled
                                   *       it. #188's "all our endpoints are
                                   *       isochronous" was true when written
                                   *       and expired when #207 added EP 0x83.
                                   *       Per-endpoint: 0x81/0x02/0x82 keep
                                   *       stalling it, correctly (§9.4.5).
                                   *       6011 of 6016 -- which is why
                                   *       MBOX_TLM_STALL=1 does NOT build in
                                   *       this tree. #209's counter and #214
                                   *       cannot both fit, and a fix that
                                   *       affects real hosts outranks an
                                   *       instrument for a compliance footnote.
                                   *       #208 REVERTED here too, and #209's
                                   *       instrument exists behind the flag.
                                   *       The two wLen==0 early-returns are out
                                   *       of stage_reply()/stage_immediate():
                                   *       they changed nothing on the wire,
                                   *       because the device sees only the 8
                                   *       SETUP bytes and cannot distinguish
                                   *       arming a ZLP from arming nothing.
                                   *       In their place, MBOX_TLM_STALL=1
                                   *       restores block 4 as reply_stall() and
                                   *       wLength==0 dispatch counters -- read
                                   *       as a DELTA across the one request,
                                   *       they place the stall on one side of
                                   *       the firmware boundary or the other.
                                   *       25 bytes, and only when asked for.
                                   *       See FINDING_208.
                                   * 0054: FIVE FEATURES, first hardware run.
                                   *       #203 Selector gains position 3 =
                                   *            INSTRUMENT (Line 1, S/PDIF 2
                                   *            unchanged -- appended, not
                                   *            renumbered).
                                   *       #204 terminal names on all five
                                   *            terminals, strings 4..8.
                                   *       #205 release keeps telemetry block 0.
                                   *       #207 UAC1 status interrupt on EP3 IN,
                                   *            raised by a front-panel press.
                                   *            Capture buffer 640 -> 632 to pay
                                   *            for its 8 bytes.
                                   *       #208 wLength=0 arms no IN packet.
                                   *            THE RISKIEST OF THE FIVE -- it is
                                   *            the only one that can wedge
                                   *            enumeration, so check EP0 first.
                                   *       Block 11 retired; blocks 1/2 behind
                                   *       MBOX_TLM_FULL, block 9 behind
                                   *       MBOX_TLM_ROUTING.
                                   * 0053: #202 and #202b REVERTED. Both attempts
                                   *       to spend the calibration without a
                                   *       host stream are refuted by
                                   *       measurement, so this is 0x004F's
                                   *       behaviour exactly -- the verified
                                   *       floor. 0x0052 corrected 0x0051's
                                   *       too-short window and still measured
                                   *       8774 lead zeros, so the self-driven
                                   *       capture produces no LRCK at all.
                                   *       FINDING_202_the_cport_does_not_free_run.md
                                   * 0051: #202b -- RUN OUR OWN CAPTURE at 30 s so
                                   *       the calibration is CLOCKED with no
                                   *       host streaming. #202 proved raising
                                   *       RST alone does nothing: tRTV is 8960
                                   *       LRCK EDGES and LRCK does not run
                                   *       between streams. Target: EVERY
                                   *       capture clean, including the first.
                                   * 0050: #202 -- spend the calibration with NO
                                   *       stream open, so the FIRST capture of a
                                   *       power-up is clean too. 0x23.2 is held
                                   *       high once g_ref_settled, so a stream
                                   *       open no longer produces an RST edge.
                                   *       Tests FINDING_197_RESOLVED #3 ("the
                                   *       C-port idles when no stream is open"),
                                   *       which contradicts CPTBLK=0 in our own
                                   *       CPTCNF4=0x03. If #3 is right the first
                                   *       capture still shows ~8800 lead zeros --
                                   *       a null, not a regression.
                                   * 004F: #201 -- RECLAIM THE 183 ms. Calibrate at
                                   *       every stream open UNTIL one lands with
                                   *       the analog reference settled, then stop.
                                   *       Measured: after one settled calibration,
                                   *       five captures over 185 s with calibration
                                   *       disabled were all clean, 0 lead zeros,
                                   *       floors -103.7 dBFS. So the per-open
                                   *       calibration was never needed for
                                   *       correctness -- only a good one was.
                                   *       Stock pays the 183 ms on EVERY capture
                                   *       (Rev 20 0x072F, Rev 22 0x0716, above the
                                   *       mode dispatch, so unconditional); this is
                                   *       better than stock, not a restoration of
                                   *       it. #200's runtime diagnostics retired to
                                   *       pay for it -- the captured audio is a
                                   *       better instrument than they were.
                                   * 004E: #200 -- g_diag_clr_mask, LATCHED. Selects
                                   *       which pair bits set_rate clears, so the
                                   *       pre-fix condition can be reproduced at
                                   *       RUNTIME (mask 0x00) and compared against
                                   *       shipping (0x0C) in the same power-up.
                                   *       Every probe before this returned null
                                   *       because a calibrated part has nothing to
                                   *       reveal. Replaces #199's self-clearing
                                   *       flag, which could not survive the
                                   *       set_rate that arecord's SET_CUR drives.
                                   *       Block 12 reports mask + RST cycles.
                                   *       The structural arms did not fit and are
                                   *       ABSENT rather than inert. 5997/6016.
                                   * 004D: #199 bench diagnostic -- SET_CLOCK with
                                   *       wIndexH == 0xD1 reprograms the clocks
                                   *       with the ADC's RST left HIGH. Supplies
                                   *       the one arm neither shipping build can.
                                   *       Shipping behaviour unchanged for every
                                   *       other wIndexH.
                                   *       NOT a re-flash of 004C: 004C is already
                                   *       recorded in the findings as the plain
                                   *       revert, and a build id meaning two
                                   *       different images is the trap this list
                                   *       exists to prevent. 004C was never
                                   *       flashed to anything.
                                   * 004C: 004B REVERTED -- a cold boot proved the
                                   *       conditional makes a bad boot calibration
                                   *       permanent. Unconditional, as stock.
                                   * 004B: reprogram the clock ONLY when the mode
                                   *       changed. REGRESSION, see streaming.c.
                                   * 004A: restore stock's CLR of the mute pair
                                   *       before clock reprogramming -- the ADC
                                   *       offset calibration. Pulse deleted.
                                   * 0049: boot self-capture REVERTED, measured
                                   *       not to work. Same behaviour as 0047.
                                   * 0048: #198 stage 2 -- boot self-capture, for
                                   *       a clean FIRST take.
                                   * 0047: sof_count WRAPS -- it was saturating
                                   *       at 65535, so no SOF wait could elapse
                                   *       after 65 s of uptime.
                                   * 0046: block 11 now reports #198 pulse state.
                                   * 0045: pulse on EVERY capture bring-up --
                                   *       diagnostic, proves the mechanism.
                                   * 0044: #198 -- dwell + re-arm, after 0043's
                                   *       one-shot was eaten by the bind-time alt.
                                   * 0043: #198 -- pulse moved from boot to the
                                   *       first capture bring-up.
                                   * 0042: gate probe removed, serials back.
                                   * 0041: #197 gate probe -- the ADC as the
                                   *       instrument. 0040: #197 v8 -- INTERRUPTS OFF across each
                                   *       codec publish. codec_write_word()
                                   *       bit-bangs P1; a host SET_CUR runs it
                                   *       inside isr_int0 where nothing
                                   *       preempts it, while the same call
                                   *       from the main loop can be preempted
                                   *       mid-shift. Telemetry could not see
                                   *       it -- block 9 reports our MIRROR of
                                   *       the word, not what the chip latched.
                                   * 003F: #197 v7 -- drive the mute through
                                   *       g_host_mute + codec_apply_mute(),
                                   *       which is EXACTLY what a host SET_CUR
                                   *       does. Hand-written writes with the
                                   *       same bits only reached -61 dBFS
                                   *       where the host path reaches -101, on
                                   *       the same unit in the same boot.
                                   * 003E: #197 v6 -- drop the WHOLE PAIR, not
                                   *       just the capture gate. 0x003D fired
                                   *       (phase bit proved it) and only got
                                   *       -41.8 -> -62.2 dBFS; every host
                                   *       pulse that reached -101 went through
                                   *       codec_apply_mute() with
                                   *       g_path_enabled == 0, which drops
                                   *       both bits.
                                   * 003D: #197 v5 -- delay to 8 s, and REPORT it.
                                   *       Four builds could not distinguish
                                   *       "the pulse never fired" from "it
                                   *       fired and did not work", so block 0
                                   *       now carries TLM_PHASE_ADC_PULSE.
                                   * 003C: #197 v4 -- the clocks must have been
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
/* #197. Set when the capture-gate pulse has actually fired. Four builds were
 * flashed without any way to tell "the pulse did not fire" from "it fired and
 * did not work", and each cost a power cycle to learn nothing. One bit ends
 * that. */
/* 0x40 was TLM_PHASE_ADC_PULSE, set when the #197/#198 capture-gate pulse
 * fired. Removed 2026-08-08 with the pulse itself: the transient it existed to
 * suppress was mboxfw dropping stock's CLR of the pair before the clock
 * reprogramming (Rev 20 fcn.0x0728 @ 0x072F/0x0731, Rev 22 fcn.0x070F @
 * 0x0716/0x0718), which left the AK5383's offset calibration never triggered.
 * Restoring that write makes the pulse redundant. Bit not reused. */

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
#ifdef MBOX_TLM_STALL
    /* #209. Block 4's stall counter, restored exactly as its retirement note
     * said it could be: "one struct byte plus one TLM_INC8".
     *
     * These two answer one question and are useless apart. `stalls` counts
     * every reply_stall() -- there are 24 call sites and it does not say which
     * -- and `gd_wlen0` counts arrivals at handle_get_descriptor() carrying
     * wLength == 0. The wLength=0 request is issued ONCE, against a quiet
     * device, so the pair reads unambiguously:
     *
     *   stalls +1, gd_wlen0 +1  -> our code stalls it. Fixable, and the search
     *                              narrows to the paths a GET_DESCRIPTOR can
     *                              reach.
     *   stalls  0, gd_wlen0 +1  -> firmware handled it and the UBM stalled
     *                              anyway. A silicon limitation; document it
     *                              and close #192's last divergence as
     *                              won't-fix.
     *   stalls  0, gd_wlen0  0  -> the MCU never dispatched it, despite the
     *                              SETUP arriving. Contradicts the setup_count
     *                              result by a route that does not depend on
     *                              the off-by-one I already got wrong once.
     *
     * Both are unsigned char and saturate at 0xFF via TLM_INC8. Saturation is
     * the right behaviour here: a wrapped counter reading 0 is exactly the
     * "null from an instrument that was never connected" this project keeps
     * being bitten by, and row 3 above is a genuine 0. */
    unsigned char stalls;
    unsigned char gd_wlen0;
#endif
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

/* Saturating increments — a counter that wraps mid-experiment reads as a
 * smaller number than reality and would silently corrupt a measurement. */
#define TLM_INC8(c)   do { if ((c) < 0xFF)   (c)++; } while (0)

/* #209, and OFF by default. The two counters sit on the two hottest paths in
 * the firmware -- reply_stall() and handle_get_descriptor() -- so they are
 * macros that vanish entirely rather than `if`s that cost a compare per SETUP
 * in every build that is not running the experiment. */
#ifdef MBOX_TLM_STALL
#define TLM_INC_STALL()   TLM_INC8(tlm.stalls)
#define TLM_INC_WLEN0()   TLM_INC8(tlm.gd_wlen0)
#else
#define TLM_INC_STALL()   do { } while (0)
#define TLM_INC_WLEN0()   do { } while (0)
#endif
#ifdef MBOX_RELEASE
/* RELEASE: the saturating counters exist only to be read back over EP0, and the
 * block reader that reads them is compiled out. Keeping the increments would
 * cost code in the SETUP and IEP0 paths to maintain values nothing can observe.
 * The struct itself stays -- it is DATA, not CODE, and DATA is not the
 * constrained resource here. */
#define TLM_INC16(c)  do { } while (0)
#else
#define TLM_INC16(c)  do { if ((c) < 0xFFFF) (c)++; } while (0)
#endif

/* SURVIVES MBOX_RELEASE, deliberately, and only this one counter.
 *
 * rstr_count is not diagnostic trivia -- it is the ONLY way this project can
 * tell a bus reset from a cold boot. Re-enumeration looks identical either way;
 * the established proof is a counter going DOWN, and that has been needed on
 * every power-cycle question so far (uhubctl -a cycle, -a off with a dwell, and
 * a full host reboot -- all three of which turned out NOT to drop VBUS, and all
 * three of which were settled by this number).
 *
 * A release image with no rstr_count would make every future cold-boot question
 * unanswerable without reflashing a diagnostic build first, which costs a power
 * cycle to ask a question about power cycles. Sixteen bytes is a bargain. */
#define TLM_INC16_KEEP(c)  do { if ((c) < 0xFFFF) (c)++; } while (0)

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
