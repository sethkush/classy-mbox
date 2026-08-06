# The DFU escape sat at the end of the SETUP dispatcher, and a size gate found it

2026-08-05, build 0x0031+, found while implementing #188.

## What happened

#188 added 24 bytes to the standard-request branch of `handle_setup()` —
stalling `SET_FEATURE`/`CLEAR_FEATURE` selectors that do not exist, per USB 2.0
§9.4.1 and §9.4.9. `tools/dfu_timing_profile.sh` then failed:

    baseline (no #188)        3840 cycles   320 µs
    with #188                 4080 cycles   340 µs   <- through the 4000 budget

The budget carries roughly 150x margin over any host timeout, so raising it to
4200 would have cost one line and passed. That would have thrown away what the
gate was actually reporting.

## What the gate was actually saying

`handle_setup()` dispatched in this order:

    if (reqtype == 0x40) { vendor;  return; }     /* telemetry */
    if (reqtype == 0x00) { standard switch }      /* 11 cases */
    else if (reqtype == 0x20) { class }           /* <- the DFU trigger */
    else reply_stall();

The Digi enter-DFU request is a **class** request, so it was reached only after
execution fell past the entire standard-request switch. That is the single most
safety-critical request this firmware answers: it is the only escape from a
soft-brick that does not require opening the case and shorting EEPROM SDA to
ground during power-up (`BRICK_LOG.md`). It was last in line.

Nothing was wrong before #188 — 3840 cycles passed. But the ordering meant every
future byte added to the standard branch would tax the recovery path, and the
gate would keep firing on changes that had nothing to do with DFU.

## The fix, and why it is not a workaround

The two branches test mutually exclusive values of `reqtype`, so their order is
free to choose. It had never been chosen; it was whatever order the cases were
written in. Putting the class branch first:

    with #188, class first    3012 cycles   251 µs

**22% faster than the baseline that did not include #188 at all**, and 3 bytes
smaller than the same image in the old order. The gate went green because the
underlying condition improved, not because the threshold moved.

## The general lesson

A budget gate that fails on an unrelated change is not always noise about the
change. Here it was a true statement about the code the change happened to
lengthen: *the recovery path is at the back of the queue*. The cheap response
(raise the limit) and the correct response (reorder) are distinguishable only by
asking what the number is measuring before deciding it is inconvenient.

Related: `FINDING_globctl_bit1_missed.md`, where the cheap reading of an
observation also passed review and shipped a device that was silent on USB.

## Verification

  * `tools/dfu_timing_profile.sh` — 3012 cycles, PASS.
  * `tools/sim_ep0_requests.py` — feeds real SETUP packets under ucSim. Six
    armed replies, six distinct, including the four #188 cases: SetFeature
    (remote-wakeup, and halt on an iso endpoint) both stall, ClearFeature
    (ENDPOINT_HALT) is acknowledged because it is the host's endpoint-recovery
    path and a genuine no-op here, ClearFeature (remote-wakeup) stalls because
    the feature was never advertised.
  * Full preflight.

**Still unverified: host behaviour.** The ACK-everything code this replaced was
justified by a real symptom — "a stall here makes some hosts abandon the device"
— so the simulator passing is necessary and not sufficient. #188 is not closed
until the image enumerates on both Linux and macOS.
