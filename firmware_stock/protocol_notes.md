# Digi Mbox 1 flasher — reverse-engineering notes

Source: `Update Mbox Firmware v22.app/Contents/MacOS/Update Mbox Firmware`,
extracted `i386` slice (1,074,300 bytes, sha256 stashed in
`reference/firmware/extracted/`, gitignored).

Analysis with radare2 6.1.8.

## Class hierarchy (from symbol strings)

Digi's flasher is built on their internal USB-audio toolkit rather
than being a bespoke one-shot binary:

- `CUSBDevice` — thin IOKit wrapper (Open/Close, GetDeviceReleaseNumber)
- `CUSBInterface` — Open/Close, SetAlternate, GetNumEndpoints, etc.
- `CAudioControlInterface : CUSBInterface` — the audio-control iface
  (iface 0). Owns `PrepareForDownload`, `SetInputSource`, etc.
- `CUSBAudioDevice : CUSBDevice` — composite device. Methods:
  Initialize / Finalize / FindAudioControlInterface / SetSampleRate /
  StartThread / DoThread / **PrepareForDownload** / AllocateMemTest.
- `CStream` / `CUSBIsocInput` / `CUSBIsocOutput` — audio path
  (present but role in flashing not yet nailed down).

## `CAudioControlInterface::PrepareForDownload` @ 0x00002428

**This is the "enter DFU mode" USB request. It does exactly one
control transfer:**

| Field         | Value                                             |
|---------------|---------------------------------------------------|
| bmRequestType | `0x21` (Host→Device, **Class**, to Interface)     |
| bRequest      | `0x00` (vendor-defined; NOT a standard UAC code)  |
| wValue        | `0x000a`                                          |
| wIndex        | `bInterfaceNumber` (loaded from `this + 0x18` —   |
|               |  will be 0 = the audio control interface)          |
| wLength       | 0                                                 |
| data          | NULL                                              |

Issued via `IOUSBInterfaceInterface::ControlRequest` (vtable slot at
byte offset 0x60 from the vtable base).

After this call, the device is expected to be in a firmware-download
state ready to accept the payload.

## `CUSBAudioDevice::PrepareForDownload` @ 0x00007984

Thin wrapper. Passes `this + 0x604` (the CAudioControlInterface
sub-object) to `CAudioControlInterface::PrepareForDownload` and
returns. Nothing else.

## Orchestration @ 0x000077de (calls PrepareForDownload)

Enters CFRunLoop event loop after calling PrepareForDownload. Registers
a CFRunLoopSource stored at `this + 0x43c` — this is the IOKit
async-completion source, so the actual firmware-byte transfer happens
in run-loop-driven async callbacks, not synchronously here.

**Open question:** which callback pushes the firmware bytes to the
device? Probably CUSBAudioDevice::DoThread + CUSBIsocOutput::DoTransfer,
but not confirmed. TODO: trace from `fcn.00009b66` (called by
AllocateMemTest) to see where the payload buffer gets shipped.

## Firmware payload location

**Embedded in `__DATA/__data` at file offset ~0xC0880..~0xC2580**
(roughly 7.2 KB). The section as a whole spans 0xBE080..0xC3BCC
(23.4 KB) but includes debug strings, GUI icon bitmap data, and
trailing FF/00-fill padding besides the firmware itself.

Markers that identify the payload:
- **v22 USB device descriptor** at `0xC16CB`:
  `12 01 10 01 00 00 00 08 BA 0D 00 10 22 00 01 02` —
  matches VID=0x0DBA, PID=0x1000, bcdDevice=0x0022, exactly what
  the flasher installs.
- **8051 code preceding the descriptor** (e.g. at 0xC169B):
  `A8 D2 AF 90 FF FC E0 44 80 F0 E4 F5 0A 22 75 2C 04` decodes
  as reasonable 8051 instructions (`MOVX A,@DPTR / ORL A,#0x80 /
  MOVX @DPTR,A / CLR A / MOV 0x0A,A / RET / MOV 0x2C,#0x04`).
- **8051 code near payload end** (e.g. at 0xC2510):
  `90 FF DE F0 90 FF D5 F0 90 FF B1 E0` — writes to the TAS1020A's
  0xFFXX external-memory region, i.e. USB Interface Function Registers
  (endpoint FIFOs / control regs), textbook USB firmware behavior.

Extracted candidates:
- `firmware_stock/candidate_data_section.bin` (23.4 KB) — whole __data
  section, over-inclusive but definitely contains the payload.
- `firmware_stock/candidate_firmware_slice.bin` (7.4 KB) — narrower
  window between the icon data and the FF-fill trailer.

Exact boundaries need one more pass: locate the loader in the flasher
(the function that reads this buffer and pushes it to USB) and read
its length / offset arguments directly.

## Deeper RE findings

### Payload transfer probably uses isoc OUT via CUSBIsocOutput

The flasher's error-string dump confirms the code path exists:

    "CUSBIsocOutput::DoTransfer ERROR: WriteIsochPipeAsync got error: 0x%X"

`CUSBIsocOutput` is the isochronous-output wrapper — its `DoTransfer`
method calls `WriteIsochPipeAsync` on the IOUSB interface.

### CUSBIsocOutput::HackStartup @ 0xafde is a warmup, not the payload transfer

Disassembly showed HackStartup:
1. Zeros a 288-byte buffer at `__bss+0xfeca0..0xfedc0`.
   (288 = 0x120 = 2ch × 3B × 48 samples, one audio isoc packet.)
2. Sets counters at `__bss+0xfec80..0xfec86`.
3. Calls interface vtable slot at byte offset 0x5c on `this->0x20`
   (likely `SetPipePolicy` or `ClearPipeStall`).
4. Submits the zero buffer via interface vtable slot at byte offset
   0x90 on `this->0x80` — very likely `WriteIsochPipeAsync`.

This is a "prime the pump" — one empty isoc packet to open the pipe.
The firmware bytes get pushed by follow-up transfers we haven't
pinpointed yet.

### Firmware payload doesn't get referenced by absolute address

No immediate in the binary points to any address in the range
`0xC1800..0xC3500` (VMAs of the presumed payload region). So the
payload access must be one of:
- Indirect through a pointer table set up at startup.
- Via `dyld` relocation slots that are patched at load-time.
- Via a base-register + offset computed lazily.

The flasher is stripped and has no OBJC/C++ RTTI symbols to help
identify vtable slots by name. The IOKit headers we'd need to map
`call [reg + 0xBC]` etc. to specific methods aren't part of the
binary.

### Pragmatic conclusion

Continuing pure static analysis to nail down the exact payload
transfer will keep chipping away but is bounded by the missing
type info. Faster paths:

- **Dynamic capture.** Run Digi's flasher against the Mbox with a
  USB analyzer (or `usbmon` on Linux with the Mbox on a Linux host
  running the flasher under x86_64 emulation — nope, it's a Mach-O).
  On Apple Silicon, **UTM with QEMU x86_64 emulation of macOS 10.14**
  works with USB passthrough — the flasher can then run natively on
  the emulated Mac, and we capture the USB packets from either the
  Linux/macOS host with `wireshark + usbmon` or `usbdump`.
- **Instrument the flasher.** Since we know CAudioControlInterface::
  PrepareForDownload's exact bytes, we could patch the i386 binary
  to also log the follow-up transfers, or attach lldb (if we can
  run i386 anywhere — see UTM above).
- **Ask someone.** Digi/Avid stopped supporting the Mbox 1 around
  2010; the flasher is 2007. Damien Zammit (Linux driver author) is
  a candidate to ask — he might have done this RE himself while
  writing the kernel driver.

## Known-answers box

| Question | Answer |
|---|---|
| "Enter DFU mode" USB request | ✓ Class SET to iface 0: bmRequestType=0x21 bRequest=0x00 wValue=0x000A |
| Firmware payload location | ✓ inside __DATA/__data (VMA ~0xC1800..0xC3500) but access mechanism indirect |
| How payload is transferred to the device | ~ Almost certainly isoc OUT via `WriteIsochPipeAsync` on iface 1 EP 0x02 (based on `CUSBIsocOutput::DoTransfer ... WriteIsochPipeAsync` error string). Exact chunking/framing not yet pinned. |
| Post-flash sequence | ✓ Prompt user to unplug (power-cycle); on reconnect device enumerates with new bcdDevice |
| Verification / checksum | ✗ Not identified in static analysis |

## Rewriting the flasher for arm64 macOS

Once we've pinned down the payload transfer method:
1. Open `0x0dba:0x1000` with IOUSBHost.
2. Read `bcdDevice`. Warn if already 0x22.
3. Open iface 0 (audio control).
4. Send the class control transfer described in §PrepareForDownload.
5. Open iface 1, select alt setting 1 (activates ISOC endpoints).
6. Push the firmware payload via isoc OUT on EP 0x02 (or via
   whatever method Phase 2 of the RE confirms).
7. Wait for USB re-enumeration (device disconnects, comes back
   with new bcdDevice).
8. Verify new bcdDevice = 0x22.

Target: `mboxflash` CLI, ~500 LOC of ObjC. Uses only public IOUSBHost
API — no kernel bits.
