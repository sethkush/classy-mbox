// mboxflash — reflash the original Digidesign Mbox from arm64 macOS.
//
// Usage:
//   mboxflash --probe              print connected Mbox's bcdDevice, exit
//   mboxflash --enter-dfu          send just the custom detach request, exit
//   mboxflash --parse PATH         parse a payload blob (autodetects the
//                                   record-stream start offset)
//   mboxflash --scan PATH          list all offsets with runs of >= 4 records
//   mboxflash --flash PATH         full flash cycle (not yet implemented —
//                                   needs verification against a real
//                                   DFU-mode device to sort out the exact
//                                   chunking rules)

#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/usb/IOUSBLib.h>
#import <IOKit/IOCFPlugIn.h>
#import "dfu.h"
#import "payload.h"

static void die(NSString *msg, NSError *err) {
    fprintf(stderr, "mboxflash: %s\n", msg.UTF8String);
    if (err) fprintf(stderr, "  error: %s\n", err.localizedDescription.UTF8String);
    exit(1);
}

// Read a Digi (VID=0x0DBA) device's idProduct+bcdDevice. Returns 0 if
// found (with *outPid and *outBcd populated), -1 if no matching device.
// Historic bug: earlier version only checked VID and reported anything
// as "audio mode" — misidentified app-DFU (0x0DBA:0x1001) devices and
// created the illusion of a "2 flashes needed" bootstrap pattern.
static int probeDigiDevice(int *outPid, int *outBcd) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) return -1;
    io_iterator_t it = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &it) != KERN_SUCCESS) return -1;
    io_service_t svc = IO_OBJECT_NULL;
    io_service_t cand;
    while ((cand = IOIteratorNext(it))) {
        CFNumberRef vid = (CFNumberRef)IORegistryEntrySearchCFProperty(cand,
            kIOServicePlane, CFSTR("idVendor"), NULL, kIORegistryIterateRecursively);
        int v = 0;
        if (vid) { CFNumberGetValue(vid, kCFNumberIntType, &v); CFRelease(vid); }
        if (v == 0x0DBA) { svc = cand; break; }
        IOObjectRelease(cand);
    }
    IOObjectRelease(it);
    if (!svc) return -1;
    CFNumberRef pid = (CFNumberRef)IORegistryEntrySearchCFProperty(svc,
        kIOServicePlane, CFSTR("idProduct"), NULL, kIORegistryIterateRecursively);
    CFNumberRef bcd = (CFNumberRef)IORegistryEntrySearchCFProperty(svc,
        kIOServicePlane, CFSTR("bcdDevice"), NULL, kIORegistryIterateRecursively);
    if (pid) { int v = 0; CFNumberGetValue(pid, kCFNumberIntType, &v); *outPid = v; CFRelease(pid); }
    if (bcd) { int v = 0; CFNumberGetValue(bcd, kCFNumberIntType, &v); *outBcd = v; CFRelease(bcd); }
    IOObjectRelease(svc);
    return 0;
}

static int probeBcdDevice(void) {
    int pid = 0, bcd = 0;
    if (probeDigiDevice(&pid, &bcd) < 0) return -1;
    return bcd;
}

// Return YES if a DFU-mode Mbox is present (VID 0xFFFF PID 0xFFFE
// with bDeviceClass 0xFE). This is what the Mbox becomes after
// booting with a front-panel source button held down.
static BOOL probeDFUMode(void) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) return NO;
    CFDictionarySetValue(match, CFSTR(kUSBVendorID),  (__bridge CFNumberRef)@(0xFFFF));
    CFDictionarySetValue(match, CFSTR(kUSBProductID), (__bridge CFNumberRef)@(0xFFFE));
    io_iterator_t it = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &it) != KERN_SUCCESS) return NO;
    io_service_t svc = IOIteratorNext(it);
    IOObjectRelease(it);
    if (!svc) return NO;
    CFNumberRef cls = (CFNumberRef)IORegistryEntrySearchCFProperty(svc,
        kIOServicePlane, CFSTR("bDeviceClass"), NULL, kIORegistryIterateRecursively);
    BOOL isDFU = NO;
    if (cls) {
        int v = 0;
        CFNumberGetValue(cls, kCFNumberIntType, &v);
        isDFU = (v == 0xFE);
        CFRelease(cls);
    }
    IOObjectRelease(svc);
    return isDFU;
}

static int cmd_probe(void) {
    if (probeDFUMode()) {
        printf("Mbox connected in bulletproof-DFU mode (VID 0xFFFF PID 0xFFFE, class 0xFE)\n");
        printf("  Boot-ROM DFU. Only header persists on flash — use safety_net_bootstrap.bin\n");
        printf("  to transition to app-DFU (0x0DBA:0x1001), then flash real firmware.\n");
        return 0;
    }
    int pid = 0, bcd = 0;
    if (probeDigiDevice(&pid, &bcd) < 0) {
        fprintf(stderr, "no Mbox found (neither 0x0DBA:0x1000/0x1001 nor 0xFFFF:0xFFFE)\n");
        return 1;
    }
    if (pid == 0x1001) {
        printf("Mbox connected in app-DFU mode (VID 0x0DBA PID 0x1001), bcdDevice = 0x%04x\n", bcd);
        printf("  Running firmware presents DFU class. Flash real firmware here — code\n");
        printf("  WILL persist to EEPROM. Use --flash PATH.\n");
        return 0;
    }
    if (pid == 0x1000) {
        fprintf(stdout, "Mbox 1 connected in audio mode (VID 0x0DBA PID 0x1000), bcdDevice = 0x%04x (firmware ", bcd);
        if      (bcd == 0x0022) fprintf(stdout, "v22 — OK, no flash needed)\n");
        else if (bcd == 0x0020) fprintf(stdout, "Rev 20 — BUGGY, should flash to v22)\n");
        else if (bcd == 0x0016 || bcd == 0x0018 || bcd == 0x0019)
            fprintf(stdout, "very old, %u.%u — should flash to at least Rev 20)\n",
                bcd >> 8, bcd & 0xff);
        else if (bcd == 0x0100) fprintf(stdout, "mboxfw v1.0 — custom)\n");
        else fprintf(stdout, "unknown 0x%04x)\n", bcd);
        // ORDER MATTERS — this reflects what actually works on hardware,
        // not what the docs imply.
        //
        // 1. BUTTON HOLD is the working path on stock firmware. Hold a
        //    front-panel source button while plugging in. Confirmed
        //    repeatedly on this unit by Seth; it is the primary way this
        //    device gets into DFU.
        //
        // 2. --enter-dfu (Digi class request bmReq 0x21 / bReq 0x00 /
        //    wValue 0x000A) has NEVER been made to work here. The request
        //    is accepted but the device does not drop to DFU. Do not
        //    present it as the normal route.
        //
        // 3. SDA short is the last resort, and lands in bulletproof DFU
        //    (0xFFFF:0xFFFE) which persists headers only — hence the
        //    two-stage bootstrap.
        //
        // Caveat worth keeping visible: the button-hold mechanism is NOT
        // located in Rev 20's application code. rev20_STARTUP_TRACE.md
        // shows its boot path only setting P3 = 0xFF for the input
        // pull-ups, and p3_button_scan (0x0ED5) runs from the main loop
        // for source cycling. So the trigger most likely lives in the
        // boot ROM, which we have not traced for it. Our most-relied-on
        // recovery path is the one we understand least — see
        // rev20_boot_rom_audit.md.
        //
        // BRICK_LOG.md entries reporting "button-hold did nothing" are
        // all about BRICKED mboxfw, whose broken I2C driver made every
        // software recovery path fail. They say nothing about stock
        // firmware. An earlier version of this hint got that backwards.
        printf("\nTo flash: hold a front-panel source button while plugging the Mbox\n");
        printf("in. Device re-enumerates in DFU mode. Then `mboxflash --probe`.\n");
        printf("\nFallback if that fails: short EEPROM SDA during power-up to reach\n");
        printf("bulletproof DFU (0xFFFF:0xFFFE), then use the two-stage bootstrap.\n");
        return 0;
    }
    fprintf(stderr, "Digi device present but PID=0x%04x is unrecognized (bcdDevice=0x%04x)\n", pid, bcd);
    return 1;
}

static int cmd_enter_dfu(void) {
    // Skip the bcdDevice probe — a half-enumerated device (mboxfw stuck
    // mid-SETUP, boot ROM waiting on descriptors, etc.) may not expose
    // bcdDevice, but we can still try to open it by VID + send the class
    // request. If it's completely absent we'll fail loudly in openMboxDevice.
    (void)probeBcdDevice;
    NSError *err = nil;
    if (!DFU_SendEnterDFURequest(0, &err)) die(@"enter-DFU request failed", err);
    printf("enter-DFU request sent. Device should disconnect and re-enumerate\n");
    printf("in DFU mode. Wait a second, then run:\n");
    printf("    ioreg -p IOUSB -l | grep -B2 -A20 -i digidesign\n");
    printf("to see what descriptors the device now advertises.\n");
    return 0;
}

static int cmd_descdump(void) {
    NSError *err = nil;
    if (!DFU_DescriptorProbe(&err)) die(@"descriptor probe failed", err);
    return 0;
}

static int cmd_parse(const char *path) {
    NSError *err = nil;
    NSData *blob = [NSData dataWithContentsOfFile:@(path) options:0 error:&err];
    if (!blob) die([NSString stringWithFormat:@"could not read %s", path], err);

    NSUInteger startOff = 0;
    BOOL found = MBoxPayload_Autodetect(blob, &startOff);
    if (found)  printf("autodetected payload start at 0x%lx\n\n", (unsigned long)startOff);
    else        { printf("no known Mbox 1 firmware signature found; parsing from 0x0\n\n"); startOff = 0; }

    NSArray<MBoxPayloadRecord *> *recs = MBoxPayload_Parse(blob, startOff);
    NSUInteger realCount = 0, realBytes = 0, ffCount = 0;
    uint32_t maxAddr = 0;
    for (NSUInteger i = 0; i < recs.count; i++) {
        MBoxPayloadRecord *r = recs[i];
        BOOL all_ff = YES;
        const uint8_t *b = r.data.bytes;
        for (uint32_t j = 0; j < r.length; j++) if (b[j] != 0xff) { all_ff = NO; break; }
        if (all_ff) { ffCount++; continue; }
        realCount++;
        realBytes += r.length;
        if (r.address + r.length > maxAddr) maxAddr = r.address + r.length;
        if (realCount <= 8 || i >= recs.count - 4) {
            printf("  [%3lu] @0x%05lx  addr=0x%04x  len=%3u  type=%u  data-start=",
                   (unsigned long)i, (unsigned long)r.fileOffset,
                   r.address, r.length, r.type);
            for (uint32_t j = 0; j < 8 && j < r.length; j++) printf("%02x", b[j]);
            printf("\n");
        } else if (realCount == 9) {
            printf("  ...\n");
        }
    }
    printf("\n");
    printf("%lu records total: %lu real (%lu bytes), %lu FF-fill\n",
           (unsigned long)recs.count, (unsigned long)realCount,
           (unsigned long)realBytes, (unsigned long)ffCount);
    printf("EEPROM addressable range: 0x0000 .. 0x%04x\n", maxAddr);
    return 0;
}

// Static (no-device-needed) validation of a wrapped firmware image.
// Complements the pre-flash workflow: catches file-format bugs (wrong
// chksum, misaligned records, oversize payload) before you touch the
// hardware. Independent of --flash-check which needs a DFU device.
//
// Checks:
//   1. autodetect signature at offset 0
//   2. every record: length=32, type=0, address = i*32, contiguous
//   3. header self-consistency: sig bytes, VID, chksum, payloadSize
//   4. code + header + trailing FF-fill ≤ 8 KB (TAS1020A EEPROM budget)
static int cmd_validate(const char *path) {
    NSError *err = nil;
    NSData *blob = [NSData dataWithContentsOfFile:@(path) options:0 error:&err];
    if (!blob) die([NSString stringWithFormat:@"could not read %s", path], err);

    NSUInteger startOff = 0;
    if (!MBoxPayload_Autodetect(blob, &startOff)) {
        fprintf(stderr, "FAIL: autodetect signature not found — file is not a"
                        " wrapped Mbox firmware\n");
        return 1;
    }
    if (startOff != 0) {
        fprintf(stderr, "WARN: autodetect matched at offset 0x%lx (expected 0)\n",
                (unsigned long)startOff);
    }
    printf("  autodetect signature   OK  (offset %lu)\n", (unsigned long)startOff);

    NSArray<MBoxPayloadRecord *> *recs = MBoxPayload_Parse(blob, startOff);
    if (recs.count == 0) {
        fprintf(stderr, "FAIL: parser returned zero records\n");
        return 1;
    }
    int fails = 0;
    NSUInteger expectAddr = 0;
    for (NSUInteger i = 0; i < recs.count; i++) {
        MBoxPayloadRecord *r = recs[i];
        // Every record except possibly the last must be exactly one
        // EEPROM page (32 bytes). The final record is allowed to be
        // short so the total exactly equals header+payloadSize — that's
        // what the TAS1020A boot ROM's dfuDnloadData expects, per
        // UsbDfu.c:966 (dataRemain check). Padding past payloadSize
        // triggers errFILE and bricks the flash (observed 2026-07-22).
        BOOL isLast = (i == recs.count - 1);
        if (r.length != 32 && !isLast) {
            fprintf(stderr, "FAIL: record %lu has length %u (expected 32,"
                            " only last record may be shorter)\n",
                    (unsigned long)i, r.length);
            fails++;
        }
        if (r.length == 0 || r.length > 32) {
            fprintf(stderr, "FAIL: record %lu has length %u (must be 1..32)\n",
                    (unsigned long)i, r.length);
            fails++;
        }
        if (r.type != 0) {
            fprintf(stderr, "FAIL: record %lu has type %u (expected 0)\n",
                    (unsigned long)i, r.type);
            fails++;
        }
        if (r.address != expectAddr) {
            fprintf(stderr, "FAIL: record %lu addr=0x%04x (expected 0x%04lx)\n",
                    (unsigned long)i, r.address, (unsigned long)expectAddr);
            fails++;
        }
        expectAddr += r.length;
    }
    if (fails == 0) {
        printf("  %lu records (last may be short) contiguous  OK  (0x0000..0x%04lX)\n",
               (unsigned long)recs.count, (unsigned long)expectAddr - 1);
    }

    // Header validation — first 18 bytes of record 0's data.
    const uint8_t *h = recs[0].data.bytes;
    uint16_t sum = 0;
    for (int k = 1; k < 18; k++) sum += h[k];
    uint8_t expectedChk = sum & 0xFF;
    if (h[0] != expectedChk) {
        fprintf(stderr, "FAIL: header chksum=0x%02X, computed=0x%02X\n",
                h[0], expectedChk);
        fails++;
    } else {
        printf("  header chksum          OK  (0x%02X)\n", h[0]);
    }
    if (h[1] != 18)     { fprintf(stderr, "FAIL: headerSize=%u (expected 18)\n", h[1]); fails++; }
    if (h[2] != 0x12 || h[3] != 0x34) {
        fprintf(stderr, "FAIL: sig bytes = 0x%02X 0x%02X (expected 0x12 0x34)\n", h[2], h[3]);
        fails++;
    }
    uint16_t vid = (h[4] << 8) | h[5];
    uint16_t pid = (h[6] << 8) | h[7];
    if (vid != 0x0DBA) { fprintf(stderr, "FAIL: VID=0x%04X (expected 0x0DBA)\n", vid); fails++; }
    if (pid != 0x1000 && pid != 0x1001) {
        fprintf(stderr, "FAIL: PID=0x%04X (expected 0x1000 or 0x1001)\n", pid);
        fails++;
    } else {
        printf("  VID:PID                OK  (0x%04X:0x%04X — %s)\n",
               vid, pid, pid == 0x1001 ? "flasher/DFU" : "audio-mode");
    }
    uint16_t payloadSize = (h[16] << 8) | h[17];
    NSUInteger totalImage = recs.count * 32;   // header + code + trailing FF
    if (18 + payloadSize > totalImage) {
        fprintf(stderr, "FAIL: header says payload=%u B but image only has %lu B after"
                        " the 18-B header\n", payloadSize, (unsigned long)totalImage - 18);
        fails++;
    } else if (18 + payloadSize > 8192) {
        fprintf(stderr, "FAIL: payload+header = %u B exceeds 8 KB EEPROM budget\n",
                18 + payloadSize);
        fails++;
    } else {
        printf("  payload size           OK  (%u B code, %lu B total image incl. header + FF-fill)\n",
               payloadSize, (unsigned long)totalImage);
    }

    if (fails) {
        printf("\nFAIL: %d validation issue(s) in %s\n", fails, path);
        return 1;
    }
    printf("\nPASS: %s is a valid Mbox 1 firmware image\n", path);
    return 0;
}

// Show all offsets in the blob where a run of >= threshold records validates.
// Useful for understanding whether there are multiple stream sections.
// --scan is no longer meaningful with the correct record format: it either
// finds the (one) record stream or it doesn't. Kept as a stub redirect.
static int cmd_scan(const char *path) { return cmd_parse(path); }

static NSArray<MBoxPayloadRecord *> *loadPayload(const char *path, NSError **err) {
    NSData *blob = [NSData dataWithContentsOfFile:@(path) options:0 error:err];
    if (!blob) return nil;
    NSUInteger startOff = 0;
    if (!MBoxPayload_Autodetect(blob, &startOff)) {
        // Fall back to parsing from offset 0 — file is presumed to be raw
        // record stream (like our extracted rev20_flasher_payload.bin).
        startOff = 0;
    }
    return MBoxPayload_Parse(blob, startOff);
}

static int cmd_flash_check(const char *path) {
    NSError *err = nil;
    NSArray<MBoxPayloadRecord *> *recs = loadPayload(path, &err);
    if (!recs) die(@"payload load failed", err);
    NSUInteger totalBytes = 0;
    for (MBoxPayloadRecord *r in recs) totalBytes += r.data.length;
    printf("payload: %lu records, %lu bytes (%.1f KB)\n",
           (unsigned long)recs.count, (unsigned long)totalBytes, totalBytes/1024.0);

    __block BOOL ok = NO;
    err = nil;
    ok = DFU_WithOpenDevice(^BOOL(IOUSBDeviceInterface **dev, uint16_t ifaceNum, NSError **e) {
        DFUStatus st = {0};
        if (!DFU_GetStatus_Retry(dev, ifaceNum, &st, e)) return NO;
        printf("device DFU state: %s / %s\n",
               DFU_StateName((DFUState)st.bState).UTF8String,
               DFU_StatusName(st.bStatus).UTF8String);
        if (st.bState != DFU_dfuIDLE) {
            if (e) *e = [NSError errorWithDomain:@"MBoxFlash" code:2 userInfo:@{
                NSLocalizedDescriptionKey: @"device not in dfuIDLE — abort dry-run"}];
            return NO;
        }
        printf("would send %lu DFU_DNLOAD transfers (block 0..%lu) + 1 zero-length end\n",
               (unsigned long)recs.count, (unsigned long)recs.count-1);
        return YES;
    }, &err);
    if (!ok) die(@"pre-flight check failed", err);
    printf("dry-run OK. Re-run with --flash (no --check) to actually write.\n");
    return 0;
}

static int cmd_flash(const char *path) {
    // Wrapper validation runs implicitly before every flash — same checks
    // as `--validate`. Prevents a wrap_hex.py regression (bad header
    // chksum, wrong VID/PID, non-contiguous records, oversize payload)
    // from silently bricking the EEPROM. Fork audit 2026-07-24.
    printf("=== PRE-FLASH VALIDATION ===\n");
    if (cmd_validate(path) != 0) {
        fprintf(stderr, "\nABORT: image failed validation, refusing to flash\n");
        return 1;
    }
    printf("\n");

    NSError *err = nil;
    NSArray<MBoxPayloadRecord *> *recs = loadPayload(path, &err);
    if (!recs) die(@"payload load failed", err);
    NSUInteger totalBytes = 0;
    for (MBoxPayloadRecord *r in recs) totalBytes += r.data.length;

    // Detect the "flash 0x01 from bulletproof-DFU" trap. See POLICY §7
    // and BRICK_LOG 2026-07-25 for the multi-hour hunt that led to this
    // check. Bulletproof-DFU (VID 0xFFFF PID 0xFFFE, entered via
    // SDA-short) only persists the EEPROM HEADER, not the code region.
    // A dataType=0x01 flash from bulletproof leaves the chip with a
    // valid header pointing at unwritten code → boot ROM validates on
    // cold boot, fails, re-enters bulletproof → silent USB.
    //
    // The dataType byte lives at offset 14 of the 18-byte EEPROM header,
    // which is the first ≥18 bytes of the first record we're about to
    // send. Parse it and refuse if dataType=0x01 while in bulletproof.
    if (probeDFUMode() && recs.count > 0 && recs.firstObject.data.length >= 15) {
        const uint8_t *hdr = (const uint8_t *)recs.firstObject.data.bytes;
        uint8_t dataType = hdr[14];
        if (dataType == 0x01) {
            fprintf(stderr,
                "\nABORT: device is in bulletproof-DFU (0xFFFF:0xFFFE) but this\n"
                "image has dataType = 0x01 (APPCODE). Bulletproof-DFU does NOT\n"
                "persist the code region to EEPROM — only the header. Flashing\n"
                "this directly WILL brick the device (silent USB on next boot).\n\n"
                "Correct procedure per POLICY §7:\n"
                "  1. Flash safety_net/build/safety_net_bootstrap.bin first\n"
                "     (that image has dataType=0x03 = APPCODE_UPDATING, which\n"
                "     tells the boot ROM to come up in app-DFU on next boot).\n"
                "  2. Physically unplug/replug — device should re-enumerate as\n"
                "     app-DFU (0x0DBA:0x1001). Verify with:\n"
                "         ioreg -p IOUSB -l | grep 'idProduct.*4097'\n"
                "  3. Re-run this --flash command from app-DFU. Code will\n"
                "     persist to EEPROM.\n"
                "  4. Physically unplug/replug — device boots the new firmware.\n\n"
                "This check exists because we bricked ourselves this exact way\n"
                "several times before understanding the bulletproof-DFU limit.\n");
            return 1;
        }
    }
    printf("=== ABOUT TO WRITE EEPROM ===\n");
    printf("payload: %lu records, %lu bytes\n", (unsigned long)recs.count, (unsigned long)totalBytes);
    printf("device: DFU-mode Mbox at 0xFFFF:0xFFFE\n");
    printf("proceed? [type 'yes' to confirm]: ");
    fflush(stdout);
    char answer[16] = {0};
    if (!fgets(answer, sizeof(answer), stdin) || strncmp(answer, "yes", 3) != 0) {
        printf("aborted.\n");
        return 1;
    }

    __block BOOL ok = NO;
    err = nil;
    ok = DFU_WithOpenDevice(^BOOL(IOUSBDeviceInterface **dev, uint16_t ifaceNum, NSError **e) {
        DFUStatus st = {0};
        if (!DFU_GetStatus_Retry(dev, ifaceNum, &st, e)) return NO;
        // Self-heal a non-idle entry state instead of forcing a replug.
        //
        // A prior --dump leaves the device parked in dfuUPLOAD_IDLE, and
        // an abandoned download leaves dfuDNLOAD_IDLE. Both are benign,
        // and DFU 1.0 §6.1.4 says DFU_ABORT returns the state machine to
        // dfuIDLE from any non-error state — so just do that rather than
        // making the user unplug the box. dfuERROR is handled separately
        // by the CLRSTATUS restart wrapper below.
        //
        // Flashing this device already takes more physical replugs than
        // anyone wants; do not add one for a state we can clear over the
        // wire.
        if (st.bState != DFU_dfuIDLE && st.bState != DFU_dfuERROR) {
            printf("device is in %s — sending DFU_ABORT to return to dfuIDLE\n",
                   DFU_StateName((DFUState)st.bState).UTF8String);
            if (!DFU_Abort(dev, ifaceNum, e)) return NO;
            if (!DFU_GetStatus_Retry(dev, ifaceNum, &st, e)) return NO;
        }
        if (st.bState != DFU_dfuIDLE) {
            if (e) *e = [NSError errorWithDomain:@"MBoxFlash" code:2 userInfo:@{
                NSLocalizedDescriptionKey: [NSString stringWithFormat:
                    @"device is in %s, need dfuIDLE — is DFU trigger held?",
                    DFU_StateName((DFUState)st.bState).UTF8String]}];
            return NO;
        }
        // Scope-2 whole-flash restart wrapper. On a transient dfuERROR
        // (errUSBR/errPOR/errUNKNOWN/errSTALLEDPKT), issue DFU_CLRSTATUS,
        // poll back to dfuIDLE, and restart the block loop from 0. Safe
        // per fork audit 2026-07-24: DFU_DNLOAD from dfuIDLE resets
        // loadStatus=DFU_LOAD_NOT (UsbDfu.c:500), which walks the exact
        // same init path as a first-time flash (bufferAddr, dataRemain,
        // headerCount all reset in dfuDnloadTarget/dfuDnloadHeader).
        // Bound of 2 restarts (3 total attempts) — beyond that, the
        // fault is more likely persistent hardware than transient.
        BOOL flash_complete = NO;
        for (int restart = 0; restart <= 2 && !flash_complete; restart++) {
            if (restart > 0) {
                printf("=== FLASH RESTART %d/2 ===\n", restart);
            }
            BOOL need_restart = NO;
        for (NSUInteger i = 0; i < recs.count; i++) {
            MBoxPayloadRecord *r = recs[i];
            printf("  block %3lu/%lu  size=%4lu  ", (unsigned long)i,
                   (unsigned long)recs.count-1, (unsigned long)r.data.length);
            fflush(stdout);
            // Per-block transport-retry (scope 1). Safe for transport-
            // layer failures: block was never accepted by boot ROM, so
            // re-send is idempotent.
            BOOL block_ok = NO;
            for (int attempt = 0; attempt < 3; attempt++) {
                NSError *attempt_err = nil;
                if (DFU_Download(dev, ifaceNum, (uint16_t)i,
                                 r.data.bytes, (uint16_t)r.data.length,
                                 &attempt_err)) {
                    block_ok = YES;
                    break;
                }
                printf("transport retry %d/3 ", attempt + 1);
                fflush(stdout);
                usleep(100 * 1000);
                if (attempt == 2 && e) *e = attempt_err;
            }
            if (!block_ok) { printf("FAILED\n"); return NO; }
            // Poll until dfuDNLOAD_IDLE. On dfuERROR, classify bStatus:
            // transient → CLRSTATUS + restart whole flash; terminal → bail.
            BOOL block_errored = NO;
            for (int poll = 0; poll < 100; poll++) {
                if (!DFU_GetStatus_Retry(dev, ifaceNum, &st, e)) return NO;
                if (st.bState == DFU_dfuDNLOAD_IDLE) break;
                if (st.bState == DFU_dfuERROR) {
                    // Transient bStatus codes per DFU 1.1 §6.1.2 that
                    // are safe to recover from via CLRSTATUS + restart:
                    //   12 errUSBR, 13 errPOR, 14 errUNKNOWN, 15 errSTALLEDPKT
                    BOOL transient = (st.bStatus == 12 || st.bStatus == 13 ||
                                      st.bStatus == 14 || st.bStatus == 15);
                    if (transient && restart < 2) {
                        printf("transient dfuERROR (%s) — CLRSTATUS + restart\n",
                               DFU_StatusName(st.bStatus).UTF8String);
                        if (!DFU_ClearStatus(dev, ifaceNum, e)) return NO;
                        for (int p = 0; p < 20; p++) {
                            if (!DFU_GetStatus_Retry(dev, ifaceNum, &st, e)) return NO;
                            if (st.bState == DFU_dfuIDLE) break;
                            usleep(50 * 1000);
                        }
                        if (st.bState != DFU_dfuIDLE) {
                            fprintf(stderr, "CLRSTATUS did not return to dfuIDLE"
                                    " (state=%s)\n",
                                    DFU_StateName((DFUState)st.bState).UTF8String);
                            return NO;
                        }
                        block_errored = YES;
                        need_restart = YES;
                        break;
                    }
                    printf("terminal dfuERROR: %s (bStatus=%u)\n",
                           DFU_StatusName(st.bStatus).UTF8String, st.bStatus);
                    return NO;
                }
                uint32_t poll_ms = st.bwPollTimeout[0]
                                | (st.bwPollTimeout[1] << 8)
                                | (st.bwPollTimeout[2] << 16);
                usleep((poll_ms ? poll_ms : 5) * 1000);
            }
            if (block_errored) break;  // out of block loop → outer restart
            printf("OK\n");
        }
            if (!need_restart) flash_complete = YES;
        }
        if (!flash_complete) {
            fprintf(stderr, "flash did not complete after 2 restarts\n");
            return NO;
        }
        // Zero-length DFU_DNLOAD to signal end of download
        printf("  zero-length end marker... ");
        if (!DFU_Download(dev, ifaceNum, (uint16_t)recs.count, NULL, 0, e)) {
            printf("FAILED\n"); return NO;
        }
        printf("OK\n");
        // Poll through the manifest phase. Per DFU 1.0 §7.1.7 the state
        // sequence after the zero-length terminator is:
        //   dfuDNLOAD_IDLE → dfuMANIFEST_SYNC → dfuMANIFEST →
        //     (bitManifestationTolerant=0 → dfuMANIFEST_WAIT_RESET)
        //     (bitManifestationTolerant=1 → dfuIDLE)
        // Exit on any state that means "committed" and honor bwPollTimeout.
        for (int poll = 0; poll < 100; poll++) {
            if (!DFU_GetStatus_Retry(dev, ifaceNum, &st, e)) return NO;
            if (st.bState == DFU_dfuMANIFEST_WAIT_RESET ||
                st.bState == DFU_dfuIDLE) break;
            if (st.bState == DFU_dfuERROR) {
                printf("device entered dfuERROR during manifest: %s\n",
                       DFU_StatusName(st.bStatus).UTF8String);
                return NO;
            }
            // TAS1020A boot ROM returns bwPollTimeout = 0x200000 (35 min!)
            // during dfuMANIFEST for TARGET_EEPROM — an obviously bogus
            // value that would hang the tool for over half an hour.
            // Cap at 200 ms; the manifest phase for EEPROM target is
            // essentially a state-transition formality since the final
            // metadata writes (chksum, dataType, payloadSize) already
            // committed during dfuDnloadData when dataRemain hit 0
            // (UsbDfu.c:1004-1014). Interrupting the sleep and re-polling
            // lets us catch the transition to WAIT_RESET/IDLE promptly.
            uint32_t poll_ms = st.bwPollTimeout[0]
                            | (st.bwPollTimeout[1] << 8)
                            | (st.bwPollTimeout[2] << 16);
            if (poll_ms == 0 || poll_ms > 200) poll_ms = 200;
            usleep(poll_ms * 1000);
        }
        printf("manifest complete. Final state: %s\n", DFU_StateName((DFUState)st.bState).UTF8String);

        // Boot ROM's DFU descriptor has bitManifestationTolerant=1 (UsbDfu.c:113).
        // With ManTol=1, host is NOT required to issue USB bus reset after
        // manifest. But the boot ROM's dfuSetup while-loop ONLY exits when
        // RSTR_INT fires — see UsbDfu.c:697-704, RomBoot.c dfuSetup call sites.
        // Without a bus reset, boot ROM sits in DFU indefinitely and the
        // newly-flashed app never runs. Force the reset here — it lets the
        // manifest-tolerant path actually finish and switches to the app.
        printf("issuing USB bus reset to trigger app switch...\n");
        IOReturn rrc = (*dev)->USBDeviceReEnumerate(dev, 0);
        if (rrc != kIOReturnSuccess) {
            printf("  (ReEnumerate returned 0x%08x — non-fatal; a physical unplug will also work)\n", rrc);
        }
        return YES;
    }, &err);
    if (!ok) die(@"flash failed", err);

    printf("\n=== FLASH COMPLETE ===\n");
    printf("The bus reset above should have triggered the boot ROM to hand off\n");
    printf("to the newly-flashed app. Run --probe to see current VID/PID.\n");
    printf("If it's still bulletproof (0xFFFF:0xFFFE), physically unplug/replug.\n");
    return 0;
}

// Read the entire EEPROM back via DFU_UPLOAD so the current firmware
// can be restored if a bad flash bricks enumeration.
//
// DFU 1.0 §6.2: repeatedly issue UPLOAD with block N = 0, 1, 2, ...
// The device returns up to wTransferSize bytes per block. A short (or
// zero-length) return signals end-of-image. We stitch the blocks back
// together in file order and write out the raw byte stream.
static int cmd_dump(const char *outPath) {
    NSError *err = nil;
    NSMutableData *acc = [NSMutableData data];

    // DFU_UPLOAD block size — TAS1020A boot ROM's practical ceiling is
    // 64 bytes per transfer; we ask for that and accept whatever we get.
    #define BLOCK_SIZE 64

    __block BOOL ok = NO;
    ok = DFU_WithOpenDevice(^BOOL(IOUSBDeviceInterface **dev, uint16_t ifaceNum, NSError **e) {
        DFUStatus st = {0};
        if (!DFU_GetStatus_Retry(dev, ifaceNum, &st, e)) return NO;
        printf("device DFU state: %s / %s\n",
               DFU_StateName((DFUState)st.bState).UTF8String,
               DFU_StatusName(st.bStatus).UTF8String);
        if (st.bState != DFU_dfuIDLE) {
            if (e) *e = [NSError errorWithDomain:@"MBoxFlash" code:3 userInfo:@{
                NSLocalizedDescriptionKey: @"device not in dfuIDLE — hold DFU trigger and re-plug"}];
            return NO;
        }
        uint8_t buf[BLOCK_SIZE];
        for (uint16_t block = 0; block < 8192; block++) {   // hard cap: 8192 * 64 = 512 KB, safety net
            uint16_t got = 0;
            if (!DFU_Upload(dev, ifaceNum, block, buf, BLOCK_SIZE, &got, e)) return NO;
            if (got == 0) {
                printf("  block %u  got 0 bytes → end of image\n", block);
                break;
            }
            [acc appendBytes:buf length:got];
            if ((block % 16) == 0) {
                printf("  block %3u  got %3u bytes  total %lu\n",
                       block, got, (unsigned long)acc.length);
            }
            if (got < BLOCK_SIZE) {
                printf("  block %u  short read (%u B) → end of image\n", block, got);
                break;
            }
        }
        return YES;
    }, &err);
    if (!ok) die(@"dump failed", err);

    NSData *out = [acc copy];
    if (out.length < 18) {
        fprintf(stderr, "dump too short (%lu B) — not a valid EEPROM image\n",
                (unsigned long)out.length);
        return 1;
    }

    // TAS1020A DFU_UPLOAD returns 0x00 for the header chksum byte
    // (empirically observed — the boot ROM appears to mask off byte 0
    // of the image on upload as a soft anti-tamper). Rewriting it in
    // place from sum(bytes[1:18]) & 0xFF makes the dump match what's
    // actually programmed in EEPROM and lets --validate + --flash both
    // accept the file directly.
    NSMutableData *fixed = [out mutableCopy];
    uint8_t *fb = (uint8_t *)fixed.mutableBytes;
    uint16_t sum = 0;
    for (int k = 1; k < 18; k++) sum += fb[k];
    uint8_t expectedChk = sum & 0xFF;
    if (fb[0] != expectedChk) {
        printf("  header chksum: dump had 0x%02X, recomputed to 0x%02X\n",
               fb[0], expectedChk);
        fb[0] = expectedChk;
    }

    // Re-wrap the 8 KB image into the 32-byte page-aligned record
    // stream that --flash consumes. Matches wrap_hex.py exactly.
    NSUInteger pad = (32 - (fixed.length & 31)) & 31;
    if (pad) {
        [fixed increaseLengthBy:pad];
        memset((uint8_t *)fixed.mutableBytes + fixed.length - pad, 0xFF, pad);
    }
    NSMutableData *wrapped = [NSMutableData data];
    const uint8_t *ib = fixed.bytes;
    for (NSUInteger a = 0; a < fixed.length; a += 32) {
        uint8_t hdr[12] = {
            0, 0, 0, 32,               // length BE
            (uint8_t)(a >> 24), (uint8_t)(a >> 16),
            (uint8_t)(a >> 8),  (uint8_t)a,   // addr BE
            0, 0, 0, 0,                // type = data
        };
        [wrapped appendBytes:hdr length:12];
        [wrapped appendBytes:ib + a length:32];
    }

    if (![wrapped writeToFile:@(outPath) atomically:YES]) {
        fprintf(stderr, "failed to write %s\n", outPath);
        return 1;
    }
    printf("\nsaved %lu bytes to %s (%lu-record TI format, directly flashable)\n",
           (unsigned long)wrapped.length, outPath,
           (unsigned long)wrapped.length / 44);
    printf("To restore this dump: mboxflash --flash %s\n", outPath);
    return 0;
}

// Read the DFU functional descriptor (embedded in the config descriptor)
// and report bitCanUpload / bitCanDnload / bitManifestationTolerant / bitWillDetach.
//
// Why: TAS1020A boot ROM at UsbDfu.c:246-247 strips DFU_UPLOAD_CAP_BIT from
// the descriptor when target == DFU_TARGET_RAM (bulletproof, entered via
// dataType==UNEXIST or dataType==DEVICE_TYPE). It leaves the bit SET for
// TARGET_EEPROM (app-DFU, entered via any other dataType including INVALID).
// Both states show as VID/PID 0xFFFF:0xFFFE from outside (INVALID doesn't
// trigger the descriptor override at UsbDfu.c:213-219). Only bitCanUpload
// distinguishes them, and that distinction is critical: UNEXIST means the
// ROM's I2C read failed → no WORD_ACCESS_MODE flag → subsequent writes go
// byte-mode to a chip that requires word-mode → header lands at wrong
// EEPROM offset → we're bricked in a self-sustaining loop that no amount
// of re-flashing will fix. INVALID means writes will actually persist.
static int cmd_dfu_desc(void) {
    NSError *err = nil;
    __block int rv = 0;
    __block BOOL ok = NO;
    ok = DFU_WithOpenDevice(^BOOL(IOUSBDeviceInterface **dev, uint16_t ifaceNum, NSError **e) {
        (void)ifaceNum;
        // Ask for the first config descriptor.
        IOUSBConfigurationDescriptorPtr cfg = NULL;
        IOReturn rc = (*dev)->GetConfigurationDescriptorPtr(dev, 0, &cfg);
        if (rc != kIOReturnSuccess || cfg == NULL) {
            if (e) *e = [NSError errorWithDomain:@"MBoxFlash" code:rc userInfo:@{
                NSLocalizedDescriptionKey: [NSString stringWithFormat:
                    @"GetConfigurationDescriptorPtr rc=0x%08x", rc]}];
            return NO;
        }
        uint16_t totalLen = USBToHostWord(cfg->wTotalLength);
        const uint8_t *p = (const uint8_t *)cfg;
        const uint8_t *end = p + totalLen;
        printf("Config descriptor is %u bytes total.\n", totalLen);
        // Walk the descriptor chain looking for a DFU functional descriptor
        // (bDescriptorType = 0x21). It's 7 bytes in DFU 1.0 or 9 bytes in
        // DFU 1.1 (adds wDetachTimeout and wTransferSize... actually those
        // are in both, so 9 bytes total).
        while (p + 2 <= end) {
            uint8_t bLength = p[0];
            uint8_t bDescriptorType = p[1];
            if (bLength < 2 || p + bLength > end) break;
            printf("  desc type 0x%02x len %u\n", bDescriptorType, bLength);
            if (bDescriptorType == 0x21 && bLength >= 3) {
                uint8_t bmAttributes = p[2];
                printf("\nDFU Functional Descriptor found:\n");
                printf("  bmAttributes = 0x%02x\n", bmAttributes);
                printf("    bitWillDetach            (0x08) = %s\n", (bmAttributes & 0x08) ? "YES" : "no");
                printf("    bitManifestationTolerant (0x04) = %s\n", (bmAttributes & 0x04) ? "YES" : "no");
                printf("    bitCanUpload             (0x02) = %s\n", (bmAttributes & 0x02) ? "YES" : "no");
                printf("    bitCanDnload             (0x01) = %s\n", (bmAttributes & 0x01) ? "YES" : "no");
                printf("\nInterpretation:\n");
                if (bmAttributes & 0x02) {
                    printf("  Upload capability is SET.\n");
                    printf("  → boot ROM did NOT enter TARGET_RAM (which would strip this bit).\n");
                    printf("  → we are in TARGET_EEPROM mode: EEPROM state is INVALID (bad chksum/sig)\n");
                    printf("    but I2C word-access is established. A normal --flash will actually\n");
                    printf("    persist code to EEPROM. Recovery: flash safety_net_flasher.bin.\n");
                } else {
                    printf("  Upload capability is CLEAR.\n");
                    printf("  → boot ROM entered TARGET_RAM (bulletproof).\n");
                    printf("  → EEPROM state is UNEXIST: I2C read of signature NACK'd at boot,\n");
                    printf("    so ROM never set WORD_ACCESS_MODE flag. Subsequent writes will\n");
                    printf("    go BYTE-mode to a chip that requires WORD-mode addressing → they\n");
                    printf("    ACK but land at wrong offsets. Flashing more will NOT help.\n");
                    printf("    Only way out is another cold boot where the I2C read happens to\n");
                    printf("    succeed (chip behavior may vary across power cycles).\n");
                    rv = 1;
                }
                return YES;
            }
            p += bLength;
        }
        printf("no DFU functional descriptor (0x21) found in config descriptor\n");
        rv = 2;
        return YES;
    }, &err);
    if (!ok) die(@"open device failed", err);
    return rv;
}

static int cmd_dfu_status(void) {
    DFUStatus st = {0};
    NSError *err = nil;
    if (!DFU_QueryStatus(&st, &err)) die(@"DFU_GETSTATUS failed", err);
    uint32_t pollTimeout = st.bwPollTimeout[0]
                        | (st.bwPollTimeout[1] << 8)
                        | (st.bwPollTimeout[2] << 16);
    printf("DFU status:\n");
    printf("  bStatus       = %s\n", DFU_StatusName(st.bStatus).UTF8String);
    printf("  bwPollTimeout = %u ms\n", pollTimeout);
    printf("  bState        = %s\n", DFU_StateName((DFUState)st.bState).UTF8String);
    printf("  iString       = %u\n", st.iString);
    return st.bStatus == 0 ? 0 : 1;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc < 2) {
            fprintf(stderr,
                "usage: mboxflash --probe | --enter-dfu | --dfu-status | --parse PATH | --scan PATH\n"
                "        | --flash-check PATH | --flash PATH | --dump PATH | --validate PATH\n"
                "        | --descdump\n");
            return 2;
        }
        NSString *cmd = @(argv[1]);
        if      ([cmd isEqualToString:@"--dfu-desc"])  return cmd_dfu_desc();
        else if ([cmd isEqualToString:@"--dfu-abort"]) {
            NSError *err = nil;
            BOOL ok = DFU_WithOpenDevice(^BOOL(IOUSBDeviceInterface **dev, uint16_t ifn, NSError **e) {
                return DFU_Abort(dev, ifn, e);
            }, &err);
            if (!ok) die(@"DFU_ABORT failed", err);
            printf("DFU_ABORT sent.\n");
            return cmd_dfu_status();
        }
        else if ([cmd isEqualToString:@"--probe"])     return cmd_probe();
        else if ([cmd isEqualToString:@"--enter-dfu"]) return cmd_enter_dfu();
        else if ([cmd isEqualToString:@"--descdump"]) return cmd_descdump();
        else if ([cmd isEqualToString:@"--parse"] && argc >= 3) return cmd_parse(argv[2]);
        else if ([cmd isEqualToString:@"--scan"]  && argc >= 3) return cmd_scan(argv[2]);
        else if ([cmd isEqualToString:@"--dfu-status"]) return cmd_dfu_status();
        else if ([cmd isEqualToString:@"--flash-check"] && argc >= 3) return cmd_flash_check(argv[2]);
        else if ([cmd isEqualToString:@"--flash"]       && argc >= 3) return cmd_flash(argv[2]);
        else if ([cmd isEqualToString:@"--dump"]        && argc >= 3) return cmd_dump(argv[2]);
        else if ([cmd isEqualToString:@"--validate"]    && argc >= 3) return cmd_validate(argv[2]);
        else {
            fprintf(stderr, "unknown command '%s'\n", argv[1]);
            return 2;
        }
    }
}
