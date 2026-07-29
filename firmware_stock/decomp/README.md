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

    matched 30/31 functions, 1287/1288 bytes

The one outstanding byte is `setup_get_sample_freq`, and it is a layout
artifact rather than a codegen difference — see below. Every construct that
was on the stubborn list has been solved.

Rev 20. Run `python3 tools/match51.py firmware_stock/decomp/cand/*.c`.

## What the peephole rules buy

| Rule | Keil | SDCC without it | Unlocked |
|---|---|---|---|
| `ANL/ORL/XRL acc` -> `a` | 2-byte `A,#imm` | 3-byte `direct,#imm` | `dma0_disable` |
| `mov dir,#0` -> `clr a; mov dir,a` | `CLR A` + `MOV dir,A` | `MOV dir,#0` | `evt_dispatch_epilogue` |
| commute `xrl/orl/anl a,#imm` | 2-byte `XRL A,#imm` | `push b`/`mov b,a`/`mov a,#imm`/`xrl a,b`/`pop b` | `std_clear_feature`, and every `== const` comparison idiom |
| `inc dir` (2 rules, `notUsed('a')` guard) | direct `INC` | `mov a,dir`/`inc a`/`mov dir,a` | all pointer-walking code |
| drop `mov r7,a` before `cjne` | compares A directly | stages through R7 | `std_get_descriptor`, and it cascaded |
| un-commute `cjne` operands | `MOV A,var`/`CJNE A,#c` | `MOV A,#c`/`CJNE A,var` | `setup_get_sample_freq` |
| `cjne a,#2,next` + label -> `setb c`/`subb a,#1` | `SETB C`/`SUBB` | `CJNE` used only for its carry | `std_set_configuration` |
| `jbc`/`sjmp`/label/`ret` -> `jnb`/`clr`/`ret` | naive `JNB`+`CLR` | folds to `JBC` | `toggle_bit1e` |

The 16-bit pointer advance also needs the right C. `if (++pl == 0) ph++;`
leaves a stray `mov a,_ph` that clobbers A — which the following call needs as
its address parameter, so it is a correctness bug as well as a mismatch.
Writing it as two statements, `++pl; if (pl == 0) ++ph;`, produces Keil's
sequence exactly.

The third is the big one: SDCC does not commute a bitwise op against a
constant, so it loads the constant into A and the variable into B and pays for
a push/pop pair. Collapsing that is what makes Keil's comparison idioms
reachable.

## Two things that unlocked most of the tail

**Peephole rules can match across a label**, as long as only one label is both
named and defined inside the window. An earlier attempt named two and silently
never fired, which is why `toggle_bit1e` was written off as needing hand
assembly. It does not — a four-line rule fixes it. The same shape solved the
`SETB C`/`SUBB` range check in `std_set_configuration`.

**SDCC's `notUsed()` guard is too conservative to rely on.** The rule dropping
`mov r7,a` before `cjne` refused to fire inside branch-heavy functions, which
is exactly where it was needed. Removing the guard fixed `std_get_descriptor`
outright and cascaded: `std_set_configuration` fell from 56 wrong bytes to 3 in
the same run. The guard is unnecessary because the byte match is the real
check — anywhere R7 is genuinely live afterwards, the function stops matching
and the harness says so.

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
| Shared tails with live A/DPTR | Keil merges common tails; callers jump in with registers live. ~5 per image. | solved — merge into one source function (`acg_48k_commit`, `dptr_to_ep0_out_buf`) |
| Register parameters | Keil passes the first `char` in R7, SDCC in DPL | solved — `__naked` (`ep0_buf_clear_byte`, `code_read_byte_at_srcptr`) |
| Function ordering | tail calls to an adjacent function use 2-byte `SJMP`; SDCC cannot short-jump to an external symbol | open — needs whole-image layout |
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

## When to stop using C

Most Keil/SDCC divergences are local and a peephole rule fixes a whole class.
Some are not. `hw_master_init` is the first of the second kind: Keil allocated
the accumulator across the **entire function**, loading A with 0 once at entry
and keeping it live through fifteen instructions, using `INC A` to produce the
1 for MEMCFG, and carrying DPTR from 0xFFB0 to 0xFFB1 with a 1-byte `INC DPTR`
across fifteen unrelated SFR writes. SDCC re-zeroes A after every store and
reloads DPTR every time.

That is global register allocation, not a peephole-sized difference. Chasing it
needs one narrow adjacency rule per context. Two such rules were written and
measured: they did not generalise, they failed to fire on the three-store run
at the top of the function, and they made the match *worse* (45 bytes matching
down to 16). They were reverted.

**Rule of thumb: if a fix requires a rule that encodes one specific instruction
adjacency, write the function as annotated `__naked` assembly instead.**

Functions in this class so far: `hw_master_init` (165 B), `usb_ep_dma_init`
(153 B), `audio_clock_mode_apply` (227 B). All three are long straight-line
register programming where Keil held A and DPTR live across the whole body.
Together they are 545 bytes and all three match exactly. A
narrow rule is a standing risk to every function that already matches, and the
functions that need this treatment are long straight-line register programming
where assembly carries the meaning as well as C would. `hw_master_init` is 165
bytes of SFR pokes; the annotated assembly is no harder to read than the C was.

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

`LJMP`/`LCALL` to a **local** label encodes an absolute address, so a function
compiled standalone at 0x0000 differs from the same function linked at its real
address by exactly the base. Those operands are relocated before comparison
rather than excused, so a genuinely wrong local target still fails. Only
`LJMP`/`LCALL` to an *external* symbol is excused, because the linker resolves
it.


`match51.py` masks only the two address operand bytes of `LCALL`/`LJMP` to
external symbols. It must never mask a whole instruction: `"jbc".startswith("jb")`
once caused `toggle_bit1e` to be reported as a false MATCH, because the masked
opcode byte was exactly the one distinguishing `JBC` (0x10) from `JNB` (0x30).
Masking `mov dptr,#...` would likewise hide a wrong SFR address.

Mnemonics are split on any whitespace: compiler output is tab-separated but
hand-written inline asm is not, and assuming tabs silently stopped the
relocation logic from recognising `lcall` in asm blocks.
