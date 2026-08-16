# #217 — the data-toggle test cannot be run on this device, and the first run said PASS anyway

2026-08-16. A negative result about an instrument, recorded because the
instrument's first answer was confidently wrong in the direction I was hoping
for.

## What was being tested

USB 2.0 §5.8.5: an endpoint's data toggle must reset to DATA0 when the host
clears its halt. A device that clears the halt and keeps its toggle is badly
broken in a quiet way — the host resets ITS toggle, so every packet the device
sends afterwards arrives with the wrong PID and is discarded as a
retransmission. The endpoint reports perfect health and delivers nothing.

Neither libusb nor USB20CV can see this: toggle bits live in host-controller
queue heads. `usb_device->toggle[]` holds the host's copy, so a kernel module
can. That made it a test the *reference tool does not have* — worth building.

It also only became possible at all with #214, which made `ENDPOINT_HALT` work
on EP 0x83. Before that there was no haltable endpoint to test.

## The result: VOID, not PASS

```
BEFORE halt: host toggle=1, transfer -> -110
GET_STATUS after halt -> 2, halt bit=1
AFTER clear:  host toggle=1, transfer -> -110
PASS -- data still flows after clear-halt ...
```

That PASS is false. **The endpoint timed out before the halt as well as after.**
Both states the test exists to distinguish look identical, so the run carries no
information.

The cause is a self-defeating reference arm. The arm was written to prove data
flows before the halt — and then explicitly admitted `-ETIMEDOUT`:

```c
if (before < 0 && before != -ETIMEDOUT) {   /* VOID */ }
```

An arm that accepts "nothing arrived" as evidence that something arrived is not
an arm. This is the second instrument in one day to fail in the exact shape of
the result it was looking for (the first was `ch9addr`'s stack buffer returning
`-EAGAIN`, which read as "the device stopped answering"), and both were caught
only by looking at a line that was not the verdict.

## Why the test is not runnable here, and that is not a defect

EP 0x83 is the **status interrupt endpoint**. It reports panel and status
changes and NAKs when it has nothing to say. That is correct interrupt-endpoint
behaviour — it is not broken, it is idle.

§5.8.5 needs an endpoint that produces data **on demand**, and this device has
none that can also halt:

* 0x81, 0x02, 0x82 are isochronous, and §9.4.5 requires halt only of interrupt
  and bulk endpoints, so they cannot be halted at all.
* 0x83 can halt but only speaks when something happens.
* EP0's halt is optional and unimplemented.

Making it testable would need either a front-panel event generated remotely —
`TLM_REQ_SET_MUX` is compiled out of the release tier, which is what ships — or
a bulk endpoint the device has no reason to have.

**So this stays open as unreachable rather than untested**, and the module is
kept with a working arm so that a future device state which does produce status
traffic can be tested without rewriting it. `ch9_probe --coverage` already lists
toggle reset as needing an analyser; that remains true, and now the reason is
specific to this device rather than generic.
