# #219 — IEPDCNTX/Y counts SAMPLES on an isochronous endpoint, and BPS sets the sample width

2026-08-16. Closing the one thing #211 fixed without explaining.

## The question

#211 found the feedback endpoint emitting 9 bytes against a `wMaxPacketSize` of
3, babbling on every packet since the day it was declared, and fixed it by
arming 1 instead of TI's 3. The sweep behind that fix was clean and had no
exceptions:

```
armed    1    2    3    4    6    8
emitted  3    6    9   12   18   24
```

Exactly three bytes on the wire per unit armed. The fix worked — 1005
consecutive packets, every one status 0 and exactly 3 bytes — but `usb.h`
recorded that **the mechanism was not known**, that "the datasheet describes
IEPDCNTX as a byte count and TI arms it as one", and that if the 3× were ever
explained, `AUDIO_FEEDBACK_ARM` was the line that would change.

It is explained. The line does not change. But the reason it does not change is
different from the reason it was set.

## The answer, and it is one sentence of the datasheet

§6.4.4.3, describing `DCNTX(6:0)`, says both halves in a single sentence:

> The 7-bit value is set to the number of **bytes** in the data packet for
> control, interrupt, or bulk endpoint transfers and is set to the number of
> **samples** in the data packet for isochronous endpoint transfers. To
> determine the number of samples in the data packet for isochronous transfers,
> the **bytes per sample** value in the configuration byte is used.

The earlier reading stopped at the first clause. `IEPDCNTX2` is not a byte count
on this endpoint, because this endpoint is isochronous.

The sample width comes from the endpoint's own configuration byte, and
§6.4.4.6.2 gives the isochronous `IEPCNFx` layout — which is **not** the
control/interrupt/bulk layout of §6.4.4.6.1:

| 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|
| IEPEN | ISO | OVF | BPS4 | BPS3 | BPS2 | BPS1 | BPS0 |

> `BPS(4:0)` … the number of bytes per isochronous data sample. …
> `00h = 1 byte, 01h = 2 bytes, …, 1Fh = 32 bytes`

So bytes per sample is **BPS + 1**. `streaming.c` writes `IEPCNF2 = 0xC2`:
IEPEN, ISO, OVF clear, **BPS = 2 → 3 bytes per sample**.

    emitted = armed × (BPS + 1) = armed × 3

which is the table above, with no residue.

## The confirming arm, which the project's rules require and which already exists

A rule of the form "the UBM triples the count" fits all six measured points just
as well. It is refuted by an endpoint with a different BPS, and there is one on
the same device, already measured, with its answer known in advance.

**Capture, EP1 IN**, is isochronous with `IEPCNF1 = 0xC5` — **BPS = 5 → 6 bytes
per sample**, stereo 24-bit. Its `DCNT` is loaded by the DMA engine rather than
by the MCU, at 48 samples per frame at 48 kHz. FINDING_211 measured the wire at
**287.9 bytes per packet over 4014 packets**, against 48 × 6 = 288.

| endpoint | IEPCNF | BPS | bytes/sample | multiplier measured |
|---|---|---|---|---|
| feedback, EP2 IN | 0xC2 | 2 | 3 | **3** |
| capture, EP1 IN | 0xC5 | 5 | 6 | **6** |

"The UBM triples it" predicts 3 on both. `BPS + 1` predicts both. The capture
figure was in hand before the question was asked, which is what makes it an arm
rather than a fit.

`DMATSH0`/`DMATSL0` agree independently: BPTS = 10b = 3 bytes per time slot on
slots 0 and 1 = 6 bytes per audio sample, matching BPS 5 on the same stream.

## What this changes

**`AUDIO_FEEDBACK_ARM` stays 1** — but it is now correct *by construction*
rather than empirically. One feedback value is three bytes, BPS says a sample is
three bytes, so one feedback value is exactly one sample. Arming 1 was right for
a reason, not by luck, and the constant is no longer a measured magic number.

**It is now a coupled constant.** `AUDIO_FEEDBACK_LEN` and the BPS field of
`IEPCNF2` must move together: widening the feedback value without re-cutting BPS
re-creates #211 exactly. That coupling is stated at both sites.

**TI was not wrong either.** `SoftPll.c` arms 3 on this same endpoint block for
this same purpose. TI's endpoint carries a different BPS, so 3 samples was right
*there*. #211's diagnosis — "we copied TI's 3" — stands; what was missing is
that the number was never dimensionless.

## A second error found on the way, and it is the one with teeth

Chasing BPS through §6.4.4 surfaced a **direct contradiction inside `regs.h`**.
The feedback-endpoint block reads §6.4.4.4 correctly:

> an ISOCHRONOUS endpoint gets ONE circular buffer … this differs from the X/Y
> pair a control/interrupt/bulk endpoint gets

Twenty lines below it, the #207 block asserted the opposite — that `IEPBSIZx`
sizes the **pair**, so 640 meant "320 + 320" and the feedback endpoint's 8 meant
"X and Y, 4 each, of which it uses 2". The datasheet is explicit and the first
block had it right:

- **Isochronous**: "the buffer size sets the size of the single circular
  buffer." No pair to divide. The X and Y that do exist for an iso endpoint are
  the two **DCNT registers**, ping-ponging by the LSB of `USBFNL` over one
  buffer (§2.2.7) — mistaking those count registers for a buffer pair is
  precisely where the error came from.
- **Control / interrupt / bulk**: BSIZ programs the size of **each** of X and Y,
  "both buffers programmed to the same size based on this value" — so the pair
  costs **2 × BSIZ**, the opposite of what #207 claimed.

Both allocations are correct as they stand; only the arithmetic justifying them
was wrong. But the status endpoint, EP3 IN, is **interrupt**, so the second rule
applies and its X/Y pair would cost 16 bytes at 0xFF18–0xFF27 — on top of the
feedback buffer at 0xFF20. What actually keeps that safe is `IEPCNF3 = 0x80`:
IEPEN with **DBUF clear**, and "in the single buffer mode, only the X buffer is
used".

**Setting DBUF on EP3 would silently overwrite the feedback buffer.** That
constraint was invisible under the per-half arithmetic, which made the region
look like it had slack everywhere. It is now written at the allocation.

## The shape of the mistake, for the next one

Both errors are the same shape, and it is not carelessness: **a register whose
meaning is conditioned on a mode bit, read once in the mode that was not
current.** `IEPDCNTX` is bytes *or* samples depending on ISO. `IEPBSIZ` is
per-buffer *or* whole-buffer depending on ISO. `IEPCNFx` has two different bit
maps depending on ISO. In every case §6.4.4 states the general rule first and
the isochronous exception second, and in every case the first reading took the
first clause.

Three registers, one mode bit, the same misreading each time. The isochronous
column of §6.4.4 is worth re-reading in full before touching any endpoint
register on this part.
