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

static int probeBcdDevice(void) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) return -1;
    CFDictionarySetValue(match, CFSTR(kUSBVendorID),  (__bridge CFNumberRef)@(0x0DBA));
    CFDictionarySetValue(match, CFSTR(kUSBProductID), (__bridge CFNumberRef)@(0x1000));
    io_iterator_t it = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &it) != KERN_SUCCESS) return -1;
    io_service_t svc = IOIteratorNext(it);
    IOObjectRelease(it);
    if (!svc) return -1;
    CFNumberRef bcd = (CFNumberRef)IORegistryEntrySearchCFProperty(svc,
        kIOServicePlane, CFSTR("bcdDevice"), NULL, kIORegistryIterateRecursively);
    int result = -1;
    if (bcd) {
        int v = 0;
        CFNumberGetValue(bcd, kCFNumberIntType, &v);
        result = v;
        CFRelease(bcd);
    }
    IOObjectRelease(svc);
    return result;
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
        printf("Mbox connected in DFU mode (VID 0xFFFF PID 0xFFFE, class 0xFE)\n");
        printf("  Ready to accept DFU_DNLOAD transfers. Use --dfu-status to query state,\n");
        printf("  or --flash PATH to write firmware.\n");
        return 0;
    }
    int bcd = probeBcdDevice();
    if (bcd < 0) {
        fprintf(stderr, "no Mbox found (neither audio-mode 0x0DBA:0x1000 nor DFU-mode 0xFFFF:0xFFFE)\n");
        return 1;
    }
    fprintf(stdout, "Mbox 1 connected in audio mode, bcdDevice = 0x%04x (firmware ", bcd);
    if      (bcd == 0x0022) fprintf(stdout, "v22 — OK, no flash needed)\n");
    else if (bcd == 0x0020) fprintf(stdout, "Rev 20 — BUGGY, should flash to v22)\n");
    else if (bcd == 0x0016 || bcd == 0x0018 || bcd == 0x0019)
        fprintf(stdout, "very old, %u.%u — should flash to at least Rev 20)\n",
            bcd >> 8, bcd & 0xff);
    else fprintf(stdout, "unknown 0.%u)\n", bcd);
    printf("\nTo flash: hold a front-panel source button while plugging the Mbox in.\n");
    printf("Device will re-enumerate in DFU mode (VID 0xFFFF PID 0xFFFE).\n");
    return 0;
}

static int cmd_enter_dfu(void) {
    int bcd = probeBcdDevice();
    if (bcd < 0) {
        fprintf(stderr, "no Mbox 1 found — plug it in and retry\n");
        return 1;
    }
    NSError *err = nil;
    if (!DFU_SendEnterDFURequest(0, &err)) die(@"enter-DFU request failed", err);
    printf("enter-DFU request sent. Device should disconnect and re-enumerate\n");
    printf("in DFU mode. Wait a second, then run:\n");
    printf("    ioreg -p IOUSB -l | grep -B2 -A20 -i digidesign\n");
    printf("to see what descriptors the device now advertises.\n");
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
        if (r.length != 32) {
            fprintf(stderr, "FAIL: record %lu has length %u (expected 32)\n",
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
        printf("  %lu records × 32B contiguous  OK  (0x0000..0x%04lX)\n",
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
        if (!DFU_GetStatus(dev, ifaceNum, &st, e)) return NO;
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
    NSError *err = nil;
    NSArray<MBoxPayloadRecord *> *recs = loadPayload(path, &err);
    if (!recs) die(@"payload load failed", err);
    NSUInteger totalBytes = 0;
    for (MBoxPayloadRecord *r in recs) totalBytes += r.data.length;

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
        if (!DFU_GetStatus(dev, ifaceNum, &st, e)) return NO;
        if (st.bState != DFU_dfuIDLE) {
            if (e) *e = [NSError errorWithDomain:@"MBoxFlash" code:2 userInfo:@{
                NSLocalizedDescriptionKey: [NSString stringWithFormat:
                    @"device is in %s, need dfuIDLE — is DFU trigger held?",
                    DFU_StateName((DFUState)st.bState).UTF8String]}];
            return NO;
        }
        for (NSUInteger i = 0; i < recs.count; i++) {
            MBoxPayloadRecord *r = recs[i];
            printf("  block %3lu/%lu  size=%4lu  ", (unsigned long)i,
                   (unsigned long)recs.count-1, (unsigned long)r.data.length);
            fflush(stdout);
            if (!DFU_Download(dev, ifaceNum, (uint16_t)i,
                              r.data.bytes, (uint16_t)r.data.length, e)) {
                printf("FAILED\n"); return NO;
            }
            // Poll until dfuDNLOAD_IDLE
            for (int poll = 0; poll < 100; poll++) {
                if (!DFU_GetStatus(dev, ifaceNum, &st, e)) return NO;
                if (st.bState == DFU_dfuDNLOAD_IDLE) break;
                if (st.bState == DFU_dfuERROR) {
                    printf("device entered dfuERROR: %s\n",
                           DFU_StatusName(st.bStatus).UTF8String);
                    return NO;
                }
                uint32_t poll_ms = st.bwPollTimeout[0]
                                | (st.bwPollTimeout[1] << 8)
                                | (st.bwPollTimeout[2] << 16);
                usleep((poll_ms ? poll_ms : 5) * 1000);
            }
            printf("OK\n");
        }
        // Zero-length DFU_DNLOAD to signal end of download
        printf("  zero-length end marker... ");
        if (!DFU_Download(dev, ifaceNum, (uint16_t)recs.count, NULL, 0, e)) {
            printf("FAILED\n"); return NO;
        }
        printf("OK\n");
        // Poll for manifest phase
        for (int poll = 0; poll < 100; poll++) {
            if (!DFU_GetStatus(dev, ifaceNum, &st, e)) return NO;
            if (st.bState == DFU_dfuMANIFEST || st.bState == DFU_dfuIDLE) break;
            if (st.bState == DFU_dfuERROR) return NO;
            usleep(20 * 1000);
        }
        printf("manifest complete. Final state: %s\n", DFU_StateName((DFUState)st.bState).UTF8String);
        return YES;
    }, &err);
    if (!ok) die(@"flash failed", err);

    printf("\n=== FLASH COMPLETE ===\n");
    printf("Physically unplug the Mbox, wait 3 seconds, plug it back in NORMALLY\n");
    printf("(without holding any button), then run --probe to confirm bcdDevice.\n");
    printf("Expected values: 0x0020 = Rev 20, 0x0022 = v22, other = custom.\n");
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
        if (!DFU_GetStatus(dev, ifaceNum, &st, e)) return NO;
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
                "        | --flash-check PATH | --flash PATH | --dump PATH | --validate PATH\n");
            return 2;
        }
        NSString *cmd = @(argv[1]);
        if      ([cmd isEqualToString:@"--probe"])     return cmd_probe();
        else if ([cmd isEqualToString:@"--enter-dfu"]) return cmd_enter_dfu();
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
