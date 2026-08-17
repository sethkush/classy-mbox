/*
 * Mbox 1 class-compliant firmware — main entry.
 *
 * Runs on TAS1020A 8051 core, loaded from I²C EEPROM (24C64 at addr 0x50)
 * by the TAS1020A boot ROM. Presents a standard USB Audio Class 1 device
 * (2 ch × 24 bit × 44.1/48 kHz) so no vendor driver is needed.
 */

#include "regs.h"
#include "cs8427.h"
#include "codec.h"
#include "buttons.h"
#include "mux.h"
#include "eeprom.h"
#include "telemetry.h"
#include "usb.h"
#if defined(MBOX_SERIAL_EEPROM) || defined(MBOX_PROVISION)
/* Provisioning builds take the record GEOMETRY from this header (base address,
 * lengths) without linking serialno.c -- they write the record, they do not
 * serve it. See the Makefile. */
#include "serialno.h"
#endif        /* g_dfu_request_pending */
#include "streaming.h"  /* streaming_set_rate() -- #197 boot clock bring-up */
#include "power.h"      /* g_work_code, work_dispatch() */

/* Timer-0 tick pending flag, set by isr_timer0 (isr.c). */
extern volatile __bit g_timer0_pending;

extern void hw_init(void);
extern void usb_init(void);
extern void usb_attach(void);
extern void usb_service(void);

/* The boot-time button-hold DFU trigger was REMOVED 2026-08-05.
 *
 * It never worked. BRICK_LOG records it being tried on three separate
 * incidents -- "Button-hold DFU produced nothing either. No software route
 * back in." (2026-08-03), "Button hold on replug had no visible effect.",
 * "button-hold DFU did nothing" -> I2C driver silent failure. Every actual
 * recovery this project has ever performed was the SDA short.
 *
 * It also was not the last resort it was assumed to be. The canonical
 * recovery needs no firmware cooperation at all: SDA short -> ffff:fffe
 * bulletproof-DFU -> flash safety_net_bootstrap.bin (dataType 0x03) -> replug
 * -> 0dba:1001 app-DFU -> flash the real image. That sequence is at the top of
 * BRICK_LOG.md, is hardware-proven, and has recovered a completely dead unit.
 *
 * So this path duplicated TLM_REQ_ENTER_DFU when USB works, and was redundant
 * with the SDA bootstrap when it does not -- while never once succeeding.
 * eeprom_smoke_test() and eeprom_read_byte() went with it; they had no other
 * caller. eeprom_write_byte() and eeprom_invalidate_signature() STAY: they are
 * what TLM_REQ_ENTER_DFU uses, and that is the trigger in daily use. */

/*
 * ISR forward declarations. SDCC's vector-table emitter runs in the
 * compilation unit that owns crt0 (main.c here) and only lays down a
 * jump at 0x0003/0x000B/etc when it sees the matching __interrupt(N)
 * declaration in the same TU. The bodies live in isr.c — these
 * declarations exist purely to plant the vector jumps.
 * (Without them: the linker leaves the vector bytes as 0xFF/random, so
 *  the first INT0/Timer0 firing jumps into the middle of some function
 *  and crashes the CPU.)
 */
void isr_int0(void)    __interrupt(0);
void isr_timer0(void)  __interrupt(1);
/* Forward decls for the 4 defensive RETI stubs in isr.c. SDCC only
 * plants vector-table LJMPs for __interrupt(N) declarations visible in
 * the compilation unit that owns crt0 (this one) — a body in isr.c
 * alone is not enough; the vector at 0x0013/0x001B/0x0023/0x002B stays
 * a 0xFF gap. Both Rev 20 and Rev 22 defend against these vectors.
 * Fork audit 2026-07-24. */
void isr_int1  (void)  __interrupt(2);
void isr_timer1(void)  __interrupt(3);
void isr_uart  (void)  __interrupt(4);
void isr_timer2(void)  __interrupt(5);

/* Phase-completion canaries — written at each boundary so sim_smoke
 * (and any future runtime verifier that can peek shared RAM) can prove
 * exactly which init phases ran. Different values per phase means we
 * can distinguish "hung in cs8427" from "hung in codec" from "hung in
 * usb_init" without a scope. Canary window is 0xFA00..0xFA03 in the
 * TAS1020A shared-memory area — sits BELOW our EP0 buffers (0xFA10)
 * so it can't collide with USB packet memory. NOVEL — reason: new
 * addresses under 0xFA10 aren't used by TI's engUsbInit or Rev 20's
 * boot init; verified by grepping both. */
#define CANARY_BASE     0xFA00
#ifdef MBOX_RELEASE
/* RELEASE: the canaries are read over EP0 or blinked on the panel, and both
 * readers are compiled out. */
#define CANARY(n, v)    do { } while (0)
#else
#define CANARY(n, v)    (*(volatile __xdata unsigned char *)(CANARY_BASE + (n))) = (v)
#endif
#define CANARY_MAIN     0xA1   /* main() entered */
#define CANARY_USB      0xA2   /* usb_init returned */
#define CANARY_HW       0xA3   /* hw_init returned */
#define CANARY_CS8427   0xA4   /* cs8427_boot_init returned */
#define CANARY_CODEC    0xA5   /* codec_init returned */
#define CANARY_LOOP     0xA6   /* about to enter main polling loop */

/* --- LED progress canary (diagnostic build: make CANARY_LED=1) -------
 *
 * The XDATA canary above is only readable over USB, which is useless
 * when USB is the thing that is broken. This one reports through the
 * front panel instead. It is what located the EP0 desync in safety_net
 * in two cycles after weeks of guessing.
 *
 * Stage ladder, blinked as TENS long flashes then UNITS short ones:
 *
 *    1  main() entered            10  usb_isr fired at all
 *    2  usb_init returned         11  VECINT gave a real source
 *    3  hw_init returned          12  VEC_RSTR seen
 *    4  boot-DFU button checked   13  VEC_SETUP seen
 *    5  EA = 1                    14  GET_DESCRIPTOR(Device) served
 *    6  usb_attach returned       15  SET_ADDRESS received
 *    7  cs8427_boot_init returned 16  USBFADR written
 *    8  codec_init returned       17  SET_CONFIGURATION received
 *    9  main loop entered
 *
 * Output is the 8-bit panel latch (P1.7 data / P1.5 clk / P1.6 latch),
 * toggled between 0xFF (all six source LEDs off) and 0xF6 (the two mic
 * LEDs on) — the two bytes Rev 20's own hw_init writes, so the control
 * lines 0x22.6/.7 stay high throughout and no new hardware state is
 * created. See firmware_stock/disasm/PANEL_LEDS.md.
 *
 * 2026-07-27 baseline being investigated: mboxfw drives the panel to the
 * two-mic state and holds, which means hw_init completed, yet the device
 * is entirely absent from USB — not even a half-enumerated entry. Stages
 * 6 through 9 bracket whether usb_attach actually ran and whether the
 * audio init after it wedges. */
#ifdef CANARY_LED

#define PANEL_DARK 0xFF
#define PANEL_LIT  0xF6

volatile __data unsigned char g_stage = 0;
volatile __data unsigned char g_last_breq = 0xEE;  /* 0xEE = no SETUP yet */
volatile __data unsigned char g_stalls = 0;
volatile __data unsigned char g_chunks = 0;
#define STAGE(n) do { if ((unsigned char)(n) > g_stage) g_stage = (n); } while (0)

static void canary_delay(unsigned char units)
{
    unsigned char u;
    volatile unsigned int i;
    for (u = 0; u < units; u++)
        for (i = 0; i < 1500; i++) { }
}

/* Blink one value as TENS long flashes then UNITS short ones. */
static void canary_emit(unsigned char n)
{
    unsigned char k, tens = n / 10, units = n % 10;
    for (k = 0; k < tens; k++) {
        mux_write(PANEL_LIT);  canary_delay(24);
        mux_write(PANEL_DARK); canary_delay(10);
    }
    for (k = 0; k < units; k++) {
        mux_write(PANEL_LIT);  canary_delay(6);
        mux_write(PANEL_DARK); canary_delay(10);
    }
}

/* Report THREE numbers per cycle, separated by medium gaps, with a long
 * gap before the sequence repeats:
 *
 *   1st group — g_stage      how far the best case got
 *   2nd group — g_last_breq  bRequest of the most recent SETUP (decimal)
 *   3rd group — g_stalls     how many requests we answered with STALL
 *   4th group — g_chunks     EP0 IN packets pushed for the LAST reply
 *                            (3 = device descriptor, 23 = full config)
 *
 * The stage ladder alone is a monotonic maximum and cannot say WHICH
 * request is failing. g_last_breq answers that directly: 6 = the host is
 * still asking for descriptors, 5 = it never got past SET_ADDRESS, 9 =
 * it reached SET_CONFIGURATION. g_stalls says whether we are actively
 * rejecting things or simply never replying. */
static void canary_blink_forever(void)
{
    MONO_OFF();            /* never assert the unverified 0x23.6 */
    for (;;) {
        canary_emit(g_stage);
        canary_delay(40);
        canary_emit(g_last_breq);
        canary_delay(40);
        canary_emit(g_stalls);
        canary_delay(40);
        canary_emit(g_chunks);
        canary_delay(110);
    }
}

#else
#define STAGE(n) do { } while (0)
#endif /* CANARY_LED */

void main(void)
{
    /* BOOT-ROM HANDOFF SNAPSHOT — must be the very first thing main() does.
     *
     * This closes WHAT_REMAINS_UNKNOWN.md §3a, which had become unanswerable by
     * observation: the question is whether the boot ROM leaves the EP0 Y buffer
     * counts non-zero when it hands over (a candidate for the measured ~12%
     * geometric loss of EP0 IN packets past the second), and usb_ep0_setup()
     * now clears both before any host can ask. The only way to recover the
     * handoff value is to sample it before we touch anything — so this sits
     * above the USBCTL = 0 below, which would otherwise destroy byte 4.
     *
     * Reads only; nothing is driven. Same pattern as the retired P1/P3 handoff samples
     * further down, which sample the ports before hw_init() drives them.
     *
     * Byte 2 also verifies the assumption behind hw_init()'s GLOBCTL |= 0x02:
     * that the boot ROM leaves GLOBCTL = 0x04. That value comes from TI
     * RomBoot.c:33's comment, not from this part, and the whole argument for
     * setting bit 1 by RMW rather than stock's outright 0x06 rests on it.
     *
     * Four bytes, not the whole handoff register file: each XDATA read costs
     * code and the budget has ~170 bytes left. The X counts are omitted because
     * block 7 reads them live and they are not the §3a question; USBIMSK is
     * omitted because datasheet §2.1.9 settles what happens to it (#152);
     * MEMCFG is omitted because hw_init only ORs SDW idempotently.
     *
     * NOVEL — reason: no reference firmware records its own handoff state;
     * stock has no equivalent because it never needed to ask. */

    /* DISCONNECT FIRST — defensive against boot-ROM-leftover USBCTL state.
     * Boot ROM's UsbDfu.c:699 zeroes USBCTL after DFU manifest, and cold
     * boot leaves USBCTL at hardware reset (0) — so on the expected paths
     * this is a no-op. Any deviation (e.g. boot ROM took a code path we
     * haven't audited that leaves USBCTL != 0) would race host enumeration
     * of boot ROM against our own attach. Rev 20 explicitly does this at
     * disasm 0x08E5 in its master init. Intentional assignment to
     * boot-ROM-owned SFR — POLICY §2 carve-out A. */
    USBCTL = 0;

    /* Sample the ports BEFORE hw_init() drives any of them, so a host
     * comparing these against the live reads in block 4 can tell a button
     * press from our own pin configuration.
     *
     * NOVEL — reason: the "source-1 button reads on P3.3" mapping is an RE
     * inference that has never fired in practice, and there is no reference
     * firmware behaviour to copy — neither Rev 20 nor the boot ROM records
     * port state anywhere. Reads only; no pin is driven. */

    CANARY(0, CANARY_MAIN);
    STAGE(1);

    /* The #172 pin-setup hoist and the DFU escape that needed it were both
     * REMOVED 2026-08-05, with the boot-button trigger itself.
     *
     * P3 = 0xFF and GLOBCTL |= 0x02 were duplicated here, ahead of usb_init(),
     * for one reason: so check_boot_dfu_button() could read the port before
     * anything that might hang. hw_init() writes both in stock's own position
     * (P3 at hw_init.c:53, GLOBCTL at :129) and always did -- these copies
     * were idempotent by design. With no escape to serve, they are dead.
     *
     * That also restores the plain form of the #47 invariant: usb_init() is
     * now unconditionally the first thing main() calls, with nothing between
     * entry and it that can halt or branch past it. The narrowed form that
     * verify_conn_reachable.py encoded -- "usb_init is always reached EXCEPT
     * after a confirmed header invalidate" -- is no longer needed. */

#ifdef MBOX_SERIAL_EEPROM
    /* #221: read the serial BEFORE usb_init(), because usb_init() brings the
     * USB engine up and the host can issue GET_DESCRIPTOR the moment it does.
     * Loading after that races enumeration against an I2C read, and the loser
     * is the device descriptor -- the one request that must carry the right
     * iSerialNumber. A failure here is not fatal: the index stays 0. */
    (void)serialno_load();
#endif

    /* usb_init() configures endpoints and buffers but does NOT attach.
     * Ordering below mirrors both stock firmwares exactly. */
    usb_init();
    tlm_phases |= TLM_PHASE_USB_INIT;
    CANARY(1, CANARY_USB);
    STAGE(2);

    hw_init();
    tlm_phases |= TLM_PHASE_HW_INIT;
    CANARY(2, CANARY_HW);
    STAGE(3);

    STAGE(4);

    /* Start Timer 0, then interrupts on, then attach — the Rev 20 order:
     *
     *   0ac8  SETB TR0        ; run Timer 0
     *   0aca  SETB EA
     *   0acc  USBCTL |= 0x80  ; attach
     *
     * Rev 22 the same at 0x0A72 / 0x0A74 / 0x0A76, and both do it again on
     * resume (Rev 20 0x0557).
     *
     * TR0 was never set anywhere in mboxfw. hw_init() writes TCON = 0x00,
     * which clears TR0 along with everything else, and nothing turned it back
     * on — so Timer 0 never counted, the timer ISR never fired, and
     * g_timer0_ticks has read 0 on every telemetry dump since it was added.
     * That went unnoticed because the main loop called buttons_poll()
     * unconditionally and so did not depend on the tick. Gating the poll on
     * the tick is what surfaced it: sim_smoke.sh breakpoints on
     * _buttons_poll and stopped being able to reach it. */
    TR0 = 1;
    EA = 1;
    STAGE(5);
    usb_attach();
    tlm_phases |= TLM_PHASE_ATTACH;
    STAGE(6);
    CANARY(5, CANARY_LOOP);

    /* Audio bring-up runs AFTER the attach. Rev 20 does it before, but
     * Rev 20's codec init is known-good; ours is not yet proven, and a
     * hang in either of these leaves the device unreachable if it happens
     * before we are on the bus. Because isr_int0 now services USB, the
     * host is answered throughout these two calls even if one wedges —
     * so a hang here costs audio, not the device.
     *
     * Move these back above the attach once both are hardware-proven. */
    /* ORDER CORRECTED 2026-07-31 (#167). codec_init() used to run AFTER
     * cs8427_boot_init(), which meant the 16-bit latch chain carrying the
     * CS8427's chip select (IRAM 0x25.7) and the external RESET (0x23.4) had
     * never been clocked when the ten register writes went out — both lines
     * sat at whatever the shift register happened to hold, undefined on a cold
     * boot and stale from the previous run on a warm one.
     *
     * Stock's order is the inverse and is deliberate: zero the word and publish
     * (Rev 20 fcn.0x080B @0x080C-0x0818), then release RESET and pulse the
     * select (@0x083E-0x0852), then write registers (@0x0855+). The release and
     * the pulse now live inside cs8427_boot_init() so this cannot be
     * reordered apart again; codec_init() only has to get the latch into a
     * known state first. */
    codec_init();
    tlm_phases |= TLM_PHASE_CODEC;
    CANARY(4, CANARY_CODEC);
    STAGE(7);

    cs8427_boot_init();
    tlm_phases |= TLM_PHASE_CS8427;

    /* #197. Bring the codec master clocks up HERE, at boot, which is what
     * stock does -- its power-up sequence runs ACGCTL |= 0xC0 before the
     * SETB 0x23.2 / SETB 0x23.3 unmute (Rev 20 0x080B-0x0852, Rev 22
     * 0x09B6-0x09F5). mboxfw used to leave ACGCTL alone until the first
     * stream, so the codec sat unclocked from boot until a host opened one.
     *
     * Two things follow. The gate pulse the main loop is about to schedule has
     * clocks to work against, which at boot it previously did not. And the
     * 2500-SOF wait now elapses during boot rather than 2.5 s into whatever
     * the host records first, so no take ever contains the pulse.
     *
     * 48000 is the boot default that g_internal_rate also carries. */
    streaming_set_rate(48000UL);
    CANARY(3, CANARY_CS8427);
    STAGE(8);


    STAGE(9);
#ifdef CANARY_LED
    /* Diagnostic build: blink the stage instead of polling buttons.
     * buttons_poll() also drives the panel latch, so the two would
     * fight over it. USB is serviced from the ISR either way. */
    canary_blink_forever();
#endif

    tlm_phases |= TLM_PHASE_MAIN_LOOP;
    for (;;) {
        /* Liveness: a changing counter distinguishes "running but silent"
         * from "wedged", which no static value can. */
        TLM_INC16(tlm_loop_count);

        /* #201. The analog reference needs ~16 s to charge (10 uF on each of
         * VREFL/VREFR, measured). Until it has, any offset calibration latches a
         * constant against a VCOM that is still moving -- which is the whole
         * cause of the #197 transient. 20000 SOFs is 20 s with margin.
         *
         * Sticky, and tested here rather than in the SOF ISR: sof_count WRAPS
         * (it saturated until 0x0047, which silently killed every SOF-based wait
         * in the firmware), so a bare `> 20000` would go false again after 65 s
         * and let a late calibration count as trustworthy when it is not.
         *
         * The HIGH BYTE alone is the test, which costs a byte compare instead
         * of a 16-bit one -- nine bytes, and that is what this had to find to
         * fit alongside the per-unit serials.
         *
         * 0x75 << 8 = 30000 SOFs = 30 s, not the 20 s the settling measurement
         * strictly needs. The margin is free: the calibration this gates is the
         * ONE that stands for the whole power-up, so landing it early is
         * permanent, while landing it late costs only that a capture opened in
         * the first 30 s pays the 183 ms -- which is exactly today's behaviour,
         * so there is no regression to trade against. The settle table is flat
         * by 16 s (-100.5 dBFS at 16 s against -89.4 at 8 s).
         *
         * Note sof_count is exposed in NO telemetry block, so this threshold
         * cannot be checked over the wire. It is checked from the audio instead:
         * the first capture of a power-up shows ~8800 leading zeros and later
         * ones show none. If every capture shows 8800, this never fired. */
        if (!g_ref_settled && (unsigned char)(tlm.sof_count >> 8) >= 0x75u) {
            g_ref_settled = 1;
        }

        /* #202. Spend the calibration HERE, with no stream open, so the first
         * capture of a power-up is clean like every later one. codec_apply_mute()
         * raises 0x23.2 once g_ref_settled is set and then holds it, so this one
         * call is the rising edge and there is never another.
         *
         * Gated on g_path_enabled == 0 deliberately. If a stream is already open
         * when the threshold trips, RST is ALREADY high -- raised at that open,
         * possibly seconds after boot with the reference nowhere near settled --
         * and publishing here would latch that bad calibration as the good one.
         * That is exactly the 0x004B failure: one calibration, taken at an
         * arbitrary moment, latched forever. Waiting for the stream to close
         * costs nothing, because until then 0x004F's per-open path is still in
         * force and still recalibrating at every open.
         *
         * If the C-port does NOT free-run (FINDING_197_RESOLVED #3), this call
         * still raises the pin but the part is unclocked, so no calibration
         * completes and the next stream open behaves as it does today. Null
         * result, not a regression -- see the note in codec_apply_mute(). */

        /*
         * Two-rate loop, mirroring Rev 20 0x0AD3-0x0B0F (Rev 22
         * 0x0A7D-0x0AB9):
         *
         *   0ad3  JB 0x20,0x0ADF     ; RAM[0x24].0 — timer-0 tick pending?
         *   0ad6  MOV A,0x0A         ; else: any deferred work code?
         *   0ad8  JZ 0x0AD3
         *   0ada  LCALL 0x02EE       ;   dispatch it
         *   0add  SJMP 0x0AD3
         *   0adf  LCALL 0x0ED5       ; tick: poll the buttons
         *   0ae3  JNB ACC.0,0x0AEC   ;   publish only if it acted
         *   0ae6  LCALL 0x0F0C / 0x0E62
         *   0aec  ... P3.1 S/PDIF presence edges -> work codes 0x0B / 0x0C
         *   0b0d  CLR 0x20           ; consume the tick
         *
         * The button poll is gated on the tick; deferred work is not, so a
         * suspend request is serviced promptly while the panel is sampled at
         * the timer rate. mboxfw called buttons_poll() unconditionally on
         * every pass of a loop that spins at CPU speed, which polls P3 at
         * hundreds of kHz and re-shifts the panel chain on every contact
         * bounce.
         *
         * USB is serviced from isr_int0 ONLY — see isr.c. Calling
         * usb_service() here as well would let the ISR re-enter it while the
         * loop is part-way through, and SDCC gives non-reentrant functions
         * static overlay locals, so the re-entry would corrupt the EP0
         * transfer state. Rev 20 has the same division of labour: its INT0
         * handler dispatches USB, its main loop handles deferred panel/codec
         * actions.
         *
         * NOT yet ported from the block above: the P3.1 S/PDIF-presence edge
         * detection at 0x0AEC-0x0B0A, which posts work codes 0x0B and 0x0C to
         * slave the clock to an incoming S/PDIF stream. That is task #145 and
         * needs the clock-mode routines it dispatches to.
         */
        if (!g_timer0_pending) {
            if (g_work_code) {
                work_dispatch();
            }
        } else {
            buttons_poll();
            g_timer0_pending = 0;
        }

#ifdef MBOX_PROVISION
        /* #226 provisioning write, deferred here for the same reason the
         * enter-DFU write below is: the 24C64 program cycle is a ~5 ms
         * busy-wait and the SETUP handler runs in interrupt context.
         *
         * THE BOUNDS CHECK IS REPEATED HERE, having already been made when the
         * request was latched. That is not belt-and-braces for its own sake:
         * this is the ONLY eeprom_write_byte() call in the firmware whose
         * address comes from the host at all, and the byte it could otherwise
         * reach is the EEPROM header checksum at 0x0000 -- the exact byte
         * enter-DFU deliberately destroys. Getting it wrong does not produce a
         * bad serial, it produces a unit that boots to DFU forever. Two
         * instructions against a bench visit that cannot be made remotely.
         *
         * The base is added HERE, from a constant. EE_SERIAL_LO is 0x00 and
         * the offset is < 27, so the sum cannot carry and the high byte is
         * always EE_SERIAL_HI -- no host value can name an address outside
         * 0x1F00..0x1F1A. */
        if (g_prov_diag_pending) {
            /* #226: the same instrumented read the SETUP handler used to do
             * inline, run here instead. Context is the only difference, and it
             * is the thing being measured -- see the dispatch in usb.c. */
            if (g_prov_diag_freq == 1)      I2C_STA &= ~0x10;  /* TI I2c.c I2CAccess line 44 */
            else if (g_prov_diag_freq == 2) I2C_STA |=  0x10;  /* TI I2c.c I2CAccess line 44 */
            eeprom_read_diag(g_prov_diag_hi, g_prov_diag_lo, g_prov_diag_buf);
            g_prov_diag_pending = 0;
        }

        if (g_prov_pending) {
            if (g_prov_offset < SERIAL_HDR_LEN + SERIAL_MAX_CHARS) {
                (void)eeprom_write_byte(
                        EE_SERIAL_HI,
                        (unsigned char)(EE_SERIAL_LO + g_prov_offset),
                        g_prov_value);
            }
            /* Cleared LAST, so the STALL-if-busy test in the SETUP handler
             * covers the whole program cycle and not just the latch. */
            g_prov_pending = 0;
        }
#endif

        /* Enter-DFU, deferred here from the class-request handler so the
         * zero-length status packet drains before we spend ~30 ms in I2C
         * program-cycle waits. See handle_digi_enter_dfu() in usb.c. */
        if (g_dfu_request_pending) {
            /* Drop off the bus first: the signature write is the point of
             * no return for this image, and a device that answers SETUPs
             * after it has decided to halt is worse than one that is
             * plainly gone. Same reasoning and same SFR as sigkill's
             * main(). Intentional assignment to a boot-ROM-owned SFR —
             * POLICY §2 carve-out A.
             *
             * NOVEL — reason: no reference firmware disconnects mid-run,
             * because none of them invalidate their own signature; the
             * write itself is byte-identical to the top-of-main
             * disconnect at Rev 20 fcn.0x08e2 @ 0x08e5. */
            USBCTL = 0;
            EA = 0;

            /* Twice, for the same reason sigkill does: a failed write
             * leaves the signature intact, which costs one replug and
             * nothing else. */
            (void)eeprom_invalidate_signature();
            (void)eeprom_invalidate_signature();

            /* Halt. NOT RESET_TO_BOOT_ROM() — clearing MEMCFG.SDW from
             * program RAM unmaps the running code (see regs.h). The
             * replug the user performs next IS the power cycle, and the
             * boot ROM then reads a bad signature and enters DFU. */
            for (;;) { }
        }
    }
}
