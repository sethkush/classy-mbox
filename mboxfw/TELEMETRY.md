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

Block 3 — VECINT histogram, one byte per vector code, saturating at 255. A large
`none` count means the ISR is firing spuriously; `other` being nonzero means a
vector we do not handle is arriving.

| byte | field |
|---|---|
| 0 | SETUP |
| 1 | IEP0 |
| 2 | OEP0 |
| 3 | RSTR (bus reset) |
| 4 | `none` — ISR fired with no vector set |
| 5 | `other` — a vector we do not handle arrived |
| 6 | SUSR (bus suspend) |
| 7 | RESR (bus resume) |

Bytes 6-7 were spare until 2026-07-29. SUSR/RESR were briefly reported in block
7 instead, which was simply where the work happened to be; a VECINT histogram is
their right home.

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
| 5 | playback frame-alignment resyncs — Rev 22's SOF watchdog firing |
| 6 | spare |
| 7 | **live** `PCON` — bit 0 is IDL, and reads 0 once awake |

On bytes 0-1: `usb_ep0_setup()` now clears both, so this cannot recover the
boot-ROM handoff value -- **block 8 does that instead**, by sampling before any
write. What it answers is whether the UBM writes anything
*back* into the Y counts during a session — the version of the question that
survives the fix. Non-zero confirms the EP0-loss mechanism; a persistent zero
alongside continued loss rules it out. See
`firmware_stock/decomp/FINDING_ep0_y_buffer_residue.md`.

On byte 4: `tlm_suspends` is incremented immediately before `PCON |= 0x01`, so
the read itself proves the device came back out of idle — any non-zero value is a
completed round trip. Read it against block 3 bytes 6-7: SUSR climbing while
suspends stays 0 means the configured-only guard is rejecting them.

On byte 5: the playback frame-alignment watchdog ported from Rev 22's SOF handler
(`fcn.0x0D58`). Non-zero means the playback DMA buffer was found holding a
partial sample frame and the path was torn down and restarted. Rev 20 has no such
check at all, and mboxfw had Rev 20's behaviour until this was added. A count that
climbs steadily under playback means something upstream keeps misaligning the
stream, which is a different problem from the watchdog catching a one-off. See
`streaming_sof()` in `mboxfw/src/streaming.c`.

Block 8 — **boot-ROM handoff snapshot.** Sampled as the very first action in
`main()`, before anything is written. Added 2026-07-29 to close
`WHAT_REMAINS_UNKNOWN.md` §3a, which had become unanswerable by observation.

| byte | field |
|---|---|
| 0 | `IEPDCNTY0` **at handoff** |
| 1 | `OEPDCNTY0` **at handoff** |
| 2 | `GLOBCTL` at handoff |
| 3 | `USBCTL` at handoff |
| 4-7 | zero (0x00, so all-0xFF stays a distinct "never sampled" sentinel) |

Why this block exists rather than reusing block 7: `usb_ep0_setup()` clears both
EP0 Y counts, so block 7's live read shows 0 whether or not the boot ROM left
residue there. The §3a question is specifically what the ROM handed us, and the
only way to see it is to read before writing. Byte 3 makes the point sharpest —
`main()` zeroes `USBCTL` two lines after this sample, so nothing later can
recover it.

Byte 2 is not idle curiosity: `hw_init()` sets stock's GLOBCTL bit 1 with
`|= 0x02` rather than stock's outright `= 0x06`, and that choice is only
equivalent if the ROM really leaves `0x04`. That number comes from a comment in
TI's `RomBoot.c`, not from this part. If byte 2 reads anything else, the RMW
needs revisiting — `tools/mboxtlm.py` says so in the decode.

All four bytes initialise to 0xFF, so a block-8 read of all-0xFF means `main()`
never reached the sample (or the image predates the block), not "the ROM left
0xFF everywhere". `tlm_reset_counters()` deliberately does NOT clear this block;
zeroing a one-time boot observation on a counter reset would turn a real
measurement into a plausible-looking zero.

Block 9 — **panel state.** Which input source is actually selected, right now.
Added 2026-07-30.

| byte | field |
|---|---|
| 0 | `g_mux_state` — the published panel/mux word, RAM[0x22] |
| 1 | mono flag (RAM[0x23].6) |
| 2 | `g_codec_state_23` — high byte of the 16-bit codec chain |
| 3 | `g_codec_state_25` — low byte |
| 4 | **live** `P3` — the three button pins, active low |
| 5 | host mux-set requests accepted |
| 6 | host mux-set requests rejected as illegal patterns |
| 7 | zero |

This block exists because a measurement that cannot state its own input routing
cannot be trusted. On 2026-07-29 a full capture session was voided when the mux
turned out to be on mic for both channels while the loopback fed a line input,
and that was discovered afterwards, from the front-panel LEDs, in person. Bytes
0-3 are the complete state of both latch chains, so the same read also confirms
whether `codec_init()` extinguished the spdif/USB/mono LEDs.

Bytes 5 and 6 are two counters rather than one because a rejected `TLM_REQ_SET_MUX`
leaves the mux word unchanged — identical to a request that never arrived. Only
separate counters tell those apart.

The companion request is `TLM_REQ_SET_MUX` (0x13), which reaches the same states
the front-panel buttons reach, by the same publish path
(`codec_source_changed()` → `mux_write()` → `codec_write_word()`, stock's order
at Rev 20 `0x0AE3-0x0AE9` / Rev 22 `0x0A8D-0x0A93`). It rejects any pattern that
is not one of the three one-cold values, because `g_mux_state = 0x00` is exactly
the illegal state that invalidated the earlier measurements. Drive it with
`tools/mboxtlm.py setmux line line`.

## What this buys

Concretely, these questions become remote and repeatable instead of one-flash-each:

- Did the `VECINT` fix work? → block 1, chunk/drain counts under a scripted sweep.
- Is enumeration failing before or after SET_ADDRESS? → blocks 0 and 2.
- Is the device alive but silent, or wedged? → block 0 loop counter.
- Is `snd-usb-audio` even reaching us? → block 2 last-SETUP.
- Did cs8427/codec init hang? → blocks 0 and 4.

## Cost

Roughly 40 bytes of counters plus ~150 bytes of code. mboxfw is 6312 B against a
7168 B budget (the hard ceiling is 8174 — 8192 minus the EEPROM header — which
is exactly what stock Rev 20 occupies), so this is affordable. Counters are `__data` and incremented in
the ISR — keep them `unsigned char`/`unsigned int` with saturating increments so
no counter can wrap into a misleading value mid-experiment.

## Status

Implemented — 10 blocks, `mboxfw/src/telemetry.c`, read with `tools/mboxtlm.py`.
The "design only, not implemented" note that stood here was stale: blocks 0-4
shipped and have been read off hardware, blocks 5-6 were added for the isoc
investigation, block 7 came in with the 2026-07-29 EP0/suspend pass, block 8
with the boot-handoff sample, and block 9 with the host mux control.

`TLM_BUILD_ID` is at **0x0013**. Bump it in `include/telemetry.h` on every
flash — block 0 byte 0-1 is the only thing that proves which image is running
rather than assuming, and it has already caught one stale-build mismatch (the
0x0002-vs-0x0003 case that led to the wildcard header dependency in the
Makefile).

## Block 10 — CS8427 readback probe (#165)

Runs a CS8427 register read over the SPI control port and reports **which pin
answered**, rather than assuming one.

    out[i] = P3 sampled after read clock i, MSB of the reply first

CDOUT is a third control-port pin (the TAS drives CCLK on P1.3 and CDIN on
P1.4). Nothing establishes that it is wired on this board: stock never reads
the CS8427 at all — its only readback probe, Rev 20 0x04DE-0x04F8, is an EEPROM
write-verify on the hardware I²C peripheral at 0xFFC0 — so Digidesign had no
reason to connect it. Guessing a pin would give a number either way, and a
wrong guess would be indistinguishable from a part that did not answer.

`mboxtlm.py` transposes: bit p across the eight samples is the byte pin P3.p
produced. The register read is CLOCKSOURCE (0x04), written as 0x40 by
`cs8427_boot_init()`, so a pin spelling **0x40** is CDOUT and the part is on
SPI and holding our configuration. 0x40 was chosen over RECVERRMASK (0x11 =
0xFF) precisely because 0xFF is indistinguishable from a pin stuck high.

Eight identical samples = no pin answered: CDOUT unwired, or the part silent.
That is a real answer, not a failed guess.

The probe runs **on demand**, never at boot — it clocks a transaction stock
never performs, so it stays off the path to enumeration.
