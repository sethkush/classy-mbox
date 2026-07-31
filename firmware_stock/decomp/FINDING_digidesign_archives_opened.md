# The three .hqx archives, opened at last — and what is NOT in them

2026-07-30, prompted by "so nothing else can be figured out without the Mbox
plugged in?" asked a second time. The first answer found one unread source and
stopped, while naming two more it did not open. This is those two.

`reference/firmware/rev20/*.hqx` had sat unopened for the life of the project.
They are BinHex 4.0, Python dropped the `binhex` module in 3.11, and macOS ships
no converter, so nobody had looked. `tools/binhex_decode.py` now does it; the
decoded payloads are StuffIt archives that `unar` extracts.

    mboxfirmware20.hqx   -> Mbox Firmware Updater Rev20.sea
                            MboxFirmwareUpdater.rev20     38 KB PowerPC PEF
    mboxfirmware20x.hqx  -> Mbox Firmware 20 OS X.sit
                            Mbox Updater OSX.rev20       571 KB Mach-O ppc
    mboxusb101.hqx       -> Digidesign_USB_Driver_1.0.1.sea
                            Digidesign USB Driver         89 KB PowerPC PEF

## NEGATIVE RESULT: the Digidesign USB driver holds no audio knowledge

This was the one worth hoping for — Digidesign's own host driver, which might
have named the codec, the controls, or the vendor protocol. It does not. Every
class in its symbol table is DFU:

    CUSBDFUDevice   CDFUInterface   DFUDriver   CUSBDevice   CUSBQueue
    DFUDownloadInitiate  GetDFUStatusInitiate  ClearDFUStatusInitiate
    FindDFUInterface  PrepareNextBuffer  EnumerateInterfaces

"Digidesign USB Driver 1.0.1" is the **firmware-upgrade driver**, not the audio
driver. Its entire subject is the DFU protocol, which this project already
understands completely. The audio driver was a Pro Tools component and is not in
any archive we hold.

Recording this as a finding specifically so the next person does not spend the
effort hoping otherwise. `PrepareNextBuffer__13CDFUInterfaceFv` also explains
`disasm/rev20_preparenextbuffer.txt` — that file came from this binary during
task #141.

## The Rev 20 payload's provenance, and why it is NOT a validation

`Mbox Updater OSX.rev20` contains `firmware_stock/rev20_flasher_payload.bin`
**verbatim, all 11264 bytes, at offset 0x5A7EC**, and `rev20_eeprom.bin`'s
header follows at 0x5A7F8.

That looked at first like independent confirmation of the image the entire
decompilation rests on. **It is not.** `git log` on those files leads to commit
`a1ae70a`, whose message describes recovering the record format by
disassembling this very updater's `CUpdateDialog::StartDownload` and extracting
the payload from it. Finding the bytes there is re-finding where they came from.

What it does confirm is narrower and still worth having: the extraction was
clean and lossless, and nothing has drifted since. It is a checksum on our own
past work, not evidence from a second party.

This is the third time in one day that a "second source" turned out to share an
origin with the first — after the descriptors (same ROM as the firmware) and
this. The habit worth keeping is checking provenance *before* calling something
independent, not after.

## What this leaves

The discovery sources for the audio path are close to exhausted:

  * two firmware images — mined hard today
  * the Linux `snd-usb-audio` quirk — mined, gave the host control protocol
  * Digidesign's drivers — DFU only, nothing audio (this document)
  * the phase 0 device dumps — confirm the vendor-class config empirically:
    "Both interfaces present on alt 0 with class 255 (vendor), 0 endpoints"
  * TI reference sources and both datasheets — already used

What remains without hardware is mostly **implementation** (#157, #158, #159,
#160), all fully specified. The one piece of genuine *discovery* still available
from the disassembly is **#147** — the 8-frame capture artifact — which has not
had a C-port/DMA-framing read the way the codec word got one today.
