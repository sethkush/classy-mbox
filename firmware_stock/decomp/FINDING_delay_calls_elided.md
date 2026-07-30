# Every busy-wait delay in mboxfw had its call site deleted by the compiler

Found 2026-07-29 by the first real run of `tools/verify_reachability.py`, built
to close `WHAT_REMAINS_UNKNOWN.md` §5. The gate was written for the `TR0` class
of bug; it found a different and worse one on the way.

## The mechanism

SDCC proves that a `static` function whose body is a loop over a NON-VOLATILE
local has no observable effect, and deletes **the call site**. It does not
delete the function body -- that stays in the image as an unreferenced symbol.

The result is the nastiest possible failure signature:

  * the source says `inter_reg_delay();`
  * `grep` finds the function, with a plausible body and a citation comment
  * the `.rst` listing shows the function compiled, loop and all
  * every SFR-write gate passes, because no SFR write changed
  * and the delay never happens at runtime

The only visible trace is a gap in the listing's source-line comments. For
`eeprom_write_byte` the listing runs `;eeprom.c:78` then `;eeprom.c:80`, with
line 79 -- the call -- emitting nothing at all.

## The three that were live

| function | called from | consequence of the elision |
|---|---|---|
| `short_delay` | `hw_init` boot panel sequence, 1 site | stock publishes panel `0x00`, delays, then `0xF6`; mboxfw published both back-to-back with no gap |
| `inter_reg_delay` | `cs8427_boot_init`, **9 sites** | the entire CS8427 register sequence ran with zero settling time between writes |
| `eeprom_write_hold` | `eeprom_write_byte`, 1 site | no wait for the 24C64's up-to-5 ms internal program cycle after any write |

`bit_delay` is a fourth uncalled delay, but it is empty **by design** --
`cs8427.c` argues natural instruction timing at 12 MHz already meets the part's
SCL spec, so there is nothing to emit. It is carved out of the new gate by name,
with that reason recorded. Worth stating plainly: that argument rests on a
comment, not on a measurement.

`canary_delay` was briefly suspected and is fine twice over: it already declares
`volatile unsigned int i`, and the whole LED-canary block sits behind
`#ifdef CANARY_LED`, so it is not in the default build at all.

## Why this matters more than it looks

**The `hw_init` case contradicts something checked earlier this same session.**
The boot panel sequence was verified to "match stock exactly" -- and at the
level of *which values are written*, it did. The delay between them was in the
source, so reading the source confirmed it. It was not in the binary. A
source-level review cannot see this class; only the listing or the call graph
can.

**The EEPROM case makes an instrument unreliable.** `eeprom_smoke_test` writes
and reads back three times in a row. Without the program-cycle hold, a read can
land while the part is still internally busy. Its result is reported as
telemetry block 4 byte 0 and has been read as "the EEPROM hardware is fine / not
fine" -- so a failure there could have been this bug rather than the part. Given
2 km per power cycle, a diagnostic that lies is expensive.

The DFU path is NOT affected: `eeprom_invalidate_signature` writes exactly one
byte (the header checksum at 0x0000) and is followed by a power cycle, so there
is no second write to be dropped.

**The CS8427 case is the one whose runtime effect is least determined.** Nine
register writes with no gap either works or does not, depending on the part's
minimum inter-access timing against a 12 MHz 8051. Its init status is telemetry
block 4 byte 1. Nothing here proves it was failing -- only that the delay Rev 20
performs was absent.

## Fix

`volatile` on each loop counter, which is the precedent already in the repo
(`canary_delay` uses it, and survives for that reason). Verified by emitted call
count, which went 0 -> 9 / 0 -> 1 / 0 -> 1. Code size 5743 -> 5808 bytes against
the 6144 budget.

## Gate

`verify_reachability.py` now fails on any real function that is emitted but
called from nowhere. SDCC's `; function <name>` listing marker distinguishes
genuine functions from data symbols and `_fn_PARM_2` argument slots, both of
which are also plain labels.

Mutation-tested: removing `volatile` from `eeprom_write_hold` alone trips it.

## Not verified on hardware

All static. What a flash would show: whether the CS8427 init status and the
EEPROM smoke result change now that the delays are real. Both are already
telemetry fields, so this is a read rather than a new experiment -- but nothing
has been flashed, and there is no recorded pre-fix baseline for either field
taken with a build whose ID is known.
