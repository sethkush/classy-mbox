# macOS can only ever expose ONE input-source control — and why that is fine

2026-08-03, build 0x0019, Mbox A on macOS 26.5.2 (Apple Silicon) and on
192.168.1.76.

Written because this looks like a firmware bug three separate ways and is not
one, and because the investigation is expensive to repeat.

## The observation

mboxfw declares two UAC1 Selector Units, one per channel (#159). Hosts differ:

  * **Linux / snd-usb-audio** — two enumerated controls, `PCM Capture Source`
    and the same at `index=1`, items Mic / Line / Analog In. Both channels
    independently settable. Works.
  * **macOS / Core Audio** — ONE `Input Source`. Setting it moves channel 1
    only. Channel 2 is unreachable from the host.

## Why: Apple's driver, read rather than guessed

`AppleUSBAudioDevice.cpp` (AppleUSBAudio 273.4.1, the last open-source drop):

    inputSelector = IOAudioSelectorControl::createInputSelector (
        selection, kIOAudioControlChannelIDAll, 0,
        (streamIndex << 16) | (engineIndex << 8) | selectorUnitID);
    ...
    usbAudioEngine->addDefaultAudioControl (inputSelector);

Three facts follow, and together they close the question:

  1. **`kIOAudioControlChannelIDAll`, always.** The driver contains no call
     that creates a per-channel input selector. Per-channel source select is
     not expressible to it, whatever the descriptors say.
  2. **One per engine.** The unit walk `break`s at the first SELECTOR_UNIT it
     finds in a path, and `finished = TRUE` ends the enclosing path loops. Our
     topology puts SU-ch1 on the first path, so SU-ch2 is never examined.
  3. **Items are PATHS, not pins.** `addSelectorSourcesToSelectorControl()`
     populates the control from the paths to the output terminal, and
     `getNameForMixerPath()` names a path through a mixer by concatenating its
     sources (`"A & B"`). The driver's model is "pick a whole signal path",
     not "pick a source for a channel".

Confirmed live rather than inferred. `kAudioObjectPropertyOwnedObjects` on the
device returns two `astr` (streams), one `evis`, and exactly one control:

    CONTROL obj 101 class 'dsrc' scope 'inpt' element 0

and `kAudioDevicePropertyDataSource` returns 4 bytes — one `UInt32`. The array
is one entry because the driver built one control, not because we asked wrong.

## The two structural escapes, and why both are shut

  * **A per-channel control** — the driver has no code for it. Shut by (1).
  * **A second engine**, which would carry its own selector and thus a second
    array entry — needs a second capture stream. The TAS1020B codec port has
    exactly **two** DMA channels: channel 0 = playback (EP2 OUT, `DMACTL0`
    0xFFE8), channel 1 = capture (EP1 IN, `DMACTL1` 0xFFEE). Both are in use.
    Shut by the silicon.

So one entry, permanently, on this hardware.

## The one design that WOULD give host control of both channels

A single Selector Unit whose pins are the nine (ch1, ch2) combinations, each a
stereo Input Terminal. One all-channels control — exactly what the driver
builds — with every combination reachable, on both hosts, fully class
compliant. It works *with* the path model rather than against it.

NOT DONE, deliberately. Costs: nine Input Terminals plus a 9-pin selector
(≈ +20 B), nine `iTerminal` strings (+72 B at cryptic 3-char names, +144 B at
readable ones — required, since all nine share terminal types and would
otherwise render as nine identical entries), and it only fits by deleting the
vendor `TLM_REQ_SET_MUX` request. Headroom at the time was 22 bytes of 6016.

## Why none of that matters much

**The front-panel buttons work on macOS, on both channels.** Verified
2026-08-03 with the unit on the Mac: pressing source-1 moved the mux word
0xF5 → 0xF3 (ch1 line → inst) and source-2 moved it 0xF3 → 0xEB (ch2 mic →
line), with `host mux sets accepted` unchanged at 9 both times — proving the
change came from the panel, not from a host request.

So both channels are fully controllable on macOS. What is limited is only the
*host-side* control, which is a convenience. Spending the vendor request, the
byte budget, readable names and a flash cycle to move a bonus from one channel
to two is a bad trade on a device whose capture path is still broken (#147).

## Loose end worth knowing

Core Audio **caches** the data-source value and does not re-poll. After a
button press its view is stale — it reported `2` (line) while the device had
moved to inst. Cosmetic, and it does not affect audio.

The class-compliant cure is a UAC status interrupt endpoint, which lets the
device tell the host a control changed so it re-reads. That costs an endpoint
and code, and is worth considering only if host/panel disagreement ever
becomes annoying in practice.

## Sources

  * `AppleUSBAudioDevice.cpp`, AppleUSBAudio 273.4.1 —
    https://github.com/nickdowell/AppleUSBAudio-273.4.1
  * `CoreAudio/AudioHardware.h` (macOS 26 SDK) — `kAudioDevicePropertyDataSource`
    is documented as "an array of UInt32s ... currently selected data sources".

**Confidence caveat.** macOS 26 uses closed-source `usbaudiod`, not this kext —
the IO registry shows only a thin `AppleUSBAudioControlNub`. The 273.4.1 code
predicts the observed behaviour exactly (one all-channels control, first
selector in the path), which is strong corroboration, but it is not proof for
the current driver. Anyone revisiting this should re-measure before assuming.


---

# FOLLOW-UP 2026-08-04: the prediction held, and #160 exploits it

This document was written to explain why *two* per-channel Selector Units could
not work on macOS. It also implied what *would*: the driver builds exactly one
input selector, at `kIOAudioControlChannelIDAll`, populated from the **paths**
to the output terminal. #160 built precisely that shape — a single Selector Unit
choosing between the analog input terminal and an S/PDIF input terminal — and it
was measured on macOS 26.5.2 (Apple Silicon), build 0x0021, unit B.

    device 97: Mbox (classc
      uid = AppleUSBAudioEngine:Digidesign:Mbox (classc:RK1672500M:2,1
      input data sources: 2
        id 0x1  name = External Line Connector
        id 0x2  name = External SPDIF Interface
      CURRENT input source: 0x1
      rates: 44100-44100, 48000-48000

Three things worth recording.

**Two sources, one control.** The earlier attempt produced one control that
moved channel 1 only. This produces one control with two meaningful items,
because the choice being offered is now a whole signal path rather than a
per-channel source — which is the model the driver actually has.

**macOS named both items itself**, from the terminal types it parsed: 0x0603
"line connector" and 0x0605 "S/PDIF interface". Linux independently produced
'Line' and 'IEC958 In' from the same two bytes. No string in the firmware
supplies either, and no quirk is loaded on either host.

**The serial is load-bearing in the device UID.** `...:RK1672500M:2,1` — so
macOS keys per-device settings on the iSerialNumber that #178 restored. Two
units with no serial would collide here.

Setting it works from Core Audio:

    AudioObjectSetPropertyData(kAudioDevicePropertyDataSource, input) -> 2
      status 0 (noErr), readback 2
    ... -> 1
      status 0 (noErr), readback 1

**Limit of that last check, stated because it is easy to overclaim.** The
readback is Core Audio's value, and this Mac has no pyusb, so the device-side
codec word was NOT confirmed from here. What the `noErr` does establish is that
AppleUSBAudio issued the SET_CUR and the device did not stall it — a rejected
request surfaces as an error. The device-side half of the same request path is
confirmed on Linux, where the identical control moved `g_codec_state_25` bit
0x25.4 and the clock mode (`FINDING_spdif_input_works.md`).

**Do not conclude that S/PDIF input is usable on macOS yet.** Selecting the
S/PDIF source slaves the clock, but opening a stream sends SET_CUR(48000) on the
sampling-frequency control and drops back to the internal clock — measured on
Linux, task #179. In that state the device is routed to S/PDIF while clocked
internally, which slips a sample every ~4.5 s. The routing is class-compliant
and discoverable; the clocking is not fixed yet.
