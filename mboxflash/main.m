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

// Print a run summary line for a parsed record list.
static void printRecordSummary(NSArray<MBoxPayloadRecord *> *recs) {
    if (!recs.count) { printf("(no records)\n"); return; }
    NSUInteger totalBytes = 0;
    NSMutableString *sizeHist = [NSMutableString string];
    NSCountedSet *sizeSet = [NSCountedSet set];
    for (MBoxPayloadRecord *r in recs) {
        totalBytes += r.data.length;
        [sizeSet addObject:@(r.data.length)];
    }
    NSArray *sortedSizes = [sizeSet.allObjects sortedArrayUsingSelector:@selector(compare:)];
    for (NSNumber *sz in sortedSizes) {
        [sizeHist appendFormat:@"  %lu×%lu", (unsigned long)[sizeSet countForObject:sz],
                                              (unsigned long)sz.unsignedIntegerValue];
    }
    printf("  records=%lu  total-data=%lu bytes  size-histogram:%s\n",
           (unsigned long)recs.count, (unsigned long)totalBytes, sizeHist.UTF8String);
    NSUInteger last = recs.lastObject.fileOffset + 16 + recs.lastObject.data.length;
    printf("  span: 0x%lx .. 0x%lx\n",
           (unsigned long)recs.firstObject.fileOffset, (unsigned long)last);
}

static int cmd_parse(const char *path) {
    NSError *err = nil;
    NSData *blob = [NSData dataWithContentsOfFile:@(path) options:0 error:&err];
    if (!blob) die([NSString stringWithFormat:@"could not read %s", path], err);

    NSUInteger startOff = 0, runLen = 0;
    BOOL ok = MBoxPayload_Autodetect(blob, /*minRun=*/4, &startOff, &runLen);
    if (!ok) {
        printf("no plausible record stream found (max run: %lu records)\n",
               (unsigned long)runLen);
        return 1;
    }
    printf("autodetected start: 0x%lx (%lu records)\n\n",
           (unsigned long)startOff, (unsigned long)runLen);

    NSArray<MBoxPayloadRecord *> *recs = MBoxPayload_Parse(blob, startOff);
    for (NSUInteger i = 0; i < recs.count; i++) {
        MBoxPayloadRecord *r = recs[i];
        printf("  [%3lu] @0x%05lx  size=%4lu  header:",
               (unsigned long)i, (unsigned long)r.fileOffset,
               (unsigned long)r.data.length);
        const uint8_t *h = r.header.bytes;
        for (int j = 0; j < 12; j++) printf(" %02x", h[j]);
        printf("\n");
    }
    printf("\n");
    printRecordSummary(recs);
    return 0;
}

// Show all offsets in the blob where a run of >= threshold records validates.
// Useful for understanding whether there are multiple stream sections.
static int cmd_scan(const char *path) {
    NSError *err = nil;
    NSData *blob = [NSData dataWithContentsOfFile:@(path) options:0 error:&err];
    if (!blob) die([NSString stringWithFormat:@"could not read %s", path], err);

    printf("scanning %lu bytes for record-stream candidates (min run = 2)...\n\n",
           (unsigned long)blob.length);
    NSUInteger last_end = 0;
    NSUInteger totalRecs = 0, totalBytes = 0, sectionCount = 0;
    for (NSUInteger off = 0; off + 16 <= blob.length; off += 4) {
        if (off < last_end) continue;  // don't re-report offsets we already covered
        NSUInteger n = MBoxPayload_ValidRunLength(blob, off);
        if (n < 2) continue;
        NSArray<MBoxPayloadRecord *> *recs = MBoxPayload_Parse(blob, off);
        printf("candidate @ 0x%05lx:\n", (unsigned long)off);
        printRecordSummary(recs);
        printf("\n");
        MBoxPayloadRecord *last = recs.lastObject;
        last_end = last.fileOffset + 16 + last.data.length;
        sectionCount++;
        totalRecs += recs.count;
        for (MBoxPayloadRecord *rr in recs) totalBytes += rr.data.length;
    }
    printf("summary: %lu sections, %lu records, %lu data bytes (%.1f KB)\n",
           (unsigned long)sectionCount, (unsigned long)totalRecs,
           (unsigned long)totalBytes, totalBytes/1024.0);
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
                "usage: mboxflash --probe | --enter-dfu | --dfu-status | --parse PATH | --scan PATH\n");
            return 2;
        }
        NSString *cmd = @(argv[1]);
        if      ([cmd isEqualToString:@"--probe"])     return cmd_probe();
        else if ([cmd isEqualToString:@"--enter-dfu"]) return cmd_enter_dfu();
        else if ([cmd isEqualToString:@"--parse"] && argc >= 3) return cmd_parse(argv[2]);
        else if ([cmd isEqualToString:@"--scan"]  && argc >= 3) return cmd_scan(argv[2]);
        else if ([cmd isEqualToString:@"--dfu-status"]) return cmd_dfu_status();
        else {
            fprintf(stderr, "unknown command '%s'\n", argv[1]);
            return 2;
        }
    }
}
