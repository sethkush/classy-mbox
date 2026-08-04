# The front-panel buttons are active-HIGH, and P3PUDIS is what makes them work

2026-08-03. mboxfw's buttons are dead on hardware; stock's work on the same
unit, minutes apart. This is why, and every step is from `decomp/` or from a
hardware observation — nothing here is inferred from `rev20_flat.asm`.

## 1. The measurement

Mbox A, mboxfw build 0x0011, `MBOX_PID=0x2000`:

  * `P3` rests at `0xFA`. Bits 3, 4 and 5 — the three buttons — read **1**.
  * With the channel-1 button **held down**, three consecutive live reads of
    P3 over telemetry still return bit 3 = **1**.
  * `g_mux_state` never leaves `0xF6`; `host mux sets=0 rejected=0`.

The pin does not move. This is not a state-machine bug, a debounce bug, or a
polarity bug in `buttons_poll()` — the input never changes, so no polarity
could have helped.

Then stock Rev 20 was flashed back onto the same unit and the buttons cycled
mic → line → inst correctly. Same board, same buttons, same wiring.

## 2. Both stock images idle those pins LOW — proved from the image

`p3_button_scan` (Rev 20 `0x0ED5`, Rev 22 `0x0F31`) fires a handler when the
**previous** sample of the pin was 0 and the **current** sample is 1:

    jb    0x05,a_btn$     ; prev P3.5 set   -> no edge
    mov   a,r5
    jnb   0xe5,a_btn$     ; cur  P3.5 clear -> no edge
    lcall _toggle_bit1e

The previous-sample shadow is IRAM `0x20`. Keil's `?C_INITSEG` table
(`cand/c51_initseg_table.c`, byte-matched) contains the record

    .db 0x01, 0x20, 0x00      ; 1 byte at IRAM 0x20 = 0x00

so the shadow is **zero** on the first scan after boot. (The same table's
`01 08 03` record — 1 byte at IRAM 0x08 = 3 — matches `hw_master_init`'s
`mov 0x08,#0x03`, which is what pins the record format as count/address/value
rather than count/address-pair.)

Therefore, on the first pass through the scan, `prev` is 0 for all three
buttons. **If those pins idled high, all three handlers would fire on the very
first scan of every boot**: channel A would step MIC→LINE, channel B would step
MIC→LINE, and mono would toggle, before the user touched anything.

`hw_master_init` seeds the panel word to `0xF6` = source pattern 6 on both
channels, and the hardware is observed to boot to **MIC** and stay there until
a button is pressed (Seth, repeatedly, most recently 2026-08-03 on stock Rev
20). The boot-time triple-fire does not happen.

The only way both facts hold is that P3.3/P3.4/P3.5 read **0** when no button
is pressed.

**The buttons are active-high. The edge lands on PRESS, not release.**

`cand/p3_button_scan.c` said "hw_master_init writes P3 = 0xFF, so the pins idle
high and a press pulls them low; the action therefore lands on button
*release*." It also said, correctly, "That is what the encoding says; I have not
checked it on hardware." The encoding was read right and the conclusion drawn
from it was wrong: `P3 = 0xFF` sets the port *latch*, which is what makes the
pin an input; it does not decide what the external network does with it.

## 3. Why mboxfw's read is stuck high: P3PUDIS

`GLOBCTL` bit 1 is **P3PUDIS** (§6.5.7.4): "Pullup resistor disable. If set to
1, disables on-chip pullup resistors on P3 GPIO pins."

  * **Stock sets it.** `hw_master_init` writes `GLOBCTL = 0x06` = `LPWR |
    P3PUDIS` (Rev 20 `0x08FE`, Rev 22 `0x081F`, reached by `INC DPTR` from the
    MEMCFG write — see `FINDING_globctl_bit1_missed.md`). Internal pull-ups
    **off**, so the board's own pull-downs hold the button pins at 0 and a press
    drives them to 1.
  * **mboxfw does not.** The boot ROM leaves `GLOBCTL = 0x04` (measured,
    telemetry block 8 byte 2) and mboxfw never writes the register. Internal
    pull-ups **on**. They overpower the external pull-downs, the pins sit at 1
    permanently, and no press can be seen.

That is exactly the measurement in §1, including the detail that P3.0 and P3.2
*do* read 0 in the same sample: those two pins are actively driven low by
something on the board, hard enough to beat the internal pull-up. The button
pins are only passively pulled, so they lose.

`regs.h` has carried `/* Front-panel buttons on P3 — active-low with pull-ups.
*/` since the port was written, and `buttons.c` repeated it. Both are wrong,
and they are why the pull-up disable looked optional.

## 4. This also explains the build 0x0010 "silent USB" bisect — #169

Build 0x0010 shipped `GLOBCTL |= 0x02` and never attached; 0x0011 dropped the
line and attached in 7 s. That was recorded as "P3PUDIS makes the device silent
on USB", which never made mechanical sense and was contradicted by stock, which
sets P3PUDIS and enumerates fine.

`check_boot_dfu_button()` reads:

    unsigned char held = 1;
    for (i = 0; i < 0x5000; i++)
        if (P3 & P3_BTN_CH1_MASK) { held = 0; break; }
    if (held) { invalidate the header; for(;;){} }

so `held` means **the pin read LOW**, which is the active-low premise. Run that
against the corrected polarity:

  * **P3PUDIS clear** (builds 0x0011 and later): pull-ups on, pin stuck at 1,
    the loop breaks on the first iteration, `held = 0`, **always**. The escape
    is not merely unreliable — it is unreachable. This is why holding the
    button at boot has never once worked, in any position the call has occupied.
  * **P3PUDIS set** (build 0x0010): pull-downs win, the idle pin reads 0, the
    loop never breaks, `held = 1` **with no button pressed**. The firmware
    invalidates its own EEPROM header and spins forever without attaching.

"Silent on USB, never attaches" is precisely what that produces. The bisect was
sound and its interpretation was inverted: build 0x0010 was not silenced by
P3PUDIS: it was silenced by an active-low button check meeting an active-high
button. It was also not passively silent — it was rewriting the EEPROM header
on the way down.

#169 is answered. The bit is required, not forbidden. What has to change with
it is the sense of the read.

## 5. What this costs

Three things were wrong at once and covered for each other:

  * the polarity (active-low vs active-high),
  * the pull-up configuration (P3PUDIS never set),
  * the boot-escape sense (inverted).

With P3PUDIS clear, the wrong polarity is invisible — the pin never moves, so
nothing fires and nothing bricks. That is the state mboxfw shipped in, and it
reads as "buttons not wired up yet" rather than as a bug. Setting P3PUDIS alone,
without fixing the read, is the 0x0010 brick. Fixing the read alone, without
P3PUDIS, changes nothing observable. Only both together do anything.

## 6. Consequences for the code

  1. `hw_init()` sets `GLOBCTL |= 0x02`, in stock's position — before the
     codec-port block, long before CPTEN.
  2. `check_boot_dfu_button()` treats **HIGH** as held, and additionally
     refuses to act when *all three* button pins read high at once, which is
     the signature of P3PUDIS not having taken. A real press is one button.
  3. The escape must be sampled **after** the P3PUDIS write, not before it.
  4. `buttons_poll()`'s `changed & now` was already the low→high edge and is
     unchanged; only its name and its comments were wrong — it detects a
     press.
  5. `prev_p3` seeds to `0x00`, matching stock's zeroed shadow, instead of
     `0xFF`.
  6. `regs.h`'s "active-low with pull-ups" comment is corrected.

## 6b. Confirmed on hardware, 2026-08-03, build 0x0016

Flashed to Mbox A. Block 4 carries the before and after in one line:

    P3 live=0xC2   boot=0xFF

`boot` is sampled before `hw_init` with the internal pull-ups still on — all
high, the stuck state. `live` is after `GLOBCTL |= 0x02` releases them: bits
3/4/5 rest LOW, exactly as §2 predicted from the zeroed shadow. Build 0x0011
read 0xFA with those bits stuck at 1 whatever was pressed.

Behaviour, in two steps so the count is pinned as well as the path:

  * source-1 pressed, LEDs cycled, block 9 went 0xF6 -> 0xF5 (ch1 = line).
    The whole chain works: pin, edge detect, source state machine, mux
    publish, panel LEDs.
  * from `line`, **exactly one** press -> `inst`. Presses are counted 1:1;
    the poll rate filters contact bounce, so the press-triggered edge does
    not double-fire. (Stock has no debounce either, so had it double-fired,
    stock would too.)

The boot-DFU escape also did not false-fire: the device attached normally,
which is precisely what build 0x0010 could not do.

## 7. What is still not proved

That the board uses pull-downs on these three pins is an inference from the
pin behaviour, not from a schematic or a meter. What is proved is the
behaviour: stock (P3PUDIS set) sees the buttons; mboxfw (P3PUDIS clear) reads a
stuck 1. The fix rests on the behaviour, not on the mechanism.

RESOLVED by §6b: mboxfw with P3PUDIS set reads P3 bits 3/4/5 as 0 at rest, and
presses register 1:1.

Still open: whether the boot-time DFU escape FIRES when the button is held
through a power cycle. It has never fired in any build, and build 0x0016 came
up on a bus reset rather than a cold boot, so it was not exercised. Until that
is demonstrated, the SDA short remains the real fallback and `preflight.sh` §4
says so. #153.
