# TI's "soft-PLL" is the feedback endpoint — and Rev 22 shipped half of it

2026-08-05, while deciding #186. Source: `reference/tas1020a/ti_uac_reference/
Application/SoftPll.c` (130 lines, in this repo all along) and the TAS1020B
datasheet SLES025B.

## The confusion this resolves

The datasheet, §2.2.7.4.1, says:

> "In most applications, the C-port timing is derived from the USB frame rate
> using a soft-PLL provided in the TAS1020B firmware."

Read cold, that sounds like a firmware control loop servoing the Adaptive Clock
Generator to the USB frame clock — an alternative to publishing a UAC feedback
endpoint, and a more elegant one, since it needs no endpoint and no bandwidth.

It is not. `softPll()` never writes the ACG frequency word. It:

  1. reads `ACGCAPL`/`ACGCAPH`, the 16-bit capture register;
  2. differences successive captures to get MCLK cycles per 1 ms frame;
  3. averages four frames (`fbCount` counts 3,2,1,0);
  4. encodes 10.14 — `fbvalue = (nInt << 14) | (nFrac << 4)`;
  5. writes `INEP2_X[0..2]` and sets `IEPDCNTX2 = 3; IEPDCNTY2 = 3`.

That is a **UAC isochronous feedback endpoint on EP2 IN**. The ACG frequency
word is written once, in `softPllInit()`, and left alone. "Soft-PLL" names the
loop closed through the HOST — the device measures its own clock and reports it,
the host adjusts how many samples it sends. The device clock never moves.

So "implement the soft-PLL properly" and "add a feedback endpoint" are the same
task. There is no servo to destabilise, which also means no failure mode that
costs a physical trip.

## What ACGCAP gives us

§2.2.6:

> "The capture counter and register circuit consists of a 16-bit free running
> counter which runs at the capture clock frequency... At each USB
> start-of-frame (SOF) event or pseudo-start-of-frame (PSOF) event, the capture
> counter value is stored into the 16-bit capture register."

This is the measurement the whole question needed, in hardware. No timer
arithmetic, no software counting. The datasheet adds that because the counter
free-runs and rolls over only after several frames, "the capture count values
obtained are correlated over several SOF cycles" — so a frame in which the MCU
misses the read is recoverable from the next one rather than lost.

Also settled there: there is only ONE capture counter, and it always follows
MCLKO's clock selection, so **MCLKO2 cannot be synchronised to the USB stream**
— and does not need to be, because "Synchronization to the USB bus for record is
handled by the handshaking protocol established between the assigned DMA channel
and the USB buffer manager." Capture therefore needs no feedback endpoint, which
makes #185's capture half a one-byte relabel to `SYNC_ASYNC`.

## Rev 22's SOF watchdog is the tail of this function

`softPll()` ends with:

    EngAcgCap2 = DMABCNT0L | (DMABCNT0H << 8);
    if (EngAcgCap2 % DEV_NUM_BYTE_PER_SAMPLE)
    {
        DMACTL0 = 0;      // non-integral, so reset pointers
        DMACTL0 = 0x81;
    }

That is Rev 22 `fcn.0x0D58` — the playback SOF watchdog documented in
`FINDING_rev22_playback_sof_watchdog.md` — instruction for instruction in intent:
read the playback buffer count, and if it is not a whole number of samples,
tear the DMA down and re-arm it to realign the UBM and DMA pointers.

**So Rev 22 ported TI's `softPll()`, kept the DMA-realignment tail, and dropped
the ACGCAP measurement and the feedback endpoint.** That is a direct explanation
of a fact measured independently the same day (#181): both stock images write
`ACG1FRQ` at exactly two sites each, both in the rate-setting path, neither in a
SOF handler —

    rev20  ACG1FRQ0/1/2 at 0x075F-0x076B and 0x0DEC-0x0DF8
    rev22  ACG1FRQ0/1/2 at 0x0746-0x0752 and 0x0EC8-0x0ED4

— so stock never measures its own clock and never reports it. Stock free-runs
exactly as mboxfw does. The difference is that stock serves a vendor-class
configuration and never claims `SYNC_ADAPTIVE`, so the false claim is ours alone.

## Warning carried from TI's own source

`softPll()` contains this, live, not commented out:

    //debug test for capture counter malfunction
    MclkPerMs = 11290;

TI's own reference overrides the captured value with a constant, under a comment
naming a capture-counter malfunction. Whatever that was, ACGCAP is to be proved
on our silicon before anything is built on it — which is why the work is staged
with a measurement-only build first.

## Method note

An earlier `grep -il "pll"` across `ti_uac_reference/` returned NOTHING, and the
conclusion "TI shipped no soft-PLL" was one sentence away from being written
down. The file is named `SoftPll.c` and was found by listing the directory
instead. `POLICY.md`'s rule — never argue from absence in a tool's output —
earned itself again, on a tool whose output was simply wrong.
