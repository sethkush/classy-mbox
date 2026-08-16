# #206 — why the interrupt endpoint and the 16-bit mode do not fit, and what the EP0 fix needs

2026-08-15. Written while building out the feature list the release-build
headroom was supposed to unlock. Three of six shipped; the other three each hit
something that is not code space, which is worth recording so nobody re-derives
it from the budget alone.

## Shipped

| # | feature | cost |
|---|---|---|
| #203 | Selector Unit: Line / S/PDIF / **Instrument** | 65 B |
| #204 | terminal names on all five terminals | ~180 B |
| #205 | release build keeps telemetry block 0 | ~110 B |

## #3 — the status interrupt endpoint: NO BUFFER SLOT EXISTS

The idea was sound and the endpoint registers are free: `IEPCNF3` sits at
0xFF50 and nothing uses EP3. The blocker is **packet memory**.

`EP_BBAX(addr)` and `EP_BSIZE(bytes)` both work in **8-byte units**
(`>> 3`, regs.h), so every endpoint buffer must be 8-byte aligned and a multiple
of 8 long. The datasheet's external-data-memory map puts the endpoint data
buffers at **0xFA10–0xFF27**, with the setup-packet buffer at 0xFF28 and the
endpoint configuration blocks from 0xFF30 up.

Our allocation fills it exactly:

| region | bytes |
|---|---|
| EP0 OUT 0xFA10, EP0 IN 0xFA18 | 16 |
| playback 0xFA20–0xFC9F | 640 |
| capture 0xFCA0–0xFF1F | 640 |
| feedback 0xFF20–0xFF27 | 8 |

There is no free 8-byte slot. **Sharing the feedback slot does not work**: the
feedback endpoint arms only 3 of its 8 bytes, but 8-byte granularity means EP3's
base would have to be 0xFF20 as well, so both endpoints would write bytes 0–1 of
the same slot and collide whenever both are armed.

The only way to free a slot is to shrink an audio buffer below stock's 640 B.
That moves the X/Y double-buffer split the DMA and Rev 22's SOF watchdog depend
on — the geometry #46 and #162 both had trouble with — so it is not a change to
make without hardware in the loop.

**Consequence for #203.** A front-panel button press still does not notify the
host; the host only learns the new source when it next issues `GET_CUR`. That is
mitigated, not solved: #203 made `GET_CUR` report the PUBLISHED mux state rather
than a shadow of the last request, so a host that polls is at least told the
truth.

## #2 — the 16-bit alternate setting: THE PATH CANNOT CONVERT

Descriptors and endpoint config are the easy half (~100 B of descriptors, a BPS
field change from 5 to 3 in `IEPCNF1`/`OEPCNF2`). The obstacle is the signal
path.

**The C-port carries 24-bit samples to and from the converters, and the 8051
never touches a sample** — DMA moves bytes directly between the USB endpoint
buffers and the C-port. So declaring a 16-bit alternate setting does not make the
data 16-bit; it relabels 24-bit data, and a host would read garbage.

Making it real means changing the C-port's bits-per-slot so the I2S frame itself
is 16-bit, and that lands on the same class of question #46 answered badly for
sample rate: *the converters follow the clock and have no way to be told.*
Truncating an I2S frame to its first 16 SCLKs is legal and the AK5383/AK4393 are
MSB-first, so it is PLAUSIBLE — but plausible is exactly what #46's doubled rates
were before 30 kHz came back at 18 kHz.

**This needs a measurement before a line of it is written**: set the C-port to
16-bit, play a known tone, and see whether the captured signal is intact or
mangled. That is one build and one bench session, and it is the honest gate on
whether the feature exists at all.

## #4 — the wLength = 0 EP0 case: NOT ATTEMPTED, DELIBERATELY

Diagnosed but not written. `stage_reply()` caps the reply to `wLength`, so
`wLength = 0` arms a zero-length **IN** packet as the data stage. A device-to-host
request with no data has no data stage at all: the host goes straight to the
status stage, which is an **OUT** the device must acknowledge. Arming IN where
the host sends OUT is the direction mismatch that stalls.

The fix is to complete the request without staging a data packet. It is small.
It is also the one change on this list that can wedge enumeration outright, and
there are already three unflashed features stacked on top of a build that has not
run on hardware since 0x0053.

**So it waits for the flash-and-validate pass, not for headroom.** Doing it blind
on top of three other untested descriptor changes would mean a silent device with
four candidate causes.

## The budget was never the binding constraint

The release build has ~830 bytes free after the three that shipped. Every
remaining item is blocked by hardware — a packet-memory slot that does not exist,
a data path that cannot convert, and a sequencing rule about not stacking
untested EP0 changes. Freeing more code space would not move any of them.

---

# CORRECTION, 2026-08-15 — "#3 is blocked" was wrong

**The status interrupt endpoint shipped.** It is in build 0x0054, it enumerates,
and `ch9_probe` addresses it. Everything above under "#3 — the status interrupt
endpoint: NO BUFFER SLOT EXISTS" is a **wrong conclusion from a misread
register**, left in place because the way it was wrong is the useful part.

The error is one sentence in the table: the allocation does **not** fill
0xFA10–0xFF27 exactly.

`IEPBSIZx` sizes the **X/Y buffer PAIR, not one buffer.** So `EP_BSIZE(640)` on
the playback and capture endpoints was never claiming 640 bytes each — it was
claiming 640 for the pair, i.e. 320 per buffer. The map I drew double-counted
both streaming endpoints and reported the region as full when half of it was
free.

**What proves it, rather than merely re-reading the datasheet:** the feedback
endpoint's 8 bytes sit at 0xFF20–0xFF27, which is *exactly* the last 8 bytes of
the region. If the pair reading were wrong and each `IEPBSIZ` sized one buffer,
the allocation above would already have overrun the region end — and it
enumerates and streams. The map that was already working is the evidence.

Isochronous endpoints have no DBUF bit to turn double-buffering off
(§6.4.4.6.2 gives bits 4:0 to BPS), so iso *always* uses X and Y, which is why
the pair reading is the only one consistent with a working device.

EP3 got its slot at 0xFF18 by shrinking the capture pair from 640 to 632 (#207).

**The transferable lesson is in the last section, and it survives.** "The budget
was never the binding constraint" was correct; "blocked by hardware" was not.
The failure mode was diagnosing a hardware limit from a register description I
had not cross-checked against a layout that was already known to work. A
register whose meaning is in doubt should be resolved against the running
device before it is written up as a wall.
