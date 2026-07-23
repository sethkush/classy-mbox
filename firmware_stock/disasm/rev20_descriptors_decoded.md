# Rev 20 USB descriptor blob — byte-for-byte decode

Decoded from `firmware_stock/rev20_firmware_code.bin` (raw code image, CPU
addr == file offset).

## Locations in ROM

| CPU addr | What                                                            |
|----------|-----------------------------------------------------------------|
| 0x0596   | Device descriptor (18 B)                                        |
| 0x05A8   | Configuration 1 (audio) — 200 B                                 |
| 0x0670   | Configuration 2 (vendor/legacy) — 54 B, **not advertised**      |
| 0x06A6   | String descriptor block (LangID + 2 UTF-16LE strings)           |

## How they get pushed to XDATA at reset

The MOVC-driven walker documented in `NOTES.md` at "0x0A50" turned out
NOT to be the descriptor copy. `NOTES.md`'s addresses are offsets into
`rev20_eeprom.bin` (18-byte header included); CPU addresses are −0x12.
The walker at **CPU 0x0A50** just reads a tiny opcode stream sourced
from **CPU 0x0F9C** — its only job is to zero 13 IRAM state bytes
(`IRAM[0x08]=3`, all others=0). No USB payload.

The actual descriptor copy path is a separate `MOVC / MOVX @DPTR` fetch
that the EP0 GET_DESCRIPTOR handler runs against the tables above,
picked by (bDescriptorType, bDescriptorIndex, wIndex). Confirmed by the
CS_INTERFACE table at 0x05A8-0x0670 being a valid UAC1 descriptor chain
with self-consistent wTotalLength=200.

---

## Device descriptor (0x0596)

```
12 01 10 01 00 00 00 08 ba 0d 00 10 20 00 01 02 00 01
```
| Field           | Value      | Note                                          |
|-----------------|------------|-----------------------------------------------|
| bcdUSB          | 0x0110     | USB 1.1                                       |
| bDeviceClass    | 0x00       | Composite (class at interface level)          |
| bDeviceSubClass | 0x00       |                                               |
| bDeviceProtocol | 0x00       |                                               |
| bMaxPacketSize0 | 8          |                                               |
| idVendor        | 0x0DBA     | Digidesign                                    |
| idProduct       | 0x1000     |                                               |
| bcdDevice       | 0x0020     | Rev 20                                        |
| iManufacturer   | 1          | "Digidesign Inc"                              |
| iProduct        | 2          | "Mbox USB Audio Device copyright Digidesign 2001" |
| iSerialNumber   | 0          | **no SN**                                     |
| bNumConfigurations | 1       | **Config 2 exists in ROM but is not exposed** |

---

## Configuration 1 — Audio (0x05A8, 200 B)

```
09 02 c8 00 03 01 00 80 f0
```
wTotalLength=200, bNumInterfaces=**3**, bConfigValue=1, iConfig=0,
bmAttributes=0x80 (bus-powered), MaxPower=0xF0 × 2 mA = **480 mA**.

### Interface 0 — AudioControl (bIfnum=0, bAlt=0, 0 EPs)

Class=1/Sub=1/Proto=0. Class-specific chain (72 B, matches wTotalLength
in AC HEADER):

**AC HEADER** `0a 24 01 00 01 48 00 02 01 02`
- bcdADC=0x0100, wTotalLength=0x0048, bInCollection=2, AC->[1, 2]

**IT (bTerminalID=1)** `0c 24 02 01 01 01 05 02 03 00 00 00`
- wTerminalType=0x0101 (**USB Streaming — playback path source**),
  bAssocTerminal=5, bNrChannels=2, wChannelConfig=0x0003 (L+R)

**IT (bTerminalID=2)** `0c 24 02 02 01 06 03 02 03 00 00 00`
- wTerminalType=0x0601 (**Analog connector — line-in**),
  bAssocTerminal=3, bNrChannels=2, wChannelConfig=L+R

**IT (bTerminalID=6)** `0c 24 02 06 05 06 00 02 03 00 00 00`
- wTerminalType=0x0605 (**S/PDIF Interface — digital-in**),
  bAssocTerminal=0, bNrChannels=2, wChannelConfig=L+R

**OT (bTerminalID=3)** `09 24 03 03 03 06 02 01 00`
- wTerminalType=0x0603 (**Line connector — line-out**),
  bAssocTerminal=2, **bSourceID=1** (fed by IT(1) USB stream), iTerminal=1

**OT (bTerminalID=4)** `09 24 03 04 01 01 01 05 00`
- wTerminalType=0x0101 (**USB Streaming — capture sink**),
  bAssocTerminal=1, **bSourceID=5** (fed by SU(5))

**SU (bUnitID=5)** `08 24 05 05 02 02 06 00`
- bNrInPins=2, baSourceID=[**2** (Analog), **6** (S/PDIF)], iSelector=0
- ⇒ **Input-source selection is exposed as a standard UAC1 Selector
  Unit. Class-compliant hosts drive it with SET_CUR/GET_CUR on the SU
  via bmReq=0x21/0xA1, wIndex=(SU_ID<<8)|AC_iface.**

### Topology
```
Playback:  IT(1)=USB ────────────────────→ OT(3)=Line-out
Capture:   IT(2)=Analog ─┐
                          ├→ SU(5) ─────→ OT(4)=USB
           IT(6)=S/PDIF ──┘
```
**No Feature Unit anywhere.** Rev 20 exposes zero software-adjustable
volume/mute/gain. Everything analog is fixed by the front-panel knobs.
The only software-adjustable control is the input Selector.

### Interface 1 — AudioStreaming (playback, host→device)
- alt 0: `09 04 01 00 00 01 02 00 00` — zero-bandwidth idle
- alt 1: `09 04 01 01 01 01 02 00 00` — 1 EP active
  - **AS_GENERAL** `07 24 01 09 01 01 00` — bTerminalLink=**0x09**,
    bDelay=1, wFormatTag=0x0001 (PCM). *Note: no terminal with ID 9
    exists. Digi wrote a wrong/vendor cross-reference here. macOS and
    Linux ignore it — do the same in our firmware but pick a real ID
    (1) to be clean.*
  - **FORMAT_TYPE_I** `0e 24 02 01 02 03 18 02 44 ac 00 80 bb 00`
    - bFormatType=I, **2 ch × 3-byte subframes × 24-bit resolution**
    - **bSamFreqType=2 discrete rates: 44100, 48000**  (only these two)
  - **EP** `09 05 02 05 30 01 01 00 00`
    - bEndpointAddress=**0x02 (OUT)**, bmAttributes=0x05 (**iso, async**),
      wMaxPacketSize=**0x0130 = 304 B**, bInterval=1
  - **AS iso EP** `07 25 01 01 01 00 02` — EP_GENERAL,
    bmAttributes=0x01 (**Sampling Frequency Control available**),
    bLockDelayUnits=1 ms, wLockDelay=512 ms

### Interface 2 — AudioStreaming (capture, device→host)
- alt 0: zero-bandwidth idle
- alt 1:
  - **AS_GENERAL** `07 24 01 08 01 01 00` — bTerminalLink=**0x08**
    (again, no such terminal — Digi bug; should be 4). PCM.
  - **FORMAT_TYPE_I** identical to playback (2 ch × 24-bit × [44.1, 48])
  - **EP** `09 05 81 05 30 01 01 00 00`
    - bEndpointAddress=**0x81 (IN)**, bmAttributes=0x05 (iso, async),
      wMaxPacketSize=304 B, bInterval=1
  - **AS iso EP** `07 25 01 01 01 00 02` — Sampling-Freq control

**Both audio endpoints declare bmAttributes=0x05 (Async) but there is
NO isochronous feedback endpoint.** The OUT (playback) EP being Async
without feedback is technically non-conformant; macOS and Linux tolerate
it by treating the stream as Adaptive. Windows sometimes complains.
For classy-mbox: either add a real feedback EP (correct) or declare the
OUT endpoint as Adaptive (bmAttr=0x09) to match what Rev 20 effectively
behaves as.

---

## Configuration 2 — Vendor/legacy (0x0670, 54 B) — NOT ADVERTISED

```
09 02 36 00 02 01 00 80 f0
```
bNumInterfaces=2, bConfigValue=**2**. Device says bNumConfigurations=1
so hosts won't see this. It's a vendor-class (bInterfaceClass=0xFF)
alternate that presents the same two iso endpoints. Almost certainly
a stub — probably an early prototype config Digi left in ROM. Ignore
for classy-mbox.

---

## String descriptors (0x06A6)

| Idx | UTF-16LE Content                                             |
|-----|--------------------------------------------------------------|
| 0   | LangID list: 0x0409 (English US)                             |
| 1   | `Digidesign Inc` (iManufacturer)                             |
| 2   | `Mbox USB Audio Device copyright Digidesign 2001` (iProduct) |

No serial number.

---

## Class request map (all standard UAC1)

The four class-request `bmRequestType` values seen in the dispatcher at
NOTES.md's `0x0026` (CPU 0x0014) are standard UAC1 — not vendor
Digi-specific:

| bmRequestType | Meaning                                            | Rev 20 handler |
|---------------|----------------------------------------------------|----------------|
| 0x21          | SET_CUR to Interface → **Selector Unit input**     | 0x0055 (r2)    |
| 0x22          | SET_CUR to Endpoint → **Sampling Frequency**       | 0x006B         |
| 0xA1          | GET_CUR from Interface → SU current input          | 0x0073         |
| 0xA2          | GET_CUR from Endpoint → current sampling freq      | 0x008A         |

Encoding for classy-mbox:
- **SET input source (analog / S/PDIF)** — SET_CUR, wValue=(CUR<<8), wIndex=(0x05<<8)|0x00 (SU_ID=5, iface 0 = AC), wLength=1, payload=1 (=IT(2)=Analog) or 2 (=IT(6)=S/PDIF).
- **SET sample rate** — SET_CUR, wValue=(SAMPLING_FREQ_CONTROL<<8)=0x0100, wIndex=EP address (0x02 or 0x81), wLength=3, payload=3-byte LE rate.

Reads mirror with bmReq=0xA1 / 0xA2 and same wIndex/wValue/wLength.

---

## Summary for classy-mbox

| Feature                            | Rev 20 has it? | Class-compliant? |
|------------------------------------|:---:|:----:|
| Selector Unit (input source)       | ✅  | ✅   |
| Feature Unit (volume/mute/gain)    | ❌  | —    |
| Sampling-frequency control on EP   | ✅  | ✅   |
| Feedback endpoint (async OUT)      | ❌  | ⚠   |
| Sample rate list                   | 44.1 kHz, 48 kHz | ✅ |
| Subframe / resolution              | 24-bit / 3-byte  | ✅ |
| Channels                           | 2                | ✅ |
| Serial number                      | ❌  | —    |
| Config 2 (vendor)                  | present but hidden | — |
| bTerminalLink cross-reference      | wrong (9, 8) but works | ⚠ |

**Deltas to make classy-mbox strictly better than Rev 20 without
breaking parity:**
1. Fix bTerminalLink values (9→1, 8→4) — cosmetic, but correct.
2. Add a real feedback endpoint (say 0x83 IN, usage=Feedback, 3-byte
   packets) so Windows accepts async playback without warnings.
3. Optionally add a Feature Unit with mute + a nominal software volume
   control the box ignores — gives DAW hosts a fader that visually
   works even though the analog path stays hardware-controlled.
4. Everything else — keep identical to Rev 20. The topology, rates,
   subframe layout, endpoint addresses, packet sizes, and string
   layout are all already class-compliant.
