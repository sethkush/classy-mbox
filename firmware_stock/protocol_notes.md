# Digi Mbox 1 flasher — reverse-engineering notes

## Executive summary

**The Mbox 1 uses standard USB DFU 1.0 for firmware upload**, plus one
custom vendor request to enter DFU mode. All the protocol details are
in the publicly-documented USB DFU 1.0 spec:
<https://www.usb.org/sites/default/files/DFU_1.1.pdf>.

This was pinned down by reverse-engineering **Digi's OS X Rev 20
flasher** (`Mbox Updater OSX.rev20`, PPC Mach-O, ~571 KB) — a much
cleaner codebase than the v22 flasher, with **full C++ symbol names
preserved** (mangled Itanium ABI). Dedicated classes `CUSBDFUDevice`
and `CDFUInterface` implement the DFU state machine cleanly.

## Sources analyzed

| Binary | Format | Size | RE quality |
|---|---|---|---|
| `Update Mbox Firmware v22.app` (i386 slice) | Mach-O i386 | 1074 KB | Stripped — vtable slots without names, hard |
| `Mbox Updater OSX.rev20` | Mach-O PPC | 571 KB | Full C++ symbols; opened the protocol |
| `MboxFirmwareUpdater.rev20` | PowerPC PEF (OS 9) | 38 KB | Not yet analyzed — likely simplest of all |

All three write to the same device via the same USB protocol; the
Rev 20 vs v22 difference is just the firmware payload.

## Complete USB protocol

### Step 1 — Custom "enter DFU mode" request

Sent to the audio-control interface (iface 0) on the normal-mode
device (VID `0x0DBA` PID `0x1000`, bcdDevice = current firmware):

| Field         | Value                                         |
|---------------|-----------------------------------------------|
| bmRequestType | `0x21` (Class, Host→Device, to Interface)     |
| bRequest      | `0x00`                                        |
| wValue        | `0x000A`                                      |
| wIndex        | `0` (iface 0)                                 |
| wLength       | 0, data NULL                                  |

This is Digi's ONLY custom request. USB DFU 1.0 spec normally uses
`DFU_DETACH` (bRequest=0x00, wValue=timeout) on a runtime interface
that has a `DFU_FUNCTIONAL` descriptor — Digi departed from spec on
the trigger but the rest of the protocol is standard.

After this request, the device detaches and re-enumerates in DFU mode
(likely with a DFU interface exposed at a specific altsetting or on a
new configuration). The user is prompted to unplug/replug for a
clean power-cycle before the actual download.

### Step 2 — Standard DFU_DNLOAD (chunked)

For each block, issue:

| Field         | Value                                              |
|---------------|----------------------------------------------------|
| bmRequestType | `0x21` (Class, Host→Device, to Interface)          |
| bRequest      | `0x01` (`DFU_DNLOAD`)                              |
| wValue        | block number (starts at 0, increments per block)   |
| wIndex        | DFU interface number (from device descriptor)      |
| wLength       | block byte length                                  |
| data          | block bytes                                        |

Reconstructed from disassembly of `CDFUInterface::DFUDownload(u8*, int, int)`
at VMA `0x3DF64` in the Rev 20 OS X flasher — see full annotation in
`disasm/rev20_dfudownload.txt`.

### Step 3 — Standard DFU_GETSTATUS after each block

Poll for `dfuDNLOAD_IDLE` state before sending the next block:

| Field         | Value                                              |
|---------------|----------------------------------------------------|
| bmRequestType | `0xA1` (Class, Device→Host, to Interface)          |
| bRequest      | `0x03` (`DFU_GETSTATUS`)                           |
| wValue        | 0                                                  |
| wIndex        | DFU interface number                               |
| wLength       | 6                                                  |

Returns the standard 6-byte `SDFUStatus`:
```c
struct SDFUStatus {
    uint8_t  bStatus;         // 0 = OK, non-zero = error code
    uint8_t  bwPollTimeout[3]; // ms to wait before next GETSTATUS
    uint8_t  bState;           // state machine (dfuIDLE=2, dfuDNBUSY=4, ...)
    uint8_t  iString;          // string descriptor index
};
```

Confirmed by presence of `CDFUInterface::GetDFUStatus(SDFUStatus*)`
symbol at VMA `0x3E008`.

### Step 4 — Zero-length DFU_DNLOAD to end

Standard DFU protocol: send DFU_DNLOAD with `wLength=0, data=NULL`
to signal end-of-download. Device transitions to `dfuMANIFEST_SYNC`,
we poll `DFU_GETSTATUS` until it enters `dfuMANIFEST` and then
resets.

### Step 5 — Manual power cycle

Digi's flasher **prompts the user to unplug and re-plug the Mbox**
rather than relying on the standard DFU auto-reset. This is a design
choice — probably because the TAS1020A doesn't clean up USB state
gracefully on soft-reset, or Digi wanted a safety pause. Our
`mboxflash` should follow the same pattern.

### Step 6 — Verify

After reconnect, read the new `bcdDevice` field of the standard USB
device descriptor. Should now match the target version (0x22 for
v22 firmware).

## Payload format (embedded in flasher binary)

`CDFUInterface::Download(uint8_t* payload, size_t length)` at
VMA `0x3DE18` calls `memcpy` to internalize the payload, then
`pthread_create` for `Thread` which does the flash work.

`CDFUInterface::PrepareNextBuffer()` at VMA `0x3DEC0` shows the
payload structure: it's a **stream of records**, each:

```
+0:   uint32_t chunk_size            // little-endian? (need to verify with actual bytes)
+4:   uint8_t  header[12]            // per-block metadata: possibly target address,
                                     //   flags, checksum — TBD by comparing across
                                     //   Rev 20 vs v22 payloads
+16:  uint8_t  data[chunk_size]      // the actual firmware bytes to send
```

Each record becomes one DFU_DNLOAD control transfer with `wValue`
incrementing per record.

## Key CDFUInterface class layout (recovered from field offsets)

```c
class CDFUInterface {
    // ... base class stuff at 0x00..0x07 ...
    /*0x08*/ uint8_t  state_or_status;      // Download() zeros this
    /*0x0C*/ uint8_t* mBuffer;              // allocated by Download()
    /*0x10*/ uint32_t mBufferLength;        // saved from Download() arg
    /*0x14*/ uint32_t mProgressPercent;
    /*0x18*/ uint8_t* mCurrentReadPos;      // walks through mBuffer
    /*0x1C*/ uint32_t ?;
    /*0x20*/ uint32_t mBytesWritten;
    /*0x28*/ void**   mInterfaceRef;        // IOUSBInterfaceInterface**
    /*0x2C*/ uint16_t mInterfaceNumber;     // used as DFU_DNLOAD wIndex
};
```

## Payload location in Rev 20 vs v22

Rev 20 binary at ~571 KB has its firmware payload embedded too, but
much easier to spot given the smaller binary. Since we care about the
**PROTOCOL** (which is the same across firmware versions) and not
the payload bytes (we already have v22 embedded in the v22 flasher's
`__data`), the exact byte location can be pinned later — this note
focuses on the wire protocol.

## Writing mboxflash

With the protocol nailed down, the arm64 macOS flasher is a
manageable amount of ObjC. Structure:

```
mboxflash/
├── main.m              (~200 LOC): open device, orchestrate the 6 steps
├── dfu.{h,c}           (~300 LOC): standard USB DFU 1.0 state machine
├── enter_dfu.c         (~50 LOC):  Digi's custom detach request
├── payload_parser.c    (~100 LOC): unpack the [size][header][data] records
└── Makefile
```

Uses IOUSBHost, no external deps. Runs on any modern macOS on
Apple Silicon or Intel.

Fallback: everything except the custom-detach request is stock DFU,
so `dfu-util` (open source DFU tool) can probably do steps 2-6 for
us once step 1 has put the device in DFU mode. Worth trying before
writing a bespoke tool.
