// USB DFU 1.0 wire protocol — the standard, plus Digi's custom enter-DFU request.

#ifndef MBOXFLASH_DFU_H
#define MBOXFLASH_DFU_H

#import <Foundation/Foundation.h>

// Unit selection. nil = no filter; set from --serial <SN>. See the
// refuse-to-guess note on openMboxDevice in dfu.m.
extern NSString *gMboxTargetSerial;
BOOL MBox_IsOurDevice(int vid, int pid);
#import <IOKit/usb/IOUSBLib.h>

// DFU 1.0 spec: https://www.usb.org/sites/default/files/DFU_1.1.pdf §6.1

typedef NS_ENUM(uint8_t, DFURequest) {
    DFU_DETACH      = 0x00,  // (not used — Digi uses a custom request instead)
    DFU_DNLOAD      = 0x01,
    DFU_UPLOAD      = 0x02,
    DFU_GETSTATUS   = 0x03,
    DFU_CLRSTATUS   = 0x04,
    DFU_GETSTATE    = 0x05,
    DFU_ABORT       = 0x06,
};

typedef NS_ENUM(uint8_t, DFUState) {
    DFU_appIDLE                = 0,
    DFU_appDETACH              = 1,
    DFU_dfuIDLE                = 2,
    DFU_dfuDNLOAD_SYNC         = 3,
    DFU_dfuDNBUSY              = 4,
    DFU_dfuDNLOAD_IDLE         = 5,
    DFU_dfuMANIFEST_SYNC       = 6,
    DFU_dfuMANIFEST            = 7,
    DFU_dfuMANIFEST_WAIT_RESET = 8,
    DFU_dfuUPLOAD_IDLE         = 9,
    DFU_dfuERROR               = 10,
};

typedef struct __attribute__((packed)) {
    uint8_t bStatus;
    uint8_t bwPollTimeout[3];  // little-endian 24-bit ms
    uint8_t bState;
    uint8_t iString;
} DFUStatus;

// Digi's custom "enter DFU mode" request — sent to the audio-control interface
// (iface 0) on the runtime-mode device:
//   bmRequestType 0x21 (Class, H→D, to Interface)
//   bRequest      0x00
//   wValue        0x000A
//   wIndex        0
//   wLength       0
BOOL DFU_SendEnterDFURequest(io_service_t device, NSError **error);

// Standard DFU requests, once the device has re-enumerated in DFU mode.
BOOL DFU_Download(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
                  uint16_t blockNum, const void *data, uint16_t length,
                  NSError **error);
// DFU_Upload: read one block back from device memory. `length` is the
// max bytes to accept; `*outLength` receives the actual returned size
// (a short/zero read signals end-of-image per DFU 1.0 §6.2). `data`
// must point to a buffer of at least `length` bytes.
BOOL DFU_Upload(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
                uint16_t blockNum, void *data, uint16_t length,
                uint16_t *outLength, NSError **error);
// DFU_GetStatus_Retry: same as DFU_GetStatus but retries up to N
// times on transport error with 50 ms backoff. Safe: GET_STATUS is
// spec-idempotent (§6.1.3). Fork audit 2026-07-24 (scope-3 retry).
BOOL DFU_GetStatus_Retry(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
                         DFUStatus *out, NSError **error);

BOOL DFU_GetStatus(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
                   DFUStatus *out, NSError **error);
BOOL DFU_QueryStatus(DFUStatus *out, NSError **error);
// DFU_Abort (§6.1.4): from any non-error state, returns the state machine
// to dfuIDLE without touching any user memory. Useful to recover from
// dfuUPLOAD_IDLE after --dump so --flash (which requires dfuIDLE) accepts.
// DFU_ClearStatus (§6.1.2): from dfuERROR back to dfuIDLE. Boot ROM
// preserves loadStatus/bufferAddr/dataRemain across CLRSTATUS, but a
// subsequent DFU_DNLOAD with wValue=0 resets loadStatus=DFU_LOAD_NOT
// (UsbDfu.c:500), falling into dfuDnloadHeader which re-inits bufferAddr
// and dataRemain from scratch. Safe to restart the whole flash after
// CLRSTATUS. Fork audit 2026-07-24.
BOOL DFU_ClearStatus(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
                     NSError **error);

BOOL DFU_Abort(IOUSBDeviceInterface **dev, uint16_t interfaceNumber,
               NSError **error);

// Open the DFU-mode Mbox, execute a callback with an open handle
// suitable for DFU_Download / DFU_GetStatus calls. Cleanly closes on exit.
// The callback returns YES to continue, NO to abort (and set *error).
BOOL DFU_WithOpenDevice(BOOL (^body)(IOUSBDeviceInterface **dev,
                                     uint16_t interfaceNumber,
                                     NSError **error),
                        NSError **error);

NSString *DFU_StateName(DFUState s);
NSString *DFU_StatusName(uint8_t bStatus);

// Host-side descriptor probe. Opens any VID 0x0DBA device and walks the
// standard GET_DESCRIPTOR sequence a host performs during enumeration,
// printing the raw bytes and the IOReturn for each step.
//
// Exists because the interesting failures happen AFTER the device is on
// the bus, where macOS has already given up quietly and ioreg only shows
// the absence of a property. Asking the device directly costs no reflash
// and no SDA short — the firmware under test is running and reachable.
BOOL DFU_DescriptorProbe(NSError **error);

#endif
