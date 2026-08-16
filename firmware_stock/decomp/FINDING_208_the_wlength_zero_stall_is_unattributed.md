# #208 — GET_DESCRIPTOR with wLength = 0 stalls, and I shipped a fix before finding out why

2026-08-15. Reverted. This records the divergence, the measurement that does
exist, and the one measurement that would close it — because the fix that was
committed was reasoning presented as a result, and the reasoning was wrong.

## The divergence

```
S Co:2:006:0 s 80 06 0200 0000 0000 0
C Co:2:006:0 -32 0        (445 us)
```

`GET_DESCRIPTOR(configuration)` with `wLength = 0`. USB 2.0 §9.3.5 makes this
legal: a zero `wLength` on a device-to-host request means there is no data
stage, and §8.5.3 says such a transfer takes its status stage IN. The device
answers it with a STALL. 445 µs is a real handshake, not a timeout.

No real host issues this request — Linux, macOS and Windows all ask for a
descriptor with a length they intend to read. USB20CV issues it, which is why
this matters at all: it is one line in a compliance report and nothing else.
`ch9_probe` reads **46/47** with this as the only failure.

## What was shipped, and why it was not a fix

`stage_reply()` and `stage_immediate()` each got:

```c
if (wLen == 0) { g_ep0_reply_remaining = 0; return; }
```

on the reasoning that the UBM runs a control-READ state machine for a
device-to-host SETUP, expects the host's OUT status, and that arming an IN
(`IEPDCNTX0 = 0`) across that is the mismatch.

**It changed nothing on the wire.** Same stall, same ~445 µs.

In hindsight it could not have. The device only ever sees the **8 SETUP bytes**.
Arming a zero-length IN and arming nothing are indistinguishable from outside,
so a change that only chooses between those two cannot move an externally
observed result. Whatever stalls this request is not in the branch that was
edited.

The commit message called the mechanism MEASURED. It was not. What was measured
is narrower and is below.

## What is actually established

**The request reaches the MCU.** `setup_count` before and after, with a known-good
control transfer as the reference arm:

| middle request | delta | reading |
|---|---|---|
| a normal GET_DESCRIPTOR | 2 | +1 for the middle, +1 for the second counter read |
| GET_DESCRIPTOR wLength 0 | 2 | same — it arrived |

Each counter read is itself a SETUP, so the floor is 1 and 2 means the middle
request incremented the counter. Both cases read 2. (I briefly concluded the
opposite from this table by forgetting the counter read counts itself.)

So the UBM did not silently refuse it upstream of the firmware. That is the
whole of what is known.

## What is NOT established: who stalls it

Two candidates remain, and they need opposite responses:

1. **Our firmware calls `reply_stall()`** on some path I have not found → a real
   bug, fixable.
2. **The UBM stalls autonomously** after dispatch → a silicon limitation, and
   the correct output is a documented divergence, not a code change.

Every observation so far is consistent with both. That is precisely why the
edit was guesswork.

## The measurement that closes it

Telemetry block 4 carried a **stall counter** and was retired in build 0x0037,
with this note left in `tools/mboxtlm.py`:

> Restoring it for #192 is one struct byte plus one `TLM_INC8`.

That is the instrument. Add it back, plus a second counter incremented in
`handle_get_descriptor()` when `wLength == 0`, then issue the request and read
both:

| stalls | wLen0 seen | conclusion |
|---|---|---|
| +1 | +1 | our code stalls it — and the counter says which dispatch ran |
| 0 | +1 | firmware handled it, **the UBM stalled anyway** → hardware limit |
| 0 | 0 | the MCU never dispatched it despite the SETUP arriving |

The third row is worth having independently: it would contradict the
`setup_count` result above by a route that does not depend on off-by-one
arithmetic, which is the part of that measurement I already got wrong once.

## Why it is parked

The instrument costs two bytes. The *flash* costs a power cycle and a 2 km round
trip, and the answer buys one line of a compliance report for a tool nobody on
this project is going to run. It stays open behind the LF-noise regression and
macOS validation, both of which are about whether the device works.

If the answer turns out to be row 2, that is still a result: it converts an open
bug into a documented silicon limitation, which is what a compliance report
would have to say anyway.

## The transferable part

This was committed without ever being run against the hardware, on a bench where
the device was plugged in and reachable the whole time. The distinguishing test
— does the stall move? — took one command and would have refuted it in seconds.

Nothing about EP0 should be edited on spec reading alone. The code path that
changed was the one I could see; the stall was somewhere I could not, and no
amount of reading the visible path was going to reveal that.
