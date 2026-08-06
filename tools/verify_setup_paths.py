#!/usr/bin/env python3
"""
Pin the enumeration-critical control-transfer plumbing in a compiled
mboxfw image. Complements verify_usb_init.py (SFR setup) and
verify_descriptors.py (descriptor bundle structure): this file verifies
that the *code paths* required to complete USB enumeration and to
recover a soft-bricked device are actually reachable in the .ihx.

What it checks
--------------
1. USBFADR (0xFFFF) is written from the deferred-address slot (not just
   the `= 0` in usb_init). Rev 20 does this in its VEC_IEP0 handler
   after the SET_ADDRESS status stage ACKs; mboxfw does the same via
   `g_pending_address`. Missing this write is the exact bug that
   left the device half-enumerated on 2026-07-18 (visible VID, no
   PID/bcdDevice, host times out enumeration).

2. Digi's custom enter-DFU class request (bmReq=0x21, bReq=0x00,
   wValue=0x000A on interface 0) has a recognition path in the
   compiled image. Without this, a soft-brick can only be recovered
   by physically shorting the EEPROM SDA line during boot — every
   flash becomes a one-way ticket.

3. The status-stage helper `reply_zero_length` is present — needed for
   SET_ADDRESS, SET_CONFIGURATION, SET_INTERFACE, and the enter-DFU
   ACK. If SDCC dropped it as unused (e.g. from a refactor), those
   requests would silently time out on the host.

Usage: python3 tools/verify_setup_paths.py [path/to/mboxfw.ihx]
"""

import re
import sys
from pathlib import Path


def parse_ihx(text: str) -> bytes:
    chunks = {}
    max_addr = 0
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith(":"):
            continue
        n = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        rec_type = int(line[7:9], 16)
        data = bytes.fromhex(line[9:9 + 2 * n])
        if rec_type == 0x00:
            chunks[addr] = data
            max_addr = max(max_addr, addr + n)
        elif rec_type == 0x01:
            break
    out = bytearray(max_addr)
    for addr, data in chunks.items():
        out[addr:addr + len(data)] = data
    return bytes(out)


def resolve_iram_slot(build_dir: Path, symbol: str) -> int | None:
    """Return the IRAM address SDCC assigned to `symbol`.

    SDCC's global-symbol table lives in the per-source .rst files as
    lines like ``      000028                        236 _symbol:``.
    The .map/.mem files list *sections* but not always every static
    global, so we scan every .rst instead.
    """
    label = f"{symbol}:"
    for rst in build_dir.glob("*.rst"):
        for line in rst.read_text(errors="ignore").splitlines():
            if label not in line:
                continue
            # Line format: "      HHHHHH   ...   NNN _symbol:"
            m = re.match(r"\s*([0-9A-Fa-f]{4,6})\s", line)
            if m:
                v = int(m.group(1), 16)
                if 0 <= v <= 0xFF:  # IRAM only fits in one byte
                    return v
    return None


def check_usbfadr_deferred_write(image: bytes, pending_slot: int) -> bool:
    """
    Look for `mov dptr, #0xffff; mov a, _g_pending_address; movx @dptr, a`.
    The two loads may have an intervening byte; require them within an
    8-byte window and followed by the movx.
    """
    load_dptr = bytes([0x90, 0xFF, 0xFF])
    load_a_from = bytes([0xE5, pending_slot])   # mov a, <direct>
    movx = 0xF0

    i = 0
    while True:
        j = image.find(load_dptr, i)
        if j < 0:
            return False
        window = image[j + 3:j + 3 + 8]
        idx = window.find(load_a_from)
        if idx >= 0 and idx + 2 < len(window) and window[idx + 2] == movx:
            return True
        i = j + 1


def check_digi_dfu_recognition(image: bytes) -> bool:
    """
    handle_digi_enter_dfu() gets called only when handle_setup sees
    bmReq=0x21, bReq=0x00, wValue=0x000A on iface 0. The tightest
    fingerprint SDCC emits is:

      90 FF 2A     mov dptr, #0xff2a   ; SETPACK_WVAL_L
      E0           movx a, @dptr       ; a = wValueL
      [move to r7 optional]
      B4 0A xx     cjne a, #0x0a, ...    OR
      BF 0A xx     cjne r7, #0x0a, ...

    The `mov dptr,#0xff2a` load exists nowhere else in mboxfw (there's
    only one reader of wValueL as a full byte), so pairing it with a
    following cjne-against-0x0A within a small window is a strong
    signal that the recognition path compiled in.
    """
    load_wvall_dptr = bytes([0x90, 0xFF, 0x2A])
    i = 0
    while True:
        j = image.find(load_wvall_dptr, i)
        if j < 0:
            return False
        window = image[j + 3:j + 3 + 12]
        # Accept CJNE against #0x0A in ANY of its addressing forms:
        #   B4 = CJNE A,#data      B6/B7 = CJNE @R0/@R1,#data
        #   B8..BF = CJNE R0..R7,#data
        #
        # This listed only B4 (A) and BF (R7) until 2026-08-03, when adding the
        # Selector Units changed register pressure in handle_setup and SDCC
        # emitted `cjne r6,#0x0a` (B6... no: BE). The recognition path was fully
        # intact and this gate reported MISS on it -- on the CHECK THAT GUARDS
        # THE RECOVERY PATH, whose failure text tells the reader that recovery
        # now needs a physical SDA short. A false alarm there is worse than a
        # silent one: it is the check people learn to wave through.
        #
        # Which register SDCC picks is not a contract and never was. Match the
        # instruction, not the allocator's mood.
        for opcode in [0xB4, 0xB6, 0xB7] + list(range(0xB8, 0xC0)):
            if bytes([opcode, 0x0A]) in window:
                return True
        i = j + 1


def check_boot_dfu_button_wired(build_dir: Path) -> bool:
    """
    check_boot_dfu_button() must exist AND be called from main() — that's
    the second software recovery path (holding source-1 during boot triggers
    EEPROM signature invalidation before any USB code runs). It's the
    hardware escape hatch when enumeration itself is dead. Losing this
    silently would mean a soft-brick could only be recovered by opening
    the box and shorting the EEPROM SDA line.

    Look in main.rst for the call site — SDCC emits `lcall _check_boot_dfu_button`
    (case-sensitive symbol name in the assembly listing).
    """
    main_rst = build_dir / "main.rst"
    if not main_rst.exists():
        return False
    text = main_rst.read_text(errors="ignore")
    # The function must be defined AND called somewhere in main.rst
    # (main.c defines it static so both sites are in the same TU).
    has_def  = "_check_boot_dfu_button:" in text
    has_call = "lcall\t_check_boot_dfu_button" in text or \
               "lcall _check_boot_dfu_button" in text
    return has_def and has_call


def check_reply_zero_length_present(image: bytes) -> bool:
    """
    reply_zero_length() is just `mov dptr, #0xff6b; clr a; movx @dptr, a`
    (IEPBCTX0 = 0). SDCC emits either `E4 F0` (clr a; movx) or `74 00 F0`.
    We accept the presence of either after a mov dptr,#0xff6b within 6
    bytes.
    """
    load_dptr = bytes([0x90, 0xFF, 0x6B])
    i = 0
    while True:
        j = image.find(load_dptr, i)
        if j < 0:
            return False
        window = image[j + 3:j + 3 + 6]
        # clr a + movx
        if bytes([0xE4, 0xF0]) in window:
            return True
        # mov a,#0 + movx
        if bytes([0x74, 0x00, 0xF0]) in window:
            return True
        i = j + 1


def check_set_address_acks(image: bytes, pending_slot: int,
                           reply_zlp_addr: int | None) -> bool:
    """
    The SET_ADDRESS handler must ACK the status stage — either by
    calling reply_zero_length() (LCALL) or by inlining an IEPBCTX0 = 0
    write. The tell-tale is the store to _g_pending_address: `85 <src>
    <slot>` (mov direct,direct) OR `F5 <slot>` (mov direct,a). Right
    after that store, within ~20 bytes, we require either an LCALL
    to _reply_zero_length or the inlined `mov dptr,#0xff6b; ... movx`
    sequence.

    Without this ACK the SET_ADDRESS status IN packet never ships,
    host times out the enumeration retry cycle, and the device fails
    to advance past address assignment. Removing the call while the
    reply_zero_length function still exists (called from other
    handlers) slips past a bare presence-check.
    """
    hits = []
    # `mov <pending_slot>, a`   → F5 slot
    i = 0
    while True:
        j = image.find(bytes([0xF5, pending_slot]), i)
        if j < 0: break
        hits.append(j + 2)
        i = j + 1
    # `mov <pending_slot>, <src>`   → 85 src slot   (8051 quirk: dst last)
    i = 0
    while True:
        j = image.find(bytes([0x85]), i)
        if j < 0: break
        if j + 2 < len(image) and image[j + 2] == pending_slot:
            hits.append(j + 3)
        i = j + 1
    if not hits:
        return False

    lcall_zlp = None
    ljmp_zlp = None
    if reply_zlp_addr is not None:
        hi, lo = (reply_zlp_addr >> 8) & 0xFF, reply_zlp_addr & 0xFF
        lcall_zlp = bytes([0x12, hi, lo])
        # SDCC tail-call-optimizes `reply_zero_length(); break;` at the
        # end of a switch case into LJMP.
        ljmp_zlp  = bytes([0x02, hi, lo])
    load_zlp_dptr = bytes([0x90, 0xFF, 0x6B])

    for start in hits:
        window = image[start:start + 20]
        if lcall_zlp and lcall_zlp in window:
            return True
        if ljmp_zlp and ljmp_zlp in window:
            return True
        # Inlined IEPBCTX0 = 0: dptr load then clr-a/movx or mov #0/movx.
        k = window.find(load_zlp_dptr)
        if k >= 0:
            tail = window[k + 3:k + 3 + 4]
            if bytes([0xE4, 0xF0]) in tail or bytes([0x74, 0x00, 0xF0]) in tail:
                return True
    return False


def resolve_code_symbol(build_dir: Path, sym: str) -> int | None:
    """Return the CODE address of `sym`. Static functions don't land in
    the .map — search .rst listings too (line-labeled form:
    `<addr>                        NNN <sym>:`)."""
    map_file = build_dir / "mboxfw.map"
    if map_file.exists():
        with map_file.open() as f:
            for line in f:
                m = re.search(r'\bC:\s*([0-9A-Fa-f]{4,8})\s+' +
                              re.escape(sym) + r'\b', line)
                if m:
                    v = int(m.group(1), 16)
                    if 0 <= v <= 0xFFFF:
                        return v
    label = re.compile(r'^\s*([0-9A-Fa-f]{4,6})\s+\d+\s+' +
                       re.escape(sym) + r':\s*$')
    for rst in build_dir.glob("*.rst"):
        with rst.open() as f:
            for line in f:
                m = label.match(line)
                if m:
                    v = int(m.group(1), 16)
                    if 0 <= v <= 0xFFFF:
                        return v
    return None


def main() -> int:
    ihx_path = Path(sys.argv[1] if len(sys.argv) > 1
                    else "mboxfw/build/mboxfw.ihx")
    image = parse_ihx(ihx_path.read_text())

    pending_slot = resolve_iram_slot(ihx_path.parent, "_g_pending_address")
    if pending_slot is None:
        print("FAIL: _g_pending_address symbol not found in .map — is the"
              " SET_ADDRESS deferred-write patch even in this build?")
        return 1
    print(f"  _g_pending_address IRAM slot: 0x{pending_slot:02X}")
    reply_zlp_addr = resolve_code_symbol(ihx_path.parent, "_reply_zero_length")
    if reply_zlp_addr is not None:
        print(f"  _reply_zero_length code addr: 0x{reply_zlp_addr:04X}")

    checks = [
        ("SET_ADDRESS writes USBFADR from g_pending_address",
         lambda: check_usbfadr_deferred_write(image, pending_slot),
         "SET_ADDRESS status stage completes but device never latches new "
         "address — half-enumeration bug (fixed 2026-07-19)"),
        ("SET_ADDRESS handler ACKs status stage via reply_zero_length",
         lambda: check_set_address_acks(image, pending_slot, reply_zlp_addr),
         "Removing the reply_zero_length() call after the pending-address "
         "store leaves the status stage un-ACKed — host times out enum."),
        ("Digi enter-DFU class request has a recognition path",
         lambda: check_digi_dfu_recognition(image),
         "mboxflash --enter-dfu won't be able to soft-reset a running "
         "device; recovery requires physical EEPROM SDA short"),
        ("reply_zero_length() present in code stream",
         lambda: check_reply_zero_length_present(image),
         "SET_CONFIGURATION / SET_INTERFACE / SET_ADDRESS status stages "
         "never send zero-length IN → host times out enumeration"),
        # REMOVED 2026-08-05 with the boot-button DFU trigger itself.
        #
        # This check asserted that a second software recovery path existed.
        # It never did: BRICK_LOG records the button trigger being tried on
        # three separate incidents and producing nothing every time, while
        # every actual recovery was the SDA short. The consequence text was
        # also wrong to treat "physical EEPROM SDA short" as a bad outcome --
        # that is the canonical, hardware-proven recovery (BRICK_LOG top:
        # short -> ffff:fffe -> safety_net_bootstrap -> app-DFU -> image),
        # and it needs no firmware cooperation at all.
        #
        # A gate that requires a feature which never worked is not protecting
        # anything; it just makes removing dead code look like a regression.
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
