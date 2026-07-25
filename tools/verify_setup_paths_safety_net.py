#!/usr/bin/env python3
"""
safety_net twin of tools/verify_setup_paths.py.

Pins the enumeration-critical control-transfer plumbing in the
compiled safety_net image. safety_net is the recovery firmware — if
IT can't complete enumeration or answer the Digi enter-DFU class
request, we have no soft recovery path at all, and every mboxfw
brick becomes a physical-EEPROM-short job.

What the mboxfw twin checks, and what we do here
------------------------------------------------
1. USBFADR (0xFFFF) written from a deferred-address slot.
   In safety_net that slot is `_pending_addr` (static __data in
   src/main.c), written by SET_ADDRESS and consumed by the VEC_IEP0
   handler in usb_service().

2. Digi enter-DFU class request (bmReq=0x21, bReq=0x00, wValue=0x000A)
   has a recognition path. In safety_net this is handle_dfu_trigger();
   the fingerprint is the same — a load of wValueL (0xFF2A) paired
   with a cjne against #0x0A.

3. Status-stage helper present. safety_net's helper is `reply_zlp`
   (not `reply_zero_length`) — same body: IEPBCTX0 = 0 → `mov
   dptr,#0xff6b; clr a; movx @dptr,a`. Also accepted: `74 00 F0`.

4. SET_ADDRESS ACKs the status stage. Same shape check as mboxfw:
   after the store into _pending_addr, an LCALL/LJMP to _reply_zlp
   OR an inlined IEPBCTX0 = 0 within ~20 bytes.

5. Boot-time DFU button check — SKIPPED for safety_net. safety_net
   deliberately has no GPIO probing or timers; its whole raison
   d'être IS to be the second recovery path. Adding a "second-second"
   recovery would just be more code to brick.

Usage: python3 tools/verify_setup_paths_safety_net.py \
           [path/to/safety_net.ihx]
"""

import sys
from pathlib import Path

# Reuse every primitive from the mboxfw twin — the byte-level check
# logic is firmware-agnostic; only the symbol names and skip list
# change. Importing keeps the two gates in lock-step: any future
# refinement to the fingerprint (e.g. a new SDCC codegen pattern for
# reply_zero_length) upgrades both at once.
sys.path.insert(0, str(Path(__file__).parent))
from verify_setup_paths import (  # noqa: E402
    parse_ihx,
    resolve_iram_slot,
    resolve_code_symbol,
    check_usbfadr_deferred_write,
    check_reply_zero_length_present,
)


# --- safety_net-specific fingerprints --------------------------------- #
#
# safety_net's handle_setup() reads all four SETUP bytes at once via a
# single `mov dptr,#0xff28` followed by three `inc dptr`s (bmReq / bReq
# / wValueL / wValueH → r7 / r6 / r5 / r4). The mboxfw twin's
# fingerprints assumed a dedicated `mov dptr,#0xff2a` load for wValueL
# and register-agnostic mov-direct opcodes for the pending-address
# store — both patterns are legal SDCC output, both firmwares boot,
# they just took different codegen paths. So the byte-level pattern
# differs; the *check semantics* are identical (does the recognition
# path exist? does the SET_ADDRESS handler ACK?).

def check_digi_dfu_recognition_safety_net(image: bytes) -> bool:
    """
    Pair the batched SETUP-read (`mov dptr,#0xff28`) with a nearby
    `cjne rN,#0x21` (bmReq==0x21) AND a nearby `cjne rN,#0x0A`
    (wValueL==0x0A). Both are the tightest signals in the safety_net
    compile:

      90 FF 28              mov dptr,#SETPACK_BMREQ  ← batched read
      E0 FF A3 E0 FE A3 …   walk the SETUP block
      BF 21 xx              cjne r7,#0x21,not_dfu    ← bmReq test
      BD 0A xx              cjne r5,#0x0a,not_dfu    ← wValueL test

    We require the 0xff28 load AND at least one B8-BF followed by
    0x21 AND at least one B8-BF followed by 0x0A within the next 64
    bytes (handle_setup is ~40 bytes to the DFU dispatch).
    """
    load_bmreq_dptr = bytes([0x90, 0xFF, 0x28])
    i = 0
    while True:
        j = image.find(load_bmreq_dptr, i)
        if j < 0:
            return False
        window = image[j + 3:j + 3 + 64]
        has_21 = any(
            window[k] in range(0xB8, 0xC0) and window[k + 1] == 0x21
            for k in range(len(window) - 1)
        )
        has_0a = any(
            window[k] in range(0xB8, 0xC0) and window[k + 1] == 0x0A
            for k in range(len(window) - 1)
        )
        if has_21 and has_0a:
            return True
        i = j + 1


def check_set_address_acks_safety_net(image: bytes, pending_slot: int,
                                      reply_zlp_addr: int | None) -> bool:
    """
    Same intent as the mboxfw twin, but also accepts SDCC's
    `mov direct, rN` (opcodes 0x88..0x8F) as a valid store into
    _pending_addr. safety_net's `pending_addr = wVL;` compiles to
    `8D 0A` (mov _pending_addr, r5) — the register form — because
    wVL was already sitting in r5 from the batched SETUP read.

    Store opcodes we accept for the pending-address write:
       F5 <slot>          mov direct, a
       85 <src> <slot>    mov direct, direct   (8051 quirk: dst last)
       88..8F <slot>      mov direct, R0..R7
    """
    hits: list[int] = []

    # F5 slot
    i = 0
    while True:
        j = image.find(bytes([0xF5, pending_slot]), i)
        if j < 0: break
        hits.append(j + 2)
        i = j + 1
    # 85 src slot  (dst is second byte after opcode)
    i = 0
    while True:
        j = image.find(bytes([0x85]), i)
        if j < 0: break
        if j + 2 < len(image) and image[j + 2] == pending_slot:
            hits.append(j + 3)
        i = j + 1
    # 88..8F slot  (mov direct, Rn)
    for opc in range(0x88, 0x90):
        i = 0
        while True:
            j = image.find(bytes([opc, pending_slot]), i)
            if j < 0: break
            hits.append(j + 2)
            i = j + 1

    if not hits:
        return False

    lcall_zlp = None
    ljmp_zlp = None
    if reply_zlp_addr is not None:
        hi = (reply_zlp_addr >> 8) & 0xFF
        lo = reply_zlp_addr & 0xFF
        lcall_zlp = bytes([0x12, hi, lo])
        ljmp_zlp = bytes([0x02, hi, lo])   # SDCC tail-calls reply_zlp
    load_zlp_dptr = bytes([0x90, 0xFF, 0x6B])

    for start in hits:
        window = image[start:start + 20]
        if lcall_zlp and lcall_zlp in window:
            return True
        if ljmp_zlp and ljmp_zlp in window:
            return True
        k = window.find(load_zlp_dptr)
        if k >= 0:
            tail = window[k + 3:k + 3 + 4]
            if bytes([0xE4, 0xF0]) in tail or \
               bytes([0x74, 0x00, 0xF0]) in tail:
                return True
    return False


def main() -> int:
    ihx_path = Path(sys.argv[1] if len(sys.argv) > 1
                    else "safety_net/build/safety_net.ihx")
    image = parse_ihx(ihx_path.read_text())

    # safety_net names the deferred-address slot `_pending_addr`
    # (main.c:162), not `_g_pending_address`.
    pending_slot = resolve_iram_slot(ihx_path.parent, "_pending_addr")
    if pending_slot is None:
        print("FAIL: _pending_addr symbol not found — is this actually a"
              " safety_net build? (mboxfw uses _g_pending_address)")
        return 1
    print(f"  _pending_addr IRAM slot: 0x{pending_slot:02X}")

    # safety_net's status-stage helper is _reply_zlp, not
    # _reply_zero_length. Same body (IEPBCTX0 = 0).
    reply_zlp_addr = resolve_code_symbol(ihx_path.parent, "_reply_zlp")
    if reply_zlp_addr is not None:
        print(f"  _reply_zlp code addr: 0x{reply_zlp_addr:04X}")

    checks = [
        ("SET_ADDRESS writes USBFADR from pending_addr",
         lambda: check_usbfadr_deferred_write(image, pending_slot),
         "SET_ADDRESS status ACKs but USBFADR never latched — device"
         " stalls at address assignment, host times out enum."),
        ("SET_ADDRESS handler ACKs status stage via reply_zlp",
         lambda: check_set_address_acks_safety_net(image, pending_slot,
                                                   reply_zlp_addr),
         "reply_zlp() call missing after pending_addr store — status"
         " stage un-ACKed, host retries then abandons enumeration."),
        ("Digi enter-DFU class request has a recognition path",
         lambda: check_digi_dfu_recognition_safety_net(image),
         "safety_net's ONLY job is to answer this request — if the"
         " recognition path compiled out, the recovery firmware itself"
         " has no recovery."),
        ("reply_zlp() present in code stream (IEPBCTX0 = 0)",
         lambda: check_reply_zero_length_present(image),
         "SET_ADDRESS / SET_CONFIGURATION / SET_INTERFACE / DFU-trigger"
         " status stages all rely on it; host times out enumeration"
         " without it."),
    ]

    fails = []
    for name, fn, consequence in checks:
        ok = fn()
        marker = "OK  " if ok else "MISS"
        print(f"  {marker}  {name}")
        if not ok:
            fails.append((name, consequence))

    if fails:
        print(f"\nFAIL: {len(fails)}/{len(checks)} critical enumeration paths"
              f" missing from {ihx_path.name}")
        for name, cons in fails:
            print(f"       {name}")
            print(f"         → {cons}")
        return 1
    print(f"\nPASS: all {len(checks)} enumeration-critical paths present in"
          f" {ihx_path.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
