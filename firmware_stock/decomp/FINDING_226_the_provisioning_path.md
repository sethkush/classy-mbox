# #226 part 2 — the provisioning path, built

2026-08-16. `FINDING_226_dfu_writes_exactly_payloadsize.md` established the rule
and designed this; here is what was built and what building it turned up.

## What exists now

Two vendor requests, DEVICE recipient, present ONLY in `make MBOX_PROVISION=1`:

| req | dir | wValue | wIndex | meaning |
|---|---|---|---|---|
| `TLM_REQ_PROV_WRITE` 0x19 | OUT | offset into the record | byte | write one byte |
| `TLM_REQ_PROV_READ` 0x1A | IN | offset into the record | — | read 8 bytes back |

plus `tools/mboxprov.py` (`list` / `show` / `write` / `--selftest`).

**wValue is an offset into the record, never an address.** The firmware rejects
`offset >= 27` and supplies the `0x1F00` base itself; `EE_SERIAL_LO` is `0x00`
and the maximum offset is 26, so the sum cannot carry and the high byte is
always `0x1F`. No value any host can send names an address outside
`0x1F00..0x1F1A`. That is the one thing the previous finding said must not be
got wrong, and it is enforced twice — once where the request is latched, once in
`main()` before the write — because this is the only `eeprom_write_byte()` call
in the firmware whose address comes from the host at all, and the byte it could
otherwise reach is the header checksum at `0x0000`, the exact byte enter-DFU
destroys on purpose.

The write runs in the **main loop**, not the SETUP handler: the 24C64 program
cycle is a ~5 ms busy-wait and the handler is interrupt context. A second
request arriving while one is pending is **STALLed, not dropped**, so the host
tool retries that byte instead of silently losing it — a dropped byte would
produce a record that fails its own XOR, which reads back as "no serial" with
nothing to say why.

## Three things the build turned up

**1. The provisioning image cannot also serve the serial — measured, 6023 > 6016.**
`MBOX_PROVISION` + `MBOX_SERIAL_EEPROM` links seven bytes over program RAM. The
previous finding's "a provisioning build has no such constraint" was wrong: it
has the same hardware ceiling as every other build; what it lacks is a reason to
be *minimal*. Splitting them is better than shaving anyway — the boot-time read
is the SHIPPING image's job, and testing it there tests the thing that actually
ships rather than a proxy. So the provisioning image proves the BYTES (read
straight back off the part, no power cycle), and the shipping image proves the
DESCRIPTOR. `usb.c` now `#error`s if the two flags are combined.

**2. The requests were written into the wrong tier and compiled away to nothing.**
Placed first in the diagnostic branch of the vendor dispatch — but the
provisioning image must be a RELEASE build, because the diagnostic tier links at
**6086** before provisioning is added at all. The image built, gained 199 bytes
of `eeprom_read_seq` and an unreachable `main()` write path, and would have
answered every provisioning request at the desk with a STALL. **No gate caught
this.** Preflight passed; the size check passed; the citation gates passed. It
was caught by asking which tier the 199 bytes had landed in, i.e. by not
accepting a plausible number. The cases are now hoisted above the tier split,
with a comment saying why that placement is load-bearing.

**3. `audit_sfr_writes.py` scans the last build, so its manifest was single-tier.**
The gate was already failing on `HEAD` for the shipping image — five writes from
`eeprom_read_seq` and one reclassified `IEPDCNTX0` that the baseline predated —
and the only remedy the tool offered was `--update`, which re-baselines onto
whatever is in `build/`. That is the failure mode the gate exists to prevent: run
it on the wrong tier and the fix is to erase the evidence.

`tools/sfr_writes.tier_optional` now lists writes that legitimately vary by
tier, exempt from the REMOVED check **only** — absence is forgiven, presence is
not, so a genuinely new write still fails. Two documentation defects surfaced
with it and are fixed: `0xffc2` (`I2CDATI`, read once per byte by
`eeprom_read_seq`) had no justification row at all, and the `0xffc0 or 0x02` row
still asserted that "mboxfw no longer reads the EEPROM" — untrue since #221
restored the read path. Both images now pass all 38 gates.

## The sequence

```
make MBOX_PID=0x2000 MBOX_RELEASE=1 MBOX_PROVISION=1     # provisioning image
sudo tools/mboxflash_linux.py flash mboxfw/build/mboxfw_flasher.bin
# replug
sudo tools/mboxprov.py --addr <bus>:<addr> write RK10874600Q
make MBOX_PID=0x2000 MBOX_RELEASE=1 MBOX_SERIAL_EEPROM=1  # shipping image
sudo tools/mboxflash_linux.py flash mboxfw/build/mboxfw_flasher.bin
# replug -- lsusb -v now shows iSerialNumber, read from EEPROM at boot
```

`--addr`, not `--serial`: the provisioning image serves no serial, which is the
whole point — it is what runs *before* the unit has one. With two units on the
bench that means `list` first, because selecting the wrong one writes the wrong
serial onto it.

`tools/mkserial.py` is **deleted**, not fixed; its assumption does not hold. Its
`--selftest` slot in `preflight.sh` and `ci_bisect_gates.sh` is taken over by
`mboxprov.py --selftest`, which additionally checks the record geometry against
`serialno.h` and asserts that every single-bit corruption of the header is
rejected — the layout is duplicated between C and Python, and a record built to
the wrong layout does not fail loudly, it just serves no serial.

## Status

Built, gated, **not yet run against hardware.** Nothing here has touched a unit.
