# #200 — a build that reproduces the transient, and what to run on it

Design only. Nothing here is built or flashed yet.

## Why a build is needed at all

Every experiment so far has been null, and the reason is now clear: **on a
calibrated part there is nothing to reveal.** The transient's DC has to be
PRESENT before any disturbance can expose it. 0x004A and 0x004D calibrate at
every stream open, so the offset is ~0 and every probe measures the loss of a
mux-injected correction instead of the thing actually being sought.

So the build's job is not to add a new probe. It is to **make the phenomenon
exist again, on demand, at runtime**, so that the probes already written finally
have something to act on.

The decisive consequence: `#199` fired the reprogramming diagnostic mid-stream
and found nothing. That result does not mean reprogramming is innocent — it means
reprogramming had nothing to expose. **Re-running exactly that experiment with
the offset present is the single most important thing this build enables.**

## The one capability missing

`g_diag_no_rst` self-clears after each `TLM_REQ_SET_CLOCK`, so it cannot affect
the `streaming_set_rate()` that `arecord`'s own SET_CUR triggers at stream open —
which is the moment under investigation.

Replace it with **one latched byte**:

```c
/* Which pair bits streaming_set_rate() clears before reprogramming.
 * 0x0C = shipping. Latched; survives until changed or power-cycled. */
__data unsigned char g_diag_clr_mask = CODEC23_MUTE_PAIR_ALL;
```

and change the fix's line from a constant to that variable:

```c
g_codec_state_23 &= (unsigned char)~g_diag_clr_mask;
codec_write_word();
```

One byte of state, one operand change. It buys four configurations:

| mask | behaviour |
|---|---|
| `0x0C` | shipping — clears both, calibrates every open |
| `0x00` | **pre-fix** — no edge, no calibration, transient returns |
| `0x04` | clears the ADC's RST only |
| `0x08` | clears the DAC's gate only |

Plus one flags byte for the structural arms:

| bit | behaviour |
|---|---|
| `0x01` | skip the ACG reprogramming entirely in `set_rate` |
| `0x02` | skip the endpoint/DMA re-arm, keeping the reprogramming |

`TLM_REQ_DIAG_MODE = 0x17` (0x15 and 0x16 are retired and must not be reused),
`bmRequestType 0x40`, `wValue` = mask, `wIndex` = flags. Both readable back in a
telemetry block so the state is confirmed rather than assumed — every void run
this session came from assuming a stimulus happened.

**Defaults are the shipping values**, so a power cycle always returns the unit to
correct behaviour and a diagnostic build left on a unit is not a trap.

## Experiments, in dependency order

Each has a control arm whose answer is known in advance. That is not ceremony:
four measurements this session were void because the instrument was doing
nothing, and every one was caught by such an arm.

### E1 — does it even reproduce?

Set mask `0x00`, open a capture, measure. Expect the historical signature:
~−20 dBFS opening level, DC ~+0.0038 at 48 kHz, decaying with the double-pole
high-pass, no leading zero run.

*Control:* mask `0x0C` in the same session, minutes apart, same analog
conditions. This is strictly better than any cross-build comparison the project
has ever made — same unit, same power-up, same room.

**If it does not reproduce, stop.** Everything downstream assumes it does, and a
failure to reproduce would itself be the finding: the phenomenon depends on
something that changed between 0x0040 and 0x004D other than the bracket.

### E2 — which bit matters

Masks `0x04` and `0x08`. If clearing the DAC gate alone also removes the
transient, the mechanism is not the ADC's RST and most of `FINDING_197` needs
rewriting.

*Control:* `0x00` and `0x0C` interleaved, so drift cannot masquerade as an effect.

### E3 — the experiment #199 could not do

Mask `0x00`, stream running, offset present. Now fire the existing
reprogramming diagnostic mid-stream:

- **transient appears** → reprogramming is the injector, and #199's null was an
  artefact of testing on a calibrated part
- **nothing** → reprogramming is genuinely innocent and the injector is elsewhere
  in the stream-open path

*Control:* the same request with the mask at `0x0C`, which must show nothing —
that is #199's result, and reproducing it proves the rig is the same one.

### E4 — clock, or endpoints?

Flags `0x01` (no reprogramming) and `0x02` (no endpoint re-arm), each with mask
`0x00`. Between them these split stream-open into its two halves. Whichever half
carries the transient is the answer; if neither does, it is the alt-setting
switch or the host's first URBs, and that is testable with `SET_INTERFACE`
directly.

### E5 — upstream or downstream of the high-pass

Mask `0x00`, let the transient decay, then measure the standing DC over 10 s.

- **decays to zero** → the offset is upstream, the high-pass removes it, and the
  transient is that filter re-converging. Consistent with everything measured.
- **standing offset** → downstream of the filter, and the decaying transient is a
  separate phenomenon that happens to share a time constant.

*Control:* the same measurement at mask `0x0C`, where DC is known to sit at
5–24 LSB24.

### E6 — how the injection scales

Mask `0x00`; vary the gap between stream close and reopen (0.5 s to 60 s) and
measure the transient's amplitude. A dependence on the gap points at something
charging or discharging; independence points at a discrete event at open.

## Telemetry

One block reporting `g_diag_clr_mask`, `g_diag_flags`, a count of RST rising
edges since boot, and the SOF count at the last one. The edge counter is the
cheap insurance: it says what the firmware actually did, so a null can be
distinguished from a stimulus that never fired.

## Code budget — the real risk

The shipping build is **5923 of 6016 bytes, 93 free**. The mask change is
close to free (a constant becomes a variable). The flags branches, the request
handler, and the telemetry block are perhaps 80–140 bytes. **It may not fit.**

In order of preference if it does not:

1. drop the `0x02` endpoint-skip arm — E4 can be split across two flashes, and
   both units can carry different flag sets
2. retire a telemetry block that has served its purpose
3. build the diagnostic without the feedback endpoint

Not on the list: dropping the per-unit serial descriptors. The bench cannot tell
the units apart without them, and that has already caused one round of retracted
measurements.

## Flashing plan

**Both units get the same image.** Every arm is runtime-selectable, so there is
no reason for two different builds, and identical images mean a bad flash is
recoverable on the second unit without another trip — which is the standing
protocol.

Trigger DFU on both remotely, one replug, flash B, verify block 0 and the new
telemetry block, run E1. If E1 reproduces, flash A the same way and run the rest
across both. If E1 does not reproduce, A is still in DFU and can take a corrected
image without a second trip.

## What is not at risk

The pre-fix condition is how the device ran for the entire project. It leaves the
ADC un-calibrated; it does not stress any part, and the datasheet's clock-presence
warning is not engaged because the clock never stops (measured). Defaults are the
shipping values, so this is a build that behaves correctly until told otherwise.

And whatever comes out, **the fix is not in question** — it is proved by
measurement and by stock doing the same thing. What is in question is only the
explanation, and three versions of that have already been retired by experiment.
