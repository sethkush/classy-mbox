# Byte-exact recompilation of the stock Mbox firmware

Goal: C source that compiles to bytes **identical** to the stock images, so the
decompilation is proved rather than believed.

The original was built with **Keil C51**; we build with **SDCC 4.6.0**. That
turns out to be workable — see the scoreboard — with two supports:

* `keil.peep` — peephole rules steering SDCC toward Keil's encoding choices.
* Source-level function boundaries reconstructed from the Ghidra listing.
  Keil merges common tails across functions, so several Ghidra "functions" are
  fragments. `acg_48k_commit` is one source function spanning what Ghidra
  splits into `acg_set_freq_48k_family` + `acg_commit_and_ctl`.

## A byte match is self-validating

If our bytes equal the stock bytes, the code is correct because the stock
firmware is correct. That is why `keil.peep` can carry transforms that would
be unsafe in general (the `CLR A` rule clobbers A): any function where the
transform is wrong stops matching, and the harness reports it. Re-run the whole
candidate set after touching `keil.peep`.

## Usage

    python3 tools/match51.py firmware_stock/decomp/cand/*.c
    python3 tools/match51.py -v firmware_stock/decomp/cand/foo.c   # byte diff

Each candidate declares its target in a header comment:

    // MATCH: image=rev20 addr=0x0B50 len=15 func=ep0_clear_stall_both \
    //        cflags=--peep-file,firmware_stock/decomp/keil.peep

`len` is optional; the stock length comes from the Ghidra function table.

## Scoreboard

    matched 17/19 functions, 333/393 bytes

Rev 20. Run `python3 tools/match51.py firmware_stock/decomp/cand/*.c`.

## What the peephole rules buy

| Rule | Keil | SDCC without it | Unlocked |
|---|---|---|---|
| `ANL/ORL/XRL acc` -> `a` | 2-byte `A,#imm` | 3-byte `direct,#imm` | `dma0_disable` |
| `mov dir,#0` -> `clr a; mov dir,a` | `CLR A` + `MOV dir,A` | `MOV dir,#0` | `evt_dispatch_epilogue` |
| commute `xrl/orl/anl a,#imm` | 2-byte `XRL A,#imm` | `push b`/`mov b,a`/`mov a,#imm`/`xrl a,b`/`pop b` | `std_clear_feature`, and every `== const` comparison idiom |

The third is the big one: SDCC does not commute a bitwise op against a
constant, so it loads the constant into A and the variable into B and pays for
a push/pop pair. Collapsing that is what makes Keil's comparison idioms
reachable.

## Idiom mismatches — the actual grind

Keil and SDCC pick different instruction sequences for the same C. Each needs
either a source rewrite that steers SDCC, or a targeted peephole. Found so far:

| Operation | Keil | SDCC |
|---|---|---|
| `x == const` | `XRL A,#const` + `JNZ` (4 B) | `CJNE A,#const` (3 B) |
| `x >= const` | `SETB C` + `SUBB A,#const-1` + `JC` | `MOV R7,A` + `CJNE R7,#const` + `JC` |
| test-then-clear bit | `JNB` + `CLR` | `JBC`, then `CPL` |

Note SDCC is often the *better* compiler here. Matching means defeating it.

## Known-stubborn constructs

| Construct | Why | Plan |
|---|---|---|
| `std_set_configuration` @0x025B | `SETB C`/`SUBB` range check vs SDCC's `CJNE`/`JC` | source rewrite or targeted peephole |
| `toggle_bit1e` @0x1028 | SDCC folds test-then-clear into `JBC` then `CPL`; not expressible as a peephole (patterns match instructions, not label definitions) | hand-written `__naked` asm |
| Helper-call idiom | Keil factored `DPTR <- IRAM 0x1B:0x1C` into a called helper; SDCC inlines the pointer load. Affects most EP0 buffer code. | `__naked` helper + asm at call sites |
| Register parameters | Keil passes the first `char` in R7, SDCC in DPL | `__naked` prologue |
| Shared tails with live A/DPTR | Keil merges common tails; callers jump in with registers live. ~5 per image. | merge into one source function (proven by `acg_48k_commit`) |
| Keil library routines | `?C?CASE` (38 B), `udiv16` (85 B), C51 startup + initialiser interpreter | hand-written asm; all four fully decoded |

## Harness correctness

`match51.py` masks only the two address operand bytes of `LCALL`/`LJMP` to
external symbols. It must never mask a whole instruction: `"jbc".startswith("jb")`
once caused `toggle_bit1e` to be reported as a false MATCH, because the masked
opcode byte was exactly the one distinguishing `JBC` (0x10) from `JNB` (0x30).
Masking `mov dptr,#...` would likewise hide a wrong SFR address.
