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
| 0xffe1 | assign | 0x0d | rev20 | RESOLVED 2026-07-28 — mboxfw emits this (`streaming.c` 44.1 branch). CORRECTED: 0xFFE1 is **ACGCTL**, the adaptive clock generator control register (TI Reg_stc1.h; datasheet §6.5.3.11), NOT a DMA register. This row previously called it "Rev-20-empirical DMACTL1" and claimed skipping it meant "DMA never armed → no audio". The register name was invented and the causal claim was wrong. Cite: Rev 20 fcn.0x0728 @ 0x074D, Rev 22 @ 0x0734 (#180: was rev20_flat.asm 0x075F, an EEPROM-relative address; true 0x075F is an ACG1FRQ1 (0xFFE6) write, not ACGCTL (0xFFE1)). |
| 0xffe1 | and_not | 0x3f | rev20 | SAFE_OMIT — RMW inside `fcn.0x0728` preserving the upper ACGCTL bits. mboxfw writes ACGCTL with a plain assign in the 44.1 branch and an `\|= 0xC0` in the tail; the intermediate mask only matters on the mode-3 path we do not implement. |
| 0xffe1 | or | 0xc0 | rev20 | RESOLVED 2026-07-28 — mboxfw emits this in the streaming tail. CORRECTED: these are ACGCTL bits 6-7, not a DMA arm. The old text ("arms DMA channels 0+1", "without this the streaming EPs stay configured but not running") was the load-bearing error behind the zero-length-isoc bug: the write was present, so the DMA looked armed, while DMAEN on 0xFFE8/0xFFEE had never been set. Cite: Rev 20 @ 0x07CC, Rev 22 @ 0x07AD (#180: was rev20_flat.asm 0x07DE, +0x12; true 0x07DE is an OEPDCNTX2 (0xFF9B) write). |
| 0xffe2 | assign | 0x10 | rev20 | RESOLVED — ACGDCTL (TI Reg_stc1.h; datasheet §6.5.3.10), the ACG1 divider control. mboxfw emits it in `streaming_set_rate()`. This row previously called it "Rev-20-empirical DMACTL2" used "to halt (=0) then arm capture DMA", citing a `DMACTL2 = 0` line in rev20_dynamic_reconfig.md §2. Rev 20 never writes 0x00 there; `fcn.0x0E18` writes 0x10 to both ACGDCTL and ACG2DCTL. |
| 0xffe8 | assign | 0x02 | rev20 | RESOLVED — DMACTL0 (datasheet §6.5.2.3). 0x02 = DMAEN 0, EPDIR 0 (OUT), EPNUM 2 → channel 0 serves EP2 OUT, i.e. playback. mboxfw `hw_init.c` writes the same. Cite: Rev 20 @ 0x09E0, Rev 22 @ 0x0901 (#180: was rev20_flat.asm 0x09F2, +0x12; true 0x09F2 is the USBFADR (0xFFFF) write). |
| 0xffe8 | or | 0x80 | rev20 | RESOLVED 2026-07-28 — bit 7 is **DMAEN** (datasheet §6.5.2.3), not "probably auto-reload or start". mboxfw now sets it in `streaming_playback_enable()`. Rev 20 sets it at 0x03DF and 0x043B, after enabling OEPCNF2 (0xFF98), and clears it at 0x1013. |
| 0xffe8 | and_not | 0x7f | rev20 | RESOLVED 2026-07-28 — the clear half of the same DMAEN RMW, emitted by `streaming_playback_enable(0)`. Not a "mask of the low bits before the OR": SDCC renders `x &= ~0x80` as `and_not 0x7f`. |
| 0xffe9 | assign | 0x80 | rev20 | RESOLVED — DMATSH0. Bits 7:6 = BPTS = 10b = **3 bytes per time slot**; bits 5:0 = TSL(13:8) = 0 (datasheet §6.5.2.2). mboxfw `hw_init.c` matches. Cite: Rev 20 @ 0x09CE, Rev 22 @ 0x08EF (#180: was rev20_flat.asm 0x09E0, +0x12; true 0x09E0 is the DMACTL0 (0xFFE8) write). |
| 0xffea | assign | 0x03 | rev20 | RESOLVED — DMATSL0 = TSL(7:0) = time slots 0 and 1 (datasheet §6.5.2.1). With BPTS=3 that is 2 channels × 3 B = **6 bytes per audio sample**, which agrees with IEPCNF1 = 0xC5 (BPS field 5 → 6 B/sample) and with the 288 B/frame a stock unit delivers at 48 kHz. The old reading "24-bit target size 0x800380 or similar" was a guess and is withdrawn. Cite: Rev 20 @ 0x09C8, Rev 22 @ 0x08E9 (#180: was rev20_flat.asm 0x09DA, +0x12; true 0x09DA is the DMATSH1 (0xFFEF) write). |
| 0xffee | assign | 0x09 | rev20 | RESOLVED — DMACTL1. 0x09 = DMAEN 0, EPDIR 1 (IN), EPNUM 1 → channel 1 serves EP1 IN, i.e. capture. mboxfw `hw_init.c` matches. Cite: Rev 20 @ 0x09E6, Rev 22 @ 0x0907 (#180: was rev20_flat.asm 0x09F8, +0x12). |
| 0xffee | or | 0x80 | rev20 | RESOLVED 2026-07-28 — DMAEN for the capture channel, now set in `streaming_capture_enable()`. Rev 20 sets it at 0x03CF, immediately after IEPCNF1 (0xFF60) = 0xC5. **This single missing write is why every isochronous IN packet came back zero-length** (usbmon, 2026-07-28: ours 0 B/frame, stock Rev 18 288 B/frame on the same host). |
| 0xffee | and_not | 0x7f | rev20 | RESOLVED 2026-07-28 — DMAEN clear on capture stop, emitted by `streaming_capture_enable(0)`. Rev 20 does it at 0x033F and 0x03F1. |
| 0xffef | assign | 0x80 | rev20 | RESOLVED — DMATSH1, same encoding as DMATSH0 (0xFFE9) above. Cite: Rev 20 @ 0x09DA, Rev 22 @ 0x08FB (#180: was rev20_flat.asm 0x09EC, +0x12; true 0x09EC is the USBIMSK (0xFFFD) write). |
| 0xfff0 | assign | 0x03 | rev20 | RESOLVED — DMATSL1, same encoding as DMATSL0 (0xFFEA) above. Cite: Rev 20 @ 0x09D4, Rev 22 @ 0x08F5 (#180: was rev20_flat.asm 0x09E6, +0x12; true 0x09E6 is the DMACTL1 (0xFFEE) write). |
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
| 0xff61 | assign | 0x94 | usb.c | IEPBBAX1 = 0x94 -> capture buffer @ 0xFCA0. **MATCHED (#162), 2026-08-04.** Identical to stock in both images (Rev 20 fcn.0x0970 @ 0x09A8, Rev 22 fcn.0x0891 @ 0x08C9). Previously 0x60 (0xFB00) on the strength of a layout chosen to fit "below the 0xFF00 SFR boundary" -- see 0xff62 for why that boundary does not exist. Also moved out of streaming.c into usb_ep0_setup() by #163, so it is now written once per boot as stock does, not once per SET_INTERFACE. |
| 0xff62 | assign | 0x50 | usb.c | IEPBSIZ1 = 0x50 -> **640 bytes**, stock's size. **MATCHED (#162), 2026-08-04.** The stated reason for the old 512 B -- "to fit below the 0xFF00 SFR boundary" -- was false, and the same comment contradicted it three lines later by listing a free tail at 0xFF00-0xFF27. Datasheet Figure 6-3: endpoint data buffers run 0xFA64-0xFF27, setup packet buffer 0xFF28-0xFF2F, config blocks 0xFF30-0xFFAF, MMRs 0xFFB0-0xFFFF. Nothing is reserved at 0xFF00. Per §6.4.4.4 BSIZ sizes a single circular buffer for isochronous endpoints, so 512 B cannot hold two 288-byte 48 kHz frames where 640 B can. Stock's constant is carried in A from the OEPBSIZ2 write -- Rev 20 0x09AB / Rev 22 0x08CC hold `90 ff 9a 74 50 f0 90 ff 62 f0` -- which is why the access map reports it as write-computed rather than as a literal. SDCC emits the identical ten bytes from the two C assignments, so the whole four-write block is byte-for-byte stock's (Rev 20 0x099F, 22 bytes). |
| 0xff63 | assign | 0x00 | streaming.c | IEPDCNTX1 (data-count) reset on stream (dis)arm. Matches Rev 20 pattern (rev20_audio_dispatch.md §1 "clear the four BCTX/BSIZ bytes"). |
| 0xff98 | assign | 0x00 / 0xc5 | streaming.c | OEPCNF2 — playback EP config. Same pattern as IEPCNF1 (0xff60). Rev 20 writes 0xC5, mboxfw does the same on stream arm. SAFE. |
| 0xff99 | assign | 0x44 | usb.c | OEPBBAX2 = 0x44 -> playback buffer @ 0xFA20. **MATCHED (#162), 2026-08-04.** Identical to stock (Rev 20 fcn.0x0970 @ 0x09A2, Rev 22 fcn.0x0891 @ 0x08C3). Layout is now stock's exactly: EP0 0xFA10-0xFA1F, EP2 OUT 0xFA20-0xFC9F, EP1 IN 0xFCA0-0xFF1F, 8 free bytes to 0xFF27. Both audio buffers overlap the region Figure 6-3 labels "ROM support" -- deliberate, and what stock does: TI Mmap.h line 24 states the ROM DFU code and the application never run at the same time. mboxfw already depended on this for EP0's own buffers. |
| 0xff9a | assign | 0x50 | usb.c | OEPBSIZ2 = 0x50 -> 640 bytes, same rationale as 0xff62 and now **MATCHED (#162)** likewise. |
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
            /* mode 2 — Rev 20 @ 0x075F (#180: was flat 0x0771, +0x12) */
            XDATA(0xFFE5) = 0x6A; XDATA(0xFFE6) = 0x4B; XDATA(0xFFE7) = 0x20;
            XDATA(0xFFF7) = 0x6A; XDATA(0xFFF8) = 0x4B; XDATA(0xFFF9) = 0x20;
        } else {
            /* mode 1 — Rev 20 @ 0x074D (#180: was flat 0x075F, +0x12). Note: mode 1 does NOT
             * write DMASRC (relies on power-on defaults per RE). */
            XDATA(0xFFE1) = 0x0D;                   /* Rev-20 DMACTL1 mode 1 */
        }

        /* Common tail — Rev 20 @ 0x07B3-0x07F1 (#180: was flat 0x07C5-0x0803, +0x12). */
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
| 0xffb1 | CHANGED_REV and_not 0xfe + assign 0x06 | **IMPLEMENTED** | `GLOBCTL` — **RE-CORRECTED 2026-08-04: mboxfw DOES set CPTEN. The 2026-07-31 "no GLOBCTL write at all" correction was itself wrong and is retracted.** `hw_init.c:302` reads `GLOBCTL  |= 0x01;   /* enable codec port (CPTEN) */` and has since the initial skeleton commit `ffa7da4` — `git log -S` confirms it was never added or removed. Note the **two spaces** before `|=`, which is how a whitespace-sensitive grep missed it. It is in the shipped image, not just the source: `build/hw_init.rst` line 400 emits `90 FF B1 / E0 / 44 01 / F0`, and that byte string occurs in `build/mboxfw_flasher.bin` at offset 0x43F. Bit 0 is **CPTEN**, the codec port enable (§6.5.7.4: "the codec port interface configuration registers must be fully programmed before this bit is set by the MCU"), and mboxfw sets it after all six CPTCNF/CPTRXCNF writes, matching stock's boot bracket at Rev 20 `0x0934` / Rev 22 `0x0805`. Stock's `and_not 0xfe` / `set-bits 0x01` pairs are the CPTEN-off → reconfigure → CPTEN-on bracket, which TI's own `Application/Codec.c` `coInitCodec()` performs identically. All three mboxfw GLOBCTL sites are read-modify-write (verified in the listings), so none clobbers CPTEN. See FINDING_capture_works_anyway.md and #168. The `assign 0x06` half was **RETRACTED 2026-07-29**. It was called a scanner artifact because no `mov a,#0x06; movx @dptr,a` to 0xffb1 exists — true, and the wrong conclusion. DPTR is never loaded with 0xFFB1 there; it arrives by `INC DPTR` from the MEMCFG write at 0xFFB0, 27 instructions earlier (Rev 20 0x08D4 → 0x08FB, Rev 22 0x07F5 → 0x081C). Byte-scanned: `a3 74 06 f0` occurs exactly once per image. `rev20_STARTUP_TRACE.md` step 14 recorded it correctly all along, so two docs contradicted each other. Also note the dismissal cited rev20_flat.asm, the known-bad disassembly. Now implemented as `GLOBCTL \|= 0x02` in hw_init.c (RMW per #48 reaches the same 0x06 from the ROM's 0x04). **GLOBCTL bit 1 is P3PUDIS** — "Pullup resistor disable. If set to 1, disables on-chip pullup resistors on P3 GPIO pins" (§6.5.7.4, read 2026-07-31). No longer unknown; see FINDING_globctl_bits_named_and_cpten_missing.md §1 for why setting it made the device silent (it defeats `check_boot_dfu_button`'s pull-up-dependent read). |
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
| 0xffb1 | and_not | 0xfe | rev20 | **Half of the 2026-07-31 correction is itself retracted — see 2026-08-04 above.** What stands: bit 0 is not a "USB-enable bit"; §6.5.7.4 names it **CPTEN**, codec port enable, and stock's clear/set pairs bracket every C-port reconfiguration. What was wrong: mboxfw *does* set `\|= 0x01`, at `hw_init.c:302`, present since the first commit and verified in the shipped binary at offset 0x43F. mboxfw has no equivalent of stock's *clearing* half because it never reconfigures the C-port after boot — the clear/set bracket only exists to make a mid-flight reconfiguration safe, and the one stock path that does so is the clock-mode-5 branch, reachable only from work code 0x0A, which no site in either image can post (all addressing modes scanned; see FINDING_capture_works_anyway.md). See the 0xffb1 row above and FINDING_capture_works_anyway.md. |
| 0xffb1 | assign | 0x06 | rev20 | **RETRACTED 2026-07-29 — REAL, now implemented.** Reached via `INC DPTR` from 0xFFB0, so the direct form correctly did not exist; the inference from its absence was wrong. Both images do it (Rev 20 0x08FB, Rev 22 0x081C). See the 0xffb1 row above. |
| 0xffb1 | runtime | - | mboxfw | `GLOBCTL \|= 0x02` (P3PUDIS), written **twice**: once in `main()` immediately before `check_boot_dfu_button()`, and again in `hw_init` in stock's position. The `main()` copy is #172 — the DFU escape reads P3, so it needs P3PUDIS set, and hoisting just this write (plus `P3 = 0xFF`) lets the escape run before `usb_init()` instead of after `hw_init()`, covering a hang anywhere in the boot path. Both writes are idempotent RMW of a bit already set, so the duplicate is free; `hw_init` stays the authority on boot order. `verify_conn_reachable.py` enforces that both prerequisites precede the escape and that the escape precedes `usb_init`. **RESTORED 2026-08-03, build 0x0016.** Removed 2026-07-29 on a bisect that was real and read backwards: build 0x0010 (present) never attached, 0x0011 (absent) attached in 7 s, and that was recorded as "P3PUDIS makes the device silent on USB" — which stock refutes, since stock sets the bit and enumerates. The real path: the front-panel buttons are **active HIGH**, `check_boot_dfu_button()` tested for LOW, so with P3PUDIS set the idle pin read as "held", the firmware invalidated its own EEPROM header and spun without attaching. Polarity proof is from the image, not from a guess: `p3_button_scan` fires on `prev==0 && cur==1` and Keil's `?C_INITSEG` zeroes the shadow at IRAM 0x20 (`01 20 00`), so idle-high pins would fire all three handlers on the first scan of every boot; the hardware boots to MIC. Without the bit, internal pull-ups pin P3 high and the buttons are dead — measured on Mbox A, P3 rests at 0xFA and bit 3 stays 1 with the button held, while stock on the same unit cycles mic→line→inst. Stock: Rev 20 `0x08FE`, Rev 22 `0x081F`, reached by `INC DPTR` from the MEMCFG (0xFFB0) write. RMW per #48 (ROM leaves 0x04). The boot-DFU read is un-inverted and moved after `hw_init` in the same commit; the two changes are not separable. See `FINDING_buttons_are_active_high.md`, #150, #169. |
| 0xffb1 | or | 0x02 | mboxfw | `GLOBCTL \|= 0x02` (P3PUDIS) hoisted into `main()` immediately before `check_boot_dfu_button()` — **#172**. Same bit, same value, same citation as the `hw_init` write above (Rev 20 `0x08FE`, Rev 22 `0x081F`); this is a subset of hardware init pulled forward, not a relocation, and `hw_init` keeps its copy. Reason: the escape reads P3, and with the internal pull-ups on it reads a stuck 1 whatever is pressed, so without this write the escape cannot fire — which is what every build up to 0x0015 shipped. Hoisting it (with `P3 = 0xFF`) lets the escape run before `usb_init()`, so it survives a hang anywhere in the boot path rather than only after `hw_init()`. A hang before it is what made BRICK_LOG.md #3 cost an SDA short. Both writes are idempotent RMW of a bit already set. `verify_conn_reachable.py` enforces the ordering and both prerequisites; mutation-tested 2026-08-03. |
| 0xfffc | rmw | - | mboxfw | USBCTL. NOT a new write: build 0x0010's boot-handoff snapshot reads USBCTL into telemetry block 8 immediately before main()'s existing `USBCTL = 0`, and SDCC keeps DPTR loaded across both, so the scanner sees read-then-store on one DPTR and reports rmw. Verified in main.rst: the store is `clr a / movx @dptr,a`, still exactly = 0, POLICY §2 carve-out A as before. The read must be first -- it is the only way to recover USBCTL's handoff value, which the next line destroys. |
| 0xffb2 | rmw | - | rev20 | FALSE_POSITIVE — VECINT ack pattern (read+write 0). mboxfw does the same. |
| 0xffb2 | runtime | - | rev20 | FALSE_POSITIVE — same. |
| 0xffc0 | and_not | 0x54 | mboxfw | JUSTIFIED — I²C CLEAR_ALL mask per TI I2c.h. Stricter than Rev 20's 0xFC. Superset behavior. |
| 0xffc0 | and_not | 0xfc | rev20 | SAFE — Rev 20 clears only STOP bits; mboxfw's stricter TI-referenced 0x54 mask is a superset. |
| 0xffc1 | assign | 0x00 | rev20 | FALSE_POSITIVE — Rev 20 writes 0 in code paths that aren't the dummy-read trigger. |
| 0xffc1 | assign | 0xff | mboxfw | JUSTIFIED — dummy trigger byte for I²C read per TI I2c.c:102. |
| 0xffc3 | assign | 0xa1 | mboxfw | JUSTIFIED — EEPROM 7-bit address 0x50 shifted + R/W=1 read. Rev 20 runtime-computes same value. |
| 0xffc3 | runtime | - | rev20 | FALSE_POSITIVE — Rev 20 side of same. |
| 0xffd5 | runtime | - | rev20 | FALSE_POSITIVE — CPTBRRX 0xAC, both firmwares write same terminal value. |
| 0xffe2 | assign | 0x00 | mboxfw | STALE — retired 2026-07-28; see the ACGDCTL row in the C-port section. The write does not exist in mboxfw any more and the "DMACTL2" name was invented. |
| 0xfff9 | assign | 0x20 | mboxfw | FALSE_POSITIVE — DMASRC2_H mode-2 value 0x20 matches Rev 20 fcn.0x0728 @ 0x077B. Scanner classification asymmetry. |
| 0xfffc | and_not | 0x7f | rev20 | SAFE_OMIT — USBCTL bit-manipulation in Rev 20 mode-switch (bus-reset simulation on rate change). mboxfw doesn't do runtime re-enum. |
| 0xfffc | or | 0x80 | rev20 | FALSE_POSITIVE — Rev 20 runtime USBCTL |= CONN. mboxfw does same at end of usb_init. |
| 0xfffc | or | 0xc0 | rev20 | SAFE_OMIT — Rev 20 sets CONN+FEN together in a runtime path. mboxfw sets only CONN (Rev 20 also does |= 0x80 boot-time via 0x0ADE-0x0AE4). |
| 0xfffc | assign | 0x00 | safety_net | JUSTIFIED — intentional pre-init USB disconnect at top of `main()`. Assignment (not RMW) is the only way to guarantee a clean 0-state before re-configuring. POLICY §2 carve-out A applies. Rev 20 does the same at 0x08D0 / Rev 22 @ 0x07F1, inside the master-init sub (#180: was rev20_flat.asm 0x08E5, +0x12). safety_net does it one instruction earlier — semantically equivalent. |
| 0xfffc | or | 0x01 | mboxfw+safety_net | JUSTIFIED — USBCTL SDW-confirm bit set inside RESET_TO_BOOT_ROM macro (regs.h). Byte-for-byte match to TI Utils.SRC UtilResetBootCPU (lines 119-160). Bracketed by the `and_not 0xFE` clear below. Fires only in DFU-trigger recovery path. |
| 0xfffc | and_not | 0xFE | mboxfw+safety_net | JUSTIFIED — USBCTL SDW-confirm bit clear inside RESET_TO_BOOT_ROM macro, second half of the TI Utils.SRC UtilResetBootCPU handshake. Fires only in DFU-trigger recovery path. |
| 0xfffc | runtime | - | rev20 | SAFE_OMIT — Rev 20's USBCTL RMW pattern the scanner couldn't classify (a write whose operator falls outside or/and_not/assign, e.g. `xrl`-style toggle emitted by a compiler intrinsic, or a write via a helper subroutine the scanner doesn't fully trace). Every observed USBCTL write in Rev 20 is a mode-switch or attach — mboxfw does not perform runtime re-enumeration and does not need it. |
| 0xffa1 | any | any | safety_net | SPURIOUS — safety_net/src/main.c had `#define GLOBCTL XDATA(0xFFA1)` at one point; correct address is 0xFFB1 per Reg_stc1.h and Rev 20 0x100F. Any write to 0xFFA1 is unintended; if this row triggers, fix the #define. |
| 0xfffd | assign | 0x00 | rev20 | SAFE_OMIT — USBIMSK disable-all path (Rev 20 uses during mode switches). |
| 0xfffd | or | 0xf5 | usb.c | JUSTIFIED — mboxfw's USBIMSK. 0xF5 = RSTR|SUSR|RESR|SOF|SETUP|STPOW (datasheet §6.5.1.3: 7=RSTR 6=SUSR 5=RESR 4=SOF 3=PSOF 2=SETUP 1=rsvd 0=STPOW). Changed from 0xE5 on 2026-07-28: 0xE5 is the same set with SOF (bit 4) masked, and telemetry block 5 measured tlm_sof_count == 0 on hardware for exactly that reason — no frame clock. Rev 20's 0x9F also enables SOF, so this moves us TOWARD Rev 20, not away. Remaining deltas vs Rev 20: we add SUSR/RESR (bits 6/5) for suspend/resume, which Rev 20 omits, and still mask PSOF (bit 3). Masking SOF was neutral for enumeration, which is why it survived until streaming was attempted. |
| 0xfffd | assign | 0x9f | rev20 | SAFE_OMIT — Rev 20 uses USBIMSK = 0x9F (Rev 20 @ 0x09EC and VEC_RSTR @ 0x0F6E; Rev 22 @ 0x090D and 0x0F8F. #180: was rev20_flat.asm 0x09FE / 0x0F7E, +0x12). mboxfw uses 0xF5. Bits per datasheet §6.5.1.3: 7=RSTR 6=SUSR 5=RESR 4=SOF 3=PSOF 2=SETUP 1=rsvd 0=STPOW. CORRECTED 2026-07-28: this row previously said changing ours to 0x9F "would silence our SETUP handler" because STPOW was bit 5 — both claims were wrong (STPOW is bit 0, SETUP is bit 2, and 0x9F sets both). Commit 189c219 fixed the identical error in safety_net's comments and never propagated it here. mboxfw was 0xE5 until 2026-07-28, which masked SOF off and made tlm_sof_count read 0 on hardware; now 0xF5 = 0xE5 + SOF. Remaining divergence from Rev 20 is PSOF (bit 3, still masked) and SUSR/RESR (bits 6/5, which Rev 20 omits). |
| 0xfffd | assign | 0xff | rev20 | SAFE_OMIT — USBIMSK enable-all. Superset of mboxfw's 0xE5. |
| 0xfffd | or | 0xe5 | mboxfw | JUSTIFIED — TI engUsbInit UsbEng.c line 647 uses exactly 0xE5. |
| 0xfffd | runtime | - | power.c | JUSTIFIED — the resume path's `USBIMSK |= 0xF5` in do_suspend(), which SDCC emits through the accumulator (hence `runtime`, not `or 0xf5`). DELIBERATE DIVERGENCE from Rev 20 fcn.0x0526 @ 0x054E (Rev 22 @ 0x054D), which ASSIGNS 0x9F here. 0x9F omits SUSR (bit 6) and RESR (bit 5), so stock masks the suspend source off after handling one suspend and never suspends again for the rest of that attach. Copying that would make our suspend a one-shot. We restore the same 0xF5 usb_init() sets, and OR rather than assign per task #48 (USBIMSK is boot-ROM-owned). |
| 0xffff | assign | 0x00 | rev20 | FALSE_POSITIVE — USBFADR clear on bus reset. mboxfw does same in VEC_RSTR / usb_init. |
| 0xff9a | assign | 0x50 | rev20 | ⚠ BLOCKER-REV20-SIDE — Rev 20 boot init @ 0x09BD writes OEPBSIZ2=0x50 (640B buffer). mboxfw's 0x20 (256B) is undersize for 48kHz. See BLOCKER note. |
| 0xffd5 | assign | 0xac | hw_init.c | **#161 RESOLVED — this register's BYOR bit is INERT.** `CPTRXCNF3` bit 2 is BYOR (datasheet §6.5.4.12, address FFD5h). Build 0x001D set it (0xA8 -> 0xAC) with CPTCNF3 held at its proven-good 0xAC and the analog loopback returned results IDENTICAL to 0x001B to five digits: out -29.20/-41.20/-53.20/-65.20 dBFS for in -9/-21/-33/-45, amplitude/rms = 1.41421 = sqrt(2). The receive path does not take its byte order from here, even though CPTCNF1 = 0x0D selects MODE = 101b = I2S mode 5 and §6.5.4.12's "only used in I2S Mode 5" condition is met. CPTCNF3 governs BOTH directions, as its own direction-agnostic wording says. The 0xA8 mboxfw used to carry here was residue from #147 and was doing nothing; 0xAC matches stock's boot init (Rev 20 fcn.0x08CB @0x0923, Rev 22 @0x0844) so both codec-port config registers agree with stock and no future reader has to re-derive why they differed. See FINDING_161_byor_asymmetry.md. |
| 0xffd5 | assign | 0xac | rev20 | **SAFE_OMIT, but not for the reason previously given.** 0xAC is stock's BOOT value only (Rev 20 fcn.0x08CB @0x0923, Rev 22 @0x0844); stock overwrites it with 0xA8 at the first SET_CONFIGURATION and never returns to it. Omitting it therefore matches stock's steady state rather than diverging from it. The earlier text here — "stock's CPTRXCNF3 with BYOR SET, i.e. big-endian capture ... do NOT fix this back to match stock" — was right about the action and wrong about every premise. See the 0xffd5/0xa8 row above and FINDING_147_cport_and_ep_buffer_divergences.md §2. |
| 0xffde | assign | 0xac | hw_init.c | **NOT A DIVERGENCE from stock's BOOT init** (Rev 20 fcn.0x08CB @0x090B, Rev 22 fcn.0x07EC @0x0831 both write 0xAC), but a DELIBERATE, MEASURED divergence from stock's RUNNING state. Stock overwrites both this and 0xffd5 with 0xA8 at the first SET_CONFIGURATION via the helper at Rev 20 0x0FF4 / Rev 22 0x0FE2, which writes ONE accumulator to BOTH registers; mboxfw never runs that helper. `CPTCNF3` bit 2 is BYOR (datasheet §6.5.4.3, address FFDEh). **0xAC is UNIQUELY correct for playback, by falsification:** build 0x001C set 0xA8 here and playback was destroyed — amplitude/rms 0.045 vs sqrt(2)=1.414, harmonics at or above the fundamental, and output pinned near -31 dBFS across a 36 dB input sweep, which is the byte-swap signature (LSB promoted to MSB gives near-constant-power noise that does not track input). mboxfw declares S24_3LE; stock is declared S24_3BE by `sound/usb/quirks-table.h`, so the two firmwares legitimately want different byte order. See FINDING_170_audio_works.md. |
| 0xffde | assign | 0xa8 | hw_init.c | **CHANGED_REV, and the SAME deliberate divergence as the 0xac row above -- this is stock's RUNNING value, not a second register.** It became visible to the gate on 2026-08-05, when `load_rev20_helper_writes()` started recovering writes a callee performs against the caller's DPTR: stock reaches this one through the helper at Rev 20 0x0FF4 (site 0x0355), which the linear scan of rev20_flat.asm could never see. mboxfw writes 0xAC and never runs that helper. 0xAC is uniquely correct for playback by falsification -- build 0x001C set 0xA8 and playback was destroyed (amplitude/rms 0.045 vs sqrt(2), output pinned near -31 dBFS across a 36 dB input sweep: the byte-swap signature). mboxfw declares S24_3LE where stock is declared S24_3BE, so the two firmwares legitimately want different byte order. #161, FINDING_170_audio_works.md. |

## #46 rows retired 2026-08-05

The 88.2/96 kHz support that justified them was removed from the firmware, so
the writes they described are no longer emitted and the rows would have been
stale justifications for absent code -- worse than no row, per CLAUDE.md.

* `0xff9a` / `0xff61` / `0xff62` (endpoint buffer size and bases) are back to
  stock's 640/640 at 0xFA20/0xFCA0 and now MATCH both images, so they need no
  justification at all. The asymmetric 696/576 existed only to give playback
  slack at a 576 B frame.
* `0xffb1` (GLOBCTL CPTEN bracket), `0xffd4` (CPTCNF4) and `0xffdd`
  (CPTRXCNF4) were the runtime divider changes that reached the doubled rates.
  hw_init still writes the dividers once at boot; only the runtime re-write is
  gone.

Why the rates went: the converter follows MCLK and the ACG cannot double it,
so 88.2/96 presented the codec with 256 fs, it kept converting at the base
rate, and every tone folded about that rate's Nyquist. Measured -- see
firmware_stock/decomp/FINDING_46_no_bandwidth_above_24k.md.

| 0xffd4 | assign | 0x01 | rev20 | SAFE_OMIT — `CPTRXCNF4` (0xFFD4) DIVB2 = ÷2, the MCLKO2→SCLK2 divider on the I²S **receive** clock, written by Rev 20's mode-5 branch @0x07A0 (Rev 22 @0x077E). mboxfw does not implement mode 5 (I²S "1 OUT and 1 IN at different frequencies") and does not mirror this write. hw_init writes the BOOT value 0x03 (÷4) instead — Rev 20 fcn.0x08CB @ 0x0929, Rev 22 fcn.0x07EC @ 0x084A. **This row was briefly the justification for a RUNTIME ÷2 write of our own**, added by #46 to reach 88.2/96 kHz alongside the playback-side divider `CPTCNF4` (0xFFDD); that support was removed on 2026-08-05 and the row reverts to what it always was — an omission of an unreachable stock branch. Taking the mode-5 value by accident between 2026-07-26 and 2026-07-28 doubled the capture sample rate, which is how the divider's effect was first measured; see the long comment in hw_init.c. |

| 0xffc0 | or | 0x02 | rev20 | SAFE_OMIT — `I2CSTA` (0xFFC0) bit 1 = STOP_READ, set by stock when it READS the EEPROM. mboxfw no longer reads it: `eeprom_read_byte()` and `eeprom_smoke_test()` were removed on 2026-08-05 with the boot-button DFU trigger, their only caller. The WRITE path is unaffected and still matches — `TLM_REQ_ENTER_DFU` invalidates the header checksum through `eeprom_write_byte()`, which is the DFU trigger in daily use. Two sibling writes went the same way and need no separate rows because they are read-only-path too: `I2CDATO` (0xFFC1) = 0xFF, the dummy write that clocks a read, and `I2CADR` (0xFFC3) = 0xA1, the read address for the EEPROM at slave 0x50. Recovery from a non-enumerating image is the SDA-short bootstrap in BRICK_LOG.md, which needs no firmware cooperation. |
| 0xff58 | assign | 0xc2 | streaming.c | **MBOXFW_ONLY, #186 stage 2** — `IEPCNF2`, the config byte for EP2 IN, the playback FEEDBACK endpoint. 0xC2 = IEPEN \| ISO \| BPS field 2, i.e. 3 bytes per sample (datasheet §6.4.4.6.2: 00h = 1 byte, so 02h = 3). Compare `OEPCNF2` = 0xC5 on the playback data endpoint, BPS 5 = 6 bytes = stereo 24-bit. Stock has no feedback endpoint at all, so there is no address to mirror: Rev 22 ported only the DMA-realignment tail of TI's `softPll()` and dropped the ACGCAP measurement and the endpoint that consumes it (FINDING_186_ti_softpll_is_the_feedback_endpoint.md). This exists because #181/#182 measured the ACG free-running (+4.263 +/- 0.989 ppm between two units sharing one SOF, confirmed device-side at +4.53 ppm), which makes the previous SYNC_ADAPTIVE declaration false; asynchronous OUT obliges an explicit feedback endpoint per USB 2.0 §5.12.4.2. |
| 0xff58 | runtime | - | streaming.c | **MBOXFW_ONLY, #186 stage 2** — the same register cleared to 0 when playback stops, mirroring how `OEPCNF2` is cleared on the same path (Rev 20 fcn.0x1013 @ 0x1001 for the DMA half). The feedback endpoint comes up and goes down with the stream it serves. |
| 0xff59 | assign | 0xe4 | streaming.c | **MBOXFW_ONLY, #186 stage 2** — `IEPBBAX2`, base address of the feedback buffer, encoded (addr - 0xF800) >> 3. 0xE4 = 0xFF20, the free 8-byte tail of the endpoint data region (which ends at 0xFF27; capture runs to 0xFF1F). Eight bytes suffice because an isochronous endpoint gets ONE circular buffer, not the X/Y pair a control/interrupt/bulk endpoint gets in double-buffer mode — datasheet §6.4.4.4 states the distinction explicitly. So the 640/640 audio geometry, and the byte-identical stock write block #162 matched, are untouched. |
| 0xff5a | assign | 0x01 | streaming.c | **MBOXFW_ONLY, #186 stage 2** — `IEPBSIZ2` = 1, i.e. 8 bytes, the BSIZ granularity minimum. The packet is 3 bytes; 8 is the smallest expressible size. |
| 0xff5b | assign | 0x01 | streaming.c | **MBOXFW_ONLY, #186 stage 2, resolved by #219** — `IEPDCNTX2` (0xFF5B), the feedback packet's count. TI does exactly this in `SoftPll.c::softPll`, which arms this register and `IEPDCNTY2` (0xFF5F) together — the closest thing to a reference this write has. **IT IS A SAMPLE COUNT, NOT A BYTE COUNT.** This endpoint is isochronous, and datasheet §6.4.4.3 gives `DCNTX(6:0)` as bytes for control/interrupt/bulk but as *samples* for isochronous, with the width taken from the BPS field of the configuration byte. `IEPCNF2 = 0xC2` is BPS 2 = 3 bytes per sample (§6.4.4.6.2, `00h = 1 byte`), so one feedback value is exactly one sample and `AUDIO_FEEDBACK_ARM = 1` emits the 3 bytes UAC1 requires. It was `assign 0x03` until 2026-08-16, then `runtime` while #215's `TLM_REQ_FB_TUNE` swept it, and is back to `assign` at **0x01** now that #219 has explained the sweep's result and the knob is retired. **This value is coupled to IEPCNF2's BPS field and may not be changed alone** — widening the feedback value without re-cutting BPS re-creates #211, where the endpoint emitted 9 bytes against a `wMaxPacketSize` of 3. |
| 0xff5f | runtime | - | streaming.c | **MBOXFW_ONLY, #186 stage 2** — `IEPDCNTY2`, armed to the same `AUDIO_FEEDBACK_ARM` = 1 alongside the X count, again following `SoftPll.c::softPll`. See the 0xff5b row for why that 1 is a *sample* count (#219). **THE `runtime` PATTERN HERE IS A SCANNER ARTEFACT, NOT A VARIABLE VALUE.** Both stores take the same compile-time constant, but SDCC keeps it in the accumulator across them and emits `mov dptr,#0xff5b / mov a,#0x01 / movx @dptr,a` then `mov dptr,#0xff5f / movx @dptr,a` — the second store has no immediate for the scanner to attribute, so it classifies as `runtime` while 0xff5b classifies as `assign 0x01`. Verified in `build/streaming.asm` at the #219 commit. A future SDCC that reloads the constant would flip this row to `assign 0x01`; that is a codegen change, not a behaviour change. |
| 0xff50 | runtime | - | usb.c | **MBOXFW_ONLY, #207** — `IEPCNF3`, the config byte for EP3 IN, the UAC1 status interrupt endpoint. 0x80 = IEPEN with ISO **clear**, so §6.4.4.6.1's control/interrupt/bulk bit map applies rather than §6.4.4.6.2's isochronous one, and DBUF stays 0 — single buffered, X only, 2 of its 4 bytes used. Stock declares no status endpoint and has no reason to: it serves a vendor-class configuration whose UAC block is dead data. UAC1 §3.7.1.2 defines the endpoint and its two-byte status word. Written in `usb_ep0_setup()` rather than at stream start because it must answer a host poll whenever the device is configured — a front-panel press with nothing streaming is exactly the case it exists for. |
| 0xff50 | or | 0x08 | usb.c | **MBOXFW_ONLY, #214** — sets `STALL` (bit 3) on `IEPCNF3`, halting EP3 IN on `SET_FEATURE(ENDPOINT_HALT)`. Stock ships no interrupt endpoint, so no Rev 20 or Rev 22 address writes this register and none can be cited; TI's `UsbEng.c` has no halt handler either. USB 2.0 §5.6.3 exempts **isochronous** endpoints from the halt feature, which is why 0x81/0x02/0x82 correctly keep stalling the request, but EP 0x83 is `bmAttributes 3` = Interrupt on the live device and §9.4.9 makes halt mandatory there. Bit 3 is the same `STALL` bit TI's `hwMacro.h` `STALLInEp0` sets and that `reply_stall()` uses on EP0. MEASURED: `ch9_probe --invasive` recorded the stall this replaces. |
| 0xff50 | and_not | 0xd7 | usb.c | **MBOXFW_ONLY, #214** — clears `STALL` (bit 3) **and** `TOGGLE` (bit 5) on `IEPCNF3`, the `CLEAR_FEATURE(ENDPOINT_HALT)` half. §9.4.1 requires the clear; §5.8.5 additionally requires the data toggle to reset to DATA0 when a halt is cleared, which is why bit 5 goes down with bit 3 rather than bit 3 alone. `~0x28` is `0xd7`. Note this is the mask `reply_stall()` was once wrongly given on EP0 (`IEPCNF0 &= 0xD7`, which un-stalled instead of stalling); here clearing is the intent, so the same mask is correct for the opposite reason. |
| 0xff51 | assign | 0xe3 | usb.c | **MBOXFW_ONLY, #207** — `IEPBBAX3` = `EP_BBAX(0xFF18)` = (0xFF18 − 0xF800) >> 3 = 0xE3. Buffer base for the status endpoint, in the 8-byte units the register takes. The 8 bytes come from the capture allocation, which drops 640 → 632: iso endpoints have no DBUF bit (§6.4.4.6.2 gives bits 4:0 to BPS) so they always use X and Y, and `IEPBSIZx` is the size of the PAIR — 316 per half against a 294-byte maximum packet, 22 bytes clear. See EP_STATUS_BUF_ADDR in regs.h for why the region had room after a first reading said it did not. |
| 0xff52 | assign | 0x01 | usb.c | **MBOXFW_ONLY, #207** — `IEPBSIZ3` = `EP_BSIZE(8)` = 1. Eight bytes for the status endpoint, 4 per half, of which the two-byte status word uses 2. |
| 0xff53 | assign | 0x02 | usb.c | **MBOXFW_ONLY, #207** — `IEPDCNTX3` = 2, arming the two-byte UAC1 status word (bStatusType, bOriginator). Fire-and-forget: if a previous packet is still unconsumed this overwrites it, which is correct — the newest state is the only one worth delivering, and the host reads the actual value with GET_CUR. |
| 0xff19 | runtime | - | usb.c | **MBOXFW_ONLY, #207** — byte 1 of the status packet buffer at 0xFF18, carrying `bOriginator` = the Selector Unit's ID. Endpoint DATA memory, not a register; it appears in the SFR audit only because the region shares the XDATA window, exactly as the feedback buffer at 0xFF20 does. |
| 0xff18 | assign | 0x00 | usb.c | **MBOXFW_ONLY, #207** — byte 0 of the status packet buffer, carrying `bStatusType` = 0x00: originator type 0 (AudioControl interface), no pending bit. Same note as 0xff19 about this being data memory rather than a register. |
| 0xff20 | runtime | - | streaming.c | **MBOXFW_ONLY, #186 stage 2** — byte 0 of the feedback packet buffer at 0xFF20. This is endpoint DATA memory, not a register; it appears in the SFR audit only because the region shares the XDATA window. The value is samples-per-frame in 10.14, little-endian, derived from ACGCAP (see 0xff21/0xff22). |
| 0xff21 | runtime | - | streaming.c | **MBOXFW_ONLY, #186 stage 2** — byte 1 of the feedback packet. |
| 0xff22 | runtime | - | streaming.c | **MBOXFW_ONLY, #186 stage 2** — byte 2 of the feedback packet, the high byte of the 10.14 value. Full-speed feedback is 3 bytes in 10.14 format, confirmed independently by TI's `SoftPll.c` (which builds `(nInt << 14) \| (nFrac << 4)`) and by the Linux driver's own comment, "full speed devices report feedback values in 10.14 format as samples per frame". Linux range-checks the value and silently falls back to nominal when it is out of band, so a mis-scaled value would present as "the endpoint does nothing" rather than as an error. |
