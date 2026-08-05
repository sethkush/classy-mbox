# The EP0 multi-packet loss is fixed, and was fixed weeks before anyone looked

Measured 2026-08-05 on build 0x002C, both bench units, both host controllers.

## The recorded defect

    2026-07-27, 60 trials per size:
      1-2 packets  60/60
      3 packets    53/60
      4 packets    47/60
      8 packets    27/60
      23 packets    9/60
    -- a clean geometric ~12% loss per continuation past the first.

That number is quoted in `mboxfw/include/telemetry.h`, in `TELEMETRY.md`, and
in the comment on `usb.c`'s VEC_IEP0 case, and it is the entire reason
telemetry blocks are exactly 8 bytes: the protocol was designed to never
depend on the path that was losing packets.

## The re-measurement

    A (RK10874600Q) on 192.168.1.86, xHCI:
      wLength  packets  ok/60
      8        1        60
      16       2        60
      24       3        60
      32       4        60
      64       8        60
      184      23       60      <- was 9/60

    Stress, 300 transfers of the full 286 B configuration descriptor
    (36 packets each):
      A on 1.86 (xHCI)   300/300
      B on 1.76 (EHCI)   300/300

21,600 packets across two units and two host-controller families, zero losses.

## Why this is a real measurement and not a host cache

Linux caches configuration descriptors, so a host-side loop can appear to
succeed without touching the device. The device's own counters were read
immediately after 60 transfers of 184 B:

    setup_count        65
    iep0_count       1389
    chunks pushed    1388      <- 60 x 23 = 1380, plus the telemetry reads
    transfers drained  64

The chunk count can only be incremented by `push_reply_chunk()` on the device.
The packets reached the hardware.

(The tool prints "chunks < iep0_count -- pushes are being missed" on a
difference of 1. That is the in-flight packet at the instant of the read, not a
miss; the heuristic wants a tolerance.)

## What fixed it

`usb.c` VEC_IEP0 now acknowledges the vector BEFORE arming the next packet:

    VECINT = 0;
    ...
    if (g_ep0_reply_remaining) push_reply_chunk();

matching TI UsbEng.c engEx0 (`case IEP0_INT: VECINT=0;` precedes
engEp0TxDone()). The old order shipped the chunk first and cleared VECINT at
the bottom of the case, so a packet completing between those two points had its
completion event wiped and the continuation never fired again. safety_net
showed the identical curve from the identical ordering, which is what ruled out
every mboxfw-specific theory at the time.

The comment describing this has been in the file, next to the stale numbers,
the whole time. Nobody re-ran the experiment after landing the fix.

## Consequences

* **The 8-byte telemetry constraint is no longer load-bearing.** It stays --
  it is simple, proven, and costs nothing -- but the rationale changes from
  "the multi-packet path is broken" to "this is simple and there is no reason
  to change it". Anything new may use multi-packet replies.
* **Block 1 (EP0 continuation forensics) has answered its question.** It is
  kept as the regression instrument that would catch this coming back, and
  because it is what verified the fix here.
* The Y-buffer residue hypothesis (`FINDING_ep0_y_buffer_residue.md`) is moot
  as an explanation for the loss. Its clears stay -- stock does them and they
  are correct -- but they were not what was wrong.

## The lesson worth keeping

A measurement taken before a fix, left in a comment next to the fix, reads
exactly like a measurement of the fixed code. Every doc that quoted "~12%"
was quoting 2026-07-27. Re-measure after landing a fix, or the number outlives
the defect.
