# #193: the default build serves no serial, and that is the correct answer

2026-08-06. Decision, plus the gate that keeps it honest.

## What ships today

| build | iSerialNumber | serial string |
|---|---|---|
| default (`make`) | 0 | absent |
| `MBOX_UNIT=A` | 3 | `RK10874600Q` |
| `MBOX_UNIT=B` | 3 | `RK1672500M` |

**This stays.** The per-unit serial is not negotiable — it is the only thing
that tells the two bench units apart, since both deliberately run the same PID
(`BENCH_WIRING.md`, "Trust the serial"). What #193 decides is only what the
*default* build does, and the answer is: nothing, on purpose.

## Why zero, and why not a constant

**Stock agrees, in both images.** Rev 20 @0x0596 and Rev 22 @0x057D both carry
`iSerialNumber = 0`:

    12 01 10 01 00 00 00 08 BA 0D 00 10 20 00 01 02 00 01
                                                    ^^ iSerialNumber

**A baked-in constant would be worse than nothing.** One default image is
flashed to any number of devices. A fixed string would make every one of them
claim the *same* serial, and hosts key device identity on exactly that field —
Windows builds its device-instance path from it, so two units with one serial
collide in the device database rather than appearing as two devices. "No
serial" is a legal, honest statement; "the same serial as every other unit" is
a false one. USB 2.0 §9.6.1 makes the field optional precisely for this case.

**There is no runtime source to read.** This was the option worth wanting: if
the unit's serial lived somewhere the firmware could read, one image could
serve the true serial on every device and the whole question would dissolve.
It does not. The serials are **label-only** — searched every stock image, every
payload, and both EEPROM-side dumps in the tree for `RK10874600Q` and
`RK1672500M`, and they appear nowhere but in this repo's own documents. The
strings printed on the units were typed into `usb.h` by hand.

Provisioning them into spare EEPROM (there is room — 8192 bytes total against
~6032 used) would mean a separate per-unit write step, re-adding
`eeprom_read_byte` (removed in the boot-button cull, no callers left), a RAM
buffer for the string on a part with 128 bytes of direct IRAM, and I²C traffic
in the boot path. That is a lot of new failure surface, in the delicate part of
the image, to replace a `-DMBOX_UNIT=` that already works.

## The gate

Nothing checked `iSerialNumber` at all before today, and it can be wrong in two
directions that fail differently:

- **index set, string absent** — the host asks for string 3 and gets a STALL
  mid-enumeration. Some hosts shrug; some abandon the device.
- **index 0, string present** — dead bytes on a part with none spare, and a
  build that *looks* per-unit while serving no serial.

The second is not hypothetical. It is **build 0x0020**: built with
`MBOX_PID=0x2000` but without `MBOX_UNIT=B`, so `--serial RK1672500M` matched
nothing and read as *"unit absent"* rather than as an error — 1 km away, after a
flash. `verify_descriptors.py` now cross-checks the field against whether the
serial string is actually linked, and both directions are mutation-tested.

`mboxtlm.py` was carrying the other half of that failure: its "no Mbox found"
message was identical for *nothing is plugged in* and *the unit is right there
but its image serves no serial*, which need opposite responses. It now lists
what IS attached, marks a unit serving no serial as
`(NONE -- image built without MBOX_UNIT=)`, and says plainly that this is a
build problem rather than a missing device.

## What is still open

Whether a shipped image should carry a serial at all is a **product** question,
not a compliance one, and it needs a provisioning story before it needs code.
The honest position until then is the one both stock images take.
