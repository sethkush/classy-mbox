# #220 — the diagnostic tier does not fit, and the host tool read its absence as a fault

2026-08-16. Two problems found by flashing build 0x0058 to unit A. Neither is a
device defect; both had been silently true for several builds.

## 1. The diagnostic tier has no useful configuration that fits

#219 retired the #211 knob and freed 56 bytes, and the commit message said "the
diagnostic tier fits again: 6011 against the 6016 limit". That is true and
misleading. It fits **with every diagnostic block switched off**, and without a
serial descriptor. Neither is the build a bench unit needs.

Authoritative sizes from the linker, `--code-size 0x1780` (6016):

| build | bytes | vs limit |
|---|---|---|
| diagnostic, no flags | 6011 | fits, **5 spare** |
| + serial descriptor (`MBOX_UNIT=A`) | 6044 | 28 over |
| + `MBOX_TLM_STALL` | 6036 | 20 over |
| + `MBOX_TLM_ROUTING` (block 9) | 6078 | 62 over |
| + `MBOX_TLM_FULL` (blocks 1, 2) | 6150 | 134 over |
| + `MBOX_TLM_ROUTING` + `MBOX_TLM_STALL` | 6164 | 148 over |

So the image flashed to unit A has exactly **one** live block, block 0. Blocks 1
and 2 need `MBOX_TLM_FULL`; block 9 needs `MBOX_TLM_ROUTING`; block 4 needs both
`MBOX_TLM_ROUTING` and `MBOX_TLM_STALL`. All were off.

The consequential one is **block 9**. `mboxtlm.py`'s own docstring says to read
it alongside every audio measurement, because a capture whose input routing is
unstated cannot be trusted -- a full session was voided on 2026-07-29 for
exactly that. Putting it on a serialised unit costs about **95 bytes** (62 for
the block, 33 for the serial), not the 28 the #219 commit implied.

## 2. The measurement that produced those numbers was wrong the first time

The first sweep of build flags reported `MBOX_TLM_ROUTING=1 MBOX_TLM_STALL=1` at
**6011 bytes -- byte-identical to the no-flag build**, while each flag *alone*
came out over budget. Adding flags cannot shrink an image, and that impossibility
is the only reason it was caught.

The cause was the harness, not the firmware: the loop passed flags as
`make MBOX_PID=0x2000 $f` with `f="MBOX_TLM_ROUTING=1 MBOX_TLM_STALL=1"`, **and
zsh does not word-split unquoted parameter expansions**. make received one
argument containing a space, defined neither flag, and built the no-flag image,
which of course fit. The `if make ...; then` guard passed because that build
genuinely succeeded.

Had the numbers been merely plausible rather than impossible, this would have
been reported as "both diagnostic blocks fit for free" and a replug spent on it.
**A build-flag sweep must assert monotonicity** -- more flags never shrink the
image -- because the shell can silently drop flags and the compiler cannot tell
you it never saw them.

## 3. `mboxtlm.py` decoded an uncompiled block as a device in a bad state

Reading block 9 from the flashed unit printed, with full confidence:

```
  mux word  =0xFF   ch1=ILLEGAL(0x7)  ch2=ILLEGAL(0x7)
  host mux sets accepted=255  rejected=255
  selector  =S/PDIF   clock=?(0xFF)
    MISMATCH: routed to S/PDIF but clocked internally...
    ILLEGAL PATTERN: not one of mic/line/inst. No source is selected, so any
    audio measurement taken now is void...
    255 host mux request(s) were REJECTED as illegal patterns...
```

**Every line of that is false.** The unit is healthy. `tlm_read_block()` returns
an all-0xFF sentinel for any index not compiled in, and the decoder ran on the
sentinel, turning each field into its most alarming legal value simultaneously.

The tool already labelled the RETIRED indices -- "the all-0xFF here is the
sentinel, not a failed read". Block 9 was missed because it is absent by **build
flag** rather than by retirement, and only retirement had been considered.

This is this project's own rule pointed at its instrument: *a null from an
instrument that was never connected looks exactly like a null from a refuted
hypothesis*. Here it looked like a fault, and the give-away -- three independent
alarms firing at once, on a unit that had just enumerated cleanly -- is the same
give-away as always.

**FIXED**: `block9()` now detects the sentinel and says the block is not
compiled in. Deliberately it does **not** say "fine": it says the routing is
UNKNOWN, which for a capture measurement is a different thing, and points at the
front panel or a `MBOX_TLM_ROUTING=1` build.

## What is NOT concluded

Not that build 0x0058 is bad. It runs: build id 0x0058, `stage 20`, all six
phases, `bus resets 3` from a genuine cold boot, and unit B alongside it on
0x0057. The #219 work stands and nothing on the wire changed.

What is concluded is that **the diagnostic tier has been unusable on a bench
unit for longer than anyone noticed**, that the 6016-byte ceiling is now the
binding constraint on instrumentation rather than on features, and that deciding
what to cut for those ~95 bytes is a real design question and not a tidy-up.
