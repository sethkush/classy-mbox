# #226 — DFU writes exactly payloadSize bytes, and that decides how a serial can be provisioned

2026-08-16. Read `UsbDfu.c` after `errFILE` stopped two flashes dead. The answer
was in the reference source the whole time, and `POLICY.md` already recorded the
same failure from 2026-07-22.

## The rule, from the ROM

`dfuDnloadTarget()` seeds a countdown from the EEPROM header:

```c
DfuStateMachine.dataRemain = DfuEeprgHeaderTemp.payloadSize;
```

`dfuDnloadData()` spends it, and refuses to overspend:

```c
if (DfuStateMachine.dataRemain >= (EngParms.dataCount - DfuStateMachine.headerCount)) {
    dfuEepromCopy((byte xdata *)(USB_EP0_XFERDATA + headerCount),
                  DfuStateMachine.bufferAddr,
                  EngParms.dataCount - headerCount);
    DfuStateMachine.bufferAddr += ...;
    DfuStateMachine.dataRemain -= ...;
} else {
    DfuStateMachine.status     = DFU_STATUS_errFILE;
    DfuStateMachine.state      = DFU_STATE_dfuERROR;
    DfuStateMachine.loadStatus = DFU_LOAD_ERROR;
    return;
}
if (DfuStateMachine.dataRemain == 0) { /* finalize: checksum, dataType, payloadSize */ }
```

Three consequences, and they are not obvious from the outside:

1. **Exactly `payloadSize` bytes are written. The next byte is `errFILE`.** Not
   truncation, not a warning -- the state machine goes to `dfuERROR` and stops.
2. **Completion is `dataRemain == 0`**, and completion is what finalizes the
   header. An over-long file can therefore NEVER complete: it sits in
   `dfuDNLOAD_IDLE` having written a prefix, with `dataType` left at
   `EEPROM_APPCODE_UPDATING`. That is the documented 2026-07-22 brick, where the
   unit then drops to app-DFU on boot instead of running firmware.
3. **`payloadSize` is also what the boot ROM copies into program RAM**, which is
   6016 bytes on this part. So padding the *payload* to reach a high EEPROM
   address is not an escape either -- it trades `errFILE` for an image that
   cannot exist in RAM, which is the other documented brick.

## What that kills

**A serial record cannot be appended to a flash image.** Not by padding to
0x1F00 (rejected, `errFILE`), and not by growing `payloadSize` to cover it
(rejected by the 6016-byte ceiling, and it would spend program RAM on data).

`tools/mkserial.py` was built on exactly that assumption and is refuted. The
2.5 KB of 0xFF padding it emitted is the same mistake `POLICY.md` records
against `wrap_hex.py` from 2026-07-22 -- same cause, same `errFILE`, same
`dataType` left updating. The root cause named in that entry is "we didn't read
`UsbDfu.c` before writing wrap_hex.py", and it was not read this time either.

**It cost nothing only because the ROM refuses rather than obeys.** Both units
were recovered with one flash each; the flasher's `DFU_ABORT` cleared the error
state first. A ROM that had written the padding would have bricked two units at
once, with no known-good control on the bench.

## What the same code makes possible

`dfuEepromCopy` writes only the bytes it is handed, at `bufferAddr`, and the
loop stops at `dataRemain == 0`. **Nothing beyond the payload is ever touched by
a flash.** So an EEPROM record above the payload is not merely reachable in
principle -- it is *persistent across reflashing*, which is precisely the
property provisioning wants.

The firmware already owns the write primitive: `eeprom_write_byte`, exercised
daily by `TLM_REQ_ENTER_DFU`. So provisioning goes through the running app, not
through DFU:

1. Flash a PROVISIONING image once. It carries a vendor request that writes the
   serial record at 0x1F00.
2. Send the unit's serial. One EEPROM write per byte, at the desk, unit in hand.
3. Flash the SHIPPING image (`MBOX_SERIAL_EEPROM=1`). It writes offsets
   18..payloadSize and leaves 0x1F00 alone.
4. The unit reads its serial at every boot, through any number of future
   reflashes, because no flash can reach that address.

This also removes the size problem the direct approach was about to hit. The
write path exists ONLY in the provisioning build, so the shipping image stays at
5989 bytes with 27 spare. A provisioning build has no such constraint -- it does
not need to be the image that ships.

## The one thing that must not be got wrong

The provisioning request writes EEPROM from the running app, so **it must be
bounds-checked to the record region and nothing else**. A request that can write
arbitrary offsets can overwrite the header or the payload, and unlike tonight
the ROM would not be there to refuse it -- that is a real brick, from software,
with no bench visit possible. The check belongs in the firmware, not in the host
tool, because the host tool is the thing most likely to be wrong.

## Status

`mkserial.py` is retired rather than fixed; its assumption does not hold. The
firmware read side (`serialno.c`) is untouched by this and still measures 5989.
The provisioning request is designed here and NOT yet built.
