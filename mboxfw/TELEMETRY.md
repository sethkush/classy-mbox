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
| 4-7 | reserved |

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

Design only. Not implemented. Ordering note: implement and build this **before**
the next trip, since its whole purpose is to be present in the one image that
trip loads.
