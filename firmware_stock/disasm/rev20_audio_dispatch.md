# Rev 20 audio-dispatch mechanism

**Bottom line:** the audio pipeline is fully fire-and-forget. Firmware
programs the DMA channels + EP config once (at boot and again on
mode/rate change), then the TAS1020A hardware autonomously ferries
bytes between the USB EP buffers and the C-port I²S rails. Firmware
never sees SOF, EP1-IN completion, or EP2-OUT completion.

## 1. EP1-IN / EP2-OUT commit registers — writes are boot/mode only

Every write to `IEPBCTX1 (0xFF63)` and `OEPBCTX2 (0xFF9B)` in Rev 20:

| Addr    | Site                                           | Value | Context                          |
|---------|------------------------------------------------|-------|----------------------------------|
| 0x07E5  | `fcn.0x0728` common tail (mode reconfig)       | 0     | reset EP1-IN commit on mode switch |
| 0x07EE  | same tail                                      | 0     | reset EP2-OUT commit             |
| 0x09C3  | boot init (`fcn.0x08CB` chain)                 | 0     | initial EP1-IN commit clear      |
| 0x09C7  | boot init                                      | 0     | initial EP2-OUT commit clear     |
| 0x09CC  | boot init                                      | 0     | (same block)                     |
| 0x0F58  | USB RESET handler (`fcn.0x0F55`, VEC_RSTR)     | 0     | reset EP commits on bus reset    |
| 0x0F5C  | same                                           | 0     | (same block)                     |

`IEPBSIZ1 (0xFF62)` / `OEPBSIZ2 (0xFF9A)`: same pattern — written at
0x09BD-0x09C7 during boot only. **Never rewritten per-frame or per-packet.**

Implication: the TAS1020A does not require the CPU to re-arm the
streaming EPs. It's a hardware ping-pong under DMA control.

## 2. VECINT dispatcher — streaming interrupts are RETI stubs

USB interrupt entry at `0x0DBE`. Reads VECINT (`0xFFB2`) into `a`,
computes `dptr = 0x0C93 + 2*vec`, MOVC-reads two bytes (hi, lo), and
calls the handler via `fcn.0x0F96`. Decoded table:

| Vec  | Name       | Handler | Content at handler                            |
|------|------------|---------|-----------------------------------------------|
| 0x00 | VEC_OEP0   | 0x0D25  | EP0 OUT (SET data phase, class writes)        |
| 0x02 | **VEC_OEP2** | 0x0011  | `22` (RET) — **bare stub**                  |
| 0x08 | VEC_IEP0   | 0x0FC4  | EP0 IN completion (status stage helpers)      |
| 0x09 | **VEC_IEP1** | 0x001A  | `22` (RET) — **bare stub**                  |
| 0x12 | VEC_SETUP  | 0x0026  | SETUP dispatcher (already documented)         |
| 0x14 | **VEC_SOF**  | 0x1034  | `22 22 22 22 22 22` — **bare stub**         |
| 0x15 | VEC_RESR   | 0x1035  | RET stub                                       |
| 0x16 | VEC_SUSR   | 0x0006  | Reseeds action reg (`mov 0x0A, #0x0E`) + RET  |
| 0x17 | VEC_RSTR   | 0x0F43  | USB bus reset — clears EP commits, USBFADR=0  |
| 0x24 | VEC_NONE   | 0x103D  | RET stub                                       |

**EP1-IN, EP2-OUT and SOF are wired to bare RET stubs.** Rev 20 does
zero per-frame audio handling. No feedback-endpoint update path exists.

## 3. DMA polling in the main loop — none

Every DMA control access (`0xFFE0-E7`, `0xFFE8-EF`, `0xFFF0-F9`) is
inside `fcn.0x08CB` (boot init) or the `fcn.0x0728` mode-configure
branches (0x0538, 0x0748, 0x075F, 0x07DE, 0x0836, 0x0E22). Zero DMA
status polling from the main loop or ISR body.

## 4. Feedback endpoint — not declared

`IEPCNF2 (0xFF58)` — the register you would enable for an EP2-IN
feedback endpoint — is never written. Rev 20 exposes only EP1-IN
(capture) and EP2-OUT (playback), no feedback EP.

The Mbox 1 must therefore use synchronous or adaptive isoc, not async.
That's consistent with the CS8427 being the master clock domain when
S/PDIF is selected, and the codec being self-clocking otherwise.

Aside: three DMA channels are configured (not two) — DMA0 (0xFFE0-E7,
playback), DMA1 (0xFFE8-EF, purpose currently untraced — possibly
CS8427 channel-status bytes), DMA2 (0xFFF0-F9, capture). NOTES.md
currently only mentions channels 0 and 2.

## 5. What mboxfw must do per SOF / per completion / per DMA

**Per SOF: nothing.** `streaming_sof()` staying empty is correct.

**Per EP1-IN or EP2-OUT completion: nothing.** Don't service VEC_IEP1
or VEC_OEP2; leave them as no-ops. The TAS1020A EP engine + DMA moves
bytes without CPU involvement once the pipe is armed.

**Per DMA completion: nothing.** DMA autoruns.

**Once, at boot and again on rate/source change:** program
`IEPCNF1 = 0xC5`, `OEPCNF2 = 0xC5`, clear the four BCTX/BSIZ bytes,
program DMA0/DMA1/DMA2 source/dest/count, program C-port
(0xFFD4-0xFFDE) for the target sample rate, and set
`DMACTL1 |= 0xC0`. This is exactly what `fcn.0x0728` does.

**On USB bus reset (VEC_RSTR):** clear `IEPBCTX1`, `OEPBCTX2`,
`USBFADR`, and re-enable `OEPCNF0 = 0x84`. mboxfw needs this handler
or the streaming EPs go silent after any USB reset (which Logic
frequently issues during config negotiation).
