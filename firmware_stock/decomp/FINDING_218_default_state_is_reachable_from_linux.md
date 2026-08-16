# #218 — Default-state testing IS reachable from Linux, and the device passes the mandatory part

2026-08-16. Retracting a claim I made three times today, and recording what
replaced it.

## The claim I kept repeating

"Default-state behaviour is structurally unreachable from Linux — USB20CV drives
enumeration itself, and by the time libusb or a module can see the device, that
phase is over." It went into `ch9_probe`'s coverage map, into a commit message,
and into two summaries as the honest residue of #192.

It is wrong, and the route was already built.

`SET_ADDRESS(0)` puts the device into the Default state — which #212's module had
already been doing and verifying. A device in the Default state is **answering
at address 0**. usbcore addresses transfers from `udev->devnum`, so pointing that
at 0 talks to it there. No control over enumeration is required, only the state,
and the state was one request away the whole time.

The error was reasoning about the tool ("USB20CV drives enumeration") instead of
about the requirement ("the device must be in Default state"). Those are not the
same thing and only one of them is a constraint.

## What the device does

```
--- Default state (talking to address 0) ---
PASS GET_DESCRIPTOR(device,8) @addr0 -> 8
     bLength=18 bDescriptorType=1 bMaxPacketSize0=8
OBSERVED SET_CONFIGURATION(1) @addr0 -> 0 (accepted). NOT SCORED.
```

**The mandatory one passes.** §9.6.1's 8-byte device-descriptor read at address 0
is the first request every host makes, before it knows `bMaxPacketSize0`, and a
device that fails it cannot be enumerated by anything. Ours answers correctly:
`bLength` 18, type 1, `bMaxPacketSize0` 8.

`SET_CONFIGURATION` in Default state is **recorded and deliberately not scored.**
The first version called accepting it a FAIL, citing "§9.4.7: not valid before
the device is addressed" — which I wrote without opening the document. §9.4 marks
several requests' Default-state behaviour as *not specified* rather than
requiring a Request Error, and if this is one of them the FAIL was a fabricated
defect. Given the §5.6.3-vs-§5.6.4 mess earlier the same day, it stays an
observation until someone reads the section.

## What it costs: a port cycle, and the reason is itself a finding

The device does **not** come back when re-addressed out of Default state. It ACKs
`SET_ADDRESS(24)` and then does not answer at 24. Recovery is `uhubctl -a cycle`,
which works every time and needs no physical access.

Whether that is a device defect or the host stack interfering is **not
established**. mboxfw defers the address write to the status stage
(`g_pending_address`, stages 15/16), and usbcore still believes the device is at
its old address throughout, so both sides have a plausible claim. Worth chasing;
not chased here.

## The instrument lied twice before it told the truth

**First**, the restore path printed `re-addressed to 24; NO port cycle needed`
the moment `SET_ADDRESS` returned >= 0. That is an ACK, not evidence. The unit
was not back — the next run found every request stalling and it needed a port
cycle after all.

That is the identical mistake this same file warns about forty lines earlier,
about `SET_ADDRESS(0)`: *"an ACK only proves the device answered, not that it
acted."* Written, and then not applied to the restore path in the same function.
It now performs a real transfer and only claims success if 18 bytes come back.

**Second**, on the run before that, the follow-up probe used a stack buffer with
`usb_control_msg`, which DMA-maps what it is given. It tripped `WARNING at
drivers/usb/core/hcd.c:1487` and returned `-EAGAIN` — which reads exactly like
"the device stopped answering", i.e. exactly like the PASS being looked for.

Three instruments in one day failed in the shape of the result they were looking
for: this one twice, and `eptoggle`'s reference arm admitting `-ETIMEDOUT` as
proof that data flowed. Every one was caught by reading a line that was not the
verdict — a kernel warning, a follow-up run, a reference row. **The verdict line
is the least trustworthy line in any of these outputs.**
