# Rev 20 standard-request path + SET_INTERFACE analysis

## Top-line: Rev 20 delegates all standard USB requests to the TAS1020A boot ROM

The setup dispatcher at `0x0026` (see NOTES.md) branches only on
`bmRequestType`. Anything that isn't `0x21/0x22/0xA1/0xA2` (class,
in/out, iface/endpoint) falls to `0x0118`:

```
0x0118: 90 ff 29    mov dptr, #0xff29     ; SETPACK+1 = bRequest
0x011b: e0          movx a, @dptr         ; A = bRequest
0x011c: 12 0f 70    lcall 0x0f70          ; table dispatch (never returns)
0x011f: 02 2f 00    ljmp 0x2f00           ; fallback → boot ROM std handler
```

`0x0f70` is a jump-table dispatcher (pops the return address into
DPTR, walks a table of {matcher, target-BE16} entries). The inline
table after 0x011c has only `0x01`/`0x02` matchers — those don't map
to any standard USB `bRequest` I can justify. Every non-match falls
through to `ljmp 0x2f00`, and **`0x2f00` is TAS1020A boot ROM's
standard-request handler** (ROM is mapped at ≥`0x2000`; firmware code
ends at `0x1FEE`).

**Consequence for `SET_ADDRESS`, `GET_DESCRIPTOR`, `SET_CONFIGURATION`,
`GET_STATUS`, `CLEAR_FEATURE`, `SET_FEATURE`, `SYNCH_FRAME`, and yes
`SET_INTERFACE` / `GET_INTERFACE`:** Rev 20 does **not** implement
them. Boot ROM does. That's why we can't find explicit handlers in the
8 KB code — there aren't any.

This also means the `SET_ADDRESS` bug we hit with mboxfw's own EP0
dispatcher is a bug we invented; classy-mbox should either **do the same
"defer to boot ROM" trick** or handle SET_ADDRESS itself with the
deferred-USBFADR-write pattern (which we already have working).

## Streaming activation — NOT tied to SET_INTERFACE

Rev 20 turns on the audio streaming endpoints and their DMA channels
during **master boot init**, not on `SET_INTERFACE(alt=1)`:

```
0x09be: 90 ff 98 74 c5 f0   ; OEPCNF2 = 0xC5  ← EP2 OUT enabled at BOOT
0x09c4: 90 ff 60      f0    ; IEPCNF1 = 0xC5  ← EP1 IN  enabled at BOOT
0x09c8: 90 ff ea 74 03 f0   ; DMA source
0x09ce: 90 ff e9 74 80 f0   ; DMA source
```

DMA channels (`DMACTL0/1`, `0xFFE0/0xFFE1`) and the C-port I²S master
(`CPTCTL`, `0xFFD4`) are similarly set up unconditionally by
`fcn.0x08CB` (see NOTES.md master boot init table). The full audio
pipe is "hot" from the moment enumeration finishes; the host is free
to fire isoc packets at any point.

**This is not USB-Audio-Class compliant.** UAC1 requires alt-setting 0
= zero-bandwidth (no isoc EP descriptor) and alt-setting 1 = active
stream. Digi's driver skips SET_INTERFACE entirely and drives the box
via the vendor SET Clock Source request instead.

Sites that write `IEPCNF1`/`OEPCNF2`/`DMACTL1`/`OEPBCTX2`/`IEPBCTX1`:

| Site      | Context                                             |
|-----------|-----------------------------------------------------|
| `0x03B2`  | mode branch (44.1 or 48 kHz set-clock)              |
| `0x03C7`  | mode branch (44.1 or 48 kHz set-clock)              |
| `0x041E`  | mode branch                                         |
| `0x07E4/0x07EA/0x07D3/0x07DC/0x07CC` | `fcn.0x0728` ApplyAudioMode  |
| `0x09BE/0x09C4/0x09B5/0x09BA/0x09B0`  | master boot init             |
| `0x0E10`  | mode-transition prep (`fcn.0x0DEC` DMA-const load)  |

**None of these are reached from a `SET_INTERFACE` handler.** They all
fire either at boot or from an `ApplyAudioMode` triggered by the
vendor SET Clock Source class request landing an action code in
`RAM[0x0A]`.

For classy-mbox to work with the OS's class-compliant driver, we must
implement `SET_INTERFACE` ourselves:
- `alt=0` on an AudioStreaming interface → set `IEPCNF1`/`OEPCNF2` to
  disabled (bit 7 = 0), clear DMA-enable in `DMACTL1`, stop the CS8427
  from clocking data (or don't — codec is happy with silence).
- `alt=1` → mirror Rev 20's boot values (`IEPCNF1=0xC5`,
  `OEPCNF2=0xC5`, `DMACTL1 |= 0xC0`, `CPTCTL` re-enable).

## `SET_CUR SamplingFrequency` (standard UAC1)

Rev 20's SET Clock Source handler (at `0x00cc` onwards, dispatched
from `0x0D=1` at data-phase completion in `0x0D37`) reads the **first
byte only** of the payload:

```
byte == 0x44 → RAM[0x0A] = 0x07  → 44.1 kHz mode
byte == 0x80 → RAM[0x0A] = 0x08  → 48   kHz mode
byte == 0x00 → RAM[0x0A] = 0x06  → S/PDIF-slave mode
```

This "works" for exactly {44100, 48000, 0} because 44100 & 0xFF = 0x44
and 48000 & 0xFF = 0x80 — a coincidence Digi's driver exploited. It is
**not** correct 3-byte little-endian parsing. Send SET_CUR with
{88200, 96000, 176400, 192000} and Rev 20 will silently pick the wrong
mode or fall through unchanged.

Classy-mbox must:
1. Read all 3 bytes from the EP0 OUT buffer (`0xFA10..`).
2. Validate the value against a table of supported rates
   (44100, 48000 for now; add higher rates only if we've verified the
   C-port + DMA constants).
3. Stall EP0 with `LJMP 0x1009` on unsupported values.
4. Kick the audio-path reconfig — either by reprogramming
   `CPTBRTX/CPTBRRX/DMACTL0/DMACTL1` directly, or by mapping the rate
   to Rev 20's action-code and delegating to `fcn.0x0728`.

## Recommendations for classy-mbox

1. **Standard requests → boot ROM.** Add an `ljmp 0x2F00` fallback in
   the setup dispatcher for everything not class-typed. Keep
   mboxfw's SET_ADDRESS deferred-USBFADR-write path as a belt-and-suspenders
   guarantee; the boot ROM does it correctly but doubling up is cheap.
2. **`SET_INTERFACE` is ours.** Boot ROM will happily update its internal
   alt-setting state, but only user code knows what to do with the audio
   path. Intercept before the `ljmp 0x2F00` and act on wIndex/wValue.
3. **UAC1 SamplingFrequency SET_CUR** — implement proper 3-byte parsing.
   Don't lean on Rev 20's first-byte shortcut.
4. **Boot activation is fine** — activating IEPCNF1/OEPCNF2 at boot
   mirrors Rev 20 and is safe. Just make sure `SET_INTERFACE(alt=0)`
   correctly *deactivates* them so the host can gate playback.
