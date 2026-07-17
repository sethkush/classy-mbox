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
    printf("(without holding any button). Then run --probe to confirm bcdDevice = 0x0022.\n");
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
                "usage: mboxflash --probe | --enter-dfu | --dfu-status | --parse PATH | --scan PATH\n        | --flash-check PATH | --flash PATH\n");
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
        else {
            fprintf(stderr, "unknown command '%s'\n", argv[1]);
            return 2;
        }
    }
}
