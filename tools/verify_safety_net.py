#!/usr/bin/env python3
"""
Verify the safety_net firmware's USB init sequence at the bytecode
level. Parallel to verify_usb_init.py, but tuned to safety_net's
smaller, single-file init.

Rationale: safety_net has zero other gate coverage (every other gate
hardcodes mboxfw.ihx). Without this, changes to safety_net/src/main.c
land unchecked and the flash toolchain provides no assurance the
enumeration-critical writes actually made it into the emitted image.

Checks fall into four families:

  ASSIGN(sfr, value)
    Look for `mov dptr,#SFR` (90 hi lo) with a matching `mov a,#value`
    (74 vv) or `clr a` (E4, for value == 0) within a nearby window,
    followed by `movx @dptr,a` (F0). Also accepts SDCC's `inc dptr`
    trick when the previous statement wrote sfr-1.

  RMW_OR(sfr, mask)
    Look for `mov dptr,#SFR; movx a,@dptr; orl a,#mask; movx @dptr,a`
    (90 hi lo E0 44 mm F0).

  BIT_SET(bit_addr)  /  BIT_CLR(bit_addr)
    Look for the bit-addressable opcode `D2 aa` (setb) or `C2 aa`
    (clr) at the 8051 bit address (IE.7 = 0xAF, IE.0 = 0xA8,
    TCON.0 = 0x88).

  PRESENT(sfr)
    Weakest check — just that the SFR address appears as a
    `mov dptr,#SFR`. Used only for writes where SDCC does something
    surprising (e.g. IEPBCTX0 = 0x80 emitted as `rr a` when A=1
    happens to be live).

Usage: python3 tools/verify_safety_net.py [path/to/safety_net.ihx]
"""

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


def _dptr(sfr: int) -> bytes:
    return bytes([0x90, (sfr >> 8) & 0xFF, sfr & 0xFF])


def check_assign(image: bytes, sfr: int, value: int) -> bool:
    """
    Assignment: `mov dptr,#SFR; [load a]; movx @dptr,a`.

    Accepts two A-load forms:
      value == 0  → `clr a`      (opcode E4)
      value != 0  → `mov a,#val` (opcode 74 vv)

    Also accepts SDCC's `inc dptr` optimization: if SFR-1 has a
    `mov dptr,#SFR-1` immediately preceding, and the block ends with
    `inc dptr; [load a]; movx @dptr,a`, treat that as a write to SFR.
    """
    dptr = _dptr(sfr)
    dptr_prev = _dptr(sfr - 1)
    inc_dptr = 0xA3
    movx_wr = 0xF0
    clr_a = 0xE4
    load_a = bytes([0x74, value])

    def has_load_near(img: bytes, j: int, movx_off: int) -> bool:
        # Look between the mov-dptr end and the movx for A-load; also
        # accept the load in the 16 bytes before the mov-dptr (SDCC may
        # hoist).
        middle = img[j:j + movx_off]
        after_dptr = middle[3:] if len(middle) >= 3 else b""
        if value == 0 and clr_a in after_dptr:
            return True
        if load_a in after_dptr:
            return True
        before = img[max(0, j - 16):j]
        if value == 0 and clr_a in before:
            return True
        if load_a in before:
            return True
        return False

    # Direct mov dptr,#SFR pattern.
    i = 0
    while True:
        j = image.find(dptr, i)
        if j < 0:
            break
        # Find movx within 16 bytes.
        window = image[j + 3:j + 3 + 20]
        for off, b in enumerate(window):
            if b == movx_wr:
                if has_load_near(image, j, 3 + off):
                    return True
                break
        i = j + 1

    # inc-dptr optimization from sfr-1.
    i = 0
    while True:
        j = image.find(dptr_prev, i)
        if j < 0:
            return False
        window = image[j + 3:j + 3 + 32]
        # Look for inc-dptr followed by an A-load and movx.
        for off, b in enumerate(window):
            if b == inc_dptr:
                sub = window[off + 1:off + 1 + 16]
                mov_off = sub.find(bytes([movx_wr]))
                if mov_off >= 0:
                    middle = sub[:mov_off]
                    if (value == 0 and clr_a in middle) or (load_a in middle):
                        return True
                    # Also accept hoisted load (16 bytes before mov-dptr).
                    before = image[max(0, j - 16):j]
                    if (value == 0 and clr_a in before) or (load_a in before):
                        return True
        i = j + 1


def check_rmw_or(image: bytes, sfr: int, mask: int) -> bool:
    """RMW OR: the naive `mov dptr,#SFR; movx a,@dptr; orl a,#mask; movx
    @dptr,a`, OR a `dec dpl` DPTR-reuse variant SDCC emits when it just
    wrote to SFR+1: `dec dpl; movx a,@dptr; mov rN,a; orl arN,#mask;
    mov dptr,#SFR; mov a,rN; movx @dptr,a`. Both produce the same
    read-OR-write on the SFR."""
    naive = _dptr(sfr) + bytes([0xE0, 0x44, mask, 0xF0])
    if naive in image:
        return True
    # dec-dpl reuse: search for `dec dpl (0x15 0x82); movx a,@dptr (0xE0);
    # mov rN,a (0xF8..0xFF); orl aRn,#mask (0x48..0x4F imm); mov dptr,#SFR;
    # mov a,rN (0xE8..0xEF); movx @dptr,a (0xF0)`.
    for i in range(len(image) - 12):
        if image[i:i+3] != bytes([0x15, 0x82, 0xE0]):
            continue
        # mov rN, a  (0xF8..0xFF)
        if not (0xF8 <= image[i+3] <= 0xFF):
            continue
        # OR either `orl aRn,#imm` (0x48+Rn, imm) OR `orl direct,#imm`
        # (0x43 dir imm) where dir is R0..R7 in default bank (0x00..0x07).
        # SDCC picks the direct form when it saves a byte.
        or_len = 0
        if 0x48 <= image[i+4] <= 0x4F and image[i+5] == mask:
            or_len = 2
        elif image[i+4] == 0x43 and image[i+5] <= 0x07 and image[i+6] == mask:
            or_len = 3
        if or_len == 0:
            continue
        # mov dptr,#SFR (shifted by or_len - 2 since naive path assumes 2)
        base = i + 4 + or_len
        if image[base:base+3] != _dptr(sfr):
            continue
        # mov a, rN (0xE8..0xEF); movx @dptr,a (0xF0)
        if not (0xE8 <= image[base+3] <= 0xEF) or image[base+4] != 0xF0:
            continue
        return True
    return False


def check_bit(image: bytes, opcode: int, bit_addr: int) -> bool:
    """`setb <bit>` (D2 aa) or `clr <bit>` (C2 aa)."""
    return bytes([opcode, bit_addr]) in image


def check_present(image: bytes, sfr: int) -> bool:
    """SFR appears as `mov dptr,#SFR` at least once."""
    return _dptr(sfr) in image


# Source of truth for every USB-init-critical write safety_net emits.
# (kind, sfr_or_bitaddr, value_or_none, description)
CHECKS = [
    ("assign", 0xFFFC, 0x00,
        "USBCTL = 0 disconnect at top of main (Rev 20 0x08E5)"),
    # GLOBCTL bit 0 is CPTEN (codec port enable), NOT USB enable —
    # verified against TI RomBoot.c line 33 "GLOBCTL = 0x04; // 12Mclk,
    # Ext int off, LPWR on, CODEC is off" (2026-07-25). safety_net has
    # no codec, must NOT set CPTEN. Boot ROM's GLOBCTL=0x04 already has
    # LPWR (bit 2 = USB power) on. Deliberately no GLOBCTL write from
    # safety_net now — this is the fix for the silent-USB-on-cold-boot
    # bug where CPTEN with unconfigured codec regs perturbed the USB
    # power domain via cross-coupling.
    ("rmw_or", 0xFFB0, 0x01,
        "MEMCFG |= 0x01 SDW confirm (idempotent, boot-ROM set)"),
    ("assign", 0xFF69, 0x43,
        "IEPBBAX0 = 0x43 (EP0 IN buffer @ 0xFA18/8)"),
    ("assign", 0xFF6A, 0x01,
        "IEPBSIZ0 = 0x01 (EP0 IN size 8/8, via inc dptr)"),
    ("assign", 0xFFA9, 0x42,
        "OEPBBAX0 = 0x42 (EP0 OUT buffer @ 0xFA10/8)"),
    ("assign", 0xFFAA, 0x01,
        "OEPBSIZ0 = 0x01 (EP0 OUT size 8/8, via inc dptr)"),
    ("present", 0xFF6B, None,
        "IEPBCTX0 write present (0x80 NAK — SDCC may emit `rr a` if A=1 live)"),
    ("assign", 0xFFAB, 0x00,
        "OEPBCTX0 = 0 (EP0 OUT ready to receive)"),
    ("assign", 0xFF68, 0x84,
        "IEPCNF0 = 0x84 (UBME|UBMIE, no STALL, TI UsbEng.c:614)"),
    ("present", 0xFFA8, None,
        "OEPCNF0 write present (0x84 — SDCC may reuse A from IEPCNF0)"),
    ("assign", 0xFFFF, 0x00,
        "USBFADR = 0 (initial address)"),
    ("assign", 0xFFFD, 0xE5,
        "USBIMSK = 0xE5 (TI UsbEng.c:640; Rev 20 uses 0x9F, safety_net "
        "deliberately diverges — STPOW-driven SETUP dispatch)"),
    ("bit_clr", 0x88, None,
        "IT0 = 0 level-triggered INT0 (TI UsbEng.c:644)"),
    ("bit_set", 0xA8, None,
        "EX0 = 1 enable INT0"),
    ("bit_set", 0xAF, None,
        "EA = 1 unmask global interrupts"),
    ("rmw_or", 0xFFFC, 0x80,
        "USBCTL |= 0x80 attach (Rev 20 0x0AE2, RMW not assign)"),
]


def run(image: bytes) -> tuple[int, int, list[str]]:
    passed = 0
    fails: list[str] = []
    for kind, addr, value, note in CHECKS:
        if kind == "assign":
            ok = check_assign(image, addr, value)
            label = f"0x{addr:04X} = 0x{value:02X}"
        elif kind == "rmw_or":
            ok = check_rmw_or(image, addr, value)
            label = f"0x{addr:04X} |= 0x{value:02X}"
        elif kind == "bit_set":
            ok = check_bit(image, 0xD2, addr)
            label = f"setb bit 0x{addr:02X}"
        elif kind == "bit_clr":
            ok = check_bit(image, 0xC2, addr)
            label = f"clr bit 0x{addr:02X}"
        elif kind == "present":
            ok = check_present(image, addr)
            label = f"0x{addr:04X} written (any pattern)"
        else:
            raise ValueError(kind)
        marker = "OK  " if ok else "MISS"
        print(f"  {marker}  {label:<28}  {note}")
        if ok:
            passed += 1
        else:
            fails.append(f"{label} — {note}")
    return passed, len(CHECKS), fails


def main() -> int:
    ihx_path = Path(sys.argv[1] if len(sys.argv) > 1
                    else "safety_net/build/safety_net.ihx")
    if not ihx_path.exists():
        print(f"FAIL: {ihx_path} not found — run `make -C safety_net`",
              file=sys.stderr)
        return 2
    image = parse_ihx(ihx_path.read_text())
    passed, total, fails = run(image)
    if fails:
        print(f"\nFAIL: {len(fails)}/{total} safety_net init writes missing"
              f" from {ihx_path.name}")
        return 1
    print(f"\nPASS: all {total} safety_net init writes present"
          f" in {ihx_path.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
