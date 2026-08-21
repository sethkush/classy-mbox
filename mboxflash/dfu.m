#import "dfu.h"
#import <IOKit/IOKitLib.h>
#import <IOKit/usb/IOUSBLib.h>
#import <IOKit/IOCFPlugIn.h>

static NSError *usbError(NSString *op, IOReturn rc) {
    return [NSError errorWithDomain:@"MBoxFlashUSB" code:rc userInfo:@{
        NSLocalizedDescriptionKey: [NSString stringWithFormat:
            @"%@ failed: 0x%08x", op, (unsigned)rc]
    }];
}

NSString *gMboxTargetSerial = nil;   // set by --serial; nil = no filter

// Is this a device this tool talks to? VID 0x0DBA in any mode, or the boot
// ROM's 0xFFFF:0xFFFE. PIDs 0x2000-0x200F are audio-mode aliases -- mboxfw is
// built with MBOX_PID=0x2000 so the kernel's mbox1 quirk does not claim it.
// Keep in step with AUDIO_PID_ALIASES in tools/mboxflash_linux.py.
BOOL MBox_IsOurDevice(int vid, int pid) {
    if (vid == 0xFFFF && pid == 0xFFFE) return YES;      // boot-ROM DFU
    if (vid != 0x0DBA) return NO;
    if (pid == 0x1000 || pid == 0x1001) return YES;      // stock audio / app-DFU
    return (pid >= 0x2000 && pid <= 0x200F);             // mboxfw audio aliases
}

static NSString *serialOf(io_service_t svc) {
    CFStringRef sn = (CFStringRef)IORegistryEntrySearchCFProperty(svc,
        kIOServicePlane, CFSTR("USB Serial Number"), NULL, kIORegistryIterateRecursively);
    if (!sn) return nil;
    NSString *out = [NSString stringWithString:(__bridge NSString *)sn];
    CFRelease(sn);
    return out;
}

static int intProp(io_service_t svc, CFStringRef key) {
    CFNumberRef n = (CFNumberRef)IORegistryEntrySearchCFProperty(svc,
        kIOServicePlane, key, NULL, kIORegistryIterateRecursively);
    int v = 0;
    if (n) { CFNumberGetValue(n, kCFNumberIntType, &v); CFRelease(n); }
    return v;
}

// Find & open the Mbox. REFUSES TO GUESS when more than one candidate is
// attached, exactly as mboxflash_linux.py and mboxtlm.py do, and for a sharper
// reason than either: this tool WRITES FIRMWARE. Two units share the bench and
// both may build at the same MBOX_PID -- the PID says which PRODUCT this is,
// not which UNIT -- so picking whichever IOKit enumerated first would flash the
// wrong device and look like success. Select with --serial <SN>.
//
// NB: IOKit device-matching with only a VendorID key silently returns 0 hits --
// matching wants VID+PID together -- so we iterate all IOUSBDevice services and
// filter in code.
static IOUSBDeviceInterface **openMboxDevice(NSError **error) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) { if (error) *error = usbError(@"IOServiceMatching", -1); return NULL; }
    io_iterator_t it = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &it) != KERN_SUCCESS) return NULL;

    NSMutableArray *found = [NSMutableArray array];   // boxed io_service_t
    NSMutableArray *descr = [NSMutableArray array];   // human labels, same order
    io_service_t cand;
    while ((cand = IOIteratorNext(it))) {
        int v = intProp(cand, CFSTR("idVendor"));
        int pd = intProp(cand, CFSTR("idProduct"));
        if (!MBox_IsOurDevice(v, pd)) { IOObjectRelease(cand); continue; }
        NSString *sn = serialOf(cand);
        if (gMboxTargetSerial && !(sn && [sn isEqualToString:gMboxTargetSerial])) {
            IOObjectRelease(cand);
            continue;
        }
        [found addObject:@((unsigned long long)cand)];
        [descr addObject:[NSString stringWithFormat:@"    %04x:%04x  bcdDevice %04x  serial %@",
                          v, pd, intProp(cand, CFSTR("bcdDevice")), sn ?: @"(none)"]];
    }
    IOObjectRelease(it);

    if (found.count == 0) {
        if (error) {
            NSString *m = gMboxTargetSerial
                ? [NSString stringWithFormat:@"no Mbox with serial %@ attached", gMboxTargetSerial]
                : @"no Mbox attached (0x0DBA:0x1000/0x1001/0x2000-0x200F or 0xFFFF:0xFFFE)";
            *error = [NSError errorWithDomain:@"mboxflash" code:1
                                     userInfo:@{NSLocalizedDescriptionKey: m}];
        }
        return NULL;
    }
    if (found.count > 1) {
        for (NSNumber *n in found) IOObjectRelease((io_service_t)n.unsignedLongLongValue);
        if (error) {
            NSString *m = [NSString stringWithFormat:
                @"%lu Mboxes attached and none was selected:\n%@\n"
                 "Pick one with --serial <SN>. Refusing to guess: this command "
                 "writes firmware, and a wrong guess flashes the other unit.",
                (unsigned long)found.count, [descr componentsJoinedByString:@"\n"]];
            *error = [NSError errorWithDomain:@"mboxflash" code:2
                                     userInfo:@{NSLocalizedDescriptionKey: m}];
        }
        return NULL;
    }

    io_service_t svc = (io_service_t)((NSNumber *)found[0]).unsignedLongLongValue;

    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    kern_return_t kr = IOCreatePlugInInterfaceForService(svc,
        kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS || !plugin) {
        if (error) *error = usbError(@"IOCreatePlugInInterface(device)", kr);
        return NULL;
    }
    IOUSBDeviceInterface **dev = NULL;
    HRESULT hr = (*plugin)->QueryInterface(plugin,
        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID*)&dev);
    (*plugin)->Release(plugin);
    if (hr || !dev) {
        if (error) *error = usbError(@"QueryInterface(IOUSBDevice)", (IOReturn)hr);
        return NULL;
    }
    IOReturn rc = (*dev)->USBDeviceOpenSeize(dev);
    if (rc != kIOReturnSuccess) {
        (*dev)->Release(dev);
        if (error) *error = usbError(@"USBDeviceOpen (busy? try unplug/replug)", rc);
        return NULL;
    }
    return dev;
}

// Find & open interface `wantIfaceNum` of an already-open device.
static IOUSBInterfaceInterface **openMboxInterface(IOUSBDeviceInterface **dev,
                                                    uint8_t wantIfaceNum,
                                                    NSError **error) {
    IOUSBFindInterfaceRequest req = {
        .bInterfaceClass    = kIOUSBFindInterfaceDontCare,
        .bInterfaceSubClass = kIOUSBFindInterfaceDontCare,
        .bInterfaceProtocol = kIOUSBFindInterfaceDontCare,
        .bAlternateSetting  = kIOUSBFindInterfaceDontCare,
    };
    io_iterator_t iter = IO_OBJECT_NULL;
    IOReturn rc = (*dev)->CreateInterfaceIterator(dev, &req, &iter);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(@"CreateInterfaceIterator", rc); return NULL;
    }
    io_service_t svc;
    while ((svc = IOIteratorNext(iter))) {
        IOCFPlugInInterface **plugin = NULL;
        SInt32 score = 0;
        kern_return_t kr = IOCreatePlugInInterfaceForService(svc,
            kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
        IOObjectRelease(svc);
        if (kr != KERN_SUCCESS || !plugin) continue;

        IOUSBInterfaceInterface **iface = NULL;
        HRESULT hr = (*plugin)->QueryInterface(plugin,
            CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID), (LPVOID*)&iface);
        (*plugin)->Release(plugin);
        if (hr || !iface) continue;

        UInt8 num = 0xff;
        (*iface)->GetInterfaceNumber(iface, &num);
        if (num != wantIfaceNum) { (*iface)->Release(iface); continue; }

        rc = (*iface)->USBInterfaceOpen(iface);
        IOObjectRelease(iter);
        if (rc != kIOReturnSuccess) {
            (*iface)->Release(iface);
            if (error) *error = usbError(
                [NSString stringWithFormat:@"USBInterfaceOpen(iface %u)", wantIfaceNum], rc);
            return NULL;
        }
        return iface;
    }
    IOObjectRelease(iter);
    if (error) *error = usbError(
        [NSString stringWithFormat:@"interface %u not found", wantIfaceNum], -1);
    return NULL;
}

BOOL DFU_SendEnterDFURequest(io_service_t unused, NSError **error) {
    (void)unused;
    IOUSBDeviceInterface **dev = openMboxDevice(error);
    if (!dev) return NO;

    IOUSBDevRequest req = {
        .bmRequestType = 0x21,     // Class, H->D, to Interface
        .bRequest      = 0x00,
        .wValue        = 0x000A,
        .wIndex        = 0x0000,   // interface 0
        .wLength       = 0,
        .pData         = NULL,
    };

    // Send on the DEFAULT CONTROL PIPE via the device interface, NOT by
    // opening interface 0.
    //
    // "Recipient = interface" is a field inside the SETUP packet; it does
    // not require the host to have claimed that interface. Opening it
    // does, and an interface only exists once a configuration has been
    // set. Our firmware is reachable long before that — indeed the whole
    // point of the DFU trigger is to work on a device that is not
    // usefully configured.
    //
    // Set a configuration first. An interface-recipient request is
    // validated by IOUSBHostFamily against the device's known interfaces,
    // and an unconfigured device has none — macOS then fails the request
    // with a synthesised stall without ever putting it on the wire.
    // SetConfiguration(1) is known to succeed on this device (see
    // --descdump), and configuring is harmless if it already is.
    UInt8 curCfg = 0;
    if ((*dev)->GetConfiguration(dev, &curCfg) != kIOReturnSuccess || curCfg == 0) {
        IOReturn crc = (*dev)->SetConfiguration(dev, 1);
        fprintf(stderr, "  SetConfiguration(1) -> 0x%08x\n", crc);
    }

    // This was why --enter-dfu "never worked": the old code called
    // openMboxInterface(dev, 0) first and failed with
    // kIOUSBInterfaceNotFound (0xe000404f) on an unconfigured device, so
    // the class request never reached the firmware at all. The firmware's
    // handler was fine. Every SDA short taken to get into DFU was paid to
    // work around a host-side bug.
    IOReturn rc = (*dev)->DeviceRequest(dev, &req);

    if (rc != kIOReturnSuccess) {
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
        if (error) *error = usbError(@"enter-DFU class request (default control pipe)", rc);
        return NO;
    }

    // Standard USB DFU 1.0: after DFU_DETACH (bRequest=0), the host must
    // issue a bus reset to actually trigger the mode transition. Otherwise
    // the device sits in appDETACH state indefinitely.
    fprintf(stderr, "  DFU_DETACH accepted, issuing bus reset...\n");
    IOReturn rrc = (*dev)->USBDeviceReEnumerate(dev, 0);
    (*dev)->USBDeviceClose(dev);
    (*dev)->Release(dev);
    if (rrc != kIOReturnSuccess) {
        if (error) *error = usbError(@"ResetDevice after DFU_DETACH", rrc);
        return NO;
    }
    return YES;
}

BOOL DFU_Download(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
                  uint16_t blockNum, const void *data, uint16_t length,
                  NSError **error) {
    IOUSBDevRequest req = {
        .bmRequestType = 0x21,
        .bRequest      = DFU_DNLOAD,
        .wValue        = blockNum,
        .wIndex        = interfaceNumber,
        .wLength       = length,
        .pData         = (void *)data,
    };
    IOReturn rc = (*dev)->DeviceRequest(dev, &req);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(
            [NSString stringWithFormat:@"DFU_DNLOAD block=%u len=%u", blockNum, length], rc);
        return NO;
    }
    return YES;
}

BOOL DFU_Upload(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
                uint16_t blockNum, void *data, uint16_t length,
                uint16_t *outLength, NSError **error) {
    IOUSBDevRequest req = {
        .bmRequestType = 0xA1,   // Class, D→H, to Interface
        .bRequest      = DFU_UPLOAD,
        .wValue        = blockNum,
        .wIndex        = interfaceNumber,
        .wLength       = length,
        .pData         = data,
    };
    IOReturn rc = (*dev)->DeviceRequest(dev, &req);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(
            [NSString stringWithFormat:@"DFU_UPLOAD block=%u len=%u", blockNum, length], rc);
        return NO;
    }
    if (outLength) *outLength = req.wLenDone;
    return YES;
}

BOOL DFU_GetStatus(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
                   DFUStatus *out, NSError **error) {
    IOUSBDevRequest req = {
        .bmRequestType = 0xA1,
        .bRequest      = DFU_GETSTATUS,
        .wValue        = 0,
        .wIndex        = interfaceNumber,
        .wLength       = sizeof(DFUStatus),
        .pData         = out,
    };
    IOReturn rc = (*dev)->DeviceRequest(dev, &req);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(@"DFU_GETSTATUS", rc);
        return NO;
    }
    return YES;
}

BOOL DFU_Abort(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
               NSError **error) {
    IOUSBDevRequest req = {
        .bmRequestType = 0x21,        // Class, H→D, to Interface
        .bRequest      = DFU_ABORT,
        .wValue        = 0,
        .wIndex        = interfaceNumber,
        .wLength       = 0,
        .pData         = NULL,
    };
    IOReturn rc = (*dev)->DeviceRequest(dev, &req);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(@"DFU_ABORT", rc);
        return NO;
    }
    return YES;
}

BOOL DFU_GetStatus_Retry(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
                         DFUStatus *out, NSError **error) {
    for (int attempt = 0; attempt < 3; attempt++) {
        NSError *attempt_err = nil;
        if (DFU_GetStatus(dev, interfaceNumber, out, &attempt_err)) return YES;
        if (attempt == 2) { if (error) *error = attempt_err; return NO; }
        usleep(50 * 1000);
    }
    return NO;
}

BOOL DFU_ClearStatus(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
                     NSError **error) {
    IOUSBDevRequest req = {
        .bmRequestType = 0x21,
        .bRequest      = DFU_CLRSTATUS,
        .wValue        = 0,
        .wIndex        = interfaceNumber,
        .wLength       = 0,
        .pData         = NULL,
    };
    IOReturn rc = (*dev)->DeviceRequest(dev, &req);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(@"DFU_CLRSTATUS", rc);
        return NO;
    }
    return YES;
}

NSString *DFU_StateName(DFUState s) {
    switch (s) {
      case DFU_appIDLE:                return @"appIDLE";
      case DFU_appDETACH:              return @"appDETACH";
      case DFU_dfuIDLE:                return @"dfuIDLE";
      case DFU_dfuDNLOAD_SYNC:         return @"dfuDNLOAD_SYNC";
      case DFU_dfuDNBUSY:              return @"dfuDNBUSY";
      case DFU_dfuDNLOAD_IDLE:         return @"dfuDNLOAD_IDLE";
      case DFU_dfuMANIFEST_SYNC:       return @"dfuMANIFEST_SYNC";
      case DFU_dfuMANIFEST:            return @"dfuMANIFEST";
      case DFU_dfuMANIFEST_WAIT_RESET: return @"dfuMANIFEST_WAIT_RESET";
      case DFU_dfuUPLOAD_IDLE:         return @"dfuUPLOAD_IDLE";
      case DFU_dfuERROR:               return @"dfuERROR";
    }
    return [NSString stringWithFormat:@"unknown(%u)", (unsigned)s];
}

NSString *DFU_StatusName(uint8_t s) {
    static const char *names[] = {
        "OK", "errTARGET", "errFILE", "errWRITE", "errERASE",
        "errCHECK_ERASED", "errPROG", "errVERIFY", "errADDRESS",
        "errNOTDONE", "errFIRMWARE", "errVENDOR", "errUSBR", "errPOR",
        "errUNKNOWN", "errSTALLEDPKT"
    };
    if (s < sizeof(names)/sizeof(names[0])) {
        return [NSString stringWithFormat:@"%s(%u)", names[s], s];
    }
    return [NSString stringWithFormat:@"unknown(%u)", s];
}

// Open the DFU-mode Mbox (VID 0xFFFF PID 0xFFFE) and read its DFU status.
// Safe, read-only.
BOOL DFU_QueryStatus(DFUStatus *out, NSError **error) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) { if (error) *error = usbError(@"IOServiceMatching", -1); return NO; }
    // Try bulletproof-DFU first (0xFFFF:0xFFFE), then TAS1020A app-DFU
    // fallback (0x0DBA:0x1001) — the boot ROM enters app-DFU when the
    // EEPROM header signature is valid but dataType != APPCODE_TYPE
    // (e.g., a failed flash left dataType stuck at APPCODE_UPDATING).
    // Both accept standard DFU class requests at interface 0.
    io_iterator_t it = IO_OBJECT_NULL;
    io_service_t svc = IO_OBJECT_NULL;
    for (int attempt = 0; attempt < 2 && svc == IO_OBJECT_NULL; attempt++) {
        CFMutableDictionaryRef m = IOServiceMatching(kIOUSBDeviceClassName);
        if (!m) continue;
        uint16_t vid = attempt == 0 ? 0xFFFF : 0x0DBA;
        uint16_t pid = attempt == 0 ? 0xFFFE : 0x1001;
        CFDictionarySetValue(m, CFSTR(kUSBVendorID),  (__bridge CFNumberRef)@(vid));
        CFDictionarySetValue(m, CFSTR(kUSBProductID), (__bridge CFNumberRef)@(pid));
        if (IOServiceGetMatchingServices(kIOMainPortDefault, m, &it) != KERN_SUCCESS) continue;
        svc = IOIteratorNext(it);
        IOObjectRelease(it);
    }
    // The outer function still references `match` for its cleanup path
    // in the original, but the loop above supersedes it — just drop the
    // dangling reference.
    (void)match;
    if (!svc) { if (error) *error = usbError(@"DFU-mode Mbox not found", -1); return NO; }

    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    IOCreatePlugInInterfaceForService(svc, kIOUSBDeviceUserClientTypeID,
        kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(svc);
    IOUSBDeviceInterface **dev = NULL;
    (*plugin)->QueryInterface(plugin,
        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID*)&dev);
    (*plugin)->Release(plugin);
    if (!dev) { if (error) *error = usbError(@"QueryInterface(DFU device)", -1); return NO; }

    IOReturn rc = (*dev)->USBDeviceOpenSeize(dev);
    if (rc != kIOReturnSuccess) {
        (*dev)->Release(dev);
        if (error) *error = usbError(@"USBDeviceOpen(DFU device)", rc);
        return NO;
    }
    IOUSBDevRequest req = {
        .bmRequestType = 0xA1, .bRequest = DFU_GETSTATUS,
        .wValue = 0, .wIndex = 0, .wLength = sizeof(DFUStatus),
        .pData = out,
    };
    rc = (*dev)->DeviceRequest(dev, &req);
    (*dev)->USBDeviceClose(dev);
    (*dev)->Release(dev);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(@"DFU_GETSTATUS", rc);
        return NO;
    }
    return YES;
}

BOOL DFU_WithOpenDevice(BOOL (^body)(IOUSBDeviceInterface **dev,
                                     uint16_t interfaceNumber,
                                     NSError **error),
                        NSError **error) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) { if (error) *error = usbError(@"IOServiceMatching", -1); return NO; }
    // Try bulletproof-DFU first (0xFFFF:0xFFFE), then TAS1020A app-DFU
    // fallback (0x0DBA:0x1001) — the boot ROM enters app-DFU when the
    // EEPROM header signature is valid but dataType != APPCODE_TYPE
    // (e.g., a failed flash left dataType stuck at APPCODE_UPDATING).
    // Both accept standard DFU class requests at interface 0.
    io_iterator_t it = IO_OBJECT_NULL;
    io_service_t svc = IO_OBJECT_NULL;
    for (int attempt = 0; attempt < 2 && svc == IO_OBJECT_NULL; attempt++) {
        CFMutableDictionaryRef m = IOServiceMatching(kIOUSBDeviceClassName);
        if (!m) continue;
        uint16_t vid = attempt == 0 ? 0xFFFF : 0x0DBA;
        uint16_t pid = attempt == 0 ? 0xFFFE : 0x1001;
        CFDictionarySetValue(m, CFSTR(kUSBVendorID),  (__bridge CFNumberRef)@(vid));
        CFDictionarySetValue(m, CFSTR(kUSBProductID), (__bridge CFNumberRef)@(pid));
        if (IOServiceGetMatchingServices(kIOMainPortDefault, m, &it) != KERN_SUCCESS) continue;
        svc = IOIteratorNext(it);
        IOObjectRelease(it);
    }
    // The outer function still references `match` for its cleanup path
    // in the original, but the loop above supersedes it — just drop the
    // dangling reference.
    (void)match;
    if (!svc) { if (error) *error = usbError(@"DFU-mode Mbox not found — hold source-button + replug", -1); return NO; }

    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    IOCreatePlugInInterfaceForService(svc, kIOUSBDeviceUserClientTypeID,
        kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(svc);
    if (!plugin) { if (error) *error = usbError(@"CreatePlugInInterface(DFU dev)", -1); return NO; }
    IOUSBDeviceInterface **dev = NULL;
    (*plugin)->QueryInterface(plugin,
        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID*)&dev);
    (*plugin)->Release(plugin);
    if (!dev) { if (error) *error = usbError(@"QueryInterface(DFU dev)", -1); return NO; }
    IOReturn rc = (*dev)->USBDeviceOpenSeize(dev);
    if (rc != kIOReturnSuccess) {
        (*dev)->Release(dev);
        if (error) *error = usbError(@"USBDeviceOpen(DFU dev)", rc);
        return NO;
    }
    BOOL ok = body(dev, 0, error);
    (*dev)->USBDeviceClose(dev);
    (*dev)->Release(dev);
    return ok;
}

// ---------------------------------------------------------------------
// Descriptor probe
// ---------------------------------------------------------------------

static void probeOne(IOUSBDeviceInterface **dev, const char *label,
                     uint8_t type, uint8_t index, uint16_t langid,
                     uint16_t wLength) {
    uint8_t buf[256];
    memset(buf, 0, sizeof buf);
    if (wLength > sizeof buf) wLength = sizeof buf;

    IOUSBDevRequest req = {
        .bmRequestType = 0x80,              // D->H, standard, device
        .bRequest      = 0x06,              // GET_DESCRIPTOR
        .wValue        = (uint16_t)((type << 8) | index),
        .wIndex        = langid,
        .wLength       = wLength,
        .pData         = buf,
    };
    IOReturn rc = (*dev)->DeviceRequest(dev, &req);

    printf("  %-28s wLength=%-3u -> ", label, wLength);
    if (rc != kIOReturnSuccess) {
        printf("FAIL rc=0x%08x (%s)\n", rc,
               rc == kIOReturnNotResponding ? "not responding" :
               rc == kIOReturnTimeout       ? "timeout"        :
               rc == kIOUSBPipeStalled      ? "STALL"          : "?");
        return;
    }
    printf("OK %u byte(s):", req.wLenDone);
    for (UInt32 i = 0; i < req.wLenDone && i < 32; i++) printf(" %02x", buf[i]);
    if (req.wLenDone > 32) printf(" ...");
    printf("\n");
}

// Probe-specific open. openMboxDevice() uses USBDeviceOpenSeize and
// treats failure as fatal, which is right for flashing. Here the whole
// point is to interrogate a device that may be in a degraded state, so
// try Seize, then plain Open, and report exactly which succeeded —
// "cannot even be opened" is itself a finding worth printing.
static IOUSBDeviceInterface **openForProbe(NSError **error) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) return NULL;
    io_iterator_t it = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &it) != KERN_SUCCESS) return NULL;
    io_service_t svc = IO_OBJECT_NULL, cand;
    while ((cand = IOIteratorNext(it))) {
        CFNumberRef vid = (CFNumberRef)IORegistryEntrySearchCFProperty(cand,
            kIOServicePlane, CFSTR("idVendor"), NULL, kIORegistryIterateRecursively);
        int v = 0;
        if (vid) { CFNumberGetValue(vid, kCFNumberIntType, &v); CFRelease(vid); }
        if (v == 0x0DBA) { svc = cand; break; }
        IOObjectRelease(cand);
    }
    IOObjectRelease(it);
    if (!svc) { if (error) *error = usbError(@"no 0x0DBA device present", -1); return NULL; }

    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    kern_return_t kr = IOCreatePlugInInterfaceForService(svc,
        kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS || !plugin) {
        if (error) *error = usbError(@"IOCreatePlugInInterface(device)", kr);
        return NULL;
    }
    IOUSBDeviceInterface **dev = NULL;
    HRESULT hr = (*plugin)->QueryInterface(plugin,
        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID*)&dev);
    (*plugin)->Release(plugin);
    if (hr || !dev) {
        if (error) *error = usbError(@"QueryInterface(IOUSBDevice)", (IOReturn)hr);
        return NULL;
    }

    IOReturn rc = (*dev)->USBDeviceOpenSeize(dev);
    if (rc == kIOReturnSuccess) { printf("opened via USBDeviceOpenSeize\n\n"); return dev; }
    printf("USBDeviceOpenSeize -> 0x%08x, retrying plain USBDeviceOpen\n", rc);

    rc = (*dev)->USBDeviceOpen(dev);
    if (rc == kIOReturnSuccess) { printf("opened via USBDeviceOpen\n\n"); return dev; }
    printf("USBDeviceOpen      -> 0x%08x\n", rc);
    printf("\nNeither open succeeded. 0xe00002d8 is kIOReturnNotReady, which\n");
    printf("macOS returns for a device it has enumerated but not brought to a\n");
    printf("usable state — consistent with the device being UNCONFIGURED.\n\n");
    (*dev)->Release(dev);
    if (error) *error = usbError(@"could not open device", rc);
    return NULL;
}

BOOL DFU_DescriptorProbe(NSError **error) {
    IOUSBDeviceInterface **dev = openForProbe(error);
    if (!dev) return NO;

    printf("=== DESCRIPTOR PROBE ===\n");
    printf("Walking the same GET_DESCRIPTOR sequence a host issues during\n");
    printf("enumeration. A STALL or short reply pinpoints which request the\n");
    printf("firmware mishandles.\n\n");

    // Isolation test: issue the failing request FIRST, on a freshly
    // opened device, before any other transfer. If it succeeds here but
    // fails later in the sequence, the fault is accumulated EP0 state,
    // not the request itself.
    printf("ISOLATION — failing request issued first:\n");
    probeOne(dev, "STRING 1 (mfr, 18) FIRST", 0x03, 1, 0x0409, 18);
    printf("\n");

    printf("Device descriptor:\n");
    probeOne(dev, "DEVICE (first 8)",        0x01, 0, 0x0000, 8);
    probeOne(dev, "DEVICE (full 18)",        0x01, 0, 0x0000, 18);
    probeOne(dev, "DEVICE (over-ask 64)",    0x01, 0, 0x0000, 64);

    printf("\nConfiguration descriptor:\n");
    probeOne(dev, "CONFIG (header 9)",       0x02, 0, 0x0000, 9);
    probeOne(dev, "CONFIG (full 18)",        0x02, 0, 0x0000, 18);
    probeOne(dev, "CONFIG (over-ask 64)",    0x02, 0, 0x0000, 64);

    printf("\nString descriptors:\n");
    probeOne(dev, "STRING 0 (langid, 4)",    0x03, 0, 0x0000, 4);
    probeOne(dev, "STRING 0 (over-ask 255)", 0x03, 0, 0x0000, 255);
    probeOne(dev, "STRING 1 (mfr, 2)",       0x03, 1, 0x0409, 2);
    probeOne(dev, "STRING 1 (mfr, 8)",       0x03, 1, 0x0409, 8);
    probeOne(dev, "STRING 1 (mfr, 9)",       0x03, 1, 0x0409, 9);
    probeOne(dev, "STRING 1 (mfr, 16)",      0x03, 1, 0x0409, 16);
    probeOne(dev, "STRING 1 (mfr, 17)",      0x03, 1, 0x0409, 17);
    probeOne(dev, "STRING 1 (mfr, 18 exact)",0x03, 1, 0x0409, 18);
    probeOne(dev, "STRING 1 (mfr, 255)",     0x03, 1, 0x0409, 255);
    probeOne(dev, "STRING 2 (product, 2)",   0x03, 2, 0x0409, 2);
    probeOne(dev, "STRING 2 (product, 255)", 0x03, 2, 0x0409, 255);

    printf("\nConfiguration state:\n");
    UInt8 cfg = 0xFF;
    IOReturn rc = (*dev)->GetConfiguration(dev, &cfg);
    printf("  GetConfiguration -> rc=0x%08x value=%u%s\n", rc, cfg,
           (rc == kIOReturnSuccess && cfg == 0) ? "  (UNCONFIGURED)" : "");

    printf("\nAttempting SetConfiguration(1)...\n");
    rc = (*dev)->SetConfiguration(dev, 1);
    printf("  SetConfiguration(1) -> rc=0x%08x %s\n", rc,
           rc == kIOReturnSuccess ? "OK" : "FAILED");
    if (rc == kIOReturnSuccess) {
        cfg = 0xFF;
        (*dev)->GetConfiguration(dev, &cfg);
        printf("  GetConfiguration now -> %u\n", cfg);
    }

    (*dev)->USBDeviceClose(dev);
    (*dev)->Release(dev);
    return YES;
}
