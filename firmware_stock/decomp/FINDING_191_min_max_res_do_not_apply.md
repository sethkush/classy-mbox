# #191: GET_MIN / GET_MAX / GET_RES do not apply to this device, and it already answers correctly

2026-08-06, build 0x0037 on hardware. Closes #191 at a cost of zero bytes.

## What #191 was

The UAC1 sampling-frequency endpoint control supports `SET_CUR` and `GET_CUR`,
and the spec also defines `GET_MIN` (0x82), `GET_MAX` (0x83) and `GET_RES`
(0x84). We implement the first two. #191 was "add the other three", parked at
~50 bytes and blocked on #192 with the note: *do not spend 191's bytes on
speculation about a host we do not own.*

That instinct was right, and the reason turns out to be stronger than
"unverified" — the three requests **do not apply to a device shaped like this
one**.

## Why they do not apply

**Our sampling frequencies are DISCRETE.** The Type I format descriptor declares
`bSamFreqType = 2` with the explicit list `[44100, 48000]`. MIN / MAX / RES
describe a *continuous* range — the lower bound, upper bound and step of a
sweep. For a discrete list there is no step to report, and the valid values are
already published in the format descriptor, which is where a host reads them.
Answering `MIN = 44100, MAX = 48000, RES = ...` would imply everything in
between is selectable. It is not: `SET_CUR` of 45000 is rejected, and rightly.

So the honest answer to those three requests is a stall, which is what the
device already does.

## Four independent lines agreeing

**1. The device already behaves correctly.** Measured on unit A, both endpoints,
driver detached so the requests actually reached the device:

    capture EP 0x81            playback EP 0x02
      GET_CUR -> 48000 Hz        GET_CUR -> 48000 Hz
      GET_MIN -> STALL           GET_MIN -> STALL
      GET_MAX -> STALL           GET_MAX -> STALL
      GET_RES -> STALL           GET_RES -> STALL

`GET_CUR` returns the real rate on both; the other three stall. That is also
exactly the house rule #188 and #195 established: a request naming something we
do not support gets a Request Error rather than an invented answer.

**2. TI's own reference for this part implements none of them.** `Usbaudio.h`
defines `AUD_GET_MIN` / `AUD_GET_MAX` / `AUD_GET_RES` as 0x82/0x83/0x84, and no
source file in the reference tree handles any of the three. The vendor shipped
the constants and not the handlers.

**3. Neither stock image dispatches on them.** Byte-scanned Rev 20 and Rev 22
for a compare against 0x82/0x83/0x84 in the request path: no hits in either.

**4. Linux never asks.** `snd-usb-audio` takes the rate list from the format
type descriptor and drives the control with `SET_CUR`/`GET_CUR` only. Which is
consistent with 1-3, and with both units having worked at both rates all along.

## Caveat, stated rather than buried

Line 3 is a scan for one opcode form (`CJNE A,#imm`); a jump table would evade
it. It is corroboration, not proof on its own. Lines 1 and 2 do not depend on
it — the hardware measurement is direct evidence of our own behaviour, and TI's
reference is a source-level fact.

## Verdict

**#191 closes as not-applicable.** No code, no bytes, and the per-unit build
stays where it is. If #192 ever contradicts this, the fix is ~50 bytes and this
document is the thing to re-read first — but a device with discrete rates
answering "no such attribute" is the compliant behaviour, not a gap.
