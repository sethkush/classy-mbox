# #212 — SET_ADDRESS accepted an illegal address, and the unit went unreachable

2026-08-15. The first genuine Chapter 9 **violation** this project has found, as
opposed to a divergence. Found by `tools/ch9mod/ch9addr.ko` on its first run,
which is the module written specifically because usbfs will not pass
SET_ADDRESS through.

## The measurement

```
ch9addr: target 0dba:2000 at bus 2 dev 4, current address 4
ch9addr: FAIL SET_ADDRESS(200) -> 0 (accepted)  [address > 127 must be a Request Error]
ch9addr: DEVICE ACCEPTED AN ILLEGAL ADDRESS.
```

USB 2.0 Table 9-3 gives `bDeviceAddress` **seven bits**; §9.4.6 makes anything
above 127 a Request Error, which must be answered with a STALL. The device
acknowledged it.

The consequence was immediate and exactly what the spec is protecting against.
Unit B stopped answering at address 4:

```
bus 2 addr 5 devdesc 18B ok  serial=RK10874600Q
bus 2 addr 4 FAILED [Errno 32] Pipe error
```

Capture and playback on that unit were gone with it.

## The cause, which is one line

`usb.c`, `handle_setup()`:

```c
case REQ_SET_ADDRESS:
    STAGE(15);
    g_pending_address = wValueL;   /* no range check whatsoever */
    reply_zero_length();
    break;
```

Every value of `wValueL` was accepted. Fixed by stalling anything above 127
before the assignment. **14 bytes**, and all 37 gates pass.

`0xFF` deserves its own mention: it is also the "nothing pending" sentinel for
`g_pending_address`, tested at the deferred write. So without the range check
`SET_ADDRESS(255)` would be **acknowledged and then silently discarded** — the
device agreeing to an address it never takes, which is a worse failure than
either accepting or refusing cleanly.

## Recovery worked as designed, with no physical access

`uhubctl -l 2-1.3 -p 1 -a cycle` returned the unit at a fresh address with its
serial visible, descriptors answering, 576,044 bytes captured and playback
clean. This is why the module runs only out-of-range addresses by default: a
conforming device stalls them and nothing moves, and a non-conforming one is a
bus reset away from recovery. Every unit here is 1 km from the person who can
replug it, and none of that was needed.

Incidental: bus resets went 8 -> 11 across the port cycle. **Up, not down** — so
the cycle did not drop VBUS, consistent with everything else known about both
hubs.

## The second result in that run was an artefact, and it was mine

The same run reported `SET_ADDRESS(128) -> -32 (STALL)` as a PASS. It is not a
pass. It is not anything.

By the time 128 was issued, the device had already taken the illegal address and
was no longer answering where the host was asking. That result describes a
device that was not listening. Read naively it says something actively
misleading — "the boundary case is handled correctly, only large values fail" —
which would have pointed the fix at the wrong place.

The module now stops after an unexpected accept. **A test that runs after the
thing under test has changed state is not a test**, and ordering that two-step
without a guard was a design error in the instrument, not a subtlety of the
device.

## Why this one needed a kernel module

usbfs refuses SET_ADDRESS: usbcore owns the address map, and a userspace program
that moved the device would leave the host addressing something that is no
longer there — precisely what happened here, under controlled conditions. This
is the single Chapter 9 subject that `ch9_probe.py` structurally cannot reach,
and it is the subject that turned out to contain the real bug.

`ch9_probe` reads 46/47 and every one of those checks passed. The suite was
clean; the gap was where the defect lived.
