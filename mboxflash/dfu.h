// USB DFU 1.0 wire protocol — the standard, plus Digi's custom enter-DFU request.

#ifndef MBOXFLASH_DFU_H
#define MBOXFLASH_DFU_H

#import <Foundation/Foundation.h>
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
BOOL DFU_Download(IOUSBInterfaceInterface **iface, uint16_t interfaceNumber,
                  uint16_t blockNum, const void *data, uint16_t length,
                  NSError **error);
BOOL DFU_GetStatus(IOUSBInterfaceInterface **iface, uint16_t interfaceNumber,
                   DFUStatus *out, NSError **error);
BOOL DFU_QueryStatus(DFUStatus *out, NSError **error);
NSString *DFU_StateName(DFUState s);
NSString *DFU_StatusName(uint8_t bStatus);

#endif
