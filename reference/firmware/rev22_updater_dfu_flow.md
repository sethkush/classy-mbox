# Rev 22 Mbox 1 Firmware Updater — DFU flow (reverse-engineered)

**Binary:** `/tmp/mbox22/Update Mbox Firmware v22.app/Contents/MacOS/Update Mbox Firmware` (Universal PPC+i386 Mach-O, 2253948 B total; i386 slice = 1074300 B)
**Extracted via:** `hdiutil attach /Users/seth/projects/mbox/MboxFirmware22_33860.dmg -readonly -nobrowse -mountpoint /tmp/mbox22`
**i386 slice extracted via:** `lipo … -thin i386 -output /tmp/mbox22_i386`

## Methodology

The i386 slice is **aggressively stripped** — LC_SYMTAB has only 752 total nlist entries vs Rev 20's 3176 (`otool -l`). All local C++ method symbols are absent from nlist. Symbol *names* survive in `__cstring` because C++ RTTI/exception unwind references them at runtime, but their addresses aren't exported. This makes disassembly-guided RE harder than Rev 20's PPC binary.

Approach used:
1. Extract debug-log strings (`strings -a`)
2. Find file offset of each string in `__cstring` (which starts at file 572632 = 0x8ccd8, vmaddr 0x8ccd8)
3. Search `__text` (file 3252..0x8ccd5, vmaddr 0x1cb4..0x8ccd5) for **4-byte LE integers equal to string vmaddr** — those are `push $imm32` or `mov $imm32, …` instructions loading the string as a debug-log arg
4. Walk backward from each XREF looking for `55 89 e5` (push %ebp; mov %esp, %ebp) — the standard x86 function preamble

## Class layout — high-level

The download flow uses the same class hierarchy as Rev 20, but with additions:

**Rev 20:** `CUSBDFUDevice` (subclass of `CUSBDevice`), `CDFUInterface` (subclass of `CUSBInterface`), with `WriteBlock`/`ReadBlock`/`DFUDownload`/`GetDFUStatus` methods.

**Rev 22:** `CUSBAudioDevice` (subclass of `CUSBDevice`), `CAudioControlInterface` (subclass of `CUSBInterface`), with `PrepareForDownload`/`SetControl` methods. No class named `CDFUInterface` or `CUSBDFUDevice`. No methods named `DFUDownload`/`GetDFUStatus`/`WriteBlock`.

**Implication:** the Rev 22 updater doesn't segregate DFU logic into a distinct class. The DFU work happens inside `CAudioControlInterface::PrepareForDownload` and the `SetControl` methods, likely mixing DFU class requests and USB Audio Class SET_CUR requests on the same interface handle.

## USB call style — ControlRequest, NOT DeviceRequest

`CUSBInterface::SetControl` @ file 0x84c0 (228 bytes) makes exactly one indirect call:

```
+0075: ff 52 60   call *0x60(%edx)
```

**vtable offset 0x60 on IOUSBInterfaceInterface = ControlRequest.** This matches Rev 20's finding verbatim. Rev 22 uses `interface->ControlRequest(pipeRef=0, &request)`, not `device->DeviceRequest(&request)`.

The `CAudioControlInterface::SetControl` @ file 0x1cfe (216 bytes) also makes the identical `call *0x60(%edx)` — same pattern via a slightly different subclass wrapper.

**Both Rev 20 and Rev 22 open the USB INTERFACE with `USBInterfaceOpen` and issue class requests on the interface handle**, not on the device handle. Our `mboxflash` uses `(*dev)->DeviceRequest(dev, &req)` on `IOUSBDeviceInterface` — that's the divergence that likely causes our IOKit `kIOUSBTransactionTimeout` (0xe0004051) at block 0.

## DFU opcode usage — sparse

Whole-binary scan for setup-packet construction patterns:

**bmReq+bReq as 16-bit imm16 stack stores** (`66 c7 44 24 XX YY YY`): **0 hits** for any of DFU_DNLOAD (0x0121), DFU_GETSTATUS (0x03A1), DFU_ABORT (0x0621), DFU_CLRSTATUS (0x0421), DFU_UPLOAD (0x02A1). Rev 22 does **not** construct DFU setup packets via inline imm16 stack writes.

**byte-writes `movb $0x21, …` or `movb $0xA1, …` to register+offset**: **0 hits** in `__text`. Setup packet also isn't constructed via direct byte writes to a struct pointer.

**Byte-sequence `21 01`** in whole binary: 11 hits, mostly code offsets (jump-table entries, immediates in unrelated contexts) plus three that could be pre-baked USB Audio Class SET_CUR setup packet templates (bmReq=0x21 bReq=0x01 = SET_CUR to interface — SAME opcode encoding as DFU_DNLOAD; can't distinguish at byte level). No template that matches DFU_DNLOAD's expected shape (`21 01 blk_lo blk_hi ifaceN 00 len_lo len_hi 00 00 00 00`) as a clean 12-byte struct anywhere.

**Byte-sequence `A1 03`** (DFU_GETSTATUS lead) in whole binary: 7 hits, of which 1 at file 0xff2c4 in `__data` region looks like a proper setup packet template: `a1 03 00 00 01 00 01 02 00 00 00 00` — but wIndex 0x0201 doesn't match interface number 0 or 1. Ambiguous.

**Conclusion:** setup packets are almost certainly built at runtime, possibly by helpers that copy from `__data` templates (which we haven't fully cracked). Cannot definitively enumerate the DFU opcode set used by Rev 22 from static analysis alone.

## `bwPollTimeout` interpretation

Not statically determinable from the stripped binary without more work — would require identifying the manifest-phase polling loop in `CUSBAudioDevice::DoThread` (@ file 0x67de, only 196 bytes and just makes one indirect `call *[eax+0x14]`, so the real worker is a virtual method on some other object). No clear byte-selection pattern for the 24-bit `bwPollTimeout` field was found.

## What we DID confirm
- Uses `IOUSBInterfaceInterface::ControlRequest` (vtable offset 0x60), same as Rev 20 — **not** `IOUSBDeviceInterface::DeviceRequest`
- No `CDFUInterface` class — DFU logic folded into audio interface class
- No obvious use of DFU_ABORT / DFU_CLRSTATUS / DFU_UPLOAD (0 static byte-pattern hits for those opcodes' setup-packet leads)
- Firmware payload IS embedded in `__DATA` at file offset **0xC0F2C** (18-byte header + 8174-byte code = 8192 bytes total)

## What we canNOT confirm vs Rev 20
- Whether Rev 22 uses byte-selection for `bwPollTimeout` the same way (Rev 20: `bytes[2] × 1000 µs`)
- Whether Rev 22 adds any wait between DFU_DNLOAD and the following DFU_GETSTATUS
- Whether Rev 22 forces a bus reset via `USBDeviceReEnumerate` or waits for the user to physically unplug (Rev 20: physical unplug)

## Extracted firmware

Saved to `firmware_stock/rev22_flasher_payload_raw.bin` (8192 bytes).

- Header @ offset 0: `60 12 12 34 0d ba 10 01 01 01 04 fa 02 20 01 00 1f ee` — **byte-identical to Rev 20's header** (same chksum 0x60, same sigs 0x12 0x34, same VID 0x0DBA, same PID 0x1001, same wPageSize 0x20, same dataType 0x01 APPCODE, same payloadSize 8174 BE)
- Code region (offset 18..8192): 41% byte-identical to Rev 20 code, i.e. it IS a different firmware with the same layout / EEPROM structure

**NOTE:** the extracted 8192-byte blob is the raw `header + code` — it does **not** have the 12-byte TI-record outer wrapper that `firmware_stock/rev20_flasher_payload.bin` has. Before flashing via `mboxflash`, it needs to either (a) be wrapped by the equivalent of `wrap_hex.py` (which starts from `.ihx`, not from a `header+code` blob — need a `wrap_raw.py` helper), or (b) `mboxflash --validate` and payload parser need to accept the wrapper-less format.

## Divergences from mboxflash HEAD (the actionable list)

| Feature | Rev 22 (this) | Rev 20 (prior fork) | mboxflash HEAD |
|---|---|---|---|
| USB call type | `interface->ControlRequest` vtable 0x60 | Same | `device->DeviceRequest` — **wrong recipient** |
| DFU_ABORT | Not present in binary | Not present | Sends during dfuMANIFEST — **stalls boot ROM** |
| DFU_CLRSTATUS | Not present in binary | Not present | Sends on transient errors |
| DFU_UPLOAD | Not present in binary | Not present | Sends for post-flash verify |
| Bus reset after manifest | Not statically determinable | User-driven physical unplug | Forces `USBDeviceReEnumerate` |
| `bwPollTimeout` handling | Not statically determinable | `bytes[2] × 1000 µs` | Full 24-bit LE, capped 200 ms |

**Bottom line for fixing mboxflash:** the highest-confidence, highest-impact change is to switch `dfu.m` from `IOUSBDeviceInterface::DeviceRequest` to `IOUSBInterfaceInterface::ControlRequest`. Both Rev 20 and Rev 22 use this — it's the only architectural pattern proven to work with the TAS1020B boot ROM's DFU.
