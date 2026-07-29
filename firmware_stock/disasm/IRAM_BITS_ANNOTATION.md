# IRAM bit-addressable locations — bytes 0x20, 0x22–0x26

Companion to `IRAM_LOW_ANNOTATION.md` (layout), `MUX_IRAM22_ANNOTATION.md` and
`IRAM23_IRAM25_ANNOTATION.md`. Every access site is in `LEDGER_WORKLIST.txt`.

Reminder, because this trips every pass: the bit address of IRAM byte B bit N is
`(B - 0x20) * 8 + N`. Bit ops print the bit address, byte ops print the byte
address, and they look identical in a listing.

## Byte 0x20 bits — the previous-P3 sample

Byte 0x20 holds the last P3 read (see the button handler in
`IRAM23_IRAM25_ANNOTATION.md`). Its bits are tested individually as the "was it
already pressed" half of the edge detection:

    0x20.3   Rev 20 0x0EED  JB 0x03  -> ch1 source button, previous state
    0x20.4   Rev 20 0x0EFA  JB 0x04  -> ch2 source button, previous state
    0x20.5   Rev 20 0x0EE0  JB 0x05  -> mono button, previous state

**0x20.1 is a fourth input, and it is not one of the three panel buttons:**

    Rev 20 0x0AEC  JB  0x01,0x0AFC
    Rev 20 0x0AFC  JNB 0x01,0x0B0D

That is P3.1, tested twice in the function at 0x0A96 — the same function that
owns the overlaid locals 0x27–0x2B. P3.1 is therefore an input the firmware
reads and acts on, distinct from the source and mono buttons. This is the first
place to look for the TRS jack-presence switches; what it physically connects
to is not determined by the firmware.

## 0x22.7 — panel shift word, top bit

    Rev 20 0x03A0  CLR  0x17     (boot init, then published via LCALL 0x0F0C)
    Rev 20 0x03E6  SETB 0x17     (then published immediately at 0x03E8)

Only two sites, both followed by a panel republish, so this bit is shifted out
to the panel hardware and nothing in the firmware ever reads it back. Cleared
at boot, set at one point. Rev 22 at 0x03A4 / 0x03EA.

## Byte 0x23 bits 0–4 — codec control word

Byte 0x23 is the codec control shift word (consumer Rev 20 0x0E62). These bits
are written and never read, so the firmware's use of them is fully described by
where they are set and cleared; what they switch is inside the codec.

    0x23.0   SETB Rev 20 0x07B8   Rev 22 0x0796    set once, adjacent to 0x23.1
    0x23.1   SETB Rev 20 0x07BA   Rev 22 0x0798    set once, adjacent to 0x23.0
    0x23.4   SETB Rev 20 0x0840   Rev 22 0x09E5    set once

    0x23.2   CLR  Rev 20 0x072F   SETB 0x07EE, 0x0831
    0x23.3   CLR  Rev 20 0x0731   SETB 0x07F0, 0x0833
             (Rev 22: CLR 0x0716/0x0718, SETB 0x07CF/0x07D1, 0x09D8/0x09DA)

0x23.2 and 0x23.3 are the **#147 pair**. They move together at all three sites
in each image and never individually, so they are a two-bit field or a matched
pair of channel switches — a single mute would not need two.

Recorded honestly: the firmware-side account is complete (every site
enumerated, always paired, write-only), and what they switch in the codec is
NOT established. The #147 change made mboxfw set them where stock does, which
is correct on parity grounds and never depended on knowing their meaning. The
"44.1 kHz mute" reading was inference from timing and remains unproven.

## Byte 0x25 bits 4–7

Established in `IRAM23_IRAM25_ANNOTATION.md`; claimed here for completeness.

    0x25.4   input to 0x22.6; also tested at Rev 20 0x0076, 0x0485, 0x049F
    0x25.5   input to 0x22.6            0x22.6 = !(0x25.4) && !(0x25.5)
    0x25.6   gates the boot init run — Rev 20 0x038F JB immediately precedes
             the 0x0395–0x03AD initialisation
    0x25.7   two toggle sites, Rev 20 SETB 0x083E/0x0850/0x0C8D,
             CLR 0x084B/0x0C4F; 0x084B is the instruction formerly mislabelled
             "the bare chip-select pulse"

## 0x26.0 — shift-loop flag in the codec shift routine

    Rev 20 0x0E66  SETB 0x30     (inside the 0x0E62 codec shift-out routine)
    Rev 20 0x0E8B  JZ / JNB test
    Rev 20 0x0E8E  CLR  0x30

Set on entry to the shift routine, tested and cleared as it finishes: a
loop/first-pass flag local to that routine.

## Not yet established

Byte 0x21's seven flag bits — 0x21.0 through 0x21.6 — and 0x24.0 / 0x24.2.

Byte 0x21 is never accessed as a byte, only bitwise, so it is a pure flag word.
Structurally it is clear that these are program state flags: they are set,
cleared, tested, and three of them move through the carry
(`MOV 0x08,CY` at Rev 20 0x02C3, `ORL CY,0x0A` at 0x0528, `MOV CY,0x0E` at
0x0526). Several are cleared together at Rev 20 0x09F7–0x09FD and again at
0x0F63–0x0F69, which marks those two sites as a shared reset of the flag group.

That is structure, not meaning, and I am not claiming them until each one is
traced to what it actually gates. 0x21.2 and 0x21.6 have 15 and 18 sites and
are branched on all over the request-handling region, so they are the two that
need real work rather than a label.
