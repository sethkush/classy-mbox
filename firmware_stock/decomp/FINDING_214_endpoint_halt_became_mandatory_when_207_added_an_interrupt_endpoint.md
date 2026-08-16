# #214 — ENDPOINT_HALT became mandatory the moment #207 added an interrupt endpoint, and nothing rechecked

2026-08-15. Found by `ch9_probe --invasive`, the two state-changing Chapter 9
tests that had never been run. Result: **50 passed, 2 failed.**

```
CH9 FAIL: 50 passed, 2 failed
  - GET_DESCRIPTOR(config, wLength 0): device STALLed a request it must answer
  - SET_FEATURE(ENDPOINT_HALT, EP 0x83): stall -- §9.4.9 makes the halt
    feature mandatory on an interrupt endpoint
```

The first is the known #209 divergence. The second is new.

## The rule

USB 2.0 §5.6.3 exempts **isochronous** endpoints from the halt feature. It does
not exempt interrupt endpoints. §9.4.9 makes `SET_FEATURE(ENDPOINT_HALT)`
mandatory on interrupt and bulk endpoints, §9.4.1 requires `CLEAR_FEATURE` to
clear it, and §9.4.5 requires `GET_STATUS(endpoint)` to report it.

EP 0x83 read from the live device:

```
bEndpointAddress     0x83  EP 3 IN
bmAttributes            3
  Transfer Type            Interrupt
wMaxPacketSize     0x0002  1x 2 bytes
bInterval               8
```

`bmAttributes 3` is Interrupt. Not exempt.

## The cause is a justification that expired

`usb.c`, in the `REQ_SET_FEATURE` handler, from #188:

```
 * ENDPOINT_HALT:        the streaming endpoints are
 *                       isochronous and have no halt (§5.6.4);
```

**That was correct when it was written.** At the time the device had EP0 and
three isochronous endpoints — 0x81 capture, 0x02 playback, 0x82 feedback — and
not one of them supports halt. "This device has no settable feature at all" was
a true statement about the device as it then existed.

Then **#207 added EP 0x83**, an interrupt endpoint, and the premise silently
stopped holding. Nothing re-derived the conclusion, because nothing connected
"we added an endpoint" to "a comment in the feature handler enumerates our
endpoint types".

This is not a reasoning error. It is a **correct inference left standing after
its inputs changed**, which is a different failure and needs a different guard:
the comment enumerates endpoint types, so adding an endpoint type must revisit
it. `check_terminal_evidence.py` does this for terminals; nothing does it for
endpoints.

## What the test did and did not disturb

Nothing got stuck. `SET_FEATURE` stalled, so the `CLEAR_FEATURE` half never ran
and no endpoint was left halted. Verified after: build 0x0054, stage 20, bus
resets unchanged at 11, 576,044 bytes captured, `aplay` rc=0.

The **Unconfigured State test passed** — `SET_CONFIGURATION(0)`,
`GET_CONFIGURATION` returning 0, descriptors still answering while unconfigured,
and the restore to configuration 1. That is one of the two subjects that had
never been exercised at all, and the device handles it correctly.

## The fix, and the budget question it raises

Correct behaviour is narrower than "implement halt":

* EP 0x83 (interrupt): `SET_FEATURE`/`CLEAR_FEATURE(ENDPOINT_HALT)` must work,
  and `GET_STATUS` must report the bit.
* EP 0x81, 0x02, 0x82 (isochronous): must **continue** to stall it. §5.6.3
  exempts them, and #188's handling of these is right and must not be
  "fixed" along with the rest.
* EP0: halt support is optional; leaving it stalled is conforming.

So the change is per-endpoint, not global — the existing stall is correct for
three of the four endpoints and wrong only for the one added last.

The budget is the awkward part. 0x0055 currently sits at **5966 of 6016** with
#212's SET_ADDRESS fix and #209's stall counter, leaving 50 bytes. A per-endpoint
halt with status reporting will be tight in that space. #209's counter is a
temporary experiment worth 25 bytes and is the obvious thing to drop if the two
do not both fit — a real conformance fix outranks an instrument for a question
that only matters to a compliance report.

That is a flash-payload decision, not a code decision, and it is Seth's to make.
