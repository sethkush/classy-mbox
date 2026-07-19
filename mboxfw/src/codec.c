/*
 * Codec / audio-path state — Rev 20 fcn.0x0970 (init) + fcn.0x0E62
 * (state adjuster) + fcn.0x0E74 (16-bit codec bit-serial write).
 *
 * The codec + input mux + phantom-power state lives in three IRAM bytes
 * on Rev 20:
 *   RAM[0x22] — mux bits (74HC595 payload, written by mux_write())
 *   RAM[0x23] — codec control-word high byte (MSB shifted first)
 *   RAM[0x25] — codec control-word low  byte + mode flags used by the
 *               state adjuster
 *
 * We mirror them as g_mux_state (in mux.c), g_codec_state_23, and
 * g_codec_state_25. Rev 20 uses bank-0 R5 as a temp for the bit-shift
 * loop; we express the same idiom in plain C so SDCC picks its own
 * register allocation.
 *
 * Wire protocol on the codec side (P1 pins):
 *   P1.0 = SDIN (data), rising edge of P1.2 samples
 *   P1.1 = LATCH (rising edge commits the 16 shifted bits)
 *   P1.2 = SCLK (clock)
 *
 * References:
 *   firmware_stock/disasm/rev20_flat.asm
 *     fcn.0x0970 init sequence           : lines 1193–1201
 *     fcn.0x0E62 state adjuster          : lines 1880–1892
 *     fcn.0x0E74 16-bit bit-serial write : lines 1893–1921
 */

#include "regs.h"
#include "mux.h"
#include "codec.h"

/* RAM[0x23] high byte and RAM[0x25] low byte of the codec control word.
 * Externally visible so future control-change handlers (source cycle,
 * phantom toggle, sample-rate switch) can flip the individual bits and
 * then re-commit. Rev 20 uses the same shared-state idiom. */
__data unsigned char g_codec_state_23 = 0;
__data unsigned char g_codec_state_25 = 0;

/*
 * Port of Rev 20 fcn.0x0E62 (entry mid-function at rev20_flat.asm:1885).
 *
 * Pure state-machine adjuster: forces RAM[0x22] bit 2, then computes
 * RAM[0x22] bit 6 from RAM[0x25] bits 4 and 5. The tail logic at
 * 0x0E64..0x0E73 collapses to:
 *     bit 22.6 = (bit 25.4 == 0) && (bit 25.5 == 0)
 * i.e. only both-clear leaves the mux P1.6 line asserted.
 */
static void codec_state_adjust(void)
{
    g_mux_state |= (unsigned char)0x04;              /* setb 0x22.2 */

    if (g_codec_state_25 & 0x10) {                   /* 0x25.4 */
        g_mux_state &= (unsigned char)~0x40;         /* clr 0x22.6 */
    } else {
        g_mux_state |= (unsigned char)0x40;          /* setb 0x22.6 */
    }
    if (g_codec_state_25 & 0x20) {                   /* 0x25.5 */
        g_mux_state &= (unsigned char)~0x40;         /* clr 0x22.6 */
    }
}

/*
 * Shift one byte out on P1.0 (SDIN), MSB first, clocked by a rising then
 * falling edge on P1.2 (SCLK). Rev 20's rl-a inner loop at 0x0E85..0x0E9B
 * decodes to exactly this pattern.
 */
static void codec_shift_byte(unsigned char b)
{
    unsigned char i;
    unsigned char v = b;

    for (i = 0; i < 8; i++) {
        if (v & 0x80) {
            P1 |= P1_CODEC_SDIN_MASK;      /* orl 0x90, #0x01 */
        } else {
            P1 &= (unsigned char)~P1_CODEC_SDIN_MASK; /* anl 0x90, #0xfe */
        }
        P1 |= P1_CODEC_SCLK_MASK;          /* orl 0x90, #0x04 (clock rise) */
        P1 &= (unsigned char)~P1_CODEC_SCLK_MASK; /* anl 0x90, #0xfb (fall) */
        v <<= 1;
    }
}

/*
 * Port of fcn.0x0E74 top-half — the actual 16-bit bit-serial write:
 *
 *   1. Set r5 = RAM[0x23]; shift 8 bits.
 *   2. Set r5 = RAM[0x25]; shift 8 bits.
 *   3. Pulse P1.1 (LATCH) high then low to commit.
 *
 * Rev 20 uses RAM[0x26].0 as a "which byte am I on?" flag inside a
 * single loop; the C version below just calls codec_shift_byte twice —
 * same emitted signal, half the register plumbing.
 *
 * Caveat: byte-level scanning of Rev 20 finds ZERO callers of the
 * matching routine at 0x0E74 (see NOTES.md § "fcn.0x0E74 is dead
 * code"). The codec chip is autoconfigured from its I²S clock signals
 * and the P1.0/P1.1/P1.2 shift never actually reaches control-word
 * logic on real hardware. We keep this call anyway to mirror Rev 20's
 * intended-but-unused behavior — cost is a few dozen cycles per state
 * change and it means future codec revisions that DO listen to the
 * shift-in port will just work.
 */
void codec_commit(void)
{
    codec_state_adjust();
    mux_write(g_mux_state);

    codec_shift_byte(g_codec_state_23);   /* high byte first (MSB) */
    codec_shift_byte(g_codec_state_25);   /* low  byte second     */

    P1 |= P1_CODEC_LATCH_MASK;             /* orl 0x90, #0x02 */
    P1 &= (unsigned char)~P1_CODEC_LATCH_MASK; /* anl 0x90, #0xfd */
}

void codec_init(void)
{
    /* fcn.0x0970: clr 0x22.0, clr 0x22.3, clr 0x23.6 */
    g_mux_state    &= (unsigned char)~0x01;   /* bit 0: mux data-A */
    g_mux_state    &= (unsigned char)~0x08;   /* bit 3: mux mode select */
    g_phantom_48v   = 0;                      /* RAM[0x23].6 */

    /* fcn.0x0970 next: lcall 0x0F0C — kick the mux to publish RAM[0x22]. */
    mux_write(g_mux_state);

    /* fcn.0x0970 tail: clr RAM[0x25], clr RAM[0x23], lcall 0x0E62.
     * Rev 20's fcn.0x0E62 ends with `ret` at 0x0E73 — it does NOT chain
     * into 0x0E74. So init only runs the state adjuster; the actual
     * 16-bit codec shift only happens on control-change events (source
     * cycle, phantom toggle, rate switch). Match that exactly here. */
    g_codec_state_23 = 0;
    g_codec_state_25 = 0;
    codec_state_adjust();
}
