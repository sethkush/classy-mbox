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
`entry=1` marks a merged-tail entry point (see below); `span=1` says the
candidate holds more than one function and the whole run of bytes is the claim.

Then link the lot at their stock addresses:

    python3 tools/link51.py rev20
    python3 tools/link51.py rev20 -v

## Scoreboard

    match51:  175/175 candidates, 8335/8335 bytes, no partials
    link51 rev20:  IMAGE IDENTICAL -- all 8174 bytes
    link51 rev22:  IMAGE IDENTICAL -- all 8174 bytes

Both stock ROMs rebuild from this source, bit for bit:

    $ python3 tools/link51.py rev20 --emit-image /tmp/r20.bin
    $ cmp firmware_stock/rev20_firmware_code.bin /tmp/r20.bin && echo same

    0519fc81b2a4a393b823bf5dcf642f3a361915565c3e501e7f1ecc8feb992679  rev20 stock
    0519fc81b2a4a393b823bf5dcf642f3a361915565c3e501e7f1ecc8feb992679  rev20 built
    1c5aea39fea93eef33fe2b7ebb83d9a8f181bb5602c565941801a77583e62c06  rev22 stock
    1c5aea39fea93eef33fe2b7ebb83d9a8f181bb5602c565941801a77583e62c06  rev22 built

That replaces the coverage percentage as the headline claim. A percentage is an
accounting statement about a denominator someone chose; a matching SHA-256 over
the whole 8174-byte part is not.

## Code was not the whole ROM

Reaching 100% of *instruction* bytes still left 513 bytes per image that existed
only inside the .bin -- pure data, so no amount of decompiling functions would
ever have reached them:

  * the 402-byte USB descriptor block (rev20 0x0596, rev22 0x057D),
  * the 74-byte VECINT dispatch table (rev20 0x0C93, rev22 0x0C7D),
  * the 40-byte Keil ?C_INITSEG initialiser table (rev20 0x0F9C, rev22 0x0FBA).

`tools/gen_data_blocks.py` emits them as candidates, decoded to field level: the
descriptor block walked by bLength/bDescriptorType with string descriptors shown
as text, the vector table labelled with the TI interrupt-source constant each
slot answers to, the initialiser table split into its three-byte records. The
bytes are transcribed by a program so they cannot be mistyped, and the byte
match proves the transcription. Everything the candidates do not place is 0xFF,
which is what an unprogrammed EEPROM byte reads as -- so the fill is the correct
value for "nothing was written here", not a convenience.

Two things surfaced while doing it, both worth keeping:

  * **match51 was silently under-reading long data rows.** sdas prints at most
    seven bytes per listing row and wraps the rest onto continuation lines with
    no address and no mnemonic. The parser ignored those, so a 402-byte block
    read as 362. Only `.db` runs are long enough to hit it -- no instruction is
    -- so no earlier result was affected, but the failure mode was silent.
  * **The BAD EXCLUSION check caught its own obsolescence.** Rev 22's ?C_INITSEG
    tail was excluded from the denominator because Ghidra decodes it as
    AJMP/RR A/NOP. The moment the table became real source, those bytes were
    placed, and the check correctly refused an exclusion covering placed code.
    `DATA_IN_LISTING` is now empty, and its emptiness is the point.

## Closing the last three gaps

Three things stood between 98% and 100%, and none of them was a function nobody
had decompiled:

**Merged-tail prologues (2 bytes, Rev 20).** `0x0DEB` (`MOVX @DPTR,A`) and
`0x0E17` (`INC DPTR`) are one-byte entry points that fall through into the
function after them. They went uncovered because link51 took a candidate's
defined symbols from its `func=` header alone, so a candidate could not emit a
second label without link51 also emitting a stub equate and failing on a
duplicate. The `defines=` header key fixes that: one candidate owns both names.
`acg_set_both_dctl_10` absorbs its prologue as an extra label in its assembly;
`acg_48k_commit` is plain C, so its prologue is a one-byte `__naked` function
with no `RET` placed immediately before it, relying on SDCC emitting functions
in definition order -- the same mechanism as the `setup_get_sample_freq` merged
tail, and, as there, the byte match is what proves the adjacency.

**The DPTR-across-a-call partial (62 bytes, Rev 20).** `std_get_interface` was
a declared 3-byte partial: stock re-reads `SETUP_wIndexL` with a bare
`MOVX A,@DPTR` because DPTR survives a call into a helper that writes IRAM
only, and SDCC reloads it. That is Keil's inter-procedural register analysis,
which SDCC does not have, so no rewrite of the C reached it -- `__naked`,
same-unit compilation and a jump-to-next-instruction rule were all tried and
all made it worse.

A peephole rule closed it, and the rule's *length* is what makes it sound
rather than lucky. The window spans BOTH `mov dptr,#%1` loads and binds `%1` to
the same symbol in each, so it can only fire where DPTR is provably being
reloaded with what it already holds; the instructions in between touch A and
the carry and never DPTR; and the callee is named literally, so "this call
preserves DPTR" is auditable by reading one seven-byte function rather than
being an assumption about calls in general.

`cand/partial/` is now empty. The mechanism stays, because the next stubborn
function may need it, and because a declared shortfall that is checked exactly
is worth having available.

## Rev 22 needed no new peephole rules

The whole Rev 22 corpus -- 109 functions, 3481 bytes -- matched with the rule
set developed for Rev 20 and not one rule added. That is worth stating plainly,
because it was measured rather than assumed: a three-function probe was run
first specifically to find out, and the full batch then confirmed it.

The two images are the same source built by the same compiler, and where they
differ it is nearly always Keil re-drawing the boundaries of factored code
rather than anything in the source changing. Three shapes recur:

  * A helper subroutine in one image is inlined in the other
    (`queue_chip_reg4_val40`, Rev 20 0x0E20, has no Rev 22 counterpart).
  * The boundary of an extracted common tail moves by one instruction, paid for
    by transposing two instructions inside the block so the block length is
    unchanged.
  * A branch reaches its target directly in one image and via an intervening
    jump in the other, same size, same semantics.

The one IRAM change: the EP0 working pointer moved from `0x1B:0x1C` in Rev 20
to `0x1D:0x1E` in Rev 22. `ep0_*_buf_ptr_load` are otherwise byte-identical
(rev20 `0x0B3E` `75 1b fa 75 1c 18 22` against rev22 `0x0B37`
`75 1d fa 75 1e 18 22`). Because `__data __at` defines the symbol in every
translation unit, a shared declaration of that pointer is a link-time
multiple-definition error rather than a silent wrong address -- which is how it
was found. It is therefore declared per candidate, not in `mbox.h`.

## Why per-function matching is not enough

match51 compiles each candidate standalone at address zero. It therefore
cannot know where any other function will land, and deliberately excuses the
address operands of `LCALL`/`LJMP` to external symbols. That is a real hole:
a call to entirely the wrong function still reports MATCH.

link51 closes it. Every candidate is compiled into its own code segment, the
segments are placed at their stock addresses, and the linker resolves every
inter-function reference for real. Functions not yet decompiled are supplied
as absolute equates generated from the Ghidra function table, so a matched
function can call an unmatched one and still link.

Three defect classes only become visible there, and all three were present
when the tool was first run:

* **Wrong call target.** Deliberately breaking one `symbols.map` address
  turns four bytes red across two functions. match51 reports all of them as
  MATCH.
* **Overrun.** A function that compiles longer than its stock counterpart is
  a length difference per-function, but linked it runs into its neighbour.
* **Layout dependence.** `sdld` bumps an area forward rather than honouring a
  base that would overlap, so two candidates claiming the same stock bytes is
  reported as a displaced segment instead of silently comparing the wrong
  bytes against the wrong function.

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

## Declared partials

Some stock encodings depend on a compiler capability SDCC does not have, and no
rewrite of the C reaches them. Rather than fake the number or throw away good C,
those candidates live in `cand/partial/` and declare the shortfall in the header
as `partial=N at=0xOFF`: N bytes SDCC emits that Keil did not, all at one offset.

The check is not a tolerance. match51 cuts the N bytes back out and requires the
result to equal stock exactly, excusing only relative-branch displacements
(which any insertion shifts) and unresolved external call operands. Size,
location, and every other byte are pinned, so a partial cannot drift into
covering an unrelated mistake -- overstating N, understating it, moving the
offset, or changing an unrelated instruction each turn it back into a DIFF.

`cand/` itself must be exact; preflight fails if a `partial=` appears there.

The recurring cause is Keil's inter-procedural register analysis. Keil knew
which registers a callee left alone and kept values live across calls. In
`std_get_interface` DPTR still holds `SETUP_wIndexL` after a call into a helper
that writes only IRAM 0x1B and 0x1C, so stock re-reads with a bare
`MOVX A,@DPTR`; SDCC reloads DPTR first, three bytes.

## Merged tails, and how a short jump proved one

Keil merges common tails, so several things Ghidra lists as functions are
entry points into the middle or end of another function. They have their own
callers, which is why Ghidra names them, but they were never separate
functions in the source. Two are modelled explicitly:

* `dptr_from_ep0_ptr` (0x0B17) is the tail of `dptr_to_ep0_out_buf` (0x0B11).
* `send_3byte_ep0_reply` (0x010D) is the tail of `setup_get_sample_freq`.

Such a candidate carries `entry=1` and is linked as an absolute equate rather
than placed, because its bytes already exist once — inside its container.
`symbols.map` records the address, and link51 checks that the address really
does land inside a placed function.

`send_3byte_ep0_reply` is worth the detail, because getting it wrong cost
three bytes and the fix was a modelling change, not a codegen trick. Written
as a call — which is what it looks like — the tail compiles to a three-byte
`LJMP` through a trampoline, and no arrangement of C, `__naked`, or peephole
rules removes it: SDCC will not emit a short jump to another function, and its
peephole runs per function so it never sees the boundary. Written as what it
actually is, the last three statements of `setup_get_sample_freq`, the whole
142-byte run matches exactly.

The tell was in the encoding all along. All three success paths reach 0x010D
with a **two-byte SJMP**, and a short jump only reaches an adjacent target.
That is not a call to a function that happens to be nearby; it is a jump to
the end of the function you are already in. **Function ordering is part of the
match**, and here the ordering was the evidence.

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
