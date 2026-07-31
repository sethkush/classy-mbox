/*
 * CS8427 driver — 3-wire SPI on P1.3 (CCLK) / P1.4 (CDIN), chip-selected by
 * IRAM 0x25.7 published through the 16-bit codec latch chain.
 *
 * REWRITTEN 2026-07-31 (#157, #166, #167). What was here before framed the
 * register writes as I²C — START/STOP conditions and an ACK slot per byte —
 * and never touched the chip select at all. Three things were wrong with that,
 * and they compound:
 *
 *   1. Stock speaks SPI, not I²C. Rev 20 `cs8427_ctl_write` @0x0C45 (Rev 22
 *      @0x0C31, which Ghidra independently names `spi3wire_write_3bytes`) has
 *      no START, no STOP and no ACK slot: 24 clocks for three bytes, MSB
 *      first, framed by the select. See FINDING_cs8427_is_spi_not_i2c.md.
 *
 *   2. The select was never driven, so the part never saw the transition that
 *      *chooses* SPI. DS477F5 §9: "SPI mode is selected if there is a high to
 *      low transition on the AD0/CS pin after the RST pin has been brought
 *      high." Holding it static is the datasheet's own description of
 *      selecting I²C mode instead. `tools/sim_cs8427_bitstream.py` shows what
 *      the old code actually shifted in if the part *was* on SPI: one
 *      281-bit transaction whose first byte is 0x90 where 0x20 is required,
 *      with no chance to resync because the select never rose.
 *
 *   3. None of it could have worked anyway, because mboxfw never released the
 *      external-chip RESET (IRAM 0x23.4). DS477F5 §15.1: "When RST is low, the
 *      CS8427 enters a low power mode and all internal states are reset,
 *      including the control port and registers, and the outputs are muted."
 *      See FINDING_cs8427_held_in_reset.md.
 *
 * So the framing fix alone would have fixed nothing, and shipping it without
 * the select and the reset would have been a fourth round of the same mistake.
 * All three are here, in stock's order.
 */

#include "regs.h"
#include "cs8427.h"
#include "codec.h"

#define CS8427_ADDR_WRITE   0x20    /* 0010000b chip address + R/W=0 (DS477F5 §9.1) */

/*
 * Rev 20 spins RAM[0x2E] down from 0xFF (`MOV 0x2e,#0xff` / `DJNZ 0x2e,$`) at
 * 0x0812, 0x082B, 0x0838, 0x0845 and between register writes — roughly 256
 * machine cycles each.
 *
 * `i` MUST be volatile. Without it SDCC proves the function has no observable
 * effect and deletes every CALL SITE while leaving the body in the image, so
 * the listing gives no hint that the delays vanished. That happened once
 * already and cost a hardware round trip — see FINDING_delay_calls_elided.md.
 */
static void settle_delay(void)
{
    volatile unsigned char i = 0xFF;
    do { } while (--i);
}

/*
 * Assert / release the chip select. Stock drives it through the codec latch,
 * not a port pin: Rev 20 `CLR 0x2f; LCALL 0x0E62` @0x0C4F-0x0C51 to assert and
 * `SETB 0x2f; LCALL 0x0E62` @0x0C8D-0x0C8F to release. Active low.
 */
static void cs8427_select(void)
{
    g_codec_state_25 &= (unsigned char)~CODEC25_CS8427_CS_N;
    codec_write_word();            /* Rev 20 fcn.0x0C45 @ 0x0C51 */
}

static void cs8427_deselect(void)
{
    g_codec_state_25 |= CODEC25_CS8427_CS_N;
    codec_write_word();            /* Rev 20 fcn.0x0C45 @ 0x0C8F */
}

/*
 * One byte, MSB first, one rising CCLK edge per bit. Rev 20's loop at
 * 0x0C54-0x0C75 rotates the byte left and tests ACC.0, which is MSB-first;
 * data is presented on P1.4 while the clock is low and the CS8427 samples on
 * the rising edge (DS477F5 §9.1). No ACK slot — that was the I²C artefact.
 */
static void cs8427_shift_byte(unsigned char b)
{
    unsigned char i;
    unsigned char v = b;

    for (i = 0; i < 8; i++) {
        if (v & 0x80) {
            P1 |= P1_CS8427_SDA_MASK;               /* Rev 20 fcn.0x0C45 @ 0x0C66 */
        } else {
            P1 &= (unsigned char)~P1_CS8427_SDA_MASK; /* Rev 20 fcn.0x0C45 @ 0x0C6B */
        }
        P1 |= P1_CS8427_SCL_MASK;                   /* Rev 20 fcn.0x0C45 @ 0x0C6E */
        P1 &= (unsigned char)~P1_CS8427_SCL_MASK;   /* Rev 20 fcn.0x0C45 @ 0x0C71 */
        v <<= 1;
    }
}

void cs8427_write(unsigned char reg, unsigned char value)
{
    cs8427_select();
    cs8427_shift_byte(CS8427_ADDR_WRITE);
    cs8427_shift_byte(reg);
    cs8427_shift_byte(value);
    cs8427_deselect();
}

/*
 * Bring the external chips out of reset and put the CS8427 on SPI, then run
 * the register sequence. Ports Rev 20 `audio_path_reconfig_ext_chips`
 * @0x080B-0x0855 (Rev 22 @0x09B6-0x0A00).
 *
 * The reset release and the select pulse live INSIDE this function on purpose.
 * They used to be nowhere at all, and the reason they were nowhere is that
 * main() called cs8427_boot_init() before codec_init(), so the latch carrying
 * both lines had never been clocked when the register writes went out (#167).
 * Keeping them here makes that ordering impossible to lose again.
 */
void cs8427_boot_init(void)
{
    /* Stock guards this whole routine on IRAM 0x25.6 at each of its four call
     * sites — `JB 0x2e,<skip>; LCALL 0x080b` at Rev 20 0x038F/0x0392 (interface
     * 1 alt), 0x0416/0x0419 (interface 2 alt), 0x035D/0x0360 (clock mode) and
     * 0x04C7. The guard lives at the call site there and inside the function
     * here, which is equivalent and leaves one place to get it right.
     *
     * This is not just an optimisation. do_suspend() zeroes the codec word,
     * which CLEARS this bit — that is precisely how stock re-arms the bring-up
     * so the next stream start pulls the external chips back out of reset
     * after a resume. Without the guard-and-recall pair, a suspend would leave
     * RESET asserted for good. */
    if (g_codec_state_25 & CODEC25_BRINGUP_DONE) {
        return;
    }
    /* Rev 20 fcn.0x080B @ 0x0810 */
    g_codec_state_25 |= CODEC25_BRINGUP_DONE;

    /* Rev 20 @0x083E-0x0842: release the chip select AND the external RESET in
     * one publish, then let them settle. RESET is active low, so setting the
     * bit releases it. This is the write mboxfw never had — without it the
     * CS8427's control port and registers are held in reset and nothing below
     * can stick. */
    g_codec_state_25 |= CODEC25_CS8427_CS_N;   /* Rev 20 fcn.0x080B @ 0x083E */
    g_codec_state_23 |= CODEC23_RESET_N;       /* Rev 20 fcn.0x080B @ 0x0840 */
    codec_write_word();                        /* Rev 20 fcn.0x080B @ 0x0842 */
    settle_delay();                            /* Rev 20 fcn.0x080B @ 0x0845 */

    /* Rev 20 @0x084B-0x0852: a bare select pulse with no data. This is the
     * high->low transition on AD0/CS that, per DS477F5 §9, selects SPI mode
     * now that RST is high. It is why stock can speak SPI at all. */
    cs8427_select();                           /* Rev 20 fcn.0x080B @ 0x084B-0x084D */
    cs8427_deselect();                         /* Rev 20 fcn.0x080B @ 0x0850-0x0852 */
    settle_delay();

    /* The ten register writes, Rev 20 0x0855-0x08A2 (Rev 22 0x0A00-0x0A3x), in
     * order. Register numbers and values are byte-identical to stock;
     * verify_cs8427.py pins them.
     *
     * Names corrected 2026-07-30 against reference/cs8427/alsa_cs8427.h — they
     * were shifted by one, so 0x01 read as "chip control 2" and 0x11 as
     * "interrupt mask". Wrong names in a driver are how the next person
     * reasons wrongly about it. */
    cs8427_write(0x04, 0x00);  settle_delay();  /* CLOCKSOURCE = 0, RUN clear */
    cs8427_write(0x13, 0x10);  settle_delay();  /* UDATABUF */
    cs8427_write(0x04, 0x00);  settle_delay();  /* CLOCKSOURCE = 0 again */
    /* CLOCKSOURCE = RUN. DS477F5 §15.1: "Writing a 1 to the RUN bit will then
     * cause the part to leave the low power state and begin operation. After
     * the PLL has settled, the AES3 and serial audio outputs will be enabled."
     * Default is 0, so until this lands the part is silent. */
    cs8427_write(0x04, 0x40);  settle_delay();  /* CLOCKSOURCE = RUN set */
    /* CONTROL1 = 0x01: SWCLK=0 so RMCK outputs the RECOVERED clock (what clock
     * mode 1 slaves the codec to), TCBLDIR=1 (TCBL an output), INTMASK=0 so INT
     * is active high — and note reg 0x09 INT1MASK is never written by stock or
     * by us, so no interrupt source is ever unmasked and INT can never assert. */
    cs8427_write(0x01, 0x01);  settle_delay();  /* CONTROL1 */
    /* CONTROL2 = 0x20: HOLD = 01 = replace the sample with ZERO on a receiver
     * error; RMCKF=0 (256*Fsi); stereo receiver and transmitter. */
    cs8427_write(0x02, 0x20);  settle_delay();  /* CONTROL2 */
    /* DATAFLOW = 0x0C: TXD=01, SPD=10 = CS8427_SPDAES3RECEIVER — the serial
     * audio OUTPUT port carries the AES3 receiver, i.e. S/PDIF in. */
    cs8427_write(0x03, 0x0C);  settle_delay();  /* DATAFLOW */
    /* SERIALINPUT / SERIALOUTPUT = 0x05: SOMS=0 (slave — OSCLK/OLRCK are
     * inputs, driven by the TAS), SODEL=1 and SOSPOL=1, the I²S convention.
     * SODEL defaults to 0, which would put the MSB one bit clock early. */
    cs8427_write(0x05, 0x05);  settle_delay();  /* SERIALINPUT */
    cs8427_write(0x06, 0x05);  settle_delay();  /* SERIALOUTPUT */
    cs8427_write(0x11, 0xFF);                   /* RECVERRMASK: all sources */
}
