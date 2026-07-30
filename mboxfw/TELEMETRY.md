# Telemetry — making one loaded image answer every question

## The constraint driving the design

One power cycle buys exactly one image (`ramloader/DESIGN.md`), and a power cycle
is a 2 km round trip. So the image we spend a trip on has to be *interrogable*:
every question we would otherwise answer with "flash a variant and see" must
instead be answerable over the wire, repeatedly, from 1 km away.

## Two design rules that are not negotiable

**1. Telemetry reads must be single-packet.**

The bug we are chasing *is* the multi-packet EP0 continuation path. If reading
telemetry needed a multi-packet transfer, then on a failing build the telemetry
would fail too — we would be measuring the instrument with the broken thing it
is measuring. So every telemetry read returns **exactly 8 bytes** (one EP0
packet, on the path measured 60/60 reliable) and the host reads a *block index*
in `wValue`. Reading 64 bytes of state is 8 independent single-packet reads.

**2. Vendor requests, device recipient.**

`bmRequestType = 0xC0` (vendor / device-to-host / device). Not interface-directed:
`snd-usb-audio` claims the audio interfaces and an interface-recipient request
then returns `EBUSY` before it ever reaches the firmware — that already cost us
one wasted round of debugging. Device recipient needs no claim and no detach.

## Wire protocol

| bmRequestType | bRequest | wValue | returns | meaning |
|---|---|---|---|---|
| `0xC0` | `0x10` | block index | 8 B | read telemetry block |
| `0x40` | `0x11` | — | — | zero the counters (start a clean experiment) |

Unknown block index returns 8× `0xFF` rather than stalling, so a host reading
past the end gets a clean sentinel.

## Block map

Block 0 — identity and liveness. Answers "what is actually running, and is it?"

| byte | field |
|---|---|
| 0-1 | build id, low 16 bits of the git short hash |
| 2 | `g_stage` — high-water mark of the init/enumeration ladder |
| 3 | init phase bitmap: bit0 usb_init, bit1 hw_init, bit2 attach, bit3 cs8427, bit4 codec, bit5 main loop reached |
| 4-5 | main-loop iteration counter (wraps; nonzero and changing = alive) |
| 6-7 | RSTR count — how many bus resets, i.e. how many times the host restarted enumeration |

Block 1 — **EP0 continuation forensics.** The reason this document exists.

| byte | field |
|---|---|
| 0-1 | SETUP count |
| 2-3 | IEP0 interrupt count |
| 4-5 | continuation chunks pushed (`push_reply_chunk` calls) |
| 6-7 | transfers that fully drained (`remaining` hit 0) |

The invariant that localises the bug: for a reply of *N* packets the device
should see *N* IEP0 interrupts and push *N* chunks. **If `chunks pushed` is less
than `SETUPs × expected packets`, the device stopped being asked — a lost
interrupt.** If chunks match but drains do not, the transfer is being abandoned
elsewhere. That distinction is currently inferred from host-side timeouts; this
measures it at the source, and directly confirms or refutes the `VECINT`
ordering fix without another flash.

Block 2 — last SETUP seen. Answers "which request is failing?"

| byte | field |
|---|---|
| 0 | bmRequestType |
| 1 | bRequest |
| 2-3 | wValue |
| 4-5 | wIndex |
| 6-7 | wLength |

Block 3 — VECINT histogram. Counts per vector code (SETUP, IEP0, OEP0, RSTR,
NONE, other), one byte each, saturating at 255. A large `NONE` count means the
ISR is firing spuriously; `other` being nonzero means a vector we do not handle
is arriving.

Block 4 — peripheral results, so a hardware fault is distinguishable from a
firmware fault without a scope.

| byte | field |
|---|---|
| 0 | eeprom smoke test result |
| 1 | cs8427 init status |
| 2 | codec init status |
| 3 | stalls issued |
| 4 | **live** read of P1 at the moment the host asks |
| 5 | **live** read of P3 — hold a button while reading to see which bit moves |
| 6 | P1 sampled in `main()` before `hw_init()` drove any pin |
| 7 | P3 sampled in `main()` before `hw_init()` drove any pin |

Block 5 — isochronous streaming state. Bytes 4-5 are live register reads, so a
host can watch the endpoint config change (or fail to) while `arecord` runs.

| byte | field |
|---|---|
| 0-1 | SOF count. Zero means no frame clock is reaching us at all |
| 2 | IEP1 vectors seen (capture transactions completed) |
| 3 | OEP2 vectors seen (playback transactions completed) |
| 4 | **live** `IEPCNF1` |
| 5 | **live** `OEPCNF2` |
| 6 | sticky alt-setting-seen bitmap |
| 7 | last SET_INTERFACE: iface in the high nibble, alt in the low |

Block 6 — DMA and C-port live state: the isoc data source. Added to separate
"the endpoint is not transacting" from "the endpoint transacts but its buffer is
never filled".

| byte | field |
|---|---|
| 0 | **live** `DMACTL1` — capture channel; bit 7 = DMAEN |
| 1 | **live** `DMACTL0` — playback channel |
| 2 | **live** `CPTSTA`. Caution: read only on request, in case bits clear on read |
| 3 | **live** `ACGCTL` |
| 4 | **live** `IEPCNF1` |
| 5 | **live** `IEPDCNTX1` |
| 6 | **live** `IEPBSIZ1` |
| 7 | **live** `OEPDCNTX2` |

Block 7 — EP0 buffer counts and the suspend tally. Added 2026-07-29 with the
EP0 Y-count fix; neither Y register appeared in any earlier block, so before
this there was no way to look at them at all.

| byte | field |
|---|---|
| 0 | **live** `IEPDCNTY0` (0xFF6F) |
| 1 | **live** `OEPDCNTY0` (0xFFAF) |
| 2 | **live** `IEPDCNTX0` (0xFF6B) — bit 7 is the NAK flag |
| 3 | **live** `OEPDCNTX0` (0xFFAB) |
| 4 | completed suspend+resume cycles |
| 5 | SUSR vectors seen |
| 6 | RESR vectors seen |
| 7 | **live** `PCON` — bit 0 is IDL, and reads 0 once awake |

On bytes 0-1: `usb_ep0_setup()` now clears both, so this cannot recover the
boot-ROM handoff value. What it answers is whether the UBM writes anything
*back* into the Y counts during a session — the version of the question that
survives the fix. Non-zero confirms the EP0-loss mechanism; a persistent zero
alongside continued loss rules it out. See
`firmware_stock/decomp/FINDING_ep0_y_buffer_residue.md`.

On bytes 4-5: `tlm_suspends` is incremented immediately before `PCON |= 0x01`,
so the read itself proves the device came back out of idle — any non-zero value
is a completed round trip. SUSR climbing while suspends stays 0 means the
configured-only guard is rejecting them.

## What this buys

Concretely, these questions become remote and repeatable instead of one-flash-each:

- Did the `VECINT` fix work? → block 1, chunk/drain counts under a scripted sweep.
- Is enumeration failing before or after SET_ADDRESS? → blocks 0 and 2.
- Is the device alive but silent, or wedged? → block 0 loop counter.
- Is `snd-usb-audio` even reaching us? → block 2 last-SETUP.
- Did cs8427/codec init hang? → blocks 0 and 4.

## Cost

Roughly 40 bytes of counters plus ~150 bytes of code. mboxfw is 3299 B against a
6016 B ceiling, so this is affordable. Counters are `__data` and incremented in
the ISR — keep them `unsigned char`/`unsigned int` with saturating increments so
no counter can wrap into a misleading value mid-experiment.

## Status

Implemented — 8 blocks, `mboxfw/src/telemetry.c`, read with `tools/mboxtlm.py`.
The "design only, not implemented" note that stood here was stale: blocks 0-4
shipped and have been read off hardware, blocks 5-6 were added for the isoc
investigation, and block 7 came in with the 2026-07-29 EP0/suspend pass.

`TLM_BUILD_ID` is at **0x000C**. Bump it in `include/telemetry.h` on every
flash — block 0 byte 0-1 is the only thing that proves which image is running
rather than assuming, and it has already caught one stale-build mismatch (the
0x0002-vs-0x0003 case that led to the wildcard header dependency in the
Makefile).
