#import "dfu.h"
#import <IOKit/IOKitLib.h>
#import <IOKit/usb/IOUSBLib.h>
#import <IOKit/IOCFPlugIn.h>

// -- Helpers to build/wrap IOUSBDevRequest.

static NSError *usbError(NSString *op, IOReturn rc) {
    return [NSError errorWithDomain:@"MBoxFlashUSB" code:rc userInfo:@{
        NSLocalizedDescriptionKey: [NSString stringWithFormat:
            @"%@ failed: 0x%08x", op, (unsigned)rc]
    }];
}

// Send a control transfer via IOUSBDeviceInterface (used for the enter-DFU
// request, which goes to interface 0 of the runtime-mode device).
static BOOL sendDeviceControlRequest(IOUSBDeviceInterface **dev,
                                     uint8_t bmRequestType, uint8_t bRequest,
                                     uint16_t wValue, uint16_t wIndex,
                                     uint16_t wLength, void *data,
                                     NSString *opName, NSError **error) {
    IOUSBDevRequest req = {
        .bmRequestType = bmRequestType,
        .bRequest      = bRequest,
        .wValue        = wValue,
        .wIndex        = wIndex,
        .wLength       = wLength,
        .pData         = data,
        .wLenDone      = 0,
    };
    IOReturn rc = (*dev)->DeviceRequest(dev, &req);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(opName, rc);
        return NO;
    }
    return YES;
}

// -- Locate & open the Mbox runtime device (VID 0x0DBA, PID 0x1000).
//    Returns an IOUSBDeviceInterface** pointer or NULL. Caller must Release.
static IOUSBDeviceInterface **openMboxDevice(NSError **error) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) { if (error) *error = usbError(@"IOServiceMatching", -1); return NULL; }
    int vid = 0x0DBA, pid = 0x1000;
    CFDictionarySetValue(match, CFSTR(kUSBVendorID),
        (__bridge CFNumberRef)@(vid));
    CFDictionarySetValue(match, CFSTR(kUSBProductID),
        (__bridge CFNumberRef)@(pid));
    io_iterator_t it = IO_OBJECT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &it);
    if (kr != KERN_SUCCESS) {
        if (error) *error = usbError(@"IOServiceGetMatchingServices", kr);
        return NULL;
    }
    io_service_t svc = IOIteratorNext(it);
    IOObjectRelease(it);
    if (!svc) return NULL;  // not connected

    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    kr = IOCreatePlugInInterfaceForService(svc, kIOUSBDeviceUserClientTypeID,
                                            kIOCFPlugInInterfaceID,
                                            &plugin, &score);
    IOObjectRelease(svc);
    if (kr != KERN_SUCCESS || !plugin) {
        if (error) *error = usbError(@"IOCreatePlugInInterface", kr);
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
        if (error) *error = usbError(@"USBDeviceOpen", rc);
        return NULL;
    }
    return dev;
}

BOOL DFU_SendEnterDFURequest(io_service_t unused, NSError **error) {
    (void)unused;
    IOUSBDeviceInterface **dev = openMboxDevice(error);
    if (!dev) return NO;
    BOOL ok = sendDeviceControlRequest(dev,
        /*bmRequestType*/ 0x21,      // Class, H→D, to Interface
        /*bRequest    */ 0x00,
        /*wValue      */ 0x000A,
        /*wIndex      */ 0x0000,     // audio-control interface number
        /*wLength     */ 0, NULL,
        @"enter-DFU custom request", error);
    (*dev)->USBDeviceClose(dev);
    (*dev)->Release(dev);
    return ok;
}

BOOL DFU_Download(IOUSBInterfaceInterface **iface, uint16_t interfaceNumber,
                  uint16_t blockNum, const void *data, uint16_t length,
                  NSError **error) {
    IOUSBDevRequest req = {
        .bmRequestType = 0x21,          // Class, H→D, to Interface
        .bRequest      = DFU_DNLOAD,
        .wValue        = blockNum,
        .wIndex        = interfaceNumber,
        .wLength       = length,
        .pData         = (void *)data,
    };
    IOReturn rc = (*iface)->ControlRequest(iface, 0, &req);
    if (rc != kIOReturnSuccess) {
        if (error) *error = usbError(
            [NSString stringWithFormat:@"DFU_DNLOAD block=%u len=%u", blockNum, length],
            rc);
        return NO;
    }
    return YES;
}

BOOL DFU_GetStatus(IOUSBInterfaceInterface **iface, uint16_t interfaceNumber,
                   DFUStatus *out, NSError **error) {
    IOUSBDevRequest req = {
        .bmRequestType = 0xA1,          // Class, D→H, to Interface
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
    // DFU 1.0 §6.1.2 — non-zero means error
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
