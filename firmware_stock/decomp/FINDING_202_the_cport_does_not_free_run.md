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
