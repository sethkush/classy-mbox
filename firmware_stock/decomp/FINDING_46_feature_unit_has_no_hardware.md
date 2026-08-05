# #46, second half: there is no honest Feature Unit to declare — yet

The rates half of #46 shipped in build 0x0024. This is the other half, and the
answer is that a UAC1 Feature Unit cannot currently be declared without lying
to the host. One measurement would change that, and it costs one flash.

## What a Feature Unit can offer, against what this board has

UAC1 §4.3.2.5 defines a Feature Unit's controls as Mute, Volume, Bass, Mid,
Treble, Graphic Equalizer, Automatic Gain, Delay, Bass Boost and Loudness.
Taking them against the hardware this firmware can actually reach:

| control | hardware | verdict |
|---|---|---|
| Mute | IRAM 0x23.2 / 0x23.3, the codec word's audio-path enable | **exists — see below** |
| Volume | none | the codec has no register interface at all |
| everything else | none | no tone, EQ, AGC or delay block exists |

**Volume is not a near miss, it is absent.** The codec's entire control surface
is the 16-bit word in `codec.c`, shifted out bit by bit, and every bit of it is
now accounted for (`FINDING_codec_word_bits_resolved.md`): a mute/enable pair,
a reset, a mono bit, and the source-select nibble. There is no gain field. The
front-panel gain is analog pots on the input stage — the same class of control
as the 48 V switch, which #144 established is mechanical and invisible to
firmware. A Volume control would have to be implemented on the sample data, and
the samples never pass through the 8051: the DMA moves them between the
endpoint buffer and the codec port with no CPU involvement. On a part with
18 bytes of program RAM left, a software mixer is not a trade-off, it is
arithmetic that does not exist.

So the entire Feature Unit question reduces to Mute.

## Mute exists, but as one gate over both directions

`0x23.2`/`0x23.3` are a **global audio-path enable**. That is measured, not
inferred — #171, recorded in `FINDING_bringup_waveform.md` §COMPLETE:

    B on 0x001D, pair HIGH  ->  A ch1 = -28.12 dBFS
    B on 0x001E, pair LOW   ->  A ch1 = -99.21 dBFS

71 dB across two units, same cable, same tone, one variable. The capture half
was settled separately on unit A: with the pair low, 0 of 95232 samples were
non-zero. Both directions die.

A UAC1 Feature Unit sits on **one path**. There is no topology in the class
that expresses "this control affects the other stream too". So:

- a Feature Unit on the playback path would mute capture as a side effect;
- a Feature Unit on the capture path would mute playback as a side effect;
- two Feature Units sharing the one hardware bit would make each control
  silently move the other, which is worse than either.

Every placement ships a control that does something other than what its
descriptor says. That is the reason this half of #46 is not implemented, and it
is a hardware fact rather than a scheduling one.

## The one measurement that would settle it

**#171 never varied the two bits independently.** The `MBOX_NO_MUTE_PAIR` build
removed `orl _g_codec_state_23,#0x0c` — both bits at once — because the question
at the time was whether the pair mattered at all. It does. What was never asked
is whether it is one gate or two.

If `0x23.2` gates playback and `0x23.3` gates capture, then two Feature Units
with a Mute control each are exactly honest, and the second half of #46 becomes
a descriptor change. If both bits gate both directions, the answer above stands
and #46's Feature Unit closes as not-implementable on this hardware.

`make MBOX_MUTE_PAIR_MASK=0x04` and `=0x08` build images that raise one bit
only. The mask is a compile-time constant folded into the single `orl`, so the
variants are the same size as the shipping image and cost nothing at runtime —
this is a flash, not a code change. They report themselves as TLM_BUILD_ID
0x0025 and 0x0026 so block 0 proves which one is running.

The bench arm is the one #171 already validated, run twice:

    B on the variant, A listening    -> does B's OUTPUT survive?
    B on the variant, B capturing    -> does B's INPUT survive?

Four cells across two builds. The outcome that unlocks a Feature Unit is one
bit killing exactly one direction in each build, and the two builds killing
opposite directions.

## Why this is not simply deferred to "when there is headroom"

Headroom is a real constraint — 0x0024 is 5998 of 6016 bytes — but it is not
the binding one here. Even with the whole part free, a Feature Unit over a
global gate would still be a control that lies. The binding constraint is the
measurement above, and it is cheap.
