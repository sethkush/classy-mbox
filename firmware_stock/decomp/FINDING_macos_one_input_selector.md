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
