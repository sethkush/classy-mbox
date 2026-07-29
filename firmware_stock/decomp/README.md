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

    matched 22/25 functions, 537/597 bytes

`setup_get_sample_freq` reaches 130/131 bytes; its single remaining byte is a
layout artifact, not a codegen difference — see below.

Rev 20. Run `python3 tools/match51.py firmware_stock/decomp/cand/*.c`.

## What the peephole rules buy

| Rule | Keil | SDCC without it | Unlocked |
|---|---|---|---|
| `ANL/ORL/XRL acc` -> `a` | 2-byte `A,#imm` | 3-byte `direct,#imm` | `dma0_disable` |
| `mov dir,#0` -> `clr a; mov dir,a` | `CLR A` + `MOV dir,A` | `MOV dir,#0` | `evt_dispatch_epilogue` |
| commute `xrl/orl/anl a,#imm` | 2-byte `XRL A,#imm` | `push b`/`mov b,a`/`mov a,#imm`/`xrl a,b`/`pop b` | `std_clear_feature`, and every `== const` comparison idiom |
| `inc dir` (2 rules, `notUsed('a')` guard) | direct `INC` | `mov a,dir`/`inc a`/`mov dir,a` | all pointer-walking code |
| drop `mov r7,a` before `cjne` | compares A directly | stages through R7 | `setup_get_sample_freq` |
| un-commute `cjne` operands | `MOV A,var`/`CJNE A,#c` | `MOV A,#c`/`CJNE A,var` | `setup_get_sample_freq` |

The 16-bit pointer advance also needs the right C. `if (++pl == 0) ph++;`
leaves a stray `mov a,_ph` that clobbers A — which the following call needs as
its address parameter, so it is a correctness bug as well as a mismatch.
Writing it as two statements, `++pl; if (pl == 0) ++ph;`, produces Keil's
sequence exactly.

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
| `x == var` | `MOV A,var` + `CJNE A,#const` | `MOV A,#const` + `CJNE A,var` (commuted) |
| `++p` (16-bit) | `INC lo` + `MOV A,lo` + `JNZ` + `INC hi` | round-trips through A |
| test-then-clear bit | `JNB` + `CLR` | `JBC`, then `CPL` |

Note SDCC is often the *better* compiler here. Matching means defeating it.

## Known-stubborn constructs

| Construct | Why | Plan |
|---|---|---|
| `std_set_configuration` @0x025B | `SETB C`/`SUBB` range check vs SDCC's `CJNE`/`JC` | source rewrite or targeted peephole |
| `toggle_bit1e` @0x1028 | SDCC folds test-then-clear into `JBC` then `CPL`; not expressible as a peephole (patterns match instructions, not label definitions) | hand-written `__naked` asm |
| Register parameters | Keil passes the first `char` in R7, SDCC in DPL | `__naked` prologue |
| Shared tails with live A/DPTR | Keil merges common tails; callers jump in with registers live. ~5 per image. | merge into one source function (proven by `acg_48k_commit`) |
| Keil library routines | `?C?CASE` (38 B), `udiv16` (85 B), C51 startup + initialiser interpreter | hand-written asm; all four fully decoded |

## The helper-call idiom — solved

Keil factored `DPTR <- IRAM 0x1B:0x1C` into a helper at 0x0B17 called from
twelve sites, because `LCALL` costs 3 bytes against 6 for the inlined load.
Two things block SDCC from reproducing it:

* **Byte order.** 0x1B is the HIGH byte, 0x1C the low — Keil's convention for
  xdata pointers, the opposite of SDCC's. A real SDCC pointer variable cannot
  live at 0x1B, because the rest of the firmware increments 0x1C as the low
  byte.
* **Setting DPTR as a side effect is not expressible in C.**

The working pattern:

1. Write the helper `__naked` with the IRAM addresses **numeric**, not
   symbolic. Inline asm is opaque to the compiler, so a symbolic reference to
   an otherwise-unused `__at` variable is never emitted and the assembler
   fails on an undefined symbol.
2. At call sites, emit `lcall` from inline asm and declare the callee with a
   hand-written `.globl` **inside** an `__asm` block — SDCC rejects `__asm` at
   file scope.
3. `match51.py` excuses the two address operand bytes, since the linker
   resolves them.

Matched with this: `dptr_from_ep0_ptr`, `dptr_to_ep0_out_buf`,
`ep0_buf_clear_byte`, `setup_get_input_source`, `std_get_configuration`.

`ep0_buf_clear_byte` also demonstrates the register-parameter case: it takes
the low address byte in A, which SDCC's convention would never do, so `__naked`
covers both problems at once.

## Layout-dependent differences

Some differences cannot be resolved by per-function compilation at all.

`setup_get_sample_freq` matches 130 of 131 bytes. Its three branches end with
`SJMP` straight into `send_3byte_ep0_reply`, which Keil placed immediately
after it — a tail call costing 2 bytes. SDCC cannot short-jump to an external
symbol, so it emits a shared 3-byte `LJMP` instead. The generated code is
correct and the instruction sequences are otherwise identical; only the tail
encoding differs, and only because of where the next function sits.

This is the first evidence that **function ordering is part of the match**.
A whole-image build will have to place functions in the original order before
this class of difference disappears. Worth knowing now rather than at 95%.

## Harness correctness

`match51.py` masks only the two address operand bytes of `LCALL`/`LJMP` to
external symbols. It must never mask a whole instruction: `"jbc".startswith("jb")`
once caused `toggle_bit1e` to be reported as a false MATCH, because the masked
opcode byte was exactly the one distinguishing `JBC` (0x10) from `JNB` (0x30).
Masking `mov dptr,#...` would likewise hide a wrong SFR address.

Mnemonics are split on any whitespace: compiler output is tab-separated but
hand-written inline asm is not, and assuming tabs silently stopped the
relocation logic from recognising `lcall` in asm blocks.
