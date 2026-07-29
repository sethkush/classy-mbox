# Three open questions, worked as far as the bytes go

Investigation 2026-07-28, following `FINDING_cs8427_confirmed.md`, which named
these three as explicitly *not* settled by identifying the CS8427. Ground truth
throughout is `firmware_stock/rev20_firmware_code.bin`,
`rev22_firmware_code.bin` and the Ghidra listings. Every bit-operand scan below
was run twice: once by grepping the listings, once by walking the raw images for
every bit-addressing opcode — `SETB`/`CLR`/`JB`/`JNB`/`JBC`/`CPL`, both `MOV`
directions, and all four carry-logic forms — and keeping only hits that land on
an instruction boundary the listing agrees with. The two
methods returned the same set, with one false positive rejected: rev22 `0x0BB5`
looks like `D2 1C` but is the tail of `20 D2 1C` = `JB 0xD2,0x0BD3` at `0x0BB4`,
a test of the OV flag inside a divide routine.

No candidate file was edited. This is analysis only.

---

## 1. IRAM 0x23.2, 0x23.3, 0x23.4 — bit addresses 0x1A, 0x1B, 0x1C

### 1.1 The complete site list, both images

These are *all* the instructions in either image that name bits `0x1A`, `0x1B`
or `0x1C`:

| Bit | rev20 | rev22 | Context |
|---|---|---|---|
| `0x1A` (0x23.2) | `CLR` `0x072F` | `CLR` `0x0716` | first thing in `audio_clock_mode_apply` |
| `0x1B` (0x23.3) | `CLR` `0x0731` | `CLR` `0x0718` | " |
| `0x1A` | `SETB` `0x07EE` | `SETB` `0x07CF` | common tail of `audio_clock_mode_apply` |
| `0x1B` | `SETB` `0x07F0` | `SETB` `0x07D1` | " |
| `0x1A` | `SETB` `0x0831` | `SETB` `0x09D8` | external-chip bring-up |
| `0x1B` | `SETB` `0x0833` | `SETB` `0x09DA` | " |
| `0x1C` (0x23.4) | `SETB` `0x0840` | `SETB` `0x09E5` | external-chip bring-up |

rev20 `0x072F` is `c2 1a c2 1b 12 0e`; rev22 `0x0716` is the same six bytes.
rev20 `0x07EE` is `d2 1a d2 1b 12 0e 62`; rev22 `0x07CF` is
`d2 1a d2 1b 12 0e 56` — the same pair, calling that image's chain-B commit.

**Nothing ever reads them.** There is no `JB`, `JNB`, `JBC`, `CPL`, `MOV C,bit`,
`ANL C`, or `ORL C` on any of the three in either image. The containing byte,
IRAM `0x23`, is read in exactly one place per image: `MOV R5,0x23` at rev20
`0x0E64` and `MOV R7,0x23` at rev22 `0x0E58` — the payload fetch at the top of
`shiftreg16_commit`. So all three are pure outputs. They cannot be internal
state; the only thing writing them accomplishes is changing what the panel latch
sees at the next commit.

The only other writes to the byte are three whole-byte zeroings, all `MOV
0x23,A` with `A` already cleared: rev20 `0x0536` / `0x080E` / `0x096A`, rev22
`0x0535` / `0x088B` / `0x09B9` — the USB-suspend handler, the bring-up entry,
and the tail of `hw_master_init`.

### 1.2 Which physical line each drives

IRAM `0x23` is `g_panel_lo`, the **first** of the two payload bytes
`shiftreg16_commit` clocks out (rev20 `0x0E62`, rev22 `0x0E56`), MSB first, on
P1.0 = data / P1.2 = clock / P1.1 = latch. `0x25` (`g_panel_hi`) follows. So on
chain B these three bits are shift positions 5, 4 and 3 counting from the first
bit out, and because `0x23` goes out first they end up **furthest down the
chain** — physically the far end of a 16-bit register pair.

The whole of IRAM `0x23`, for the record:

| Bit | Bit addr | Status |
|---|---|---|
| 0x23.0 | `0x18` | `SETB` only, and only in the **clock-mode-5** branch (rev20 `0x07B8`, rev22 `0x0796`; both `d2 18 d2 19 12 0e ..`) |
| 0x23.1 | `0x19` | `SETB` only, same two sites |
| 0x23.2 | `0x1A` | this investigation |
| 0x23.3 | `0x1B` | this investigation |
| 0x23.4 | `0x1C` | this investigation |
| 0x23.5 | `0x1D` | **never named by any instruction in either image** — constant 0 |
| 0x23.6 | `0x1E` | `p_hold`, the P3.5 button toggle; see `cand/shiftreg8_commit.c` |
| 0x23.7 | `0x1F` | **never named by any instruction in either image** — constant 0 |

That `0x18`/`0x19` are set only inside the mode-5 arm is new here and is worth
recording on its own: mode 5 is the branch this project reads as externally
clocked / S/PDIF-slaved (task #145), it is the arm that writes `ACG2DCTL`
(`0xFFF6`) `= 0x10`, and the pair is set immediately after that write and
committed at rev20 `0x07BC` / rev22 `0x079A`. They are never cleared by any bit
operation, so once mode 5 has been entered they stay high until the next
whole-byte zeroing of `0x23`.

### 1.3 The ordering, exactly

Bring-up, rev20 `0x080B` (`audio_path_reconfig_ext_chips`) / rev22 `0x09B6`
(`audio_hw_bringup`). Latched chain-B state after each commit — the outputs only
move at the latch strobe, so this is the actual pin timeline:

| Step | rev20 | 0x23 | 0x25 | What just happened |
|---|---|---|---|---|
| zero both, settle, commit | `0x0818` | `0x00` | `0x40` | chain B data low; `0x25.6` (bring-up-done) already high from `0x0810`, CS low |
| ACG programmed, MCLKO1/2EN set | `0x081B`–`0x082A` | — | — | clocks running |
| settle, `SETB 0x1A`, `SETB 0x1B`, commit | `0x0835` | `0x0C` | `0x40` | the pair goes high **after** the clock is up |
| settle, `SETB 0x2F`, `SETB 0x1C`, commit | `0x0842` | `0x1C` | `0xC0` | `0x1C` and CS-idle-high rise **in the same latch strobe** |
| settle, `CLR 0x2F`, commit | `0x084D` | `0x1C` | `0x40` | CS falls — see §2 |
| `SETB 0x2F`, commit | `0x0852` | `0x1C` | `0xC0` | CS rises |
| ten CS8427 register writes | `0x0855`–`0x08A4` | `0x1C` | toggles `0x40`/`0xC0` | |

rev20 `0x082B` is
`75 2e ff d5 2e fd d2 1a d2 1b 12 0e 62 75 2e ff d5 2e fd d2 2f d2 1c 12 0e 62 75 2e ff d5 2e fd c2 2f 12 0e 62 d2 2f 12 0e 62`;
rev22 `0x09D4` is
`7f ff df fe d2 1a d2 1b 12 0e 56 7f ff df fe d2 2f d2 1c 12 0e 56 7f ff df fe c2 2f 12 0e 56 d2 2f 12 0e 56`
— identical but for the settle delay living in R7 rather than IRAM byte `0x2E`.

Clock reprogramming, rev20 `audio_clock_mode_apply` `0x0728` / rev22 `0x070F`:
`0x1A` and `0x1B` are cleared in the **first four instructions** of the function
and committed immediately (rev20 `0x0733`, rev22 `0x071A`), i.e. before a single
ACG register is touched. They are set again in the common tail at rev20
`0x07EE` / rev22 `0x07CF` — after the CS8427 write, after `ACGCTL |= 0xC0`,
after `0xFF63`/`0xFF67`/`0xFF9B`/`0xFF9F` are zeroed and after `IEPCNF1` and
`OEPCNF2` are set to `0xC5` — committed, and then the function spends a
16-bit countdown loop (rev20 `0x07F5`–`0x0807`, `0x0FFF` iterations) doing
nothing before returning.

### 1.4 What that establishes

* All three are write-only output lines on chain B. Not flags, not state.
* `0x1C` rises in the same latch strobe that first parks the CS8427 chip select
  high. It is **not** one-shot, and an earlier draft of this note said it was.
  Bring-up is guarded by bit `0x2E` (IRAM `0x25.6`, set at rev20 `0x0810` /
  rev22 `0x09BB`), and `cmd1_apply_clock_mode` CLEARS that guard — rev20
  `0x037B` and rev22 `0x0382` are both `c2 2e`. Any of the four bring-up call
  sites then re-enters, `0x23` is zeroed again at rev20 `0x080E` and committed
  at `0x0818`, so `0x1C` falls and rises once more. Its behaviour is "low from
  reset; high after each bring-up; low again briefly whenever bring-up
  re-runs", and bring-up re-runs after every clock-mode change.
* `0x1A`/`0x1B` are a **matched pair whose entire behaviour is a bracket around
  clock reprogramming**: low before the ACG is touched, high once the clock is
  stable and the endpoints are re-enabled, then a deliberate settle delay. They
  behave identically in bring-up (where the byte starts at zero, so only the
  rising half is emitted).
* Neither pattern is driven by anything a user does. No button, no USB request,
  no panel state feeds them; they are functions of "is the clock currently being
  reprogrammed" and "has bring-up run".

### 1.5 What that rules out, against the front panel

The front panel has: two three-position source selectors, a 48 V phantom
switch, a mono switch, a mix knob, and signal/clip LEDs.

* **Source selectors: ruled out.** They are chain A (IRAM `0x22`, bits 0–5),
  pinned in `disasm/PANEL_LEDS.md` against observed hardware.
* **Phantom, mono, mix: ruled out as *inputs*.** Nothing reads these bits, so
  they cannot be switch positions. Ruled out as *outputs* too, on timing: a
  phantom or mono line that dropped and rose on every host sample-rate change
  would be absurd, and `0x1C`'s single set-and-never-clear cannot represent a
  switch the user can move.
* **Signal/clip LEDs: ruled out.** `PANEL_LEDS.md` records from hardware that
  the peak LEDs flash at power-up before firmware runs and are analog.

That leaves, of the panel inventory, only the spdif / USB / mono **indicator**
LEDs that `PANEL_LEDS.md` assigns to chain B without pinning individual bits —
and functions that are not on the panel at all.

### 1.6 The reading the evidence actually supports, and its confidence

**`0x1A`/`0x1B`: an output mute or audio-path enable pair, most likely the two
audio channels or the DAC and ADC sides. Confidence: moderate-to-high, but not
established.** The argument is entirely from timing and is symmetric in both
images: a signal that is deasserted before the master clock is disturbed and
reasserted only after the clock is stable and the USB endpoints are re-armed,
followed by a settle delay, is the standard shape of pop suppression. The pair
count matches "two channels". Nothing in the firmware names it.

**`0x1C`: a reset release or static enable for an external part, with the
CS8427's reset the leading candidate. Confidence: moderate.** The argument is
ordering: the line is low from power-on (the byte is zeroed at `hw_master_init`
and again at bring-up entry), rises in the same latch strobe that parks the
CS8427 chip select high, is followed by a settle delay, and is then followed
immediately by the first CS activity and the ten register writes. That is
exactly where a reset release belongs, and it would also explain why the bare CS
pulse in §2 exists at all — a mode-select latch needs something to have just
come out of reset for there to be a mode to latch. It is still one consistent
story among several; a plain "analog section enable" fits the same trace.

**What is NOT supported:** that any of the three is one of the spdif / USB /
mono LEDs. See the test in §1.7 — this is cheap to falsify and worth doing
before anyone writes an LED name next to these bits.

### 1.7 Tests Seth could run (hardware observation outranks all of the above)

1. **The LED test — one glance, no flashing, settles §1.6's negative claim.**
   Chain B's polarity is "1 = lit" (`PANEL_LEDS.md`: committing `0x0000` is what
   extinguishes spdif/USB/mono). Bring-up is called only from the command
   handlers — rev20 `0x0360`, `0x0392`, `0x0419`, `0x04C7`; rev22 `0x0366`,
   `0x0396`, `0x0419`, `0x04CB` — so it runs once the host has configured the
   device, and from then on `0x23` = `0x1C`, i.e. bits 2, 3 and 4 are all high.
   Plug a stock unit into a Mac and let it enumerate. If spdif, USB and mono all
   stay dark, none of these three bits drives those LEDs and §1.6's negative is
   confirmed. If exactly three chain-B LEDs light up as the host configures the
   device, the mute reading is wrong and these are indicators.
2. **The mute test.** With a stock unit streaming, force a clock-mode change
   (host sample-rate change, or one of the vendor commands that calls
   `audio_clock_mode_apply`) and listen to the outputs. A short clean gap —
   roughly the `0x0FFF` settle loop plus two chain commits — is what the mute
   reading predicts. Audio continuing through the change, with or without a
   click, falsifies it.
3. **The scope test, definitive.** Probe chain B's shift-register outputs and
   trigger on the latch strobe (P1.1) during enumeration. Positions 3, 4 and 5
   from the first bit out are these three lines; following them to whatever they
   connect to answers all of §1 outright. This is the only test that produces an
   answer rather than a narrowing.

---

## 2. The bare chip-select pulse — rev20 `0x084B..0x0854`, rev22 `0x09EE..0x09F7`

rev20 `0x084B` is `c2 2f 12 0e 62 d2 2f 12 0e 62`; rev22 `0x09EE` is
`c2 2f 12 0e 56 d2 2f 12 0e 56`. Ten bytes, byte-identical apart from the commit
target: clear the chip select, latch it out, set it, latch it out. No data.

### 2.1 What is now established

**The control-port protocol the firmware speaks is not I²C.** This is settled by
`cs8427_ctl_write` (rev20 `0x0C45`, rev22 `0x0C31`) and is a stronger statement
than `FINDING_cs8427_confirmed.md` makes. That routine emits exactly three bytes
of eight bits each on P1.4 (data) with P1.3 pulsed high-then-low per bit, framed
by a chip select that is asserted before the first bit and released after the
last. There is **no start condition, no stop condition, and no ninth clock in
which a slave could acknowledge** — the bit loop is `MOV R4,#0x08` and
`DEC R4` / `JZ`, eight clocks per byte, three times. An I²C slave would never
see a valid transaction. So if the CS8427 is being configured successfully at
all — and it must be, since the box works — its control port is in its
**SPI-like mode**: chip-select-framed, 24 clocked bits, address byte `0x20` then
register index then value. That is a conclusion from the firmware's own wire
format, not from any datasheet.

**Therefore something has to put the part in that mode, and the bare pulse is
the only candidate in either image.** A scan of both images finds no other
CS8427-directed activity before the ten register writes: the chip select bit
`0x2F` is touched at rev20 `0x083E`, `0x084B`, `0x0850` and thereafter only
inside `cs8427_ctl_write` (`0x0C4F` clear, `0x0C8D` set); rev22 `0x09E3`,
`0x09EE`, `0x09F3` and `0x0C39` / `0x0C77`.

**The pulse produces the first clean falling edge on that line in the device's
life, and it is deliberate.** From the §1.3 timeline: the line is driven low at
the very first bring-up commit (rev20 `0x0818`, when `0x25` = `0x00`), stays low
across the next commit, rises at `0x0842`, and falls at `0x084D`. That `0x0842`
→ `0x084D` transition is the first high-to-low edge, and the firmware spends a
255-iteration settle delay (rev20 `0x0845` `75 2e ff`, then `d5 2e fd`) getting
to it.

**The other control-port lines are quiescent and at a known level during the
pulse.** `hw_master_init` writes `P1 = 0x00` outright — rev20 `0x08DA`
(`MOV 0x90,A` with `A` cleared at `0x08D9`), rev22 `0x07FB` (same instruction,
`A` cleared at `0x07FA`) — and every subsequent touch of P1 in either image is a
read-modify-write with a mask that leaves bits 3 and 4 alone (rev20 `0x0C66`,
`0x0C6B`, `0x0C6E`, `0x0C71` are the only instructions in the image that touch
them). So from `hw_master_init` until the first `cs8427_ctl_write`, P1.3 (clock)
and P1.4 (data) are held **low**. The pulse is a clean CS edge against an idle,
low clock and data — which is what a mode-detect on a chip select would want and
is not what a stray or leftover edge would look like.

**The pulse is slow.** The chip select is not a TAS1020B pin; it is IRAM `0x25`
bit 7, the top bit of chain B's second payload byte, so each transition costs a
whole 16-bit bit-banged shift plus a latch strobe. Counting the loop in
`shiftreg16_commit`, one commit is on the order of 500 machine cycles; the
CS-low interval is one such commit. Whether that is ~80 µs or ~250 µs depends on
the core's clocks-per-machine-cycle, which is not asserted here. The point that
does not depend on the number: this is a **level** pulse, orders of magnitude
slower than the clocked traffic around it, so nothing about it is
timing-critical and it cannot be a data or clock artefact.

### 2.2 What remains unknown

Why the part needs the pulse, in the part's own terms. `reference/cs8427/alsa_cs8427.h`
is a register map; it says nothing about pin behaviour, mode selection or reset,
and there is no CS8427 datasheet in this repo. "Protocol-mode select" is now a
**better-supported** inference than it was — the firmware demonstrably speaks the
non-I²C protocol, the pulse is the only mode-shaped event, it is deliberately
delayed and cleanly framed, and §1's reading of `0x1C` supplies a reset release
immediately before it — but it is still an inference. The competing readings
that the bytes do not exclude are: a reset or synchronisation pulse unrelated to
protocol selection; an artefact of a shared line that also serves something else
on chain B; or a defensive no-op the original author included without needing it.

### 2.3 What would settle it

* **A CS8427 datasheet.** One paragraph on control-port mode selection ends this
  question. This is the cheapest possible resolution and no scope is involved.
* **Scope the CS8427's own pins** during the first two seconds after power-up:
  chip select, clock, data, and reset. This resolves §1's `0x1C` and §2 in the
  same capture, since the candidate reset release is one latch strobe before the
  pulse.
* **A firmware experiment** — build a variant with the four instructions at
  `0x084B..0x0854` removed and see whether S/PDIF still configures — would be
  decisive, but it costs a flash, and per the project's standing rule every
  recovery is a 2 km round trip. Not recommended ahead of the scope capture,
  which costs nothing and answers more.

---

## 3. What `0xFF` means on shift-register chain A

### 3.1 Every write to `g_mux_byte` (IRAM `0x22`), both images

Byte-wide writes — this is the complete list, from a scan of both listings:

| # | rev20 | rev22 | Value written | Function |
|---|---|---|---|---|
| 1 | `0x0397` | `0x039B` | `0xFF` | `cmd2_apply_iface1_alt`, stream-START arm |
| 2 | `0x053B` | `0x053A` | `0xFF` | USB suspend handler |
| 3 | `0x093F` | `0x0860` | `0x00` (`MOV 0x22,A`, `A` cleared the instruction before) | `hw_master_init`, first pass |
| 4 | `0x095B` | `0x087C` | `0xFF` | `hw_master_init`, final pass |

Plus bit-wide writes: `0x22.0`–`0x22.2` by the channel-A selector state machine,
`0x22.3`–`0x22.5` by the channel-B one, `0x22.6` (`p_derived`) by the two state
machines and by cmd4 / cmd5 / cmd11, and `0x22.7` at exactly two sites per image
— rev20 `0x03A0` `CLR` and `0x03E6` `SETB`, rev22 `0x03A4` `CLR` and `0x03EA`
`SETB`, both inside `cmd2_apply_iface1_alt`.

The byte is read in exactly one place per image: `MOV R5,0x22` at rev20 `0x0F0E`,
`MOV R7,0x22` at rev22 `0x0EFE` — `shiftreg8_commit`'s payload fetch.

### 3.2 The apparent contradiction dissolves: `0xFF` is a preset, not a value

The objection recorded in `cand/evt0e_usb_suspend_enter_and_resume.c` was that
"all off" cannot be right because `cmd2` writes `0xFF` when a stream *starts*.
That reads the write in isolation. In context, **`cmd2` never latches `0xFF`**.

rev20 `0x0397` is `75 22 ff c2 10 c2 13 c2 1e c2 17 12 0f 0c`; rev22 `0x039B` is
`75 22 ff c2 10 c2 13 c2 1e c2 17 12 0e fc`. That is:

    MOV  0x22,#0xFF     ; preset all eight lines
    CLR  0x10           ; 0x22.0 — channel A position 0
    CLR  0x13           ; 0x22.3 — channel B position 0
    CLR  0x1E           ; 0x23.6 — p_hold, a chain-B bit, not chain A
    CLR  0x17           ; 0x22.7
    LCALL shiftreg8_commit

There is no commit between the `0xFF` and the clears. The byte that reaches the
latch is `0xFF & ~0x01 & ~0x08 & ~0x80` = **`0x76`**.

The same shape in `hw_master_init`: rev20 `0x095B` is
`75 22 ff c2 10 c2 13 c2 1e 12 0f 0c`, rev22 `0x087C` the same with its own
commit address. Latched value **`0xF6`** — which is precisely the value
`PANEL_LEDS.md` already matched against observed hardware ("exactly bits 0 and 3
clear → the two mic LEDs", confirmed on a real unit).

So of the four byte writes, `0xFF` is the value actually latched in **one** of
them: the USB suspend handler, rev20 `0x053B` `75 22 ff c2 1e 12 0f 0c` / rev22
`0x053A` `75 22 ff c2 1e 12 0e fc`, where the only intervening clear is `0x1E`,
a **chain-B** bit that does not appear in this byte at all.

### 3.3 The consistent account

**`0xFF` is "all chain-A lines high", chain A is active low, and therefore
`0xFF` really is "everything off".** The account:

* Chain A's polarity is established, from hardware, in `PANEL_LEDS.md`: bit
  clear = LED lit. It is corroborated structurally — the two selector state
  machines write one-cold triples, `0b101` / `0b011` / `0b110`, exactly one bit
  low per group, which is a one-LED-per-source active-low drive and nothing
  else.
* `MOV 0x22,#0xFF` is the idiom "release every line, then pull down the ones
  that belong down". It appears three times and is followed by targeted `CLR`s
  twice.
* The one site that latches a bare `0xFF` is the suspend handler, where "all off"
  is the correct behaviour and the surrounding code agrees: the same handler has
  just stopped both audio clock generators (`ACGCTL &= 0x3F`) and zeroed both
  chain-B bytes (rev20 `0x0534`/`0x0536`, rev22 `0x0533`/`0x0535`).
* `cmd2`'s stream-start `0xFF` is a transient that exists for four instructions
  and is never seen by the hardware.

**This resolves the question as posed.** The reading in
`evt0e_usb_suspend_enter_and_resume.c` and `rev22_cmd14_usb_suspend_and_resume.c`
is right; the objection raised against it rested on a write whose latched value
is `0x76`, not `0xFF`.

### 3.4 A by-product worth recording: `0x22.7`

Bit `0x22.7` is written at exactly two sites per image and both are in `cmd2`:
cleared on the stream-start arm (rev20 `0x03A0`, rev22 `0x03A4`) and set on the
stream-stop arm (rev20 `0x03E6` `d2 17 12 0f 0c`, rev22 `0x03EA`
`d2 17 12 0e fc`). Never read. Under the active-low polarity that is an
**asserted-while-streaming** line — the only chain-A bit that tracks stream state
rather than panel state. `PANEL_LEDS.md` already calls it "run/stop-like line,
not an LED"; this narrows it to "asserted low for the whole duration of the
audio stream and at no other time". What it drives is not established.

### 3.5 What remains unknown, and the test

Whether the six low bits of chain A drive only the six source LEDs, or drive the
analog input multiplexer as well. Nothing in the firmware distinguishes an LED
from a mux select, and the naming in `cand/shiftreg8_commit.c` ("this is the
routine that puts the Mic/Line/Inst selection onto hardware") is an
interpretation, not something the bytes carry.

**Test:** press a source-select button on a stock unit and check whether the
audio path actually changes — plug a mic into channel A, cycle the selector to
Line and back, and listen. If the LED moves but the signal path does not, the
six bits are indicators only and something else does the switching. If the
signal path follows the LED, chain A is the mux. One minute, no flashing, and it
settles a claim that is currently carried in a candidate comment on inference
alone.

---

## Summary

| Question | Status after this pass |
|---|---|
| 0x23.2 / 0x23.3 (`0x1A`/`0x1B`) | Narrowed hard: write-only output pair, driven solely by clock-reprogram bracketing. Mute / path-enable is the supported reading, moderate-to-high confidence, **not established**. Panel switches and signal/clip LEDs are ruled out. |
| 0x23.4 (`0x1C`) | Narrowed: one-shot, set once per power cycle in the same strobe that parks the CS8427 chip select, never read. Reset-release / static-enable is the supported reading, moderate confidence, **not established**. |
| The bare CS pulse | Advanced without a datasheet: the firmware provably does **not** speak I²C to the part, so the control port must be in its SPI-like mode, and this pulse is the only mode-shaped event in either image. Still an inference; a datasheet paragraph or a scope capture ends it. |
| `0xFF` on chain A | **Settled.** `0xFF` is a preset immediately overwritten by targeted clears at three of the four sites; the only site that latches it bare is USB suspend. Chain A is active low, so `0xFF` = all off. The objection recorded in the suspend candidates is withdrawn. |
