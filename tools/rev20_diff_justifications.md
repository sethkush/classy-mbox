# Rev-20-vs-mboxfw SFR-write justifications

Every diff surfaced by `tools/diff_vs_rev20.py` must have a row here or
the gate fails. Rev 20 is the only firmware we know for certain boots
on real hardware; every deviation is a place a bug can live.

Format:

    | 0xADDR | PATTERN  | IMMEDIATE | source              | reason |

Categories `diff_vs_rev20.py` surfaces:
- **MBOXFW_ONLY**  we write, Rev 20 doesn't — new SFR touch, needs a why.
- **REV20_ONLY**   Rev 20 writes, we don't — a missing init step? or
                    something we intentionally don't need (audio-path
                    state, streaming EPs before they're activated, etc.).
- **CHANGED**      same addr, different pattern or immediate — highest
                    priority; this is where tonight's brick lived.

## Justifications

| Addr | Pattern | Imm | Source | Reason |
|------|---------|-----|--------|--------|
| 0xff2e | rmw | - | usb.c | STATIC-ANALYSIS FALSE POSITIVE — read-only access to SETPACK_WLEN_L; scanner attributes a later movx to this addr because it doesn't model control flow. Not a real write. |
| 0xff29 | rmw | - | rev20 | STATIC-ANALYSIS NOISE from Rev 20 disasm — SETPACK bReq read attributed as write. Rev 20 does not actually write here. |
| 0xff29 | assign | 0x01 | rev20 | STATIC-ANALYSIS NOISE. |
| 0xff2b | rmw | - | rev20 | STATIC-ANALYSIS NOISE — SETPACK_WVAL_H read-only. |
| 0xff2b | assign | 0x44 | rev20 | STATIC-ANALYSIS NOISE — 0x44 (44100 & 0xFF) is a compared-against constant, not a write. |
| 0xff2b | assign | 0x80 | rev20 | STATIC-ANALYSIS NOISE — 0x80 (48000 & 0xFF) constant. |
| 0xff2b | assign | 0xac | rev20 | STATIC-ANALYSIS NOISE. |
| 0xff2b | assign | 0xbb | rev20 | STATIC-ANALYSIS NOISE. |
| 0xff2c | rmw | - | rev20 | STATIC-ANALYSIS NOISE — SETPACK_WIDX_L read-only. |
| 0xff2c | assign | 0x00 | rev20 | STATIC-ANALYSIS NOISE. |

## Pending review — REAL diffs that need decisions before next flash

These are actual behavioral differences between mboxfw and Rev 20 that
have not been justified. **Every one is a potential brick.** Do not
flash until each has a row above.

| Category | Addr | Pattern | Imm | What this is | Where we stand |
|----------|------|---------|-----|--------------|----------------|
| REV20_ONLY | 0xffe1 | assign 0x0d / 0x06 / and_not 0x3f / or 0xc0 | DMACTL1 — DMA channel 1 control | Rev 20 configures DMA ch 1 (probably CS8427 channel-status output). We don't. Audio might work without it but S/PDIF status bytes won't propagate. |
| REV20_ONLY | 0xffe2 | assign 0x10 | DMACTL2 | Rev 20 configures DMA ch 2 (capture). We touch DMASRC2 but not DMACTL2. Capture might not run. |
| REV20_ONLY | 0xffe8..0xffef | mix | DMA channel 1 buffer regs | Rev 20 sets up DMA ch 1 buffer addr/size. We don't. |
| REV20_ONLY | 0xfff0 | assign 0x03 | DMA ch 2 buffer size (high byte?) | We don't set. |
| REV20_ONLY | 0xfff6 | assign 0x10 / runtime | IEPBBAX2 buffer base for streaming EP2 IN (feedback slot?) | We don't set. |
| CHANGED | 0xff60 | mbox=assign 0x00 / rev=runtime | IEPCNF1 initial value | We aggressively zero; Rev 20 sets a computed value. |
| CHANGED | 0xff61 | mbox=assign 0x60 / rev=assign 0x94 | IEPBBAX1 buffer base for capture EP | Different buffer offset in TAS1020A shared memory. Might collide with other buffers. |

Note: static analysis under-decodes branches and function-boundary
crossings, so any single row here can be a false positive. But every
row without a decision is a place we might repeat tonight's mistakes.
