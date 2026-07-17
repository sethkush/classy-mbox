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

// Find & open the Mbox runtime device (VID 0x0DBA PID 0x1000).
static IOUSBDeviceInterface **openMboxDevice(NSError **error) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) { if (error) *error = usbError(@"IOServiceMatching", -1); return NULL; }
    CFDictionarySetValue(match, CFSTR(kUSBVendorID),  (__bridge CFNumberRef)@(0x0DBA));
    CFDictionarySetValue(match, CFSTR(kUSBProductID), (__bridge CFNumberRef)@(0x1000));
    io_iterator_t it = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &it) != KERN_SUCCESS) return NULL;
    io_service_t svc = IOIteratorNext(it);
    IOObjectRelease(it);
    if (!svc) return NULL;

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
    IOReturn rc = (*dev)->USBDeviceOpen(dev);
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

    IOUSBInterfaceInterface **iface = openMboxInterface(dev, 0, error);
    if (!iface) {
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
        return NO;
    }

    IOUSBDevRequest req = {
        .bmRequestType = 0x21,     // Class, H→D, to Interface
        .bRequest      = 0x00,
        .wValue        = 0x000A,
        .wIndex        = 0x0000,   // interface 0
        .wLength       = 0,
        .pData         = NULL,
    };
    IOReturn rc = (*iface)->ControlRequest(iface, 0, &req);
    if (rc != kIOReturnSuccess) {
        (*iface)->USBInterfaceClose(iface);
        (*iface)->Release(iface);
        (*dev)->USBDeviceClose(dev);
        (*dev)->Release(dev);
        if (error) *error = usbError(@"enter-DFU class request (via iface 0)", rc);
        return NO;
    }

    // Standard USB DFU 1.0: after DFU_DETACH (bRequest=0), the host must
    // issue a bus reset to actually trigger the mode transition. Otherwise
    // the device sits in appDETACH state indefinitely.
    (*iface)->USBInterfaceClose(iface);
    (*iface)->Release(iface);
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

BOOL DFU_Download(IOUSBInterfaceInterface **iface, uint16_t interfaceNumber,
                  uint16_t blockNum, const void *data, uint16_t length,
                  NSError **error) {
    (void)interfaceNumber;
    IOUSBDevRequest req = {
        .bmRequestType = 0x21,
        .bRequest      = DFU_DNLOAD,
        .wValue        = blockNum,
        .wIndex        = interfaceNumber,
        .wLength       = length,
        .pData         = (void *)data,
    };
    IOReturn rc = (*iface)->ControlRequest(iface, 0, &req);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(
            [NSString stringWithFormat:@"DFU_DNLOAD block=%u len=%u", blockNum, length], rc);
        return NO;
    }
    return YES;
}

BOOL DFU_GetStatus(IOUSBInterfaceInterface **iface, uint16_t interfaceNumber,
                   DFUStatus *out, NSError **error) {
    IOUSBDevRequest req = {
        .bmRequestType = 0xA1,
        .bRequest      = DFU_GETSTATUS,
        .wValue        = 0,
        .wIndex        = interfaceNumber,
        .wLength       = sizeof(DFUStatus),
        .pData         = out,
    };
    IOReturn rc = (*iface)->ControlRequest(iface, 0, &req);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(@"DFU_GETSTATUS", rc);
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
    CFDictionarySetValue(match, CFSTR(kUSBVendorID),  (__bridge CFNumberRef)@(0xFFFF));
    CFDictionarySetValue(match, CFSTR(kUSBProductID), (__bridge CFNumberRef)@(0xFFFE));
    io_iterator_t it = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &it) != KERN_SUCCESS) return NO;
    io_service_t svc = IOIteratorNext(it);
    IOObjectRelease(it);
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

    IOReturn rc = (*dev)->USBDeviceOpen(dev);
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
