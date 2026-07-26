# Digidesign Mbox 1 Firmware Updater — DFU flow (reverse-engineered)

**Binary:** `reference/firmware/rev20/Mbox Firmware 20 OS X/Mbox Updater OSX.rev20` (PPC Mach-O, 571168 B)

## Methodology
`otool -tvV` couldn't parse (obsolete Mach-O), but the `LC_SYMTAB` (symoff 429208, nsyms 3176) parses cleanly with a Python nlist reader. Class methods disassembled with `capstone` (`CS_ARCH_PPC, CS_MODE_32|CS_MODE_BIG_ENDIAN`) at their nlist symbol addresses.

## Class layout — `CDFUInterface` at `this`+N
- +0x00 vtable
- +0x04..+0x09 `SDFUStatus` struct (as returned by DFU_GETSTATUS): bStatus, poll[0..2], bState, iString
- +0x0C `mBuffer` (payload storage, malloc/freed)
- +0x18 stream pointer (input source for PrepareNextBuffer)
- +0x1C block-number counter (wValue on each DFU_DNLOAD)
- +0x20 bytes_sent (total)
- +0x2C mInterfaceNumber (wIndex on each request)

## DFU low-level primitives (bytecode-verified)

**`DFUDownload(this, uchar *data, int wLength, int wValue)`** — VM 0x3df64
Constructs setup packet on stack at (sp+0x40):
```
bmRequestType = 0x21   (class, host→device, interface recipient)
bRequest      = 0x01   (DFU_DNLOAD)
wValue        = arg r6 (block number, 16-bit)
wIndex        = mInterfaceNumber (from this+0x2C)
wLength       = arg r5 (16-bit)
data pointer  = arg r4
```
Calls `interface->ControlRequest(this=r3, pipeRef=0, &request)` via vtable offset 0x60.

**`GetDFUStatus(this, SDFUStatus *out)`** — VM 0x3e008
Setup packet:
```
bmRequestType = 0xA1   (class, device→host, interface)
bRequest      = 0x03   (DFU_GETSTATUS)
wValue        = 0
wIndex        = mInterfaceNumber
wLength       = 6      (SDFUStatus size)
data pointer  = arg r4
```

**No other DFU class-request opcodes appear in the binary. Confirmed by whole-binary byte-pattern scan:**
- `0x21 0x04` (DFU_CLRSTATUS): **0 hits**
- `0x21 0x06` (DFU_ABORT): **0 hits**
- `0xA1 0x02` (DFU_UPLOAD): appears in matches, but they are false positives in code sections — no `Upload`/`Abort`/`ClrStatus` methods exist in the class

## `DoThread()` state machine (VM 0x3e0d4)

8-state jump-table loop (state variable in r29, action-taken flag r26). One iteration per state; exits when r26=0 (no work done). Simplified transitions:

```
STATE 0: PrepareNextBuffer() → DFUDownload(hdr_buf, size_ret, blockN++)
         → state 3

STATE 1: send next data chunk
         DFUDownload(data_buf, min(remaining, 32), blockN++)
         → state 3

STATE 2: usleep(pollTimeoutByte × 1000)   // "1 ms per unit"
         → state 3

STATE 3: GetDFUStatus(&status)
         if err → exit
         if bState == 5 (dfuDNLOAD_IDLE):
             if all bytes sent → state 4      (send zero-length terminator)
             elif bytes_sent < total: → state 1  (more chunks)
             elif poll_byte != 0: → state 2   (sleep, then loop back to GETSTATUS)
             else: → state 0                  (immediate next)
         elif bState == 4 (dfuDNBUSY): → state 3  (poll again)
         else: → state 7 (exit)

STATE 4: DFUDownload(NULL, 0, blockN++)       // zero-length DNLOAD terminator
         → state 6

STATE 5: usleep(pollTimeoutByte × 1000)
         → state 6

STATE 6: GetDFUStatus(&status)
         if err → exit
         if bState == 7 (dfuMANIFEST_WAIT_RESET) → state 5  (sleep, poll)
         elif bState == 4 (dfuMANIFEST) → state 5           (sleep, poll)
         else → state 7 (done)

STATE 7: progress=100, free(mBuffer), exit
```

## bwPollTimeout handling — KEY DETAIL

Digidesign reads only **byte 7 of `this` (= `SDFUStatus.bwPollTimeout[2]`, MSB of the 24-bit LE poll value)** and multiplies by 1000 for microseconds → **`usleep(topByte × 1000)`**.

For a boot-ROM-returned 0x200000 (35-min) timeout, this decodes as: `bytes = 00 00 20` → `topByte = 0x20 = 32` → sleep 32 ms. Reasonable value derived by reading the “big” byte, not the full 24-bit LE integer.

## Divergences from our mboxflash

| Feature | Digidesign | Our mboxflash HEAD |
|---|---|---|
| DFU_CLRSTATUS | Never sent | Sent on transient errPROG/errUSBR (line ~429) |
| DFU_ABORT | Never sent | Sent post-manifest when state != dfuIDLE (line ~518) |
| DFU_UPLOAD | Never sent | Sent for post-flash readback verify |
| Per-block transport retry | Not present — DFU error → exit | Up to 3 retries per block (line 400) |
| Whole-flash restart on transient dfuERROR | Not present | Up to 2 restarts (line 386) |
| bwPollTimeout interpretation | `topByte × 1000` µs | Full 24-bit LE, capped at 200 ms |
| Threading model | Separate thread, main thread polls | Synchronous |

## Summary — what to change in mboxflash

The DFU_ABORT and DFU_UPLOAD calls (both added by commit 82042d0 "post-flash readback verify") are the most spec-suspicious — they send class requests during states where the boot ROM only handles them via `dfuErrStalledPkt()` → transitions to dfuERROR. That corrupts state right before the bus reset.

The CLRSTATUS retry logic (commit 9787940) is also unmatched in Digidesign's flow — the reference flasher treats any DFU error as terminal.

Digidesign's flow is essentially the DFU 1.0 spec §7 canonical sequence, without any recovery attempts.
