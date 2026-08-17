# Most of #192 did not need Windows, and it found three real defects

2026-08-06, build 0x0037, unit A. `tools/ch9_probe.py`.

## What this is, and is not

USB20CV is USB-IF's own tool, runs on Windows, and is the **authority** — its
verdict is what certification rests on. This is not that. #192 exists precisely
because "everything above this line is our own reading of the spec", and a suite
we wrote is one more reading by the same authors.

But most of USB 2.0 §9.4 is mechanical: a request is either answered with the
right shape or it is not, and an unsupported one either stalls or wrongly
succeeds. Those cases can be exercised from Linux against the live device, and
they find real bugs. **29 of 33 checks passed. The other four are below.**

## The three that matter, and they are one defect

**The device does not validate `wValue` / `wIndex` against what it declares.**
It answers requests naming things that do not exist, instead of returning a
Request Error:

| request | spec | what we do |
|---|---|---|
| `GET_DESCRIPTOR(config, index 5)` | §9.4.3 — Request Error if index ≥ `bNumConfigurations` | returns config 0 |
| `SET_CONFIGURATION(9)` | §9.4.7 — Request Error unless `wValue` is 0 or a declared configuration | accepts it |
| `SET_INTERFACE(iface 1, alt 7)` | §9.4.10 — Request Error if the alternate setting does not exist | accepts it |

This is the same shape as #188 one level up. #188 made *unsupported feature
selectors* stall; these are *unsupported values of supported requests*, and
nothing checks them. `SET_INTERFACE` is the one with teeth: alt settings drive
`streaming_*_enable()`, so accepting alt 7 runs the alt-selection path with a
value no descriptor declares.

USB20CV tests all three. Finding them here means it will not be reporting
defects we could have found ourselves.

## The three are fixed (#195, build 0x0038, confirmed on hardware)

`GET_DESCRIPTOR` now stalls a config index other than 0, `SET_CONFIGURATION`
stalls anything above 1, and `SET_INTERFACE` validates the alt against what each
interface declares — alt 0 only on interface 0, alt 0 or 1 on interfaces 1 and 2
— and stalls an interface number nothing declares, which previously fell past
both arms and still answered with a zero-length ACK.

43 bytes, which is what was free. All 37 gates pass.

**Re-run against 0x0038 on hardware, 2026-08-06: 40 passed, 1 failed.** The
three rows in the table above now stall as the spec requires, measured on the
device rather than argued from the diff. The single remaining failure is the
`wLength = 0` case below, which was deferred deliberately; the `GET_INTERFACE`
row is still recorded as inconclusive, as it must be.

The probe's own teardown also held this time — it reported
`driver rebound on interface(s) [0, 1, 2]`, and both units captured a full
3 s at 48 kHz afterwards (864044 B, the exact expected count) with both Feature
Units still present. The first run's silent-audio-loss failure mode did not
recur.

## The fourth, minor

`GET_DESCRIPTOR(config)` with `wLength = 0` **stalls**. USB 2.0 §9.3.5: a zero
`wLength` means there is simply no data phase, and the request should complete.
No real host asks for a descriptor this way, so the impact is close to nil — but
it is a divergence and USB20CV exercises it.

**Deliberately NOT fixed yet, and the reason is worth stating.** For a
device-to-host request the status stage runs in the opposite direction, so a
`wLength = 0` GET_DESCRIPTOR needs the host's status **OUT** acknowledged, while
`reply_zero_length()` arms an IN — which is right for the host-to-device case it
was written for. Getting it right means touching the EP0 state machine, the most
delicate code in the image and the source of the desync that
`g_ep0_reply_remaining = 0` exists to prevent. Two bytes of headroom is not the
budget for that, and no host issues the request. It waits for real headroom or
for USB20CV to say it matters.

## One result that is NOT a result

`GET_INTERFACE(iface 9)` returned **ENOENT**, not a stall — the host stack
declined to *route* a request naming an interface the active configuration does
not contain, so the device never saw it. That is inconclusive, and the probe now
records it as such rather than as a pass or a failure. Genuinely reaching that
case needs USB20CV or an analyser.

This is the same trap `probe_feature_requests.py` documents: **classify by
errno, never by message.** EPIPE (32) is a device stall; EIO and ENOENT are the
host stack talking.

## What still needs Windows — CORRECTED 2026-08-16

**This section was wrong for weeks, and it kept being cited.** Three of its four
bullets were dead or misfiled, and two of them were killed by work in this very
repository after the list was written. It is the #214 failure shape in a
document instead of in code: a correct-when-written conclusion left standing
after its inputs changed, re-asserted every time someone read it.

The original list, with what actually became of it:

| original bullet | status |
|---|---|
| `SET_ADDRESS` behaviour; "re-assigning it from userspace would strand the device" | **REFUTED.** True of *userspace*, false of the machine. `tools/ch9mod/ch9addr.c` re-addresses from a kernel module, `FINDING_218` records that Default state is reachable from Linux, and **#212 — a real defect, an illegal address accepted — was found exactly this way.** |
| malformed packets and timing violations | **MISFILED.** Windows does not buy these. USB20CV is a *command* verifier driving an ordinary host controller; it cannot emit a malformed packet either. This needs a Pico or a Cynthion, and it is not a Windows item at all. |
| electrical and signalling tests | **MISFILED, same reason.** USB20CV does not do these. This is a scope-and-fixture job. |
| the descriptor-vs-class-spec rulebook USB20CV encodes | **STANDS — and it is the only one.** |

### And a claim this document should never have implied

Nobody reverse-engineered USB20CV. **The tool was never in hand** — no binary, no
test list, no documentation. What exists here is **Chapter 9 implemented from the
USB 2.0 specification text**: `ch9_probe.py` covers §9.3.5, §9.4.1 through
§9.4.11, and §9.6.2/§9.6.3, plus three kernel modules for what userspace cannot
reach. That is a large overlap with what USB20CV tests and it is not the same
claim, because the USB20CV list cannot be enumerated from here to check the
match. Describing it as "USB20CV coverage" overstates it; describe it as
Chapter 9 coverage, which is what it is and which is verifiable.

### So the honest residue

**One item genuinely wants a second implementation: the UAC1 class-descriptor
rulebook.** Even that is not a capability limit — it is a finite set of
descriptor checks that can be written as a gate here, and `check_uac1_rulebook.py`
now does. What a locally-written validator cannot do is catch an error that lives
in *our reading* of the Audio 1.0 document, because it encodes that same reading.
That is an argument for an independent opinion, not for Windows specifically:
**macOS/IOUSBFamily is a second independent implementation and is free.**

Two Chapter 9 gaps were also found while auditing this list, both Linux-reachable
and neither previously covered: **§9.2.6 request-processing time limits** and
**suspend/resume behaviour**. See `tools/ch9_timing.py`.

## The probe disabled the thing it was probing

First run left unit A **unable to capture**. EP0 telemetry still answered, so the
unit looked healthy while its audio device was gone — the failure mode that
matters, because it is invisible to the instrument you would reach for first.

Cause: interface-recipient cases need `snd-usb-audio` detached, and both
`attach_kernel_driver()` and the driver's sysfs `bind` fail while **libusb still
holds the interfaces** — with `EBUSY`, which reads as "something else owns it"
rather than "you own it". The fix is ordering: release the handle with
`dispose_resources()` first, then rebind, then verify through **sysfs** rather
than through the pyusb object just disposed of.

The probe now reports which interfaces came back, and prints the manual rebind
command when they do not. A teardown that can fail silently is not a teardown.
