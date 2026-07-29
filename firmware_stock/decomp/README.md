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

## Known-stubborn constructs

| Construct | Why | Plan |
|---|---|---|
| `toggle_bit1e` @0x1028 | SDCC folds test-then-clear into `JBC`, then into `CPL`; Keil emitted naive `JNB`+`CLR`. Not expressible as a peephole (patterns match instructions, not label definitions). | hand-written `__naked` asm |
| Register parameters | Keil passes the first `char` in R7, SDCC in DPL. Affects a handful of functions. | `__naked` prologue |
| Shared tails with live A/DPTR | Keil merges common tails; callers jump in with registers live. ~5 per image. | merge into one source function, or `__naked` |
| Keil library routines | `?C?CASE` (38 B), `udiv16` (85 B), C51 startup + initialiser interpreter. No SDCC equivalent. | hand-written asm; all four are fully decoded |
