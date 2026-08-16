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

---

## Attempt 1 at the discriminating test: blocked by usbcore, by design

2026-08-15, same day. `tools/fbprobe.py` submits raw isochronous URBs through
usbfs so the SCHEDULED packet size can be chosen independently of what the
descriptor declares. If the device is emitting 8 bytes, an 8-byte schedule
should succeed and hand back what it actually sends.

```
 sched    urb  packet lengths      status counts
     3      0  0B x16              -75 x9, 0 x7   <-- CONTROL ARM
     4     --  submit failed: EMSGSIZE
     8     --  submit failed: EMSGSIZE
    16     --  submit failed: EMSGSIZE
```

**usbcore validates each `iso_frame_desc[].length` against the endpoint's
`wMaxPacketSize` before the URB ever reaches the host controller.** Asking for
more than the descriptor declares is refused in software, at the host, with
EMSGSIZE. The experiment is not reachable from userspace at all — not because
of permissions, but because the kernel enforces the device's own declaration.

Reaching it needs a module, which can raise the cached
`ep->desc.wMaxPacketSize` on `struct usb_host_endpoint` before submitting. That
is two lines on top of the infrastructure `tools/ch9mod/` already has, and it is
blocked on the same reboot that module is blocked on.

### The control arm did more than validate the harness

It was supposed to just reproduce the -75 and prove the rig. It did — but not
uniformly. Of 16 packets: **9 with status -75, and 7 with status 0** — all with
`actual_length` 0.

Under ALSA's own scheduling the rate was 100% -75. So the endpoint has two
behaviours, not one:

* **status 0, 0 bytes** — the device presented a zero-length packet. Legal, and
  what a feedback endpoint does when it has nothing new to report.
* **status -75, 0 bytes** — the device presented something LARGER than 3 bytes.

That is a sharper statement of the defect than "it always babbles". The endpoint
is alive and is being serviced; when it declines to send, it declines correctly.
It is only the packets it does send that are the wrong size. Whatever is wrong
is in the size of the transfer, not in whether the endpoint runs — which is
consistent with the `IEPBSIZ` hypothesis and inconsistent with, say, the
endpoint never being armed.

The 9:7 split is not obviously 1-in-4, which is what `FB_ARM_EVERY = 4` and
`bRefresh = 2` would predict. Not read further: a one-shot 16-packet URB has a
different phase relationship to the firmware's arming cycle than ALSA's
continuous submission does, and inferring a duty cycle from one URB would be
reading structure into 16 samples.

---

# RESOLVED, 2026-08-15 — the device emits NINE bytes, and the value inside is correct

`tools/ch9mod/fbmax.ko` raised the cached `wMaxPacketSize` and submitted its own
isochronous IN URB at a range of scheduled sizes:

| scheduled | result |
|---|---|
| 3 (control arm) | 6 x -75, 2 x status 0, **0 bytes** |
| 4 | 6 x -75, 2 x status 0, **0 bytes** |
| 8 | 5 x -75, 3 x status 0, **0 bytes** |
| 16 | **6 of 8 packets carried 9 bytes**, status 0, error_count 0 |
| 32, 64 | same — 9 bytes, status 0 |

**The endpoint emits 9 bytes per packet.** Not 3, and not the 8 the buffer-size
hypothesis predicted. Anything scheduled below 9 babbles; 16 and above succeeds.

## The feedback value was right the whole time

```
f7 ff 0b | 15 83 33 e4 50 50
```

The first three bytes are the payload the firmware actually wrote:
`0x0BFFF7` = 786423, and 786423 / 16384 = **48.000 samples per frame** in 10.14
format, which is exactly correct at 48 kHz. A later run read `fa ff 0b` =
48.0002, the PLL tracking.

So `feedback_arm()` computes and publishes the right number. Nothing about the
measurement, the 10.14 conversion, or the arming cadence is wrong. **The only
defect is the length of the transfer**, and the correct value has been sitting
in the first three bytes of an oversized packet the host was obliged to reject.

## Where the extra six bytes come from

`EP_FEEDBACK_BUF_ADDR` is 0xFF20 and `EP_FEEDBACK_BUF_SIZE` is 8, so the buffer
is 0xFF20-0xFF27. Nine bytes from the base runs to **0xFF28, which is the setup
packet buffer** — the very next thing in the endpoint data region.

The device is therefore emitting its entire 8-byte buffer plus one byte past the
end, and `IEPDCNTX2 = IEPDCNTY2 = 3` is not governing the length at all. The
trailing `50 50` is adjacent XDATA, not feedback.

This corrects the hypothesis recorded above. It was right that the length is
tied to the buffer rather than to the count, and wrong about the number: the
prediction was 8 or 4, and the answer is 8 + 1.

## What this does and does not settle

**Settled:** the value is correct; the length is wrong; the length follows the
buffer, not `IEPDCNTX`; the overshoot is one byte past the declared buffer.

**Not settled:** why 9 rather than 8. An off-by-one in how the UBM derives the
count from `IEPBSIZ` is the obvious reading, and it is still only a reading.
`EP_BSIZE()` works in 8-byte units so 8 is the smallest buffer expressible,
which means a 3-byte packet may not be reachable by shrinking the buffer at all.

**Do not fix this by declaring `wMaxPacketSize = 9`.** It would work, and it
would be wrong: UAC1 and USB 2.0 §5.12.4.2 both specify 3 bytes for a full-speed
feedback endpoint, hosts read only the first three, and it would trade a visible
failure for an invisible non-conformance. The next step is a firmware experiment
varying `IEPDCNTX2` against `IEPBSIZ2`, which costs a flash — and now has a
specific prediction to test rather than a guess.

## The instrument, and two of my own errors it caught

The control arm at 3 bytes earned its place twice. First it failed identically
to the 8-byte arm with `-EINVAL`, which identified a missing `URB_DIR_IN` in a
hand-built URB as MY bug rather than a device result. Then, once submitting, it
reproduced ALSA's `-75` exactly, which is what makes the 16-byte row credible.

The `-EINVAL` itself was the second error: the first design required a
concurrent `aplay` so ALSA would select alt 1, which made `snd-usb-audio` a
second submitter on the same isochronous stream. Having the module own the
altsetting fixed it. A test that needs the system under test to be busy doing
the same thing is not measuring what it thinks.

Unit verified clean afterwards: altsetting restored to 0, 576,044 bytes
captured, `aplay` rc=0.

---

# RESOLVED AND FIXED, 2026-08-16 — the UBM emits three bytes per unit armed

`TLM_REQ_FB_TUNE` (#215) made the armed count settable at runtime, so the whole
range could be swept on one flash instead of one round trip per value. The
answer took one command:

| armed (`IEPDCNTX2`) | 1 | 2 | 3 | 4 | 6 | 8 |
|---|---|---|---|---|---|---|
| **emitted bytes** | **3** | 6 | 9 | 12 | 18 | 24 |

**Exactly three bytes on the wire per unit armed.** Six points, perfectly
linear, no exceptions.

So `IEPDCNTX2 = 3` — which is what TI's `SoftPll.c` arms, and what we copied —
produces **9 bytes**. That is the whole of #211. The endpoint has babbled on
every packet since the day it was declared, because the register was armed with
the number of bytes wanted rather than the number of units that produces them.

At armed = 1 the packet is `fb ff 0b` and nothing else: `0x0BFFFB / 16384 =
48.0001` samples per frame. The payload was always correct.

## End to end, before changing any firmware

Armed to 1 over the wire, then `isotrace.bt` across a playback stream:

```
@status[2, 1, 0]:  1005      every packet status 0
@errcount[2, 1]:      0
@bytes[2, 1]:      3015      = 1005 x exactly 3 bytes
@len[2, 1]:  [2,4) 1005
```

Against 100% `-EOVERFLOW` and **zero bytes delivered** before. Playback
unaffected in the same run: 4018 packets, status 0.

**The feedback endpoint works, for the first time since it was declared.**

## The fix, and why it is a new constant rather than a changed one

`AUDIO_FEEDBACK_ARM = 1`, separate from `AUDIO_FEEDBACK_LEN = 3`.

They are different quantities that happened to share a number, and sharing it is
what hid this for so long. The descriptor's 3 is bytes-on-the-wire and is
correct. The armed value is in whatever unit the UBM counts, which is measured
as one-third of a byte count and is **not explained** — the datasheet describes
`IEPDCNTX` as a byte count and TI arms it as one. Collapsing two meanings into
one `3` made a wrong value look like a consistent one.

## What this cost, and what found it

The endpoint was declared in #186, its descriptor corrected in #185, and its
arming cited against TI reference code. Every one of those was reasonable and
none of them looked at the wire. #181 and #182 then measured clock drift as
acceptable **with the feedback loop open the entire time**, which is worth
re-reading now that it is closed.

What found it was an instrument that did not exist that morning: an in-kernel
histogram of every isochronous frame. usbmon could always have seen these
packets; at 1000 frames a second in each direction, nobody was going to.

Three of my own errors were caught by control arms along the way — a missing
`URB_DIR_IN`, a concurrent-submitter conflict, and a `sudo`-reset `$HOME` that
reported a host path bug as a firmware one. Each was caught because the 3-byte
reference arm failed in a way the device could not have caused.

## The host ACTS on it — the loop is closed, not merely delivering

Delivery is not the same as use. `snd-usb-audio` could have been ignoring the
endpoint. Exact playback packet sizes, same unit, same session, 40 s each:

| armed | 288 B (48 samples) | 282 B (47 samples) |
|---|---|---|
| 3 — feedback babbling | **10122** | 0 |
| 1 — feedback working | 10118 | **4** |

With the loop open the host sends a fixed nominal 288 bytes for every frame it
ever sends. With it closed the host varies the packet size. That is the host
adjusting its output rate to the device's reported one, and it is the entire
purpose of an asynchronous sink's feedback endpoint.

**The direction of those four corrections is NOT explained and is not claimed.**
The reported value is consistently a shade ABOVE nominal (0x0BFFF7 = 48.0001),
which should make the host occasionally send 49 samples, not 47. Four packets in
10,122 is far too few to fit a rate to, and `snd-usb-audio` also runs its own
buffer-level correction on top of the feedback value, so the sign here may not
be the rate at all. Recorded as an open detail rather than reasoned into a
story. The result that matters -- fixed versus varying -- does not depend on it.
