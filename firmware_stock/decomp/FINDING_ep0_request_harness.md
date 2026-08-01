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
