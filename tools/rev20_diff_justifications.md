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

## Register-naming caveat

TI's `Reg_stc1.h` and Rev 20's empirical usage disagree about several
addresses in the 0xFFE0-0xFFF9 range:

| Addr | TI Reg_stc1.h | Rev-20 empirical (what firmware treats it as) |
|------|---------------|------------------------------------------------|
| 0xFFE0 | CPTCNF1 | DMACTL0 |
| 0xFFE1 | ACGCTL | DMACTL1 |
| 0xFFE2 | ACGDCTL | DMACTL2 |
| 0xFFE5..7 | ACGFRQ2..0 | DMASRC0_L/M/H |
| 0xFFF6 | ACG2DCTL | (part of streaming-tail write) |
| 0xFFF7..9 | ACG2FRQ2..0 | DMASRC2_L/M/H |

Rev 20's behavior is authoritative for what these addresses *do* on
this silicon (it boots + streams audio successfully). mboxfw's regs.h
uses the Rev-20-empirical names for consistency with existing RE work;
the TI names are noted here so future readers know the naming
disagreement exists. Justifications below cite by address, not name.

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
| 0xffe1 | assign | 0x0d | rev20 | **MUST_ADD** — Rev-20-empirical DMACTL1. `fcn.0x0728` mode-1 (44.1k internal) branch @ 0x075F writes 0x0D. Programs DMA channel for playback at 44.1. Cite: rev20_flat.asm 0x075F; `firmware_stock/disasm/rev20_dynamic_reconfig.md` §3 row "mode 1". mboxfw's `streaming_set_rate()` currently only writes DMASRC0/2 triplets and skips DMACTL1 → DMA never armed → no audio. Fix: see [MUST_ADD patch list](#must_add-patches). |
| 0xffe1 | assign | 0x06 | rev20 | **MUST_ADD** — same register, mode-3 (S/PDIF slave) value written at 0x07A0. Different value because ch-1 DMA source is switched to CS8427-recovered clock domain. Cite: rev20_flat.asm 0x07A0; rev20_dynamic_reconfig.md §3 row "mode 3". Included in same patch. |
| 0xffe1 | and_not | 0x3f | rev20 | **MUST_ADD** — read-modify-write inside `fcn.0x0728` common tail preserving upper CONTROL bits before OR-ing 0xC0. Same address, same file. Not blocking on its own but its target bits (arm-request) are written next. Included in same patch. |
| 0xffe1 | or | 0xc0 | rev20 | **MUST_ADD** — common tail (0x07E0-ish) `DMACTL1 \|= 0xC0` arms DMA channels 0+1. Without this the streaming EPs stay in a "configured but not running" state. Cite: rev20_flat.asm 0x07E0; rev20_dynamic_reconfig.md §3 "Common ... tail" bullet 6. Included in same patch. |
| 0xffe2 | assign | 0x10 | rev20 | **MUST_ADD** — Rev-20-empirical DMACTL2. Rev 20 uses this to halt (=0) then arm capture DMA. Value 0x10 shows in the mode-1 configure path per scanner. Cite: rev20_dynamic_reconfig.md §2 "prelude" line `DMACTL2 = 0` (via fcn.0x0E18). Included in same patch. |
| 0xffe8 | assign | 0x02 | rev20 | **MUST_ADD** — TI-defined `DMACTL0` (per Reg_stc1.h:76) — the actual DMA-channel-0 control register that Rev 20 hits in `fcn.0x08CB` boot init at 0x08??. rev20_audio_dispatch.md §3 confirms "three DMA channels are configured (not two)". Cite: rev20_flat.asm boot-init block. Included in patch. |
| 0xffe8 | or | 0x80 | rev20 | **MUST_ADD** — sets high bit of DMACTL0 (probably "auto-reload" or "start") as part of arm sequence in `fcn.0x0728` tail. Included in patch. |
| 0xffe8 | and_not | 0x7f | rev20 | **MUST_ADD** — RMW clearing the low bits of DMACTL0 before the OR — matches audio_dispatch §3 pattern. Included in patch. |
| 0xffe9 | assign | 0x80 | rev20 | **MUST_ADD** — DMATSH0 (transfer size high). rev20_audio_dispatch.md §5 lists this in the required boot writes for channel 0. Cite: rev20_flat.asm boot-init. Included in patch. |
| 0xffea | assign | 0x03 | rev20 | **MUST_ADD** — DMATSL0 (transfer size low). Pair with 0xFFE9 = 0x80 → 24-bit target size 0x800380 or similar rate-derived value. Included in patch. |
| 0xffee | assign | 0x09 | rev20 | **MUST_ADD** — TI-defined DMACTL1 (per Reg_stc1.h:83), different from Rev-20-empirical 0xFFE1. Rev 20 writes 0x09 during boot init. Cite: rev20_flat.asm boot-init (0x08CB chain). Included in patch. |
| 0xffee | or | 0x80 | rev20 | **MUST_ADD** — arm bit for TI-DMACTL1. Included in patch. |
| 0xffee | and_not | 0x7f | rev20 | **MUST_ADD** — RMW mask on TI-DMACTL1. Included in patch. |
| 0xffef | assign | 0x80 | rev20 | **MUST_ADD** — DMATSH1 (channel-1 transfer size high). Included in patch. |
| 0xfff0 | assign | 0x03 | rev20 | **MUST_ADD** — DMATSL1. Rev 20 writes 0x03; pair with 0xFFEF = 0x80 = target size 0x000380. Cite: rev20_flat.asm boot-init. Included in patch. |
| 0xfff6 | assign | 0x10 | rev20 | **MUST_ADD** — Rev 20's `fcn.0x0728` common streaming tail writes 0x10 here. Per TI Reg_stc1.h:70 this is ACG2DCTL (Audio Clock Generator 2 divider control). The pending-review table misnamed it as IEPBBAX2 (which is actually 0xFF59). Rev 20 uses this to seed the second clock generator during stream re-arm. Cite: rev20_dynamic_reconfig.md §3 "Common ... tail" bullet 1 (`XDATA[0xFFF6] = 0x10`). Included in patch. |
| 0xfff6 | runtime | - | rev20 | **MUST_ADD** — same register, second write with computed value. Reflected in the same reconfig site. Included in patch. |
| 0xff60 | assign | 0x00 | usb.c/streaming.c | **FALSE_POSITIVE** for the "changed" diff. mboxfw writes 0 in `usb_init` (streaming dormant) and 0xC5 in `streaming_playback_enable`. Rev 20 writes 0xC5 via a runtime path that the scanner tags as "runtime -". Both firmwares end at IEPCNF1=0xC5 on stream arm. No behavioral difference — same final register value, same trigger. Scanner artifact. |
| 0xff60 | assign | 0xc5 | streaming.c | Matches Rev 20's runtime-computed 0xC5 write at `fcn.0x0728` tail. Same effective value. Cite: rev20_dynamic_reconfig.md §3 tail bullet "IEPCNF1 (0xFF60) = 0xC5". |
| 0xff61 | assign | 0x60 | streaming.c | mboxfw uses buffer layout EP1_IN_BUF_ADDR=0xFB00 → IEPBBAX1 = (0xFB00-0xF800)/8 = 0x60. Rev 20 chose 0x94 (buffer @ 0xFCA0). Different buffer placements in TAS1020A shared memory. **SAFE** — TI's Mmap.h line 24 states explicitly: *"The rest of the external RAM from USB_EP0_ADDR_END is used either by ROM DFU mode or application. It can not be used at the same time. So if the Application is running, the ROM DFU is not running and visa versa."* The 0xFC00-0xFCFF window is app-owned while our firmware runs; boot ROM reclaims it (with UtilResetCPU-style state clear) only when it enters DFU. No inheritance = no conflict. |
| 0xff62 | assign | 0x20 | streaming.c | IEPBSIZ1 = 0x20 → 256-byte buffer size (EP_BSIZE(0x100)). Independent of Rev 20's buffer sizing. Matches EP_AUDIO_BUF_SIZE. SAFE — different buffer layout, our sizing is internally consistent. |
| 0xff63 | assign | 0x00 | streaming.c | IEPDCNTX1 (data-count) reset on stream (dis)arm. Matches Rev 20 pattern (rev20_audio_dispatch.md §1 "clear the four BCTX/BSIZ bytes"). |
| 0xff98 | assign | 0x00 / 0xc5 | streaming.c | OEPCNF2 — playback EP config. Same pattern as IEPCNF1 (0xff60). Rev 20 writes 0xC5, mboxfw does the same on stream arm. SAFE. |
| 0xff99 | assign | 0x80 | streaming.c | OEPBBAX2 = 0x80 → buffer @ 0xFC00. Different placement from Rev 20; consistent with mboxfw's EP2_OUT_BUF_ADDR. Same caveat as 0xff61. |
| 0xff9a | assign | 0x20 | streaming.c | OEPBSIZ2 = 0x20 → 256-byte buffer. Same rationale as 0xff62. |
| 0xff9b | assign | 0x00 | streaming.c | OEPDCNTX2 reset on stream (dis)arm. Matches Rev 20 pattern. |

## MUST_ADD patches

The MUST_ADD items above are all inside the audio-streaming setup path
that mboxfw's `streaming.c` currently under-implements. Rev 20's
`fcn.0x0728` "ApplyAudioMode" chain, decoded byte-for-byte in
`firmware_stock/disasm/rev20_dynamic_reconfig.md` §5, ships a
drop-in-ready C port. Concrete patch (to be applied by whoever owns
task #67 follow-up, NOT by this fork):

1. In `mboxfw/src/hw_init.c`, after CPT config, add TI-DMACTL0/1 boot
   init (0xFFE8..0xFFEA and 0xFFEE..0xFFF0) using the RMW pattern from
   rev20_dynamic_reconfig.md §3 "Common tail":

    ```c
    /* DMA channel 0 boot init — Rev 20 fcn.0x08CB @ boot-block.
     * Reference: rev20_dynamic_reconfig.md §3 "Common streaming tail",
     * rev20_audio_dispatch.md §3 "three DMA channels are configured". */
    XDATA(0xFFE8) = 0x02;              /* TI DMACTL0 base */
    XDATA(0xFFE9) = 0x80;              /* TI DMATSH0 */
    XDATA(0xFFEA) = 0x03;              /* TI DMATSL0 */
    /* DMA channel 1 boot init — same file. */
    XDATA(0xFFEE) = 0x09;              /* TI DMACTL1 base */
    XDATA(0xFFEF) = 0x80;              /* TI DMATSH1 */
    XDATA(0xFFF0) = 0x03;              /* TI DMATSL1 */
    ```

2. In `mboxfw/src/streaming.c`, extend `streaming_set_rate()` to
   match rev20_dynamic_reconfig.md §5 verbatim:

    ```c
    void streaming_set_rate(unsigned long hz)
    {
        /* Prelude — halt capture DMA (Rev 20 fcn.0x0728 @ 0x0738). */
        XDATA(0xFFE2) = 0x00;                       /* Rev-20 DMACTL2 */

        if (hz == 48000UL) {
            /* mode 2 — rev20_flat.asm 0x0771 */
            XDATA(0xFFE5) = 0x6A; XDATA(0xFFE6) = 0x4B; XDATA(0xFFE7) = 0x20;
            XDATA(0xFFF7) = 0x6A; XDATA(0xFFF8) = 0x4B; XDATA(0xFFF9) = 0x20;
        } else {
            /* mode 1 — rev20_flat.asm 0x075F. Note: mode 1 does NOT
             * write DMASRC (relies on power-on defaults per RE). */
            XDATA(0xFFE1) = 0x0D;                   /* Rev-20 DMACTL1 mode 1 */
        }

        /* Common tail — rev20_flat.asm 0x07C5-0x0803. */
        XDATA(0xFFF6) = 0x10;                       /* TI ACG2DCTL */
        IEPBCTX1 = 0; IEPBSIZ1 = 0;
        OEPBCTX2 = 0; OEPBSIZ2 = 0;
        IEPCNF1 = 0xC5;
        OEPCNF2 = 0xC5;
        XDATA(0xFFE1) |= 0xC0;                      /* arm DMA0+1 */

        /* CS8427 analog-clock config — cs8427_write(0x04, 0x41);
         * cs8427_write(0x12, 0x00); — deferred to input_set_source. */
    }
    ```

3. After the patch, re-run `python3 tools/audit_sfr_writes.py --update`
   to re-baseline the manifest so the new writes are captured, then
   `python3 tools/diff_vs_rev20.py` should show all pending-review
   items resolved. `test_wrap_hex_golden.py`, `sim_smoke.sh`,
   `verify_setup_paths.py` all continue to pass (patch is additive,
   doesn't touch USB engine setup or SETUP dispatch paths).

## Out-of-scope observations

- **mboxfw's `regs.h` mislabels 0xFFE0-0xFFE7 and 0xFFF7-0xFFF9** as
  DMACTL0 / DMASRC0/2 when TI's Reg_stc1.h calls them CPTCNF1 / ACGCTL /
  ACGDCTL / ACGFRQn / ACG2FRQn. Rev 20's *behavior* is authoritative
  (it works on real silicon), and mboxfw's writes to these addresses
  match Rev 20 byte-for-byte. Recommend a renaming pass to align with
  TI names in a follow-up commit, with the caveat comment that the
  addresses control DMA behavior on this chip regardless of TI naming.
  Non-blocking for flash.

- `IEPBBAX1 = 0x60` (mboxfw) vs `0x94` (Rev 20) places the capture
  buffer at different XDATA offsets. Both are inside the 0xF800-0xFFFF
  shared window and don't overlap mboxfw's other EP buffers. But it
  hasn't been verified that mboxfw's 0xFC00-0xFCFF EP2-OUT window is
  outside any TAS1020A internal reserved region. Recommended: before
  the full mboxfw flash (not safety-net), dump the TAS1020A datasheet
  packet-memory map or empirically probe by writing/reading each of the
  used buffer regions in the safety-net firmware first.
