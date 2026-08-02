# FINDING: the host side of the telemetry path had never been executed

Status: closed by `tools/sim_telemetry_roundtrip.py` (preflight gate 32).
Date: 2026-08-02.

## The blind spot

Five instruments were built in a row that execute the firmware:

| gate | what it executes | what it reads |
|---|---|---|
| `sim_smoke.sh` | boot path | did it reach the main loop |
| `sim_p1_waveform.py` | boot path | the three P1 shift chains |
| `sim_ep0_requests.py` | request path | what a SETUP stages on EP0 |
| `sim_ep0_diff.py` | request path | mboxfw vs the image that enumerated |

Every one runs the **firmware**. Not one runs a line of the **host** side.

`tools/mboxtlm.py` is ~570 lines whose entire job is turning eight bytes into
the sentence a human acts on. It had never been run against a single byte of
input, on hardware or off. Neither had `tools/mboxflash_linux.py`.

The seam matters more than the line count. The firmware writes fields at
offsets in `telemetry.c`; the host reads fields at offsets in `mboxtlm.py`.
Two independently-maintained layouts with nothing between them. A disagreement
does not fail loudly — it produces a *confident wrong reading*, which is the
exact failure the telemetry path was built to avoid. One power cycle costs a
2 km round trip.

## Two real defects, found with nothing plugged in

**1. A stale second reader that looked maintained.**
`tools/mbox_telemetry.py` was a complete, working, unreferenced telemetry
client with the friendlier invocation (`sudo ./mbox_telemetry.py`). It read
`range(5)` blocks. There were 11. Everything from block 5 on silently did not
exist — including block 10, the CS8427 readback that #165 is spending a power
cycle to obtain.

It was not obviously dead: nothing in the tree referenced it, but it was last
touched on 2026-07-28 by the two-unit-workflow commit, so it carried current
PID handling and read as live.

*Resolution:* retired. Its one feature `mboxtlm.py` lacked, `--ep0-test`, is
now `mboxtlm.py ep0test`.

**2. Block 10's decoder was unreachable for its most likely result.**
The firmware answers an out-of-range block index with eight `0xFF` bytes, and
`mboxtlm.show()` treated all-`0xFF` as that sentinel *for every index*.

Block 10 reports eight raw `P3` samples. If CDOUT is not wired — which
`cs8427.c` says outright is unestablished, and is a perfectly possible
answer — every sample reads `0xFF`. The bench tool would have printed

    block 10 -- CS8427 readback probe (#165) -- which pin answered
      (all 0xFF -- unknown block index sentinel)

and never run the decoder that says, in as many words, *NO PIN ANSWERED …
this is a real answer, not a failed guess*. A measurement would have been
reported as a tool/firmware mismatch, on the one power cycle bought to make
it.

*Resolution:* the sentinel now applies only to `index >= NUM_BLOCKS`. For a
block the firmware serves, all-`0xFF` is data.

## What the gate checks

All against the **executing** firmware, not against source text.

1. `TLM_NUM_BLOCKS` / `TLM_REQ_READ` / `TLM_REQ_RESET` / `TLM_REQ_SET_MUX`
   parsed from `telemetry.h` against `mboxtlm.py`'s copies, plus
   `DECODERS` covering exactly `range(NUM_BLOCKS)`.
2. Blocks `0..NUM_BLOCKS-1` requested from the running image over EP0; each
   must stage exactly 8 bytes. The request is built from **mboxtlm's own
   constants**, so this arm fails on its own if the tool drifts — it does not
   lean on check 1.
3. Block `NUM_BLOCKS` must not be served.
4. Every block routed through `tlm.show()` — the function that runs on the
   bench — not `DECODERS[i]`. Must not raise, must produce output, and must
   not print the unknown-block sentinel for a served block.
5. The host's decoded build id must equal `TLM_BUILD_ID`. Firmware `put16`
   against host `u16`, on a value whose two bytes differ, so a byte swap
   cannot pass. The gate also fails if `TLM_BUILD_ID` is ever bumped to a
   palindromic value, which would make this check vacuous.
6. The decodings must discriminate, or the gate reports a constant.
7. `mboxtlm.AUDIO_PIDS` and `mboxflash_linux.AUDIO_PID_ALIASES` agree on the
   alias range — an invariant a comment claimed and nothing checked. The
   comparison excludes `0x1000`, which `mboxtlm` legitimately includes (the
   quirked default PID, where EP0 telemetry works but `snd-usb-audio` never
   binds) and the flasher legitimately does not.

## Mutation results

| mutation | caught by |
|---|---|
| revert the `show()` sentinel fix | block 10 prints the sentinel for a served block |
| `NUM_BLOCKS` 11 → 5 (the retired tool's bug) | constants + block 10 |
| host `u16` reads big-endian | build id 0x0015 read as 0x1500 |
| host `TLM_REQ_READ` 0x10 → 0x14 | constants **and**, independently, every block STALLs |
| a decoder that raises on real bytes | `block 6: IndexError` |

## What this does not show

That the device is on the bus, that pyusb works, or that any transport
succeeded. It shows that the bytes the firmware produces mean, to the reader,
what the firmware meant by them. #165 on real hardware is still the only thing
that proves the CS8427 heard us.

## What is still unexecuted

`tools/mboxflash_linux.py` is imported by this gate but only its constants are
read; its DFU block protocol has never been executed end to end. The macOS
`mboxflash` binary likewise. `sigkill`, `ramflash` and `ramloader` remain
built and never run.
