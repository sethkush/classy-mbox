# #227 — the status interrupt endpoint never NAKs, and macOS still shows a stale source

2026-08-16. Unit A on the Mac, release image 0x0060.

## The staleness is real, and proven physically

macOS reported `Input Source: Microphone`. Seth pressed the channel-1 source
button several times and left the unit on INST. macOS still reported
`Microphone` — stale by two positions.

That the HARDWARE moved is not an assumption. The ADC was used as the arm, the
same way #197 used it:

| capture | peak | RMS |
|---|---|---|
| before, source = MIC | 0.001239 | 0.000199 |
| after, source = INST | 0.017671 | 0.003053 |

+23 dB. An unconnected high-impedance instrument input picks up far more than
the mic input does. The mux moved; Core Audio's view did not.

This matters because the note in `c6e9421` called the staleness cosmetic and
proposed a UAC status interrupt endpoint as the cure — and that endpoint was
subsequently BUILT (#207, EP 0x83), and `buttons.c:156` already calls
`usb_status_notify(UNIT_SELECTOR)` on every press. So the cure is present and
the symptom survives it. That note is now out of date.

## A real defect found while checking: EP3 IN never NAKs

`usb_ep0_setup()` configures the endpoint:

```c
IEPBBAX3 = EP_BBAX(EP_STATUS_BUF_ADDR);
IEPBSIZ3 = EP_BSIZE(EP_STATUS_BUF_SIZE);
IEPCNF3  = 0x80;          /* IEPEN */
                          /* IEPDCNTX3 left at 0  <-- */
```

`IEPDCNTX`'s top bit is the NAK flag. Left at zero the endpoint reports "zero
bytes ready, not NAKing", so the UBM answers **every** interrupt poll with a
zero-length packet, for the life of the device.

**This project already learned this exact lesson, on EP0, in its own words:**

> `IEPDCNTX0` top bit is the NAK flag. TI's `engUsbInit` starts EP0 IN in NAK
> state (0x80) so the first IN token doesn't ship a spurious zero-length packet
> before we have data to send. **Ours previously set 0 -> could confuse strict
> hosts.**

#207 added EP3 without copying that line. Fixed here: `IEPDCNTX3 = 0x80` at
configure time. `usb_status_notify()` clears the flag by writing the byte count,
which ships exactly one packet — which is what an interrupt IN endpoint with
nothing to say is supposed to do.

**WHETHER THIS CURES THE macOS STALENESS IS NOT KNOWN.** It is a defect on its
own terms, by the standard the EP0 comment already sets, and it is the only
device-side irregularity found on this path. It is a hypothesis for the macOS
symptom, not a diagnosis. Do not record it as the cause until a unit running
this image tracks a button press on macOS.

## What would settle it

The device is not the only suspect. Untested:

* whether macOS opens the EP 0x83 pipe at all (usbaudiod on macOS 26 is closed
  source; `c6e9421`'s driver reading was of the older open kext)
* whether `bStatusType = 0x00` is the encoding a host acts on. Linux is the
  cheap control here: an ALSA mixer event on a button press proves the packet
  is both sent and understood, and the void box can watch for one. That needs a
  human at the unit to press a button, which is the only reason it has not run.

Run the Linux arm before touching the encoding. If Linux reacts and macOS does
not, the device side is correct and this is a host limitation to document
rather than chase.
