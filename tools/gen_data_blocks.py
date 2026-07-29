#!/usr/bin/env python3
"""
Emit the stock firmwares' constant data blocks as annotated assembly candidates.

The decompilation reproduces every INSTRUCTION byte of both images, but three
blocks per image are pure data and existed only inside the .bin: the USB
descriptor block, the VECINT interrupt dispatch table, and Keil's ?C_INITSEG
initialiser table. Without them the project can say "we can compile every
instruction of this firmware" but not "we can rebuild this ROM".

This generator closes that. It reads the bytes out of the stock image and
writes a candidate per block, decoded to field level rather than dumped as
hex -- a descriptor walked by bLength/bDescriptorType, a vector table labelled
with the TI interrupt-source constant each slot answers to, an initialiser
table split into its three-byte records. The bytes are transcribed by a program
so they cannot be mistyped; the structure around them is what makes the result
worth reading.

    python3 tools/gen_data_blocks.py            # regenerate all six

Regenerating is expected to be a no-op. The files are committed because they
are source, and the byte match is what proves the transcription.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware_stock")
CAND = os.path.join(FW, "decomp", "cand")

# (image, symbol, start, end_inclusive, kind)
BLOCKS = [
    ("rev20", "usb_descriptor_block", 0x0596, 0x0727, "desc"),
    ("rev22", "usb_descriptor_block", 0x057D, 0x070E, "desc"),
    ("rev20", "vecint_dispatch_table", 0x0C93, 0x0CDC, "vect"),
    ("rev22", "vecint_dispatch_table", 0x0C7D, 0x0CC6, "vect"),
    ("rev20", "c51_initseg_table", 0x0F9C, 0x0FC3, "init"),
    ("rev22", "c51_initseg_table", 0x0FBA, 0x0FE1, "init"),
]

DESC_TYPE = {1: "DEVICE", 2: "CONFIGURATION", 3: "STRING", 4: "INTERFACE",
             5: "ENDPOINT", 6: "DEVICE_QUALIFIER", 7: "OTHER_SPEED_CONFIG",
             0x21: "HID", 0x24: "CS_INTERFACE", 0x25: "CS_ENDPOINT"}

# TI's interrupt-source constants, from
# reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h. The table is indexed by
# the VECINT value directly, not by value/2.
VECINT_NAME = {0x00: "OEP0", 0x01: "OEP1", 0x02: "OEP2", 0x03: "OEP3",
               0x04: "OEP4", 0x05: "OEP5", 0x06: "OEP6", 0x07: "OEP7",
               0x08: "IEP0", 0x09: "IEP1", 0x0A: "IEP2", 0x0B: "IEP3",
               0x0C: "IEP4", 0x0D: "IEP5", 0x0E: "IEP6", 0x0F: "IEP7",
               0x10: "STPOW", 0x11: "(reserved)", 0x12: "SETUP", 0x13: "PSOF",
               0x14: "SOF", 0x15: "RESR", 0x16: "SUSR", 0x17: "RSTR",
               0x18: "CPRX", 0x19: "CPTX", 0x1A: "DPRX", 0x1B: "DPTX",
               0x1C: "I2CRX", 0x1D: "I2CTX", 0x1E: "(reserved)", 0x1F: "XINT",
               0x20: "(undefined)", 0x21: "(undefined)", 0x22: "(undefined)",
               0x23: "(undefined)", 0x24: "NO_INT"}


def db(bs, comment=""):
    body = ", ".join(f"0x{b:02X}" for b in bs)
    return f"        .db {body}" + (f"    ; {comment}" if comment else "")


def render_desc(data, base):
    """Walk the block as USB descriptors: bLength, bDescriptorType, payload."""
    out, i = [], 0
    while i < len(data):
        n = data[i]
        if n == 0 or i + n > len(data):
            # Not a descriptor header. Emit the remainder as raw bytes rather
            # than guessing -- the block has padding and at least one region
            # nothing dereferences.
            rest = data[i:]
            out.append(f"        ; 0x{base + i:04X}: {len(rest)} bytes that do "
                       f"not parse as a descriptor header")
            for k in range(0, len(rest), 8):
                out.append(db(rest[k:k + 8], f"0x{base + i + k:04X}"))
            break
        t = data[i + 1] if n >= 2 else 0
        name = DESC_TYPE.get(t, f"type 0x{t:02X}")
        out.append("")
        out.append(f"        ; 0x{base + i:04X}  {name}, bLength {n}")
        if t == 3 and n > 2:                      # STRING: show the text
            txt = bytes(data[i + 2:i + n:2]).decode("latin-1", "replace")
            out.append(f"        ;   \"{txt}\"")
        for k in range(0, n, 8):
            out.append(db(data[i + k:i + min(k + 8, n)]))
        i += n
    return out


def render_vect(data, base):
    out = ["        ; 37 entries, two bytes each, indexed by the VECINT value",
           "        ; DIRECTLY (not value/2). Each is the address the USB",
           "        ; interrupt dispatcher jumps to for that source."]
    for e in range(len(data) // 2):
        hi, lo = data[2 * e], data[2 * e + 1]
        tgt = (hi << 8) | lo
        nm = VECINT_NAME.get(e, f"slot 0x{e:02X}")
        out.append(db(data[2 * e:2 * e + 2],
                      f"[{e:2d}] {nm:<11} -> 0x{tgt:04X}"))
    return out


def render_init(data, base):
    out = ["        ; Keil ?C_INITSEG: three-byte records, count then the",
           "        ; IRAM address to clear, terminated by a zero count."]
    i = 0
    while i + 2 < len(data) and data[i] != 0:
        out.append(db(data[i:i + 3],
                      f"clear {data[i]} byte(s) at IRAM 0x{data[i + 2]:02X}"))
        i += 3
    out.append(db(data[i:], "terminator"))
    return out


RENDER = {"desc": render_desc, "vect": render_vect, "init": render_init}

WHY = {
 "desc": ("The USB descriptor block. Note the device never SERVES most of it:\n"
          " * GET_DESCRIPTOR(CONFIGURATION) returns the vendor-class config,\n"
          " * and the complete UAC block in here is dead data -- no CODE pointer\n"
          " * in std_get_descriptor targets it. It is reproduced because it is in\n"
          " * the ROM, not because the firmware reads it."),
 "vect": ("The USB interrupt dispatch table. The single functional difference\n"
          " * between the two firmwares lives in entry 20 (SOF): Rev 20 points it\n"
          " * at a one-byte RET stub, Rev 22 at its playback-DMA watchdog."),
 "init": ("Keil's C51 startup initialiser table, interpreted by the runtime at\n"
          " * boot to zero IRAM locations. Identical content in both images."),
}


def main():
    for image, sym, lo, hi, kind in BLOCKS:
        data = open(os.path.join(FW, f"{image}_firmware_code.bin"), "rb").read()
        blk = data[lo:hi + 1]
        name = f"{'' if image == 'rev20' else 'rev22_'}{sym}"
        body = RENDER[kind](blk, lo)
        txt = [
            f"// MATCH: image={image} addr=0x{lo:04X} len={len(blk)} func={name}",
            "/* GENERATED by tools/gen_data_blocks.py -- do not hand-edit the",
            " * bytes; regenerate instead. Transcribed from the stock image so",
            " * they cannot be mistyped, and proved by the byte match.",
            " *",
            f" * {WHY[kind]}",
            " *",
            " * Emitted from a __naked function so the bytes land in this",
            " * candidate's code segment, which link51 places at the stock",
            " * address like any other candidate. */",
            f"void {name}(void) __naked {{",
            "    __asm",
        ] + body + [
            "    __endasm;",
            "}",
        ]
        path = os.path.join(CAND, f"{name}.c")
        open(path, "w").write("\n".join(txt) + "\n")
        print(f"  {image} 0x{lo:04X}..0x{hi:04X}  {len(blk):3d} B  -> "
              f"{os.path.relpath(path, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
