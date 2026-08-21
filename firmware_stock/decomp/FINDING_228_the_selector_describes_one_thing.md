# #228 — the Selector Unit now describes one thing, and the panel owns the other

2026-08-17. Seth's call, and it is the correct decomposition of the hardware.

## The hardware has two controls, not one

* **S/PDIF vs analog is GLOBAL.** One bit, `0x25.4`, swings the whole capture
  stream between the CS8427 and the ADC. There is **no front-panel button for
  it** — so a host request is the ONLY way to reach the S/PDIF input.
* **mic / line / instrument is PER CHANNEL**, set by the 74HC157 muxes from the
  front-panel source buttons, independently for channel 1 and channel 2.

The Selector Unit had grown to four positions (#160 two, #203 three, #224 four)
and was doing both jobs — badly. A UAC1 Selector has one output and `SET_CUR`
carries one index, so the per-channel state was **inexpressible**, and choosing
an analog position from a host forced BOTH channels to the same front end,
silently discarding a setting made physically at the unit.

Two positions now: 1 = analog, 2 = S/PDIF, keeping their historical meaning.
`selector_apply_position()` no longer touches the mux at all.

## What falls out for free

**#227 is dissolved rather than fixed.** The stale Core Audio source display —
proven real with the ADC as the arm, mic 0.000199 RMS vs instrument 0.003053,
+23 dB, while the host kept saying "Microphone" — happened because a button
changed something the host displayed. Now nothing the panel does is
host-visible, so there is nothing to go stale.

**macOS's one-selector-per-engine limit stops mattering.** `FINDING_macos_one_
input_selector.md` records that Apple's driver only ever builds one input
selector (`kIOAudioControlChannelIDAll`) and that its unit walk breaks at the
first Selector Unit. One selector is now exactly the right number rather than a
compromise, and the nine-combination workaround that finding costed is moot —
it would have required declaring nine source entities for four physical inputs,
which `check_terminal_evidence.py` forbids on principle.

**EP 0x83 is retired** (#207, #214). Its only producer was the button
notification. An interrupt endpoint a host polls every 8 ms forever, that can
never have anything to say, is a capability claim the device does not honour.
`verify_reachability.py` caught `usb_status_notify()` as an orphan the moment
`buttons.c` stopped calling it — the gate found the design consequence before a
human did. #227's NAK-arming lesson is preserved in that finding and must be
re-applied if an asynchronous control is ever added.

## The terminal retype

The remaining analog Input Terminal is **0x0601 ANALOG CONNECTOR**, not 0x0603
LINE. The panel decides which connector is live and the host is never told, so
naming one would be the same class of lie #225 fixed — a device reporting LINE
while sitting on the XLR.

The instrument and microphone Input Terminals and their strings went with the
pins. **Their measurements stand** — instrument 18.9 dB hotter than line at an
identical gain position (`FINDING_196`), XLR 69.9 dB at 1234 Hz against an
un-miked control channel (`FINDING_224`) — and are now cited as evidence for the
one analog terminal. Nothing was un-measured; what changed is that the host no
longer chooses between them, so a terminal per connector is topology no host can
act on. String indices 5 and 9 are retired without reuse, and 6/7/8 keep their
numbers, because a string index is what an already-parsed descriptor points at.

## Two questions answered while doing it

**Can it do S/PDIF and mic at once?** No, and not because of the descriptor.
`0x25.4` swaps the stream, it does not blend. The Selector's one-output rule and
the hardware agree, which is why no Mixer Unit is declared.

**Can the output switch between line and S/PDIF?** No — it is fixed to both.
Every routing bit in the codec word (`0x25.0`–`0x25.5`) is input-side; there is
no output selector in the hardware. Playback feeds the DAC and the CS8427
transmitter in parallel, always, which is why both Output Terminals already
declare `bSourceID = UNIT_FU_PLAYBACK` and why the playback Feature Unit's mute
is master-only: `0x23.3` gates the whole path.

## Cost

| build | before | after |
|---|---|---|
| shipping | 5865 | **5596** |
| provisioning | — | 5297 |
| compile-time serial | — | 5042 |

269 bytes back, 420 free. All 38 gates pass. `TLM_BUILD_ID` 0x0060 -> 0x0061.

## CONFIRMED ON HARDWARE, 2026-08-20

Unit A, flashed from macOS, `bcdDevice 0x0161`:

```
USB Product Name  = "Mbox (class-compliant)"
USB Serial Number = "RK10874600Q"        <- still from EEPROM, survived the flash
CoreAudio Input Source: Analog In
```

Then a front-panel source button was pressed:

| | before | after |
|---|---|---|
| capture RMS | 0.000189 | 0.008028 (**+32.6 dB**) |
| capture peak | 0.001208 | 0.028596 (+27.5 dB) |
| Core Audio Input Source | `Analog In` | `Analog In` |

**The mux moved and the host's view did not change -- and that is now correct
rather than stale.** The control describes analog vs S/PDIF; the source is still
analog, so the device is telling the truth the whole time. Before #228 the host
said "Microphone" while the hardware sat on instrument, which was simply false.

The staleness of #227 is therefore not merely fixed, it is unreachable: there is
no longer any host-visible control a button can desynchronise.

Also confirmed by the same power cycles: the mux boots to MIC (capture came back
at the MIC level after every replug), matching `hw_init` and stock Rev 20
@0x0941/0x0962.
