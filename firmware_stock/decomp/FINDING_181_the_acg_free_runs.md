# The ACG free-runs: the iso endpoints are not adaptive — #181/#182

2026-08-05, build 0x0031, units A (`RK10874600Q`) and B (`RK1672500M`) on one
host controller (`0000:00:1d.0`) on 192.168.1.76.

## The question

Both iso endpoints declare `SYNC_ADAPTIVE`. Adaptive means the endpoint slaves
its converter to the other end of the link. Ours are clocked by the TAS1020B
Adaptive Clock Generator from a fixed 24-bit frequency word, and whether that
generator locks to the USB SOF or free-runs from the crystal had never been
established — `FINDING_179` flagged the same gap from the S/PDIF side and left
it open. Every audio measurement in this project before today was seconds long,
and the symptom of a wrong answer here is drift over minutes.

## Result

    48 kHz    A  -5.339 +/- 1.036 ppm    B  -9.961 +/- 1.008 ppm
              differential +4.623 +/- 1.446 ppm
    44.1 kHz  A -15.292 +/- 0.923 ppm    B -19.238 +/- 0.993 ppm
              differential +3.946 +/- 1.355 ppm

    combined  +4.263 +/- 0.989 ppm  ->  4.3 sigma
    run-to-run agreement  +0.676 +/- 1.982 ppm  ->  0.3 sigma

**The two units run on independent clocks.** Both carry the same firmware and
therefore the same ACG frequency word, so under SOF-locking they would produce
identical rates and a differential of zero. 4.26 ppm apart at 4.3 sigma is
crystal-to-crystal variation.

`SYNC_ADAPTIVE` is wrong. See #185 (correct the declaration) and #186 (an
asynchronous OUT endpoint obliges an explicit feedback endpoint).

## The prediction, stated before the second run

A crystal offset is a fractional property of the crystal and is therefore
**rate-independent**, while an SOF-locked pair would sit near zero at both
rates. So before 44.1 kHz was measured, the prediction on record was: same
sign, same magnitude, ~+4.6 ppm. Observed +3.946, agreeing with the 48 kHz
figure to 0.3 sigma. The rate-independence is the substance of the result, not
the raw separation — 3.2 sigma in one run would not have been enough.

## What does NOT discriminate, recorded so it is not misread later

Both units shifted about -9.6 ppm in common between the rates (A -9.95,
B -9.28). That is the shared mode-2 frequency word (0x204B6A) sitting slightly
low against nominal 44100 — a firmware constant both units carry. It would
appear identically whether or not the ACG were SOF-locked, so it says nothing
about the question. Only the unit-to-unit differential does.

Also not evidence: **zero overruns on either unit across 29 minutes at both
rates.** 4.26 ppm is one sample of slip every ~4.5 s, far too slow to trouble
an ALSA buffer that the capture side self-clocks against. This is precisely why
the defect was invisible until now, and why "it works on the bench" was never
going to answer it.

## Two harness designs that produced confident wrong answers first

**Dividing two timestamps.** The first tool timestamped the first and last
chunk and divided. It read -196 ppm over 60 s and -10 to -15 ppm over 1800 s on
the same hardware. One artifact explains both: ~600 frames missing at the head
of every capture is -196 ppm spread over 60 s and about -7 ppm over 1800 s. The
method has only a head and a tail, so it cannot see that the head is anomalous.

**An asserted error bar.** That same tool's docstring claimed a ~0.02 ppm noise
floor. That is the chunk-quantisation term alone; the real limit is scheduling
jitter on when the reader wakes after `read()`. The measured `ppm_se` came back
at **1.04 ppm — about 23x my estimate**, i.e. ~23 ms of jitter rather than the
1 ms assumed. On the strength of the bad estimate I had argued that 600 s runs
would suffice; at 600 s the error would have been ~5.4 ppm and a 4.3 ppm
differential would have been undecidable. The full 1800 s runs are the only
reason this result exists.

The replacement samples cumulative frames about once a second and least-squares
fits: jitter averages as 1/sqrt(N), a 60 s warm-up is discarded so the startup
deficit falls outside the fit, and the fit yields the standard error of the
slope from the residuals actually observed.

## A verdict bug worth remembering

The comparison printed a binary label at a 3 sigma cliff. On this data:

    48 kHz    3.2 sigma  ->  "free-running"
    44.1 kHz  2.9 sigma  ->  "SOF-locked, defensible"

**Opposite conclusions from two measurements that agree with each other to 0.3
sigma.** Nothing physical differed; a threshold fell between two consistent
numbers. Either line read on its own was enough to send the descriptor work the
wrong way. A cliff-edge label on a continuous quantity invents a distinction the
data does not contain.

Fixed: `--compare` now reports a band and refuses to conclude from one run;
`--combine` does the inverse-variance combination across rates and prints the
run-to-run consistency check **before** the mean, since combining runs that
disagree averages away the disagreement and yields a confident wrong number.
