# #211 — the playback feedback endpoint returns EOVERFLOW on every frame and has never delivered a byte

2026-08-15. Found with eBPF, no reflash, on the first run of an instrument that
had never existed. #186 designed this endpoint and #185 fixed its descriptor;
neither ever looked at what it does on the wire.

## The measurement

`tools/isotrace.bt` aggregates isochronous frame statistics inside
`usb_hcd_giveback_urb`, keyed `[endpoint, is_IN]`. Four seconds of streaming:

| endpoint | what | packets | status | bytes moved |
|---|---|---|---|---|
| `[1,1]` = EP 0x81 IN | capture | 4014 | **0 (OK)** on all | 1,155,652 |
| `[2,0]` = EP 0x02 OUT | playback | 4122 | **0 (OK)** on all | 1,187,136 |
| `[2,1]` = EP 0x82 IN | **feedback** | 1032 | **-75 EOVERFLOW on all 1032** | **0** |

Capture: 1,155,652 / 4014 = 287.9 bytes per packet, against the 288 that 48 kHz
stereo 24-bit demands. Playback: 1,187,136 / 4122 = 288.0. Both endpoints are
exactly right and error-free.

The feedback endpoint delivered **zero bytes in 1032 consecutive packets**, each
one completing with `-EOVERFLOW`.

## The known-answer arm held, which is why this is trustworthy

Playback OUT packet lengths are chosen by the HOST, so that histogram is a
control: it reads what ALSA asked for, from the same probe, in the same run. It
did — 4122 packets in `[256, 512)`, zero errors. The probe is attached to the
right thing and the struct offsets are right. A null on the feedback endpoint
next to a clean signal on two others is a measurement, not a disconnected
instrument.

## Both units, and playback alone

Unit A (dev 7), playback only, no capture running at all:

| endpoint | packets | status | bytes |
|---|---|---|---|
| EP 0x02 OUT | 4018 | 0 (OK) | 1,157,184 |
| EP 0x82 IN | 1005 | **-75 on all 1005** | **0** |

So it is not unit-specific, and it is not an interaction with a concurrent
capture stream.

## What is NOT wrong

**The descriptor.** Read from the live device:

```
bEndpointAddress     0x82  EP 2 IN
bmAttributes           17     Isochronous / Synch None / Usage Feedback
wMaxPacketSize     0x0003  1x 3 bytes
bInterval               1
bRefresh                2
```

That is what USB 2.0 §5.12.4.2 asks for on a full-speed asynchronous sink.

**The refill.** `feedback_arm()` (streaming.c:145) writes the three 10.14 bytes
to `EP_FEEDBACK_BUF_ADDR` and sets `IEPDCNTX2 = IEPDCNTY2 = 3`, matching TI's
`SoftPll.c`. It is called from two places (streaming.c:777 and :925), so the
endpoint is being re-armed, not armed once and abandoned.

## What EOVERFLOW means here, and what is still a guess

`-EOVERFLOW` on an isochronous IN is babble: **the device returned more data
than the host scheduled for.** The host schedules 3 bytes, from
`wMaxPacketSize`. So the device is putting more than 3 bytes on the wire.

The obvious suspect is the buffer size. `IEPBSIZ2 = EP_BSIZE(EP_FEEDBACK_BUF_SIZE)`
with `EP_FEEDBACK_BUF_SIZE = 8`, and `EP_BSIZE()` works in **8-byte units**, so
8 is the smallest value expressible — a 3-byte endpoint cannot be given a
3-byte buffer. If the UBM derives the packet length from `IEPBSIZ` rather than
from `IEPDCNTX`, it would emit 8 (or 4, if the pair reading of #207 applies)
where the host expects 3, every frame, forever.

**That is a hypothesis and is written down as one.** TI's own `SoftPll.c` sets
`IEPDCNTX2 = 3` against the same hardware, which is evidence the count is meant
to govern — so either TI's code has the same defect, or something else is going
on. Nothing here has measured what the device actually puts on the wire; only
that the host calls it too much. Distinguishing those needs either the count
register varied against a fixed buffer size, or a bus analyser.

**Do not "fix" this by editing the buffer size and shipping it.** That is #208
exactly: an untested edit to a path, justified by a plausible reading of a
register. The next step is a measurement, and the measurement costs a flash.

## Why nobody saw it

Playback works. `snd-usb-audio` falls back to its own rate estimate when
feedback never arrives, so the audible behaviour is indistinguishable from a
working feedback endpoint until the host and device clocks drift far enough to
matter. #181 and #182 measured drift and found it acceptable — with, it now
turns out, the feedback loop open the whole time.

The instrument is the story. usbmon can see these packets, but at 1000 frames a
second in each direction the one that matters is a line in a million, and the
project's habit was to sample isochronous state through telemetry mid-stream
rather than to look at every frame. An in-kernel histogram over the whole
capture makes a 100%-failure rate obvious in the first run.

**Never argue from absence — but also, look.** Nothing about this endpoint was
ever asserted falsely. It simply was never measured, and "we declared it and
playback works" was allowed to stand in for "it functions".
