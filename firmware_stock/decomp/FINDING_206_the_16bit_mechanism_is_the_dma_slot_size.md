# 16-bit: stock's DEVICE never did it, and the mechanism is the DMA slot size

2026-08-21. Prompted by Seth: there is an OS 9 driver for the stock Mbox, Sound
On Sound ran OS 9 screenshots of it, and **it offered 16 or 24 bit**. That put
the 16-bit alternate setting back on the MVP list, and it also suggested a
cheaper route than building it blind -- if the stock DEVICE did 16-bit, the
mechanism is in images this repo rebuilds bit-for-bit.

No hardware was needed for any of this, and none was available: both units were
offline.

## 1. The stock device runs 24-bit and has no runtime switch

Bit depth on this part is set in two places, and neither ever changes:

| register | Rev 20 | Rev 22 | sites |
|---|---|---|---|
| `DMATSL0` 0xFFEA | 0x03 | 0x03 | ONE, at 0x09C8 / 0x08E9 |
| `DMATSL1` 0xFFF0 | 0x03 | 0x03 | ONE, at 0x09D4 / 0x08F5 |
| `IEPCNF1` 0xFF60 | 0xC5 | 0xC5 | 0x03B2, 0x07E4, +computed |
| `OEPCNF2` 0xFF98 | 0xC5 | 0xC5 | 0x03CA, 0x0421, +computed |

`DMATSL = 3` bytes per slot on time slots 0 and 1 = 6 bytes per stereo sample.
`IEPCNF1 = 0xC5` is ISO with the BPS field at 5, which also encodes 6 bytes per
sample. Those agree with each other and with the 288 B/frame stock delivers at
48 kHz.

Read from `XDATA_ACCESS_MAP.md`, i.e. from the DPTR-aware tool rather than a
grep, because these registers are adjacent to ones that ARE written (`DMATSH0`
at 0xFFE9 sits directly below `DMATSL0`) and CLAUDE.md records four tools that
missed real writes by tracking `MOV DPTR,#imm16` alone. The generated map is
also confirmed by reading the init block: every write there carries its own
explicit `MOV DPTR,#imm16` with no `INC DPTR` chain.

**So the 16-bit option in the OS 9 driver was a HOST-SIDE conversion.** The
device sent 24-bit and the driver truncated. That is the honest reading of the
evidence, and it is a correction to the assumption that drove this
investigation -- the driver's capability was real, the device's was not.

## 2. What that costs us, and what it buys

It costs the thing worth having: there is no stock precedent to copy, so
`FINDING_206`'s verdict stands unchanged. A 16-bit mode remains PLAUSIBLE and
UNPROVEN, and #46's doubled sample rates were equally plausible until 30 kHz
came back at 18 kHz.

It buys a mechanism that is much less invasive than #206 assumed. That finding
proposed changing the **C-port's** bits-per-slot so the I2S frame itself became
16-bit, and worried it landed on "the converters follow the clock and have no
way to be told". The register map says the C-port does not have to move at all:

    DMATSL0/1   0x03 -> 0x02      2 bytes per slot instead of 3
    IEPCNF1     0xC5 -> 0xC3      BPS 5 -> 3, i.e. 6 -> 4 bytes/sample
    OEPCNF2     0xC5 -> 0xC3

The C-port keeps carrying 24-bit frames to and from the converters, untouched
and unaware. The DMA takes two bytes out of each three-byte slot. Nothing tells
the AK5383 or AK4393 anything, which is exactly why the #46 failure mode does
not apply here.

## 3. The one thing that must be measured

**Does the DMA take the FIRST two bytes of each slot or the LAST two?**

The first two are the MSBs, and taking them is a clean 16-bit truncation of an
MSB-first I2S word. The last two would be the low 16 bits of a 24-bit sample,
which is noise-shaped garbage at roughly full scale -- the same signature the
capture stream wore for weeks in #147, and equally easy to mistake for a
working path if nobody looks at the RMS.

That is one build, one flash, one power cycle, and one bench session. The rig
for it already exists: `tools/run_147.sh` (one cable, B out1 -> A src1, with a
known-answer arm) and `tools/analyse_147.py`, whose rail statistic is built to
tell a full-scale artifact from real audio.

**Predicted result if the DMA takes the MSBs:** the 16-bit capture reads the
same tone at the same level as the 24-bit capture, about -51 dBFS, with rails
at 0.000%. **If it takes the LSBs:** RMS jumps toward 0 dBFS and the rail
fraction goes non-zero, and the tone bin collapses. Those two outcomes are far
enough apart that a single arm distinguishes them.

## 4. Budget

#206 costed the descriptors at ~100 B, plus the alt-setting handling. The
release build currently sits at **5814 of 6016 bytes**, so 202 B are free. It
fits, but not with room to spare, and a diagnostic build will not fit at all --
which means the measurement build and the shipping build are the same tier
decision that #223 already forced once.

## 5. Why this is worth doing at all

No modern host needs it; every one of them takes 24-bit. Mac OS 9 does not:
Sound Manager is an 8/16-bit world, and 24-bit arrived on the Mac with Core
Audio. A 16-bit alternate setting is therefore the difference between this
firmware working on an OS 9 machine and not -- and OS 9 was the era this device
shipped in.

Note the second obstacle does not go away with it: playback is ASYNCHRONOUS
with an explicit feedback endpoint (#185, measured -- the ACG free-runs from
the crystal), and an era-appropriate class driver may not implement feedback
endpoints at all. Capture is the likelier of the two to survive, because an
async IN needs no feedback endpoint. 16-bit is necessary. Whether it is
sufficient is NOT YET TESTED, and cannot be tested without an OS 9 host.
