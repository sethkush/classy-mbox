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

**RESOLVED 2026-07-28. There was never a disagreement.** The "Rev-20
empirical" column below was not empirical: nobody observed Rev 20 treating
these as DMA registers. The names were invented in early RE, and every
later reader took the table's existence as evidence that two naming schemes
were in play. TI's names are simply correct, and the datasheet confirms
each one:

| Addr | Correct name | Datasheet | Invented name (WRONG, do not use) |
|------|--------------|-----------|-----------------------------------|
| 0xFFE0 | CPTCNF1 | §6.5.4.1 | DMACTL0 |
| 0xFFE1 | ACGCTL | §6.5.3.11 | DMACTL1 |
| 0xFFE2 | ACG1DCTL | §6.5.3.10 | DMACTL2 |
| 0xFFE5..7 | ACG1FRQ2..0 | §6.5.3.1-3 | DMASRC0_L/M/H |
| 0xFFF6 | ACG2DCTL | §6.5.3.9 | — |
| 0xFFF7..9 | ACG2FRQ2..0 | §6.5.3.6-8 | DMASRC2_L/M/H |

The real DMA registers are at 0xFFE8-0xFFF4 (§6.5.2) and are a disjoint
set of addresses. The cost of the fiction was two bugs that took three
hardware flashes to find: `ACGCTL |= 0xC0` was believed to arm the DMA
channels (it does not — DMAEN is bit 7 of 0xFFE8/0xFFEE, which no build
ever wrote), and the mode-2 clock program left ACGCTL's DIVEN bit clear,
so no master clock reached the codec. Both produced zero-length
isochronous packets. Justifications below cite by address AND correct
name.

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
| 0xffe1 | assign | 0x0d | rev20 | RESOLVED 2026-07-28 — mboxfw emits this (`streaming.c` 44.1 branch). CORRECTED: 0xFFE1 is **ACGCTL**, the adaptive clock generator control register (TI Reg_stc1.h; datasheet §6.5.3.11), NOT a DMA register. This row previously called it "Rev-20-empirical DMACTL1" and claimed skipping it meant "DMA never armed → no audio". The register name was invented and the causal claim was wrong. Cite: rev20_flat.asm 0x075F. |
| 0xffe1 | and_not | 0x3f | rev20 | SAFE_OMIT — RMW inside `fcn.0x0728` preserving the upper ACGCTL bits. mboxfw writes ACGCTL with a plain assign in the 44.1 branch and an `\|= 0xC0` in the tail; the intermediate mask only matters on the mode-3 path we do not implement. |
| 0xffe1 | or | 0xc0 | rev20 | RESOLVED 2026-07-28 — mboxfw emits this in the streaming tail. CORRECTED: these are ACGCTL bits 6-7, not a DMA arm. The old text ("arms DMA channels 0+1", "without this the streaming EPs stay configured but not running") was the load-bearing error behind the zero-length-isoc bug: the write was present, so the DMA looked armed, while DMAEN on 0xFFE8/0xFFEE had never been set. Cite: rev20_flat.asm 0x07DE. |
| 0xffe2 | assign | 0x10 | rev20 | RESOLVED — ACGDCTL (TI Reg_stc1.h; datasheet §6.5.3.10), the ACG1 divider control. mboxfw emits it in `streaming_set_rate()`. This row previously called it "Rev-20-empirical DMACTL2" used "to halt (=0) then arm capture DMA", citing a `DMACTL2 = 0` line in rev20_dynamic_reconfig.md §2. Rev 20 never writes 0x00 there; `fcn.0x0E18` writes 0x10 to both ACGDCTL and ACG2DCTL. |
| 0xffe8 | assign | 0x02 | rev20 | RESOLVED — DMACTL0 (datasheet §6.5.2.3). 0x02 = DMAEN 0, EPDIR 0 (OUT), EPNUM 2 → channel 0 serves EP2 OUT, i.e. playback. mboxfw `hw_init.c` writes the same. Cite: rev20_flat.asm 0x09F2. |
| 0xffe8 | or | 0x80 | rev20 | RESOLVED 2026-07-28 — bit 7 is **DMAEN** (datasheet §6.5.2.3), not "probably auto-reload or start". mboxfw now sets it in `streaming_playback_enable()`. Rev 20 sets it at 0x03DF and 0x043B, after enabling OEPCNF2 (0xFF98), and clears it at 0x1013. |
| 0xffe8 | and_not | 0x7f | rev20 | RESOLVED 2026-07-28 — the clear half of the same DMAEN RMW, emitted by `streaming_playback_enable(0)`. Not a "mask of the low bits before the OR": SDCC renders `x &= ~0x80` as `and_not 0x7f`. |
| 0xffe9 | assign | 0x80 | rev20 | RESOLVED — DMATSH0. Bits 7:6 = BPTS = 10b = **3 bytes per time slot**; bits 5:0 = TSL(13:8) = 0 (datasheet §6.5.2.2). mboxfw `hw_init.c` matches. Cite: rev20_flat.asm 0x09E0. |
| 0xffea | assign | 0x03 | rev20 | RESOLVED — DMATSL0 = TSL(7:0) = time slots 0 and 1 (datasheet §6.5.2.1). With BPTS=3 that is 2 channels × 3 B = **6 bytes per audio sample**, which agrees with IEPCNF1 = 0xC5 (BPS field 5 → 6 B/sample) and with the 288 B/frame a stock unit delivers at 48 kHz. The old reading "24-bit target size 0x800380 or similar" was a guess and is withdrawn. Cite: rev20_flat.asm 0x09DA. |
| 0xffee | assign | 0x09 | rev20 | RESOLVED — DMACTL1. 0x09 = DMAEN 0, EPDIR 1 (IN), EPNUM 1 → channel 1 serves EP1 IN, i.e. capture. mboxfw `hw_init.c` matches. Cite: rev20_flat.asm 0x09F8. |
| 0xffee | or | 0x80 | rev20 | RESOLVED 2026-07-28 — DMAEN for the capture channel, now set in `streaming_capture_enable()`. Rev 20 sets it at 0x03CF, immediately after IEPCNF1 (0xFF60) = 0xC5. **This single missing write is why every isochronous IN packet came back zero-length** (usbmon, 2026-07-28: ours 0 B/frame, stock Rev 18 288 B/frame on the same host). |
| 0xffee | and_not | 0x7f | rev20 | RESOLVED 2026-07-28 — DMAEN clear on capture stop, emitted by `streaming_capture_enable(0)`. Rev 20 does it at 0x033F and 0x03F1. |
| 0xffef | assign | 0x80 | rev20 | RESOLVED — DMATSH1, same encoding as DMATSH0 (0xFFE9) above. Cite: rev20_flat.asm 0x09EC. |
| 0xfff0 | assign | 0x03 | rev20 | RESOLVED — DMATSL1, same encoding as DMATSL0 (0xFFEA) above. Cite: rev20_flat.asm 0x09E6. |
| 0xffe5 | assign | 0x61 | rev20 | RESOLVED 2026-07-28 — ACG1FRQ2, top byte of the 24-bit synth word 0x61A80F written by Rev 20's **mode-3** branch. mboxfw emits this as its **48 kHz** word. Mode 3 IS 48 kHz: `setup_get_sample_freq` (Rev 20 @0x008A) reads the mode back from IRAM 0x08 and answers the host `80 BB 00` = 48000 for mode 3 and `44 AC 00` = 44100 for mode 2, so the firmware states the mapping itself. An earlier revision of this row claimed mode 3 produced 96 kHz, measured as DCNTX = 96 samples per USB frame. That measurement was real but the attribution was wrong: the doubling came from CPTRXCNF4 being set to the mode-5 value 0x01 (÷2) instead of the boot value 0x03 (÷4), which halves the receive bit clock. See 0xffd4. Modes 2 and 3 both fall into the shared tail at 0x0E0F, which writes ACGCTL (0xFFE1) = 0x06. Cite: Rev 20 fcn.0x0DEC @ 0x0DEC (entry verified by byte-exact recompilation, `firmware_stock/decomp/cand/acg_48k_commit.c`, 43/43 bytes). |
| 0xffe6 | assign | 0xa8 | rev20 | SAFE_OMIT — ACG1FRQ1, mode-3 (48 kHz) word. See 0xffe5. |
| 0xffe7 | assign | 0x0f | rev20 | SAFE_OMIT — ACG1FRQ0, mode-3 (48 kHz) word. See 0xffe5. |
| 0xfff7 | assign | 0x61 | rev20 | SAFE_OMIT — ACG2FRQ2, mode-3 (48 kHz) word. See 0xffe5. |
| 0xfff8 | assign | 0xa8 | rev20 | SAFE_OMIT — ACG2FRQ1, mode-3 (48 kHz) word. See 0xffe5. |
| 0xfff9 | assign | 0x0f | rev20 | SAFE_OMIT — ACG2FRQ0, mode-3 (48 kHz) word. See 0xffe5. |
| 0xffe1 | assign | 0x06 | streaming.c | JUSTIFIED — ACGCTL, Rev 20 @ 0x0E10 (shared tail entered at 0x0E0F by BOTH mode 2 and mode 3 — an earlier note here claimed mode 2 writes no ACGCTL, which came from a misaligned disassembly and was wrong). 0x06 = DIVEN (bit 2) set, MCLKO1 <- acg_clk after /M, MCLKO2 <- acg2_clk after /M (datasheet 6.5.3.11). The common tail's `\|= 0xC0` then enables both outputs, giving 0xC6. **DIVEN is the fix**: every build before 2026-07-28 left ACGCTL at 0xC0 with DIVEN clear, so the /M divider never ran, the codec was never clocked, no I2S frame reached the C-port, and per datasheet 2.2.7.4.1 the UBM answered every isochronous IN token with a NULL packet. Telemetry read ACGCTL back as exactly 0xC0. |
| 0xfff6 | assign | 0x10 | rev20 | **MUST_ADD** — Rev 20's `fcn.0x0728` common streaming tail writes 0x10 here. Per TI Reg_stc1.h:70 this is ACG2DCTL (Audio Clock Generator 2 divider control). The pending-review table misnamed it as IEPBBAX2 (which is actually 0xFF59). Rev 20 uses this to seed the second clock generator during stream re-arm. Cite: rev20_dynamic_reconfig.md §3 "Common ... tail" bullet 1 (`XDATA[0xFFF6] = 0x10`). Included in patch. |
| 0xfff6 | runtime | - | rev20 | **MUST_ADD** — same register, second write with computed value. Reflected in the same reconfig site. Included in patch. |
| 0xff60 | assign | 0x00 | usb.c/streaming.c | **FALSE_POSITIVE** for the "changed" diff. mboxfw writes 0 in `usb_init` (streaming dormant) and 0xC5 in `streaming_playback_enable`. Rev 20 writes 0xC5 via a runtime path that the scanner tags as "runtime -". Both firmwares end at IEPCNF1=0xC5 on stream arm. No behavioral difference — same final register value, same trigger. Scanner artifact. |
| 0xff60 | assign | 0xc5 | streaming.c | Matches Rev 20's runtime-computed 0xC5 write at `fcn.0x0728` tail. Same effective value. Cite: rev20_dynamic_reconfig.md §3 tail bullet "IEPCNF1 (0xFF60) = 0xC5". |
| 0xff61 | assign | 0x60 | streaming.c | mboxfw uses buffer layout EP1_IN_BUF_ADDR=0xFB00 → IEPBBAX1 = (0xFB00-0xF800)/8 = 0x60. Rev 20 chose 0x94 (buffer @ 0xFCA0). Different buffer placements in TAS1020A shared memory. **SAFE** — TI's Mmap.h line 24 states explicitly: *"The rest of the external RAM from USB_EP0_ADDR_END is used either by ROM DFU mode or application. It can not be used at the same time. So if the Application is running, the ROM DFU is not running and visa versa."* The 0xFC00-0xFCFF window is app-owned while our firmware runs; boot ROM reclaims it (with UtilResetCPU-style state clear) only when it enters DFU. No inheritance = no conflict. |
| 0xff62 | assign | 0x40 | streaming.c | IEPBSIZ1 = 0x40 → 512-byte buffer (EP_BSIZE(0x200)). Fixed 2026-07-23: was 0x20 (256B), smaller than a 48 kHz stereo 24-bit frame (288B) → every 48 kHz packet truncated. Bumped to 0x200 to fit 48 kHz + slack while staying below 0xFF00 SFR boundary. See regs.h EP_AUDIO_BUF_SIZE comment. |
| 0xff63 | assign | 0x00 | streaming.c | IEPDCNTX1 (data-count) reset on stream (dis)arm. Matches Rev 20 pattern (rev20_audio_dispatch.md §1 "clear the four BCTX/BSIZ bytes"). |
| 0xff98 | assign | 0x00 / 0xc5 | streaming.c | OEPCNF2 — playback EP config. Same pattern as IEPCNF1 (0xff60). Rev 20 writes 0xC5, mboxfw does the same on stream arm. SAFE. |
| 0xff99 | assign | 0xa0 | streaming.c | OEPBBAX2 = 0xa0 → buffer @ 0xFD00 (moved from 0xFC00 to make room for EP1 IN's new 512B size — see 0xff62). Non-overlapping with EP1 IN (0xFB00-0xFCFF) and below 0xFF00 SFR boundary. TI Mmap.h line 24 confirms app owns this window during runtime. |
| 0xff9a | assign | 0x40 | streaming.c | OEPBSIZ2 = 0x40 → 512-byte buffer, same rationale as 0xff62. Fixed 2026-07-23 with the buffer-size bug. |
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

## Bulk resolution — 2026-07-23 post-DMA-patch pass

Sweep of the 57 diffs remaining after commit 8b873d7 (DMA arm patch).
Grouped by SFR function.

### ⚠ BLOCKER — must fix before flashing audio-capable mboxfw

| Addr | Category | Verdict | Reason |
|------|----------|---------|--------|
| 0xff62 / 0xff9a | CHANGED (buffer size) | **MUST_ADD (SIZE FIX)** | mboxfw's `EP_AUDIO_BUF_SIZE = 0x100` (256 B) is SMALLER than one 48 kHz stereo 24-bit USB frame (288 B). Rev 20 boot init @ 0x09BD writes `OEPBSIZ2 = 0x50` and immediately reuses `a = 0x50` at 0x09C3 for `IEPBSIZ1` — both audio EPs get **640-byte** buffers. **Patch:** bump `EP_AUDIO_BUF_SIZE` in `mboxfw/include/regs.h` from `0x100` to `0x140` (320 B, first 8-byte multiple over 288) at minimum; match Rev 20's `0x280` (640 B) for headroom. Without this, every 48 kHz playback packet gets truncated → audible clicks / dropouts. |

### EP double-buffer Y-bank writes (Rev 20 uses, mboxfw doesn't)

| Addr | Category | Verdict | Reason |
|------|----------|---------|--------|
| 0xff67 | REV20_ONLY runtime | **SAFE_OMIT** | `IEPDCNTY1` — EP1 IN Y-buffer data count. TAS1020A supports X/Y double-buffering; mboxfw is X-only. Losing Y = potentially more USB jitter at high load, not incorrect audio. Add later if latency requires. |
| 0xff6f | REV20_ONLY runtime | **IMPLEMENTED** | `IEPDCNTY0` — EP0 IN Y-buffer count. Was SAFE_OMIT on "EP0 doesn't need double-buffering for control transfers", which is true and beside the point: stock's write is not configuration, it is a defensive clear of boot-ROM residue, and the boot ROM runs DFU over EP0 immediately before this image starts. `usb_ep0_setup()` now clears it, matching Rev 20 fcn.0x0970 @ 0x0988 / Rev 22 fcn.0x0891 @ 0x08A9. Candidate for the measured ~12% geometric EP0 IN loss — see decomp/FINDING_ep0_y_buffer_residue.md. |
| 0xff9f | REV20_ONLY runtime | **SAFE_OMIT** | `OEPDCNTY2` — EP2 OUT Y-buffer for playback. Same as 0xff67. |
| 0xffaf | REV20_ONLY runtime | **IMPLEMENTED** | `OEPDCNTY0` — EP0 OUT Y-buffer count. Same reasoning as 0xff6f above. Rev 20 fcn.0x0970 @ 0x0984 / Rev 22 fcn.0x0891 @ 0x08A5. |

### EP1 IN (capture) config — same net state, different write patterns

| Addr | Category | Verdict | Reason |
|------|----------|---------|--------|
| 0xff60 | CHANGED_REV runtime | **FALSE_POSITIVE** | `IEPCNF1` — Rev 20 runtime-computes 0xC5, mboxfw assigns 0xC5 in `streaming_set_rate` (task-#71 patch). Same terminal value. |
| 0xff61 | CHANGED_REV assign 0x94 | **SAFE** (already in table) | Different buffer layout, self-consistent. See earlier row. |
| 0xff63 | CHANGED_REV runtime | **FALSE_POSITIVE** | `IEPDCNTX1` — Rev 20 runtime writes 0. mboxfw explicitly assigns 0 in `streaming_set_rate` common tail. Same behavior. |

### EP0 IN (control) config — Rev 20 does per-transaction bit toggles

| Addr | Category | Verdict | Reason |
|------|----------|---------|--------|
| 0xff68 | CHANGED_REV and_not 0xf7 / or 0x08 / or 0x20 / runtime | **SAFE_OMIT** | `IEPCNF0` — Rev 20 toggles NAK / interrupt bits per transaction. mboxfw uses a simpler EP0 dispatch that sets `0x84` once and does `& 0xD7` on stall. Both handle enumeration cleanly. Reference: `rev20_std_requests.md`. |
| 0xff69 | CHANGED_REV runtime | **FALSE_POSITIVE** | `IEPBBAX0` — Rev 20 runtime, mboxfw assigns 0x43 (EP0 IN buffer @ 0xFA18). Same terminal address. |
| 0xff6a | CHANGED_REV runtime | **FALSE_POSITIVE** | `IEPBSIZ0` — Rev 20 runtime, mboxfw assigns 0x01 (8-byte EP0). Same terminal size. |
| 0xff6b | CHANGED_REV and_not 0x7f + assigns 0x01/02/03/80 + rmw | **SAFE_OMIT** | `IEPDCNTX0` — Rev 20 writes byte counts per IN packet (0x01/02/03 = 1/2/3-byte class-request replies) and toggles NAK. mboxfw's `push_reply_chunk` handles this via runtime `IEPBCTX0 = n`. Same functional outcome, different code paths. |

### EP2 OUT (playback) + EP0 OUT config

| Addr | Category | Verdict | Reason |
|------|----------|---------|--------|
| 0xff98 | CHANGED_MBOX assign 0x00 | **FALSE_POSITIVE** | `OEPCNF2` — mboxfw dormant-writes 0 in `usb_init`, sets 0xC5 in stream arm. Rev 20 also ends at 0xC5. |
| 0xff99 | CHANGED_REV assign 0x44 | **SAFE** | `OEPBBAX2` — Rev 20 buffer @ 0xFA20. mboxfw @ 0xFC00. Different layouts, both valid per TI Mmap.h. |
| 0xff9b | CHANGED_REV runtime | **FALSE_POSITIVE** | `OEPDCNTX2` — same reset-on-stream-arm pattern as 0xff63. |
| 0xffa8 | CHANGED_REV and_not 0xf7 + assign 0x84 + runtime | **SAFE_OMIT** | `OEPCNF0` — mirror of 0xff68 (EP0 OUT vs IN). Same rationale. |
| 0xffaa | CHANGED_REV runtime | **FALSE_POSITIVE** | `OEPBSIZ0` — 8-byte EP0, same terminal value. |
| 0xffab | CHANGED_REV runtime | **FALSE_POSITIVE** | `OEPDCNTX0` — mirror of 0xff6b. |

### Global control registers

| Addr | Category | Verdict | Reason |
|------|----------|---------|--------|
| 0xffb0 | CHANGED_REV runtime | **FALSE_POSITIVE** | `MEMCFG` — Rev 20 runtime writes SDW-related; mboxfw `\|= 0x01`. Same terminal SDW state. |
| 0xffb1 | CHANGED_REV and_not 0xfe + assign 0x06 | **IMPLEMENTED** (was wrongly SAFE_OMIT) | `GLOBCTL` — the `and_not 0xfe` half stands: that is the clear-then-set bit-0 dance in Rev 20's mode-switch paths, and mboxfw's single `\|= 0x01` is sufficient. The `assign 0x06` half was **RETRACTED 2026-07-29**. It was called a scanner artifact because no `mov a,#0x06; movx @dptr,a` to 0xffb1 exists — true, and the wrong conclusion. DPTR is never loaded with 0xFFB1 there; it arrives by `INC DPTR` from the MEMCFG write at 0xFFB0, 27 instructions earlier (Rev 20 0x08D4 → 0x08FB, Rev 22 0x07F5 → 0x081C). Byte-scanned: `a3 74 06 f0` occurs exactly once per image. `rev20_STARTUP_TRACE.md` step 14 recorded it correctly all along, so two docs contradicted each other. Also note the dismissal cited rev20_flat.asm, the known-bad disassembly. Now implemented as `GLOBCTL \|= 0x02` in hw_init.c (RMW per #48 reaches the same 0x06 from the ROM's 0x04). **GLOBCTL bit 1's function remains UNKNOWN.** |
| 0xffb2 | CHANGED_REV rmw + runtime | **FALSE_POSITIVE** | `VECINT` — both firmwares write 0 to ack after service. Scanner tags one side as assign and the other as runtime. |

### I²C peripheral (EEPROM driver)

| Addr | Category | Verdict | Reason |
|------|----------|---------|--------|
| 0xffc0 | CHANGED_MBOX and_not 0x54 / CHANGED_REV and_not 0xfc | **SAFE_MBOXFW_ONLY** | `I2CSTA` — mboxfw's `& I2C_CLEAR_ALL (0x54)` is the byte-for-byte port from TI `I2c.h::CLEAR_ALL`. Rev 20's `& 0xFC` clears only STOP bits. Our mask is a superset — matches TI reference. |
| 0xffc1 | CHANGED_MBOX assign 0xff / CHANGED_REV assign 0x00 | **FALSE_POSITIVE (dummy byte)** | `I2CDATO` — dummy byte to fire an I²C read. TI reference and mboxfw use 0xFF (TI `I2c.c:102`). Rev 20's 0x00 assigns are from other code paths (byte-clear before real data write). |
| 0xffc3 | CHANGED_MBOX assign 0xa1 / CHANGED_REV runtime | **FALSE_POSITIVE** | `I2CADR` — 0xA1 = EEPROM read address. Rev 20 runtime-computes; mboxfw hardcodes. Same target device. |

### C-port

| Addr | Category | Verdict | Reason |
|------|----------|---------|--------|
| 0xffd4 | CHANGED_REV assign 0x01 / CHANGED_MBOX assign 0x03 | **SAFE_OMIT** | `CPTRXCNF4` (TI Reg_stc1.h; datasheet §6.5.4.13), bits 2:0 = DIVB2, the MCLKO2→SCLK2 divider on the I²S **receive** clock. NOT `CPTCTL` — that is 0xFFDC. An earlier version of this row offered "Rev-20-empirical usage" as authority for the CPTCTL name; there was no such authority, and it is the same invented-name error as the old DMACTL1/ACGCTL confusion. Boot value is 0x03 (÷4) and mboxfw matches it; the `assign 0x01` (÷2) is Rev 20's mode-5 branch, which we do not implement. |
| 0xffd5 | CHANGED_REV runtime | **FALSE_POSITIVE** | `CPTBRRX` — Rev 20 runtime writes 0xAC; mboxfw assigns 0xAC in hw_init. Same terminal value. |

### DMA registers (Rev-20-empirical block)

| Addr | Category | Verdict | Reason |
|------|----------|---------|--------|
| 0xffe2 | CHANGED_MBOX assign 0x00 | **STALE** | Retired 2026-07-28. 0xFFE2 is `ACGDCTL`/`ACG1DCTL` (TI Reg_stc1.h; datasheet §6.5.3.10), not a DMA register — "DMACTL2" was an invented name. Rev 20 never writes 0x00 there; `fcn.0x0E18` writes 0x10 to both ACGDCTL and ACG2DCTL. mboxfw no longer emits this write at all: `streaming.c` now sets `ACGDCTL = 0x10`. Row kept for history. |
| 0xfff9 | CHANGED_MBOX assign 0x20 | **FALSE_POSITIVE** | `ACG2FRQ0` (TI) / `DMASRC2_H` (Rev-20-empirical). 0x20 matches Rev 20's mode-2 (48 kHz) branch @ 0x077B. Scanner classification asymmetry. |

### USB engine control

| Addr | Category | Verdict | Reason |
|------|----------|---------|--------|
| 0xfffc | CHANGED_REV and_not 0x7f / or 0x80 / or 0xc0 | **SAFE_OMIT** | `USBCTL` — Rev 20 does aggressive USBCTL manipulation during runtime mode switches. mboxfw does `\|= 0x80` (CONN) at boot only. Rev 20's runtime dance is for a re-enumeration pattern we don't perform. |
| 0xfffd | CHANGED_REV assign 0x00/0x9f/0xff / CHANGED_MBOX or 0xe5 | **SAFE_OMIT** | `USBIMSK` — Rev 20 dynamically enables/disables interrupt sources per mode. mboxfw uses polling + wake-event mask (0xE5 per TI `engUsbInit`) once. Same wake coverage. |
| 0xffff | CHANGED_REV assign 0x00 | **FALSE_POSITIVE** | `USBFADR` — Rev 20 clears to 0 on bus reset (its VEC_RSTR handler). mboxfw does the same in `usb_init` / VEC_RSTR path. |

## Bulk-resolution summary

- **1 blocker**: `EP_AUDIO_BUF_SIZE` too small (256 B < 288 B min for 48 kHz stereo 24-bit). Fix in `mboxfw/include/regs.h` before any audio-active flash.
- **4 SAFE_OMIT** (double-buffer Y bank writes) — potential latency improvement for v2, not required.
- **7 SAFE_OMIT** (Rev 20 does finer-grained per-transaction / per-mode toggles that mboxfw replaces with simpler static setup + polling) — no correctness impact.
- **1 SAFE_MBOXFW_ONLY** (I²C CLEAR_ALL mask — mboxfw stricter, matches TI reference).
- **Rest FALSE_POSITIVE or already JUSTIFIED** — scanner artifacts or same-terminal-state via different code paths.

Zero additional silent-brick risks beyond the buffer-size blocker.

## Bulk resolution rows (one per (addr,pattern,imm) tuple)

Individual rows for the parser. Reasoning summarized in the grouped
sections above.

| Addr | Pattern | Imm | Source | Reason |
|------|---------|-----|--------|--------|
| 0xff62 | assign | 0x20 | streaming.c | ⚠ BLOCKER — EP_AUDIO_BUF_SIZE=0x100 (→BSIZ=0x20=256B) is smaller than one 48kHz stereo 24-bit USB frame (288B). Rev 20 uses OEPBSIZ2 (0xFF9A)=IEPBSIZ1=0x50 (640B). Bump to ≥0x28 (320B). See "BLOCKER" note above. |
| 0xff62 | runtime | - | streaming.c | dormant/reset write, matches Rev 20 stream (dis)arm pattern. |
| 0xff67 | runtime | - | rev20 | SAFE_OMIT — IEPDCNTY1 (EP1 IN Y-buffer). mboxfw is X-buffer-only. |
| 0xff6f | runtime | - | rev20 | IMPLEMENTED — IEPDCNTY0 cleared in usb_ep0_setup(); see the row above. |
| 0xff9a | assign | 0x20 | streaming.c | ⚠ BLOCKER — see 0xff62. Same buffer-size undersize applies to OEPBSIZ2 (playback). |
| 0xff9a | runtime | - | streaming.c | dormant/reset write, matches Rev 20 pattern. |
| 0xff9f | runtime | - | rev20 | SAFE_OMIT — OEPDCNTY2 (EP2 OUT Y-buffer). mboxfw is X-only. |
| 0xffaf | runtime | - | rev20 | IMPLEMENTED — OEPDCNTY0 cleared in usb_ep0_setup(); see the row above. |
| 0xff60 | runtime | - | rev20 | FALSE_POSITIVE — IEPCNF1, Rev 20 runtime-computes 0xC5, mboxfw assigns 0xC5. Same terminal. |
| 0xff61 | assign | 0x94 | rev20 | SAFE — different EP1 buffer layout (Rev 20 @0xFCA0, mboxfw @0xFB00), self-consistent per TI Mmap.h. |
| 0xff63 | runtime | - | rev20 | FALSE_POSITIVE — IEPDCNTX1, both zero on stream (dis)arm. |
| 0xff68 | and_not | 0xf7 | rev20 | SAFE_OMIT — IEPCNF0 fine-grained bit toggle in Rev 20 per-transaction dispatch. mboxfw uses simpler once-set + stall pattern. |
| 0xff68 | assign | 0x84 | mboxfw | JUSTIFIED — matches TI engUsbInit IEPCNF0=0x84 boot value (Rev 20 arrives at same via runtime OR sequence). |
| 0xff68 | or | 0x08 | rev20 | SAFE_OMIT — same as 0xff68/and_not/0xf7. |
| 0xff68 | or | 0x20 | rev20 | SAFE_OMIT — same. |
| 0xff68 | runtime | - | rev20 | FALSE_POSITIVE — companion runtime write to the bit-toggle sequence. |
| 0xff69 | assign | 0x43 | mboxfw | JUSTIFIED — EP0 IN buffer @ 0xFA18 = (0x43<<3)+0xF800. Rev 20 runtime-computes same address via TI engUsbInit macro. |
| 0xff69 | runtime | - | rev20 | FALSE_POSITIVE — Rev 20 side of the same buffer-addr write. |
| 0xff6a | assign | 0x01 | mboxfw | JUSTIFIED — EP0 max-packet 8 bytes → BSIZ = 0x01. Matches TI engUsbInit. |
| 0xff6a | runtime | - | rev20 | FALSE_POSITIVE — Rev 20 side of same. |
| 0xff6b | and_not | 0x7f | rev20 | SAFE_OMIT — Rev 20 clears NAK bit per packet. mboxfw sets NAK once (0x80) and lets HW manage. |
| 0xff6b | assign | 0x01 | rev20 | SAFE_OMIT — Rev 20 sets IN packet count 1 byte for a class-request reply. mboxfw uses runtime IEPBCTX0 = n via push_reply_chunk. |
| 0xff6b | assign | 0x02 | rev20 | SAFE_OMIT — same, 2-byte reply. |
| 0xff6b | assign | 0x03 | rev20 | SAFE_OMIT — same, 3-byte reply. |
| 0xff6b | assign | 0x80 | rev20 | SAFE_OMIT — Rev 20 NAK-bit set per packet. |
| 0xff6b | rmw | - | rev20 | FALSE_POSITIVE — companion RMW to the NAK toggles. |
| 0xff98 | assign | 0x00 | mboxfw | JUSTIFIED — OEPCNF2 dormant at usb_init (streaming disabled). Set to 0xC5 in stream arm, matches Rev 20 terminal state. |
| 0xff99 | assign | 0x44 | rev20 | SAFE — different EP2 OUT buffer layout (Rev 20 @0xFA20, mboxfw @0xFC00). |
| 0xff9b | runtime | - | rev20 | FALSE_POSITIVE — OEPDCNTX2 reset on stream (dis)arm, both firmwares do it. |
| 0xffa8 | and_not | 0xf7 | rev20 | SAFE_OMIT — mirror of 0xff68 for OEPCNF0. |
| 0xffa8 | assign | 0x84 | rev20 | FALSE_POSITIVE — Rev 20 arrives at OEPCNF0=0x84 via runtime OR; mboxfw assigns directly. Same terminal. |
| 0xffa8 | or | 0x08 | mboxfw | JUSTIFIED — `OEPCNF0` STALL (bit 3) set on EP0 OUT, the mirror of the 0xff68 row above. Rev 20 sets the same bit on the same condition; mboxfw reaches it through `reply_stall()` rather than an inline per-transaction dispatch. Same terminal state. |
| 0xffa8 | or | 0x20 | mboxfw | JUSTIFIED — `OEPCNF0` TOGGLE (bit 5), data-toggle management for EP0 OUT. Mirror of the 0xff68 row. Required for multi-packet OUT continuations to stay in sequence. |
| 0xffa8 | runtime | - | mboxfw | FALSE_POSITIVE — mboxfw runtime path (`& 0xD7` in reply_stall). Rev 20 does similar bit-clear. |
| 0xffaa | assign | 0x01 | mboxfw | JUSTIFIED — mirror of 0xff6a, EP0 OUT 8-byte size. |
| 0xffaa | runtime | - | rev20 | FALSE_POSITIVE — Rev 20 side. |
| 0xffab | runtime | - | rev20 | FALSE_POSITIVE — mirror of 0xff6b, OEPDCNTX0. |
| 0xffb0 | or | 0x01 | mboxfw | JUSTIFIED — MEMCFG SDW idempotent set (boot ROM already set it). Cite: TI Utils.c UtilResetCPU. |
| 0xffb0 | runtime | - | rev20 | FALSE_POSITIVE — Rev 20 arrives at same SDW state via runtime write. |
| 0xffb1 | and_not | 0xfe | rev20 | SAFE_OMIT — GLOBCTL clear-bit-0 dance in Rev 20 mode-switch paths. mboxfw only sets (|= 0x01) — sufficient since USB-enable bit stays high. |
| 0xffb1 | assign | 0x06 | rev20 | **RETRACTED 2026-07-29 — REAL, now implemented.** Reached via `INC DPTR` from 0xFFB0, so the direct form correctly did not exist; the inference from its absence was wrong. Both images do it (Rev 20 0x08FB, Rev 22 0x081C). See the 0xffb1 row above. |
| 0xffb1 | runtime | - | mboxfw | GLOBCTL `|= 0x02` in hw_init, added 2026-07-29. Emitted as a runtime RMW (SDCC routes the mask through ar7 when it fuses this with the adjacent MEMCFG (0xFFB0) RMW), which is why the pattern reads `runtime` rather than an immediate. Mirrors stock's boot `GLOBCTL = 0x06` (Rev 20 0x08FE, Rev 22 0x081F); RMW per #48 reaches the same value from the boot ROM's 0x04. See the retraction above. |
| 0xffb2 | rmw | - | rev20 | FALSE_POSITIVE — VECINT ack pattern (read+write 0). mboxfw does the same. |
| 0xffb2 | runtime | - | rev20 | FALSE_POSITIVE — same. |
| 0xffc0 | and_not | 0x54 | mboxfw | JUSTIFIED — I²C CLEAR_ALL mask per TI I2c.h. Stricter than Rev 20's 0xFC. Superset behavior. |
| 0xffc0 | and_not | 0xfc | rev20 | SAFE — Rev 20 clears only STOP bits; mboxfw's stricter TI-referenced 0x54 mask is a superset. |
| 0xffc1 | assign | 0x00 | rev20 | FALSE_POSITIVE — Rev 20 writes 0 in code paths that aren't the dummy-read trigger. |
| 0xffc1 | assign | 0xff | mboxfw | JUSTIFIED — dummy trigger byte for I²C read per TI I2c.c:102. |
| 0xffc3 | assign | 0xa1 | mboxfw | JUSTIFIED — EEPROM 7-bit address 0x50 shifted + R/W=1 read. Rev 20 runtime-computes same value. |
| 0xffc3 | runtime | - | rev20 | FALSE_POSITIVE — Rev 20 side of same. |
| 0xffd4 | assign | 0x03 | mboxfw | JUSTIFIED — CPTRXCNF4 DIVB2 = ÷4, the boot value. Cite: Rev 20 fcn.0x08CB @ 0x0929, Rev 22 @ 0x084A. Verified by byte-exact recompilation of hw_master_init (`firmware_stock/decomp/cand/hw_master_init.c`, 165/165 bytes). |
| 0xffd5 | runtime | - | rev20 | FALSE_POSITIVE — CPTBRRX 0xAC, both firmwares write same terminal value. |
| 0xffe2 | assign | 0x00 | mboxfw | STALE — retired 2026-07-28; see the ACGDCTL row in the C-port section. The write does not exist in mboxfw any more and the "DMACTL2" name was invented. |
| 0xfff9 | assign | 0x20 | mboxfw | FALSE_POSITIVE — DMASRC2_H mode-2 value 0x20 matches Rev 20 fcn.0x0728 @ 0x077B. Scanner classification asymmetry. |
| 0xfffc | and_not | 0x7f | rev20 | SAFE_OMIT — USBCTL bit-manipulation in Rev 20 mode-switch (bus-reset simulation on rate change). mboxfw doesn't do runtime re-enum. |
| 0xfffc | or | 0x80 | rev20 | FALSE_POSITIVE — Rev 20 runtime USBCTL |= CONN. mboxfw does same at end of usb_init. |
| 0xfffc | or | 0xc0 | rev20 | SAFE_OMIT — Rev 20 sets CONN+FEN together in a runtime path. mboxfw sets only CONN (Rev 20 also does |= 0x80 boot-time via 0x0ADE-0x0AE4). |
| 0xfffc | assign | 0x00 | safety_net | JUSTIFIED — intentional pre-init USB disconnect at top of `main()`. Assignment (not RMW) is the only way to guarantee a clean 0-state before re-configuring. POLICY §2 carve-out A applies. Rev 20 does the same at rev20_flat.asm 0x08E5 (`clr a; mov dptr,#0xfffc; movx @dptr,a` inside master-init sub 0x08CB). safety_net does it one instruction earlier — semantically equivalent. |
| 0xfffc | or | 0x01 | mboxfw+safety_net | JUSTIFIED — USBCTL SDW-confirm bit set inside RESET_TO_BOOT_ROM macro (regs.h). Byte-for-byte match to TI Utils.SRC UtilResetBootCPU (lines 119-160). Bracketed by the `and_not 0xFE` clear below. Fires only in DFU-trigger recovery path. |
| 0xfffc | and_not | 0xFE | mboxfw+safety_net | JUSTIFIED — USBCTL SDW-confirm bit clear inside RESET_TO_BOOT_ROM macro, second half of the TI Utils.SRC UtilResetBootCPU handshake. Fires only in DFU-trigger recovery path. |
| 0xfffc | runtime | - | rev20 | SAFE_OMIT — Rev 20's USBCTL RMW pattern the scanner couldn't classify (a write whose operator falls outside or/and_not/assign, e.g. `xrl`-style toggle emitted by a compiler intrinsic, or a write via a helper subroutine the scanner doesn't fully trace). Every observed USBCTL write in Rev 20 is a mode-switch or attach — mboxfw does not perform runtime re-enumeration and does not need it. |
| 0xffa1 | any | any | safety_net | SPURIOUS — safety_net/src/main.c had `#define GLOBCTL XDATA(0xFFA1)` at one point; correct address is 0xFFB1 per Reg_stc1.h and Rev 20 0x100F. Any write to 0xFFA1 is unintended; if this row triggers, fix the #define. |
| 0xfffd | assign | 0x00 | rev20 | SAFE_OMIT — USBIMSK disable-all path (Rev 20 uses during mode switches). |
| 0xfffd | or | 0xf5 | usb.c | JUSTIFIED — mboxfw's USBIMSK. 0xF5 = RSTR|SUSR|RESR|SOF|SETUP|STPOW (datasheet §6.5.1.3: 7=RSTR 6=SUSR 5=RESR 4=SOF 3=PSOF 2=SETUP 1=rsvd 0=STPOW). Changed from 0xE5 on 2026-07-28: 0xE5 is the same set with SOF (bit 4) masked, and telemetry block 5 measured tlm_sof_count == 0 on hardware for exactly that reason — no frame clock. Rev 20's 0x9F also enables SOF, so this moves us TOWARD Rev 20, not away. Remaining deltas vs Rev 20: we add SUSR/RESR (bits 6/5) for suspend/resume, which Rev 20 omits, and still mask PSOF (bit 3). Masking SOF was neutral for enumeration, which is why it survived until streaming was attempted. |
| 0xfffd | assign | 0x9f | rev20 | SAFE_OMIT — Rev 20 uses USBIMSK = 0x9F (rev20_flat.asm 0x09FE-0x0A03 and VEC_RSTR 0x0F7E-0x0F80). mboxfw uses 0xF5. Bits per datasheet §6.5.1.3: 7=RSTR 6=SUSR 5=RESR 4=SOF 3=PSOF 2=SETUP 1=rsvd 0=STPOW. CORRECTED 2026-07-28: this row previously said changing ours to 0x9F "would silence our SETUP handler" because STPOW was bit 5 — both claims were wrong (STPOW is bit 0, SETUP is bit 2, and 0x9F sets both). Commit 189c219 fixed the identical error in safety_net's comments and never propagated it here. mboxfw was 0xE5 until 2026-07-28, which masked SOF off and made tlm_sof_count read 0 on hardware; now 0xF5 = 0xE5 + SOF. Remaining divergence from Rev 20 is PSOF (bit 3, still masked) and SUSR/RESR (bits 6/5, which Rev 20 omits). |
| 0xfffd | assign | 0xff | rev20 | SAFE_OMIT — USBIMSK enable-all. Superset of mboxfw's 0xE5. |
| 0xfffd | or | 0xe5 | mboxfw | JUSTIFIED — TI engUsbInit UsbEng.c line 647 uses exactly 0xE5. |
| 0xfffd | runtime | - | power.c | JUSTIFIED — the resume path's `USBIMSK |= 0xF5` in do_suspend(), which SDCC emits through the accumulator (hence `runtime`, not `or 0xf5`). DELIBERATE DIVERGENCE from Rev 20 fcn.0x0526 @ 0x054E (Rev 22 @ 0x054D), which ASSIGNS 0x9F here. 0x9F omits SUSR (bit 6) and RESR (bit 5), so stock masks the suspend source off after handling one suspend and never suspends again for the rest of that attach. Copying that would make our suspend a one-shot. We restore the same 0xF5 usb_init() sets, and OR rather than assign per task #48 (USBIMSK is boot-ROM-owned). |
| 0xffff | assign | 0x00 | rev20 | FALSE_POSITIVE — USBFADR clear on bus reset. mboxfw does same in VEC_RSTR / usb_init. |
| 0xff9a | assign | 0x50 | rev20 | ⚠ BLOCKER-REV20-SIDE — Rev 20 boot init @ 0x09BD writes OEPBSIZ2=0x50 (640B buffer). mboxfw's 0x20 (256B) is undersize for 48kHz. See BLOCKER note. |
| 0xffd4 | assign | 0x01 | rev20 | SAFE_OMIT — CPTRXCNF4 DIVB2 = ÷2, written by Rev 20's mode-5 branch @0x07A0 (Rev 22 @0x077E). mboxfw does not implement mode 5 (I2S "1 OUT and 1 IN at different frequencies"). hw_init mirrors stock's BOOT init, which writes 0x03 (÷4) at Rev 20 @0x0929 / Rev 22 @0x084A. Taking the mode-5 value here between 2026-07-26 and 2026-07-28 doubled the capture sample rate — measured DCNTX = 96 samples/USB-frame against stock's 48. |
| 0xffd5 | assign | 0xa8 | hw_init.c | JUSTIFIED — CPTRXCNF3 with BYOR (bit 2) CLEARED. BYOR is the DMA byte-order reversal for the capture path: "when this bit is set to a 1, the byte order of each audio sample is reversed when the data is moved to/from the USB endpoint buffer" (datasheet §6.5.4.12; §6.5.4.3 gives CPTCNF3 (0xFFDE) the identical layout). Stock sets it and Linux documents stock as SNDRV_PCM_FMTBIT_S24_3BE (reference/mbox1_quirks-table.h.snippet), so BYOR=1 means big-endian on the wire and BYOR=0 means little-endian. mboxfw declares S24_3LE — the spec-compliant choice, and the entire point of the project — so it wants BYOR clear here. This is a DELIBERATE divergence from stock, not a missing init: matching stock byte-for-byte would produce a device whose wire format contradicts its own descriptors. Copying stock's 0xAC is why the first successful capture looked like full-scale noise; `00 00 80` reads as -8388608 little-endian and 128 big-endian. |
| 0xffd5 | assign | 0xac | rev20 | SAFE_OMIT — stock's CPTRXCNF3 with BYOR SET, i.e. big-endian capture (Rev 20 fcn.0x08CB @0x0923, Rev 22 @0x0844). Deliberately not copied; see the 0xffd5/0xa8 row above. Do NOT "fix" this back to match stock — S24_3BE is not expressible in standard UAC1 descriptors, which is precisely why snd-usb-audio needs a hardcoded composite quirk for 0dba:1000 and why a class-compliant Mbox has to be little-endian. |
