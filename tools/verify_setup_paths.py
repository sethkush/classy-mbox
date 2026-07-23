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
        for op in (bytes([0xB4, 0x0A]), bytes([0xBF, 0x0A])):
            if op in window:
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

    checks = [
        ("SET_ADDRESS writes USBFADR from g_pending_address",
         lambda: check_usbfadr_deferred_write(image, pending_slot),
         "SET_ADDRESS status stage completes but device never latches new "
         "address — half-enumeration bug (fixed 2026-07-19)"),
        ("Digi enter-DFU class request has a recognition path",
         lambda: check_digi_dfu_recognition(image),
         "mboxflash --enter-dfu won't be able to soft-reset a running "
         "device; recovery requires physical EEPROM SDA short"),
        ("reply_zero_length() present in code stream",
         lambda: check_reply_zero_length_present(image),
         "SET_CONFIGURATION / SET_INTERFACE / SET_ADDRESS status stages "
         "never send zero-length IN → host times out enumeration"),
        ("boot-time DFU button check wired into main()",
         lambda: check_boot_dfu_button_wired(ihx_path.parent),
         "Second software recovery path missing — a broken USB stack "
         "would leave physical EEPROM SDA short as the only option"),
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
