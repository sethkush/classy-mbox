# ramloader — a RAM-resident image loader for the Mbox 1

## Why

Reaching DFU on this device requires invalidating the EEPROM signature and then
**power cycling** — there is no software path back to the boot ROM (see
POLICY §7 and the FRSTE/SDW analysis below). The Mbox lives 1 km from the
developer, so every power cycle is a 2 km round trip.

The EEPROM signature is now permanently zeroed, so *any* power cycle lands in
bulletproof-DFU with no SDA short. That makes recovery cheap in effort but still
expensive in travel: one trip buys exactly **one** RAM image.

`ramloader` converts that into "one trip buys unlimited images". It is a small
resident program that the boot ROM loads once; thereafter it accepts a new
payload over USB into high program RAM and jumps to it. No further power cycles.

## Verified constraints

Everything here is from primary sources. Do not relax these without re-reading
them.

| Fact | Value | Source |
|---|---|---|
| Program RAM | **6016 bytes** (0x1780) | TAS1020B datasheet, features list and §1 overview — stated twice |
| Program ROM | 8 KB at 0x8000, boot loader | same |
| Internal data RAM | 256 bytes | same |
| SDW survives USB reset | yes | datasheet, enumeration-bits section: "once set to 1, this bit is not affected by subsequent USB resets" |
| FRSTE resets MCU but NOT SDW/CONT | yes | datasheet: "However, the shadow the ROM (SDW) and the USB function connect (CONT) bits are not reset" |
| RAM-launch handoff state | `IE = 0`; `MEMCFG \|= SDW`; `ljmp 0x0000` | TI Utils.SRC `UtilResetCPU` |
| USB engine state at handoff | **NOT cleared** — `USBCTL`, `USBFADR`, `USBSTA` writes are all commented out | TI Utils.SRC `UtilResetCPU` |

Consequence of the last row: a RAM-launched image inherits a **live, connected**
USB engine still using the DFU session's device address. Both the loader and any
payload must therefore drive `USBCTL = 0` early and re-attach, exactly as
mboxfw's `main()` already does.

### Latent bug this exposes

`mboxfw/Makefile` and `safety_net/Makefile` both link with
`--code-size 0x1F00` (7936 bytes) against 6016 bytes of real RAM — over-committed
by 1920 bytes. Nothing has hit it yet (mboxfw is 3399 B), but the linker will
happily produce an image that cannot exist. Fix to `0x1780`.

## Memory map

```
0x0000 ┌────────────────────────────────┐
       │ ramloader                      │  reset + vector table,
       │   0x0000  ljmp loader_start    │  USB EP0 core, download
       │   0x0003  ljmp 0x0803  (INT0)  │  state machine
       │   0x000B  ljmp 0x080B  (T0)    │
       │   0x0013  ljmp 0x0813  (INT1)  │  BUDGET: <= 2048 B
       │   0x001B  ljmp 0x081B  (T1)    │
       │   0x0023  ljmp 0x0823  (UART)  │
       │   0x002B  ljmp 0x082B  (T2)    │
0x0800 ├────────────────────────────────┤
       │ payload                        │  linked --code-loc 0x0800
       │   0x0800  reset vector         │  owns its own vector table
       │   0x0803  INT0 handler         │  at +0x0800
       │   ...                          │  BUDGET: 3968 B
0x1780 └────────────────────────────────┘  end of program RAM
```

`PAYLOAD_BASE = 0x0800`. 3968 bytes is enough for mboxfw (3399 B today) and
ample for safety_net (1796 B).

### Vector forwarding

The 8051 interrupt vectors are fixed at 0x0003/0x000B/0x0013/0x001B/0x0023/0x002B
and the loader necessarily owns them. Each is a bare `ljmp` into the payload's
mirrored table at `+0x0800`. Cost: 3 bytes and ~2 cycles of latency per
interrupt. The payload is built with `--code-loc 0x0800` so SDCC emits its own
vector table there naturally.

While the loader itself is running (before any payload is launched) those
forwarding jumps point at uninitialised RAM. The loader must therefore keep
`EA = 0` except inside its own USB service window, and install a real INT0
handler for its own use — see "Open question 1".

## Wire protocol

EP0 vendor requests, `bmRequestType = 0x40` (vendor / host-to-device / device),
so no interface must be claimed and `snd-usb-audio` cannot interfere.

| bRequest | wValue | data | meaning |
|---|---|---|---|
| `0x01` WRITE | block index | 32 B | write payload bytes at `PAYLOAD_BASE + index*32` |
| `0x02` RUN | — | — | ACK, then jump to `PAYLOAD_BASE` from the main loop |
| `0x03` PING | — | in: 4 B | returns `'R','L', ver, 0` — identifies the loader |

`0x03` is `bmRequestType = 0xC0` (device-to-host). Deliberately mirrors DFU's
32-byte block size so the existing host tooling shape carries over.

RUN must jump from the **main loop**, not the ISR — the same discipline the DFU
trigger needed. Set a flag in the ISR, jump from `main()`.

## Why this doubles as the EP0 test

The loader has to receive ~3.4 KB over EP0 reliably. With the measured
~12 %-per-continuation packet loss that is essentially impossible; with the
`VECINT`-ordering fix it should be routine. **If the loader can download a
payload, the EP0 fix is correct.** No separate experiment needed.

## Open questions — resolve before spending the RAM load

1. **Interrupt ownership during loader-only operation.** Forwarding vectors point
   into empty RAM until a payload is written. Simplest safe answer: have the
   loader's own INT0 handler live at 0x0003 as a real handler, and only *rewrite*
   0x0003 to a forwarding `ljmp` immediately before jumping to the payload —
   program RAM is writable via `movx`, so the loader can patch its own vector
   table. Needs care and a citation.
2. **Is program RAM writable via `movx` at these addresses?** The boot ROM loads
   code there via `dfuCopy()` to `bufferAddr` (`UsbDfu.c:966-980`), which is a
   plain xdata write — strong evidence yes, but confirm the code/xdata overlap
   region in the datasheet memory map before relying on it.
3. **SDCC startup code.** A payload linked at 0x0800 still emits `crt0` expecting
   to run from its own reset vector. Verify what `--code-loc` does to `GSINIT`
   and that no absolute 0x0000 assumptions survive.

## Status

Design only. Nothing built or flashed. Question 2 is the gating one: if program
RAM cannot be written via `movx` from the running image, this approach does not
work and the fallback is one payload per trip.
