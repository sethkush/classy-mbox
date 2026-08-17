# #226 — serials live in EEPROM, and the bug was never in the I2C code

2026-08-16. Proven end to end on unit B:

```
2-1.3.1  bcd=0160  iSerial 3 RK1672500M
```

That is the SHIPPING image, built `MBOX_SERIAL_EEPROM=1` with **no** `MBOX_UNIT=`,
so no serial string exists anywhere in the binary. The only possible source is
the EEPROM record at 0x1F00 — written through the running application, then
survived a full reflash, then read at boot.

## The sequence, which is now the production procedure

```
make MBOX_PID=0x2000 MBOX_RELEASE=1 MBOX_PROVISION=1   # provisioning image
mboxflash_linux.py flash provision.bin ; replug
mboxprov.py --addr <bus>:<addr> check                  # known-answer arm
mboxprov.py --addr <bus>:<addr> write RK1672500M       # ~26 s, verified off the part
make MBOX_PID=0x2000 MBOX_RELEASE=1 MBOX_SERIAL_EEPROM=1
mboxflash_linux.py flash shipping.bin ; replug         # serial now served
```

`check` reads the 18-byte EEPROM header, whose first bytes are `12 12 34 0d ba
10` in every image this project builds, and every other command runs it first.
Measured: `2c 12 12 34 0d ba 10 01`.

## What the bug actually was

**XDATA is not implemented on this board outside the 0xF800-0xFFFF shared
window.** `--xram-size 0x1000` tells the linker there are 4 KB at 0x0000, so
SDCC allocated `serialno.c`'s `g_str`/`g_dev`/`g_raw` — 87 bytes — at XDATA
0x0001. Dead space: writes vanish, reads return 0x00.

`serialno_load()` was reading the EEPROM **correctly** and storing the result
into a hole. That is the entire reason #221 never worked. There was no I2C
fault at any point.

The fix is to keep every persistent byte in internal RAM, and because internal
RAM is shared with the stack, to spend fewer of them:

* `eeprom_read_seq`'s destination is `__idata`, so the mistake cannot recur
  through that door.
* `SERIAL_MAX_CHARS` 20 -> 12 (string descriptor 42 -> 26 bytes). Real serials
  are 11 and 10 characters. `mboxprov.py` rejects longer rather than truncating.
* `g_dev` deleted. It was an 18-byte RAM duplicate of the device descriptor
  differing in one byte; `descriptors.c` now carries a second `__code` variant
  `AppDevDescSN` with `iSerialNumber = 3`, selected on `g_serial_ok`.
* `g_raw` is a local, so SDCC overlays it.

Stack: 116 before, 55 with a naive move of all three to `__idata`, **73** after
dropping `g_dev`. Shipping image 5975 -> 5865.

## What was wrong along the way, and why

Five hypotheses were tested and all five were void, because **the instrument
reported through the same dead buffer it was investigating**:

| tested | result |
|---|---|
| read sequence vs TI I2c.c | 3 real defects fixed; symptom unchanged |
| bus frequency (TI sets it per transaction; we never did) | all 3 arms identical |
| ISR vs main-loop context | identical |
| interrupts on vs `EA = 0` | identical |
| stock's own routine, ported byte-for-byte | never gets RCV_DATA_FULL |

The tell was that every arm returned **byte-identical zeros**. Hardware faults
do not reproduce that exactly. `CLAUDE.md` already says to put a known-answer
arm in every run; the arm was on the EEPROM contents and never on the reporting
path, and **the instrument is part of the run**.

Worse, an intermediate conclusion — "mboxfw's I2C access does not work at all,
so `TLM_REQ_ENTER_DFU` cannot be breaking the checksum" — was published in
a19b617 and was wrong. It was refuted from operating history, not analysis: DFU
has worked for months, and unit A booted the app after every replug that night
while only the triggered unit reached DFU. The hardware observation was
available the whole time and was weighted below a bench instrument. That
inversion is the single most expensive mistake of the session.

## Corrections to earlier entries

* **Writes were never broken.** `eeprom_write_byte` takes a value, not a
  buffer, so dead XDATA never touched it. The first provisioning attempt
  probably wrote the record correctly and only the readback lied.
* **1460 ms per byte is real, and is `eeprom_write_hold`.** a19b617 claimed it
  was a `wait_bit` timeout; it is not — successful writes still take that long,
  because the hold runs `0xFF00` volatile iterations, ~290x the 24C64's 5 ms.
  The original reading was right. The loop is still not being shortened: its
  other caller is the DFU escape, and provisioning runs once per unit at a desk.
* **`TLM_REQ_ENTER_DFU` is not broken**, and the `ffff:fffe`-vs-`0dba:1001`
  argument built on top of that claim is withdrawn.

## Not to be re-derived

A byte-for-byte port of stock Rev 20 `i2c_eeprom_read_byte` (CODE:0cdd) — clear
mask `0xFC`, no second `CLEAR_ALL`, slave `0xA0` for the read phase, dummy
`0x00`, `STOP_READ` armed after — **does not work here**. It never gets
`RCV_DATA_FULL` and returns the previous byte. Either the "R6 is still 0xA0"
reading of 0x0ce4/0x0d09 is wrong or that routine depends on caller state.
TI's `I2CAccess` sequence is correct on this hardware and is what ships.

**0xFA00 is not a usable destination either.** Pinning buffers there returned
varying garbage (I2CSTA reading a constant 0xA7). Only internal RAM gives
correct reads, and the boot canaries that would vouch for that window are
compiled out of release builds.

## Status

**BOTH UNITS PROVISIONED.** Both on build 0x0060, both serving their serial from
EEPROM on an image that contains no serial string:

```
2-1.3.1  bcd=0160  iSerial 3 RK1672500M
2-1.3.2  bcd=0160  iSerial 3 RK10874600Q
```

Unit B's record additionally survived a genuine power cycle performed for an
unrelated reason (unit A's DFU trigger), which is the persistence property
tested without setting out to test it.

Both ALSA cards up, both Selectors reporting Microphone, capture verified after
provisioning: 96000 frames each for a 2 s request at 48 kHz -- exact -- unit B
at -81.7 dBFS RMS (noise floor, no source) and unit A at -53.0 dBFS (an SM58
hearing the room).

`MBOX_UNIT=A/B` is now bench-only scaffolding rather than the way units are
identified. It stays for builds made before a unit is provisioned.

`TLM_REQ_PROV_DIAG`/`PROV_DIAGRD` and `eeprom_read_diag` are retired, having
answered.
