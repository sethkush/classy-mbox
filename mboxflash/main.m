// mboxflash — reflash the original Digidesign Mbox from arm64 macOS.
//
// Usage:
//   mboxflash --probe              print connected Mbox's bcdDevice, exit
//   mboxflash --enter-dfu          send just the custom detach request, exit
//   mboxflash --parse PAYLOAD.bin  parse a payload blob and print record list
//   mboxflash --flash PAYLOAD.bin  full flash cycle (not yet implemented — needs
//                                   verification against a real DFU-mode device)

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

// Match Mbox by VID/PID and return bcdDevice; -1 if not found.
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

static int cmd_probe(void) {
    int bcd = probeBcdDevice();
    if (bcd < 0) {
        fprintf(stderr, "no Mbox 1 found on USB bus (VID 0x0DBA PID 0x1000)\n");
        return 1;
    }
    fprintf(stdout, "Mbox 1 connected, bcdDevice = 0x%04x (firmware ", bcd);
    if      (bcd == 0x0022) fprintf(stdout, "v22 — OK, no flash needed)\n");
    else if (bcd == 0x0020) fprintf(stdout, "Rev 20 — BUGGY, should flash to v22)\n");
    else if (bcd == 0x0016 || bcd == 0x0018 || bcd == 0x0019)
        fprintf(stdout, "very old, %u.%u — should flash to at least Rev 20)\n",
            bcd >> 8, bcd & 0xff);
    else fprintf(stdout, "unknown 0.%u)\n", bcd);
    return 0;
}

static int cmd_enter_dfu(void) {
    int bcd = probeBcdDevice();
    if (bcd < 0) {
        fprintf(stderr, "no Mbox 1 found — plug it in and retry\n");
        return 1;
    }
    NSError *err = nil;
    if (!DFU_SendEnterDFURequest(0, &err)) {
        die(@"enter-DFU request failed", err);
    }
    printf("enter-DFU request sent. Device should now disconnect and re-enumerate\n");
    printf("in DFU mode. Wait a couple of seconds then check `system_profiler` /\n");
    printf("`ioreg -p IOUSB` for what appears.\n");
    return 0;
}

static int cmd_parse(const char *path) {
    NSError *err = nil;
    NSData *blob = [NSData dataWithContentsOfFile:@(path) options:0 error:&err];
    if (!blob) die([NSString stringWithFormat:@"could not read %s", path], err);

    // Payload lives inside __data at offset ~0x3000 for the v22 blob we've
    // captured. TODO: auto-locate by scanning for first valid record.
    NSArray<MBoxPayloadRecord *> *recs = MBoxPayload_Parse(blob, 0x3000, &err);
    if (!recs) die(@"parse failed", err);

    printf("parsed %lu records:\n", (unsigned long)recs.count);
    NSUInteger totalData = 0;
    for (NSUInteger i = 0; i < recs.count; i++) {
        MBoxPayloadRecord *r = recs[i];
        printf("  [%3lu] seq=%u  size=%4lu  header: ",
               (unsigned long)i, r.sequenceNumber, (unsigned long)r.data.length);
        const uint8_t *h = r.header.bytes;
        for (int j = 0; j < 12; j++) printf("%02x ", h[j]);
        printf("\n");
        totalData += r.data.length;
    }
    printf("total data: %lu bytes\n", (unsigned long)totalData);
    return 0;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc < 2) {
            fprintf(stderr,
                "usage: mboxflash --probe | --enter-dfu | --parse PATH\n");
            return 2;
        }
        NSString *cmd = @(argv[1]);
        if      ([cmd isEqualToString:@"--probe"])     return cmd_probe();
        else if ([cmd isEqualToString:@"--enter-dfu"]) return cmd_enter_dfu();
        else if ([cmd isEqualToString:@"--parse"] && argc >= 3) return cmd_parse(argv[2]);
        else {
            fprintf(stderr, "unknown command '%s'\n", argv[1]);
            return 2;
        }
    }
}
