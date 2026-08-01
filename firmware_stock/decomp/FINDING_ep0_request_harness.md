# The firmware can be asked a question

`tools/sim_ep0_requests.py`, 2026-07-31.

## The inference that cost a round

Earlier the same day, `sim_p1_waveform.py` closed the "every gate is static"
gap by executing the image and decoding the pins. The conclusion drawn from
that work was that the remaining queue is hardware-bound, because *the USB
engine is not modelled*.

That reads two different claims as one. The SIE and the UBM are not modelled.
But the firmware's request handling is ordinary 8051 code that reads a SETUP
packet out of XDATA at 0xFF28, and ucSim models XDATA as plain RAM. The
missing piece was never the model — it was the **stimulus**.

Every executed check this project had ever run drives the boot path with **no
input at all**. `sim_smoke.sh` asks whether the main loop is reached.
`sim_p1_waveform.py` asks what the pins do while starting up. Neither hands
the firmware a request, so nothing had ever confirmed that mboxfw *answers*
one.

## The mechanism

    write the 8 SETUP bytes to 0xFF28
    write VECINT (0xFFB2) = 0x12          -- VEC_SETUP
    push a return address, enter usb_service()
    read IEPDCNTX0 and the EP0 IN buffer

The buffer address is not assumed: `IEPBBAX0` holds `(addr - 0xF800) >> 3`, so
the harness decodes it from the register the firmware itself programmed. That
matters — mboxfw stages at 0xFA18, Rev 20 at 0xFD38.

## What came back

    GET_DESCRIPTOR device    -> armed  count=8   12 01 10 01 00 00 00 08
    GET_DESCRIPTOR config    -> armed  count=8   09 02 B4 00 03 01 00 80
    GET_STATUS device        -> armed  count=2   00 00
    telemetry read block 0   -> armed  count=8   14 00 00 3F 01 00 00 00
    undefined bRequest 0x0C  -> STALL           IEPCNF0 0xA4 -> 0xAC

The device descriptor is `bLength=18`, `bcdUSB=0x0110`, `bMaxPacketSize0=8`.
The config descriptor is `wTotalLength=180`, three interfaces. The telemetry
reply opens `14 00` — `TLM_BUILD_ID` 0x0014, the build flashed into this image.
The unsupported request sets bit 3 of IEPCNF0, which is the STALL the spec
asks for and which `usb.c` records having got backwards once already.

This is the first evidence that mboxfw responds to anything that does not
consist of reading its source.

## What it does not show

That the SIE delivered the packet, that the UBM handed it over, that anything
happened on the wire, or any timing. It shows what the firmware replies once a
SETUP has landed in the buffer. **#165 on real hardware is still the only thing
that proves the CS8427 heard us.**

What it does change is the cost of getting there: the telemetry protocol that
carries #165's answer can now be written and debugged before it is ever
flashed, instead of after a 2 km round trip.

## Validation, three ways

A harness that delivers nothing looks exactly like firmware that answers
nothing, so the gate has to separate them.

1. **Two independent readings must agree.** The staged descriptor bytes are
   compared against the bytes in ROM at the address the linker map gives for
   `_AppDevDesc` / `_AppConfigDesc`. Executed output against static table,
   neither derived from the other. A harness that delivered nothing could not
   produce those exact bytes.
2. **A no-stimulus control.** The identical sequence runs with the SETUP packet
   and VECINT left alone, and nothing may be staged.
3. **The replies must discriminate.** Four armed replies, four distinct
   (bytes, count) pairs. A gate reporting a constant fails here.

### There is deliberately no stock arm

The obvious fourth check — drive Rev 20's `usb_ev_setup` (0x0026) with the same
packet — was built and then removed, because it was measured to be vacuous.
Stock handles class requests inline at 0x0026 but defers standard requests to
the work-code dispatcher, and writes `IEPDCNTX0 = 0` unconditionally on the way
through. Measured: `GET_DESCRIPTOR`, a class OUT, a class IN, and an all-0xFF
garbage packet all produce byte-identical results. A stock arm built on that
would pass no matter what the harness did, which is worse than having none.

## Two ways this gate was wrong first

**It judged on "did the simulator stop" rather than "did anything change".**
The no-stimulus control came back `armed` with `count=238` — which is the
poison byte the harness writes to IEPDCNTX0 before every run, i.e. proof that
nothing had been armed at all. Stopping means a breakpoint was hit; it says
nothing about what the firmware did. Outcome is now decided by whether
IEPDCNTX0 changed from the poison.

**The call had no return address.** Entering `usb_service()` with `pc` alone
leaves the stack holding whatever was there, so the RET wandered into
unrelated code that went on writing USB registers — which is *why* the
no-stimulus run looked like a response. The harness now pushes a real return
address and breaks there, bounding the run to exactly one call.

Both were caught by the controls, not by inspection. The no-stimulus control
earned its place on its first run.

## Mutations verified failing

- `GET_DESCRIPTOR device` serves the config descriptor → caught by the
  ROM-table cross-check, naming both byte strings.
- The STALL on an unsupported request becomes a no-op → `expected stall, got
  no-response`.

## What this opens

All without hardware, all pure firmware paths:

- The multi-packet EP0 continuation path — where the measured ~12% EP0 IN
  packet loss and #148's Y-buffer hypothesis live.
- `SET_INTERFACE` alt gating, `SET_CONFIGURATION`, the 3-byte LE `SET_CUR`
  sample-rate parse.
- The whole telemetry protocol, including **#165's CS8427 readback**, before it
  is flashed.
- Suspend/resume (#149), `VEC_RSTR`, the DFU trigger path.

---

# The multi-packet path, executed

Same day. The single-request harness above answers one SETUP at a time. The
path that actually carries a descriptor is longer: `stage_reply()` ships the
first chunk, and every `VEC_IEP0` afterwards ships the next until
`g_ep0_reply_remaining` hits zero. That loop had never been run.

Driving it means handing the firmware `VEC_IEP0` over and over in ONE
simulator session — the state that makes it work (`g_ep0_reply_src`,
`g_ep0_reply_remaining`) does not survive a fresh session.

## Result 1 — the continuation logic is correct

    GET_DESCRIPTOR config, wLength=180
    23 chunks, counts [8 x 22, 4]
    reassembled 180 of 180 bytes, byte-identical to _AppConfigDesc

Every chunk in order, none repeated, none skipped, correct short final packet.

**This narrows #147 and the EP0 packet-loss measurement.** The ~12% of EP0 IN
packets lost past the second, measured on hardware, is not `push_reply_chunk()`
and not the `VEC_IEP0` sequencing. Under ideal event delivery the firmware
ships the descriptor perfectly.

The scope of that claim is exactly as narrow as it sounds. This harness hands
over one completion at a time, synchronously, with no race. The failure mode
`usb.c` documents fixing is a *race* — "once IEPDCNTX0 is written the packet
can complete before we reach the clear, and VECINT = 0 then wipes the
completion event for the packet just armed". Nothing here can reproduce that.
What is now established is that the sequencing is right and the loss lives in
event delivery, which is the UBM/SIE side.

## Result 2 — the enumeration bug stays fixed

`usb.c` records shipping this once: macOS asks for the device descriptor with
wLength=64, the firmware clamps to 18 and ships 8, leaving
`g_ep0_reply_remaining = 10`. macOS only wanted `bMaxPacketSize0`, abandons the
transfer, and sends SET_ADDRESS. If the new SETUP does not clear that counter,
the completion after the status ZLP ships 8 stale descriptor bytes, EP0
desynchronises, and enumeration never completes. Found by an LED canary and
fixed 2026-07-26.

That sequence is now a gate:

    wLength=64 device request : counts [8], 12 01 10 01 00 00 00 08
    SET_ADDRESS status stage  : counts [0, 0]

The second zero is the load-bearing one — the corruption arrives on the
continuation *after* the status stage, not on the status stage itself.

**The fix turns out to be redundant across two sites.** Removing the
`handle_setup()` clear alone leaves `reply_zero_length()`'s clear standing and
the behaviour is unchanged (`[0, 0]`, gate passes). Removing both reproduces
the bug exactly (`[0, 8]`, gate fails with the diagnosis). Defence in depth,
and a single-site regression is survivable.

## Three ways the multi-packet harness was wrong first

Every one of them produced a green that meant nothing, and each was caught by
running a mutation rather than by reading the code.

**The scenario stopped before the evidence.** The loop ran `while counts[-1]
== 8`, so it halted at the zero-length status stage — one call short of where
the stale bytes appear. The abandoned-transfer scenario passed with **both**
clears removed.

**"Nothing new was armed" is not a stop condition.** Replacing the count test
with "run until the firmware stages nothing" ran the config descriptor out to
64 calls: once a transfer has drained, every further `VEC_IEP0` arms a
*zero-length* packet, so the poison byte is always overwritten and that test
never fires. Measured, not predicted.

**A mutation that does not reproduce the bug proves nothing.** Removing one of
the two clears looked like "the gate missed the historical bug". It was not —
the firmware is defended twice, and the mutation had not reintroduced anything.
Checking that before believing the gate was broken is the whole difference
between a finding and a wrong finding.

Stop conditions are now explicit per step: `drain` for a control read (stop at
the first short packet, which is how a control read ends), fixed-count for the
abandoned-transfer case, whose evidence is *past* the short packet.

## Mutations verified failing

- Continuation never advances `g_ep0_reply_src` → reassembly mismatch, first
  difference at offset 8.
- Both `g_ep0_reply_remaining = 0` sites removed → `[0, 8]`, named as the
  2026-07-26 bug.
- (Control) one site removed → passes, correctly: the firmware still defends.

---

# Three more paths, executed: #40, #41, #149

Same harness, extended with a scenario runner — a sequence of USB events
through one session, because alt settings, a pending data stage and the
suspend flag are all *state between requests*.

## #40 — SET_INTERFACE alt gating

    SET_INTERFACE iface 1 alt 1  -> count 0 (status ZLP)
    GET_INTERFACE iface 1        -> count 1, reads 1
    SET_INTERFACE iface 1 alt 0  -> count 0
    GET_INTERFACE iface 1        -> count 1, reads 0

The alt setting sticks, reads back, and goes down again. Mutation: making
`SET_INTERFACE` ignore the requested alt is caught with *"GET_INTERFACE after
alt 1 returned value=0 ... the alt setting did not stick."*

## #41 — the 3-byte SET_CUR rate, round trip

This is a control-OUT with a **data stage**: the SETUP carries no rate, the
bytes arrive later on `VEC_OEP0`. Both halves are checked in one session.

    SET_CUR + OUT [44 AC 00]  then GET_CUR -> 44 AC 00   (44100)
    SET_CUR + OUT [E0 93 04]  then GET_CUR -> 44 AC 00   (300000 refused)

The second line is the one that matters. Rev 20 reads only `src[0]`, which
works for 44100/48000 because their low bytes are unique but silently accepts
nonsense; mboxfw parses all 24 bits and refuses anything its descriptors do
not advertise. Now demonstrated rather than asserted.

## #149 — suspend re-arms the bring-up

`do_suspend()` zeroes the codec word, and bit 6 of its low byte IS the
"bring-up already ran" guard. Zeroing it is what re-arms `cs8427_boot_init()`
for the resume; if suspend stopped clearing it, a resume would never release
the external RESET again and the CS8427 would stay dead silently.

    g_codec_state_25 @ IRAM 0x0B: 0xC0 -> 0xC0 -> 0x00
                       [SET_CONFIG, VEC_SUSR, work_dispatch]

## Three ways this one was wrong before it was right

**It reported a firmware defect that was the scenario's fault.** `VEC_SUSR`
does not suspend — it posts a work code and returns. That is stock's own split
(Rev 20's entire SUSR handler is `MOV 0x0A,#0x0E; RET` at 0x0006), because
PCON idle inside an ISR cannot be woken by the interrupt that would resume it.
Delivering `VEC_SUSR` alone and reading the mirror shows nothing changed, and
the first version of this check called that a defect. The deferred work had
simply never been run.

**`do_suspend()` never returns.** It ends in PCON idle waiting for the host, so
bounding the call on a return address timed out and desynced the session. It
now breaks on the write to the mirror instead.

**A missing write looked like a failed read.** With the mutation applied the
breakpoint never fires, and the gate reported *"could not read the codec word
mirror"* — true, but it buries the finding. `run_scenario` now distinguishes
"the run never stopped on the expected write" from "the read failed", so the
mutation is named: *"do_suspend() never wrote the codec word mirror."*

**And one in the tooling.** `symbols()` only matched rows with a `C:` area tag,
so DSEG symbols — which are listed without one — silently returned nothing and
the suspend check crashed on a `None` address. `data_symbols()` parses those
rows, kept separate from code symbols because 0x0B is a legal address in both
spaces.

## Mutations verified failing

- `SET_INTERFACE` ignores the requested alt → named.
- `do_suspend()` leaves the codec word alone → named, with the consequence.
