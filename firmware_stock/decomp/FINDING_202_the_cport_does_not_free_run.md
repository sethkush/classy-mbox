# #202 REFUTED — the C-port does not free-run, and 183 ms once per power-up is the floor

Build 0x0050, unit B (`RK1672500M`), 2026-08-11.

## The hypothesis

`FINDING_197_RESOLVED` mechanism #3 says the AK5383 is not clocked between
streams, so its offset calibration can only be spent inside a capture — which is
why the first capture of every power-up carries ~183 ms of exact zeros even
under #201.

The register argued otherwise. `CPTCNF4` bit 3 is `CPTBLK`; we write
`CPTCNF4 = 0x03` (bit 3 **clear**, `hw_init.c:246`, cited to Rev 20
fcn.0x08CB @ 0x0911 and Rev 22 fcn.0x07EC @ 0x0832), `CPTEN` goes up at
`hw_init.c:321`, and MCLKO runs continuously (telemetry block 11). TI, on
`CPTCNF4` @ FFDDh:

> When CPTBLK is set to 0, CSYNC and CSCLK are free running once the C-port is
> enabled.

If that held, the calibration could be spent in the main loop with nothing
streaming and **every** capture would be clean.

It was also worth testing because #3's own evidence does not establish it. The
support offered was that opening DC is independent of the close-to-reopen gap
(0/1/20 s). That does not discriminate: **a step injected into a *running* 1 Hz
high-pass decays with the same tau = 171 ms as a filter converging from
scratch**, so gap-independence is consistent with both stories.

## The build

0x0050 holds `0x23.2` HIGH once `g_ref_settled` trips at 30 s, instead of letting
it follow `g_path_enabled`. The main loop publishes the pair once with no stream
open; after that a stream open produces no RST edge at all.

Guarded against the 0x004B failure by gating the publish on
`g_path_enabled == 0`: a stream already open when the threshold trips has RST
high already, raised at that open and possibly seconds after boot, and latching
that would be exactly 0x004B's bug.

## The measurement

`TLM_REQ_DIAG_MODE` (0x17) in this build drops RST and clears the latch, so the
main loop re-raises it within milliseconds — a real edge, with no stream open.
Then wait, then capture.

```
baseline (RST already high, calibrated):
  before re-arm                lead     0  head DC    +581.8  tail    +2.4
re-arming: drop RST, main loop re-raises it with NO stream open
  after re-arm, +5s settle     lead  8769  head DC     -38.1  tail    -8.7
  after re-arm, +15s settle    lead     0  head DC    +154.1  tail    +3.8
```

**Five seconds of RST held high with nothing streaming produced no
calibration.** It completed only once a stream opened, 183 ms into that capture.
Fifteen seconds later — i.e. after that capture — the next one is clean, which is
the calibration having landed inside the previous capture and nowhere else.

## Why this is conclusive and not merely negative

The part **did** see the edge. That is what the 8769-frame zero run is: `tRTV` is
**8960/fs, a count of LRCK edges, not a time**, so the counter starts at the
rising edge and advances only while LRCK runs. The edge landed at re-arm; the
count did not begin until audio did.

A null from an instrument that never fired looks the same as a refutation — the
standing trap in this project — and this is not that. The stimulus is visible in
the output.

## Consequence

`FINDING_197_RESOLVED` mechanism #3 stands, as a statement about LRCK. The
datasheet's CPTBLK sentence does not describe this configuration's behaviour;
the C-port is in **I2S mode 5** (`CPTCNF1 = 0x0D`, MODE = 5), and the CPTBLK
discussion is written around general-purpose mode 0 and AC '97. Whatever the
register nominally promises, LRCK does not run between streams here.

**0x004F was already the floor.** The 183 ms can be moved — #201 moved it out of
every capture but the first — but it cannot be removed, because it must be spent
somewhere the ADC is clocked, and the ADC is only clocked while a stream is open.

## 0x0050 vs 0x004F

Behaviourally identical from the host's side: first capture of a power-up pays
183 ms, every later one pays nothing. 0x0050 spends 28 more bytes (5977/5975 of
6016 against 5949/5947) to hold a bit high that changes nothing observable.

**It should be reverted at the next flash**, bundled with other work rather than
as its own trip. It is not urgent: the behaviour is the same, and 0x0050 was
cold-boot verified before being left on the units.

## The one avenue not closed

Nothing here rules out *making* LRCK run without a host stream — arming the
C-port or a DMA channel to generate frames into a discarded buffer for ~200 ms at
the 30 s mark. That is a much more invasive change than a held bit, it touches
the DMA path that #147 and #186 both had to be careful with, and the payoff is
183 ms once per power-up. Recorded as an option, not a recommendation.

## #202b — driving the clock ourselves, and why that fails too

The obvious follow-up: if the calibration needs frames rather than a host, run a
capture ourselves. `streaming_capture_enable()` is exactly the three writes that
start C-port framing (`IEPDCNTX1 = 0`, `IEPCNF1 = 0xC5`, `DMACTL1 |= DMA_EN`), so
0x0051 raised RST at the 30 s mark, started a self-driven capture, waited, and
stopped it. The samples land in the EP1 IN buffer and are never collected --
harmless, since no host is streaming and the real stream open rewrites those
registers before any URB arrives.

**0x0051's result was void, and its own instrument was the reason.** The wait was
written as a wrap-safe subtraction on the `sof_count` HIGH byte with a threshold
of one step, described in the comment as "256..511 ms". That is wrong:
`sof_count` FREE-RUNS, so the saved high byte is not a zero to count up from and
the first step arrives 1..256 ms after arming, depending only on where the
counter already was. About 73 % of arm points give a window shorter than the
186.7 ms a calibration needs. The self-capture was started and killed before it
could do anything, on both units.

This is the standing trap in this project -- a null from an instrument that never
fired looks exactly like a refutation -- and it was caught only by re-deriving
the window arithmetic rather than by any arm in the run itself.

0x0052 changed the threshold to two steps, which is 256..511 ms whatever the arm
point. Re-run on unit A:

```
baseline (RST already high, calibrated):
  before re-arm                lead  8797  head DC     -67.8  tail   +34.2
re-arming: drop RST, main loop re-raises it with NO stream open
  after re-arm, +5s settle     lead  8774  head DC     -19.7  tail   +13.4
  after re-arm, +15s settle    lead     0  head DC    +322.6  tail    +2.4
```

Still 8774. With a window now provably longer than a calibration takes, the
self-driven capture produces **no LRCK at all**. Arming the DMA is not what
starts C-port framing; the frames only run when the host is actually moving
isochronous data.

## Conclusion

Two independent attempts, one of them re-run after its instrument was corrected:

1. raise RST and wait (0x0050) -- no calibration
2. raise RST and drive the capture engine (0x0052) -- no calibration

**0x004F is the floor.** The AK5383's offset calibration costs 8960 LRCK edges,
LRCK exists only while a host stream is running, and therefore the calibration
can only ever be spent inside a capture. #201 already moved it to the first
capture of a power-up, which is as far as it goes.

Both units were reverted to that behaviour as 0x0053, which builds to 5949/5947
bytes -- byte-for-byte 0x004F's size, confirming the revert is clean.

The remaining avenue is unchanged and still not recommended: make the C-port
frame without host traffic by driving the DMA and endpoint state machinery
directly, rather than through the stream-open path. That means the machinery
#147 and #186 both had to be careful with, for 183 ms once per power-up.

## Is stock's design the better one? Measured: no, by 2.2 dB

Stock recalibrates at EVERY stream open (Rev 20 `audio_clock_mode_apply` clears
the pair at 0x072F, Rev 22 at 0x0716, both above the mode dispatch and therefore
unconditional). It pays 183 ms on every capture and in exchange always has a
fresh offset. Ours calibrates once per power-up. The fair question is what that
freshness is worth.

Interleaved on unit A, both orders, 8 arms each, request 0x17 forcing the
recalibration. Known-answer arm: recalibrated captures must show ~8800 leading
zeros and latched ones must show 0. Both held, so the stimulus fired.

| | lead zeros | mean abs(head DC) | peak in first 400 ms |
|---|---|---|---|
| recalibrate every open (stock) | 8800 | 76.3 | 367.6 = **-87.2 dBFS** |
| calibrate once (0x0053) | 0 | 200.2 | 473.5 = **-85.0 dBFS** |

A fresh calibration genuinely helps -- mean opening DC is ~2.6x smaller. But on
the peak excursion, which is what the artefact actually is, the difference is
**2.2 dB**.

So stock spends 183 ms of digital black at the top of every capture to improve an
inaudible sub--85 dBFS settling artefact by 2.2 dB. For a recording interface
that is the wrong side of the trade: losing the first 183 ms of a take is a
functional defect, and 2 dB at -85 dBFS is not detectable outside a measurement
rig.

Stock's choice is the CONSERVATIVE one rather than a wrong one -- it never has to
reason about when the reference settled. That is sound for firmware with no
telemetry and no way to measure any of this. It is not the better outcome now
that both sides can be measured.

Note also what is NOT different: both designs re-converge the ADC's high-pass at
every stream open, because neither can clock the part between streams. The
transient's existence is hardware; only its amplitude is a firmware choice, and
the amplitude difference is 2.2 dB.
