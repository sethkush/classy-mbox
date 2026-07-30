# Rev 20 dynamic reconfiguration — CS8427 rewrites, DMA rate table, input switching

Traced from `firmware_stock/rev20_firmware_code.bin` via `firmware_stock/disasm/rev20_flat.asm`. Complements `NOTES.md` — this document zooms in on run-time reconfiguration (post-boot) rather than boot-time init.

## 1. CS8427 write helpers — actual entry points

Raw writer is `fcn.0x0C45` (R7 = CS8427 subaddress, R5 = value byte, packet `[0x20, R7, R5]` on bit-banged I²C P1.3/P1.4). Wrappers above it — note that some addresses in `NOTES.md` were off by ~14 bytes; the real entry points are:

| Real addr | NOTES alias | Effect                                                       |
|-----------|-------------|--------------------------------------------------------------|
| 0x057A    | 0x0568      | `CS8427[0x04]=0x41` then `CS8427[0x12]=0x00`                 |
| 0x0594    | 0x0582      | `CS8427[caller-reg]=caller-val` (via RAM[0x2C]/[0x2D]) then `CS8427[0x24]=0x80` |
| 0x08B8    | 0x08A6      | `CS8427[0x04]=0x00`                                          |
| 0x08C5    | 0x08B3      | `CS8427[RAM[0x2E]]=0x05` (used at boot for regs 0x05, 0x06)  |
| 0x08CF    | 0x08BD      | Pass-through, R7/R5 already set by caller                    |
| 0x08D6    | 0x08C4      | Same pass-through                                            |

**CS8427 boot sequence** — reconfirmed byte-for-byte at 0x0867-0x08B4 (10 writes: 0x04=0x00, 0x13=0x10, 0x04=0x00, 0x04=0x40, 0x01=0x01, 0x02=0x20, 0x03=0x0C, 0x05=0x05, 0x06=0x05, 0x11=0xFF). Matches `NOTES.md` §"CS8427 boot sequence — DECODED ✅".

## 2. ApplyAudioMode dispatcher — `fcn.0x0728`

Body starts at 0x0738 (0x0729-0x0737 are inline data / jump table padding). Called with `R7 = mode index`. Prelude:

```
RAM[0x2E] = R7               ; save mode
clr RAM[0x23].2, .3          ; clear "48 kHz codec" bits (dead — codec is I²S-autoconfig)
lcall 0x0E62                 ; shift the 16-bit codec control word out (P1.0/P1.2, latch P1.1)
DMACTL2 (0xFFE2) = 0         ; via fcn.0x0E18 — halt capture DMA
```

Dispatch (0x074E-0x075D): `mode-2 → 0x0771 (48k)`, `mode-3 → 0x07A0 (S/PDIF slave)`, `mode-5 → 0x07AB (C-port re-init)`, `mode-1 → 0x075F (44.1k)`, else → common tail 0x07D7.

## 3. Sample-rate handlers (what each mode actually writes)

| Mode | Body    | XDATA writes                                                                                              | RAM writes            | CS8427 writes (via common tail) |
|------|---------|-----------------------------------------------------------------------------------------------------------|-----------------------|--------------------------------|
| **1** — 44.1 kHz internal | 0x075F | `DMACTL1 (0xFFE1) = 0x0D`                                                                                 | `RAM[0x08]=1`, `RAM[0x31]=0x04`, `RAM[0x32]=0x41` | `CS8427[0x04]=0x41` (RUN=1, INC=1) |
| **2** — 48 kHz internal   | 0x0771 | `DMASRC0 = 0x20 4B 6A` (0xFFE7:E6:E5), `DMASRC2 = 0x20 4B 6A` (0xFFF9:F8:F7)                              | `RAM[0x08]=2`         | none (RAM[0x31]/[0x32] not updated → common tail rewrites CS8427 with stale value from last mode-1) |
| **3** — S/PDIF slave      | 0x07A0 | via `fcn.0x0DFD` (mistakenly called 0x0DEC in NOTES): `DMASRC0 = 0x0F A8 61`, `DMASRC2 = 0x0F A8 61`, `DMACTL1 (0xFFE1) = 0x06` | `RAM[0x08]=3`         | none (sjmp 0x07D7 skips CS8427 write and C-port re-init) |
| **5** — C-port I²S re-init | 0x07AB | `GLOBCTL (0xFFB1) &= 0xFE`, `CPTCTL (0xFFD4) = 0x01`, `GLOBCTL |= 0x01`, then common tail                | `RAM[0x08]=5`         | (via tail, RAM[0x31]/[0x32] stale) |

**Common C-port + streaming-endpoint tail** — 0x07C5-0x0803 (runs for modes 1/2/5, NOT 3):
- `XDATA[0xFFF6] = 0x10` (IEPBBAX2 low, streaming EP buffer offset)
- `setb RAM[0x23].0, .1` (dead — codec-serial payload bits)
- `lcall 0x0E62` (codec commit, no-op)
- `RAM[0x08] = 5`, `lcall 0x0E20` (state hook)
- `CS8427[RAM[0x31]] = RAM[0x32]` via `lcall 0x0C45`
- `DMACTL1 |= 0xC0` (arm DMA channels 0+1)
- Zero `IEPBCTX1/BSIZ1` and `OEPBCTX2/BSIZ2` (reset stream-EP buffer counters)
- `IEPCNF1 (0xFF60) = 0xC5` (arm capture)
- `OEPCNF2 (0xFF98) = 0xC5` (arm playback)
- `setb RAM[0x23].2, .3` (dead — 48 kHz codec-serial bits)
- `lcall 0x0E62` (codec commit, no-op)

**No evidence of 88.2, 96, 176.4, 192 kHz support.** The only DMA-constant triplets found in the entire firmware are `0x20 4B 6A` and `0x0F A8 61`. C-port bit-rate registers (CPTBRRX=0xAC, CPTBRTX=0x25) are written once at boot in `fcn.0x08CB` and never touched again, which limits the master clock to one setting; the two DMA triplets are how 44.1 vs 48 gets differentiated.

## 4. Analog↔S/PDIF input switching

Actual leaf handlers (dispatched via RAM[0x0A] main-loop action codes 11 and 12):

**S/PDIF input (0x0466):**
```
clr  RAM[0x25].4      ; input = S/PDIF flag (read by GET Input Source)
setb RAM[0x22].6      ; mux bit 6 = 1 (physical analog mute?)
lcall 0x0E62          ; codec commit (no-op)
lcall 0x0F0C          ; mux commit (shifts RAM[0x22] to 74HC595)
mov  r7, RAM[0x08]    ; ApplyAudioMode(current mode) — preserves rate
lcall 0x0728
ljmp 0x0564
```

**Analog input (0x0478):**
```
setb RAM[0x25].4      ; input = analog flag
clr  RAM[0x22].6      ; mux bit 6 = 0
lcall 0x0E62
lcall 0x0F0C
mov  r7, #0x01        ; ApplyAudioMode(1) — FORCE 44.1 kHz internal
lcall 0x0728
ljmp 0x0564
```

**48 kHz analog finalizer (0x0492-0x049D)** — enforces CS8427 clock-source on analog input at 48k:
```
mov  r7, #0x02
lcall 0x0728          ; ApplyAudioMode(2) — 48k DMA
jnb  RAM[0x25].4, 0x04A0
lcall 0x057A          ; analog: CS8427[0x04]=0x41 + CS8427[0x12]=0x00
ljmp 0x0564
0x04A0:               ; S/PDIF path
mov  RAM[0x2C]=0x23, RAM[0x2D]=0x00
lcall 0x0594          ; CS8427[0x23]=0x00 (ch-A status byte 3) + CS8427[0x24]=0x80 (byte 4 valid)
```

**Interpretation:** The CS8427 is programmed differently only for the *clock source* (analog-locked vs S/PDIF-recovered), not for the sample rate. On analog input, `CS8427[0x04]=0x41` selects "clock from OMCK/OSCLK/OLRCK" and enables the transmit interpolator. On S/PDIF input, the channel-status bytes are updated (byte-4 bit 7 = "sampling frequency valid") to reflect the recovered rate. The CS8427's PLL locks to whichever clock is asserted; sample rate follows the C-port LRCLK for internal-clock modes, or the S/PDIF stream for slave mode.

## 5. C code for classy-mbox / mboxfw

Direct port of the above, ready to drop into `streaming.c` and a new `input.c`:

```c
/* streaming_set_rate — mirror Rev 20 mode 1/2/3 for internal 44.1/48/S-PDIF */
void streaming_set_rate(unsigned int hz, unsigned char slave)
{
    /* prelude — halt capture DMA, clear "48k codec" bits */
    DMACTL2 = 0;

    if (slave) {                              /* mode 3 — S/PDIF slave */
        /* 44.1-kHz-equivalent constants; rate actually follows S/PDIF */
        XDATA(0xFFE5) = 0x61; XDATA(0xFFE6) = 0xA8; XDATA(0xFFE7) = 0x0F;
        XDATA(0xFFF7) = 0x61; XDATA(0xFFF8) = 0xA8; XDATA(0xFFF9) = 0x0F;
        DMACTL1 = 0x06;
        /* skip common tail — S/PDIF drives C-port clocks itself */
        return;
    }

    if (hz == 48000) {                        /* mode 2 */
        XDATA(0xFFE5) = 0x6A; XDATA(0xFFE6) = 0x4B; XDATA(0xFFE7) = 0x20;
        XDATA(0xFFF7) = 0x6A; XDATA(0xFFF8) = 0x4B; XDATA(0xFFF9) = 0x20;
    } else {                                  /* mode 1 — 44.1 default */
        DMACTL1 = 0x0D;
        /* no DMASRC writes — mode 1 relies on power-on defaults */
    }

    /* common tail (0x07C5) */
    XDATA(0xFFF6) = 0x10;
    IEPBCTX1 = 0; IEPBSIZ1 = 0;
    OEPBCTX2 = 0; OEPBSIZ2 = 0;
    IEPCNF1 = 0xC5;                           /* arm capture EP */
    OEPCNF2 = 0xC5;                           /* arm playback EP */
    DMACTL1 |= 0xC0;                          /* arm DMA channels 0+1 */

    /* CS8427: analog-clock config for both 44.1 and 48 kHz internal */
    cs8427_write(0x04, 0x41);                 /* RUN=1, INC=1 */
    cs8427_write(0x12, 0x00);                 /* clear ch-A status byte 0 */
}

/* input_set_source — Rev 20 0x0466 / 0x0478 */
void input_set_source(int is_analog)
{
    if (is_analog) {
        g_state_25 |= (1 << 4);               /* RAM[0x25].4 = 1 */
        g_mux_22   &= ~(1 << 6);              /* RAM[0x22].6 = 0 */
        mux_commit();
        streaming_set_rate(44100, 0);         /* analog forces 44.1 internal */
    } else {                                  /* S/PDIF */
        g_state_25 &= ~(1 << 4);
        g_mux_22   |=  (1 << 6);
        mux_commit();
        streaming_set_rate(g_current_rate, 1);/* preserve rate, slave clock */
        /* also update CS8427 channel-status bytes for the reported rate */
        cs8427_write(0x23, 0x00);             /* ch-A status byte 3 */
        cs8427_write(0x24, 0x80);             /* byte 4: "sampling freq valid" */
    }
}
```

## 6. Gaps this doesn't close

- **UAC1 SET_CUR sampling-frequency** (standard-class rate change) is separate from Rev 20's vendor Digi SET Clock Source path — classy-mbox will need its own SET_CUR handler that calls `streaming_set_rate()` when the host writes to `EP1/EP2` SamplingFrequency control.
- **DMA descriptor / ping-pong / SOF handling** — Rev 20's Timer 0 ISR is trivial (per NOTES §"Timer 0 ISR"), the streaming EPs are set up with `IEPCNF*=0xC5` and DMA does the rest. But we haven't confirmed the EP-buffer wrap on isochronous ping-pong. Suggest a fork focused on the DMA-descriptor XDATA writes in `fcn.0x0E0F` / `fcn.0x0E20` (called from mode 2 setup).
- **Feedback endpoint** — Rev 20 doesn't declare one in the descriptor blob (needs separate ROM-descriptor trace); async playback timing on classy-mbox will need one or explicit adaptive-EP behavior.
- **CPTCFG / bit-rate re-clocking for rates ≠ 44.1/48** — impossible without further Rev 20 evidence; no other rates appear supported by the stock firmware.
