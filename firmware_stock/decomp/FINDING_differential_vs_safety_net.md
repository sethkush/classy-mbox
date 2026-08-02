# Checking mboxfw against something the device accepted

`tools/sim_ep0_diff.py`, 2026-08-02.

## The blind spot, which was mine

`sim_ep0_requests.py` asks mboxfw a question and checks the answer against
**expectations this project wrote**. That is worth having. It is not the same
as checking against an implementation the hardware accepted.

When that gate was built it explicitly considered a stock arm, measured Rev
20's to be vacuous — it defers standard requests to the work-code dispatcher
and answers every packet identically — wrote a paragraph explaining the
omission, and stopped. The reasoning was correct. The scope was not: having
established that *stock* was a useless comparator, nothing asked whether a
**different** one existed.

`safety_net` was in the tree the whole time. It is mboxfw's sibling — same
architecture, same EP0 buffer at 0xFA18, same handler shape — and it is the
only image here whose USB behaviour is confirmed on the real device: it
enumerated on 2026-07-26 with `bcdDevice 0xDEAD` visible on the bus. It had
never been run through any executed check.

This is the fifth time the answer to "what else can be done without the mbox"
has had the same shape: **the capability was already present, behind a
plausible-sounding reason not to use it.**

## What it does

The same SETUP packets into both images, diffed:

    GET_DESCRIPTOR device    same    12 01 10 01 00 00 00 08
    GET_DESCRIPTOR config    DIFFER  09 02 B4 00 03 01 00 80
                                  vs 09 02 12 00 01 01 00 80
    GET_STATUS device        same    00 00
    SET_ADDRESS 3            same    armed(0), a status ZLP
    undefined bReq 0x0C      same    STALL

The one divergence is recorded and reasoned: safety_net advertises a minimal
configuration, mboxfw a three-interface audio one, so `wTotalLength` and the
interface count differ by design. The **framing** — 9-byte header, type 0x02,
one 8-byte packet — still has to match, and is checked separately.

Unrecorded divergences fail. Stale entries fail too, because an entry that no
longer diverges is a claim that stopped being true.

## What this is evidence of, and what it is not

safety_net is minimal — no audio, no UAC, no telemetry — so the comparison
covers only the shared surface: enumeration, EP0 mechanics, descriptor
plumbing. And "safety_net enumerated" is evidence about *safety_net's* code.
Where mboxfw differs deliberately the diff raises a question; it does not
settle it. That is why divergences are recorded rather than tolerated.

The value is in the hand-porting. Several bugs were found in safety_net first
and copied across by hand — the `IEPCNF0 |= 0x08` stall inversion, the TOGGLE
pair, the abandoned-transfer flush. Hand-porting is exactly what a differential
polices.

## Two false divergences, both mine, both the same cause

The gate reported `undefined bReq 0x0C` as a real difference — mboxfw stalls,
safety_net does not — twice, for two different wrong reasons. safety_net's
dispatcher falls through to `stall()` for any unhandled request, so it was
never plausible.

**First: an asymmetric bound.** safety_net's `usb_service()` is static and it
has no `_buttons_poll`, so instead of a return address the run was bounded on
the third write to `IEPCNF0` — and stopped *at* that write, reading the
register before the store landed. mboxfw, bounded on a real return, read it
after. Both images now get a return address; where there is no loop symbol,
0x0000 serves as a sentinel, since the reset vector is never reached during one
service call.

**Second: an unasserted precondition.** ucSim leaves XDATA uninitialised. At
the settle point then in use, safety_net's `IEPCNF0` read **0x2A** — garbage
that happens to have bit 3 set. The stall detector compares "STALL set now"
against "STALL set before", so a genuine stall was invisible. safety_net
programs `IEPCNF0 = 0x84` only at 0x05C7, later than `IEPBBAX0` at 0x05A5,
which was later than its first write to `USBCTL`.

The gate now asserts the **whole** precondition — `IEPBBAX0 == 0x43` *and*
`IEPCNF0 == 0x84` — before delivering anything, and reports an unsettled image
as its own failure rather than measuring one. The first version asserted only
the buffer base, which is how one unasserted register produced two wrong
results in a row.

## Mutations verified failing

- mboxfw stops stalling unsupported requests → named as a divergence from the
  image that enumerated.
- `bMaxPacketSize0` 8 → 16, a byte the two images agree on → `GET_DESCRIPTOR
  device DIFFER`, unrecorded.
