# IRAM 0x23 and 0x25 — codec control word and panel/source state

Same method as `MUX_IRAM22_ANNOTATION.md`: byte-scan both images for every
instruction touching the direct address, and every bit op on that byte's bit
addresses, counting only instruction starts as decoded by the recursive-
traversal listing. **Bit address of IRAM byte B bit N = (B - 0x20) * 8 + N**,
so 0x23 -> 0x18..0x1F and 0x25 -> 0x28..0x2F.

Both images: 0x23 has 17 bit sites, 0x25 has 47, with one-to-one
correspondence between revisions.

---

# IRAM 0x23 — the codec control shift word

## Consumer

    Rev 20  0x0E62  MOV R6,#0x8      Rev 22  0x0E56
            0x0E64  MOV R5,0x23              0x0E58  MOV R7,0x23
            0x0E66  SETB 0x30

Eight bits clocked out — a second shift chain, separate from the 0x22
panel/mux chain at Rev 20 0x0F0C. Called from Rev 20 0x037D, 0x0818, 0x0835,
0x0842, 0x084D, 0x0852, 0x096C, 0x0AE9.

Byte-level writes are all computed stores (`MOV 0x23,A`): Rev 20 0x0536,
0x080E, 0x096A / Rev 22 0x0535, 0x088B, 0x09B9.

## Bit sites

    bit 0   SETB   Rev 20 0x07B8   Rev 22 0x0796
    bit 1   SETB   Rev 20 0x07BA   Rev 22 0x0798
    bit 2   CLR    Rev 20 0x072F   Rev 22 0x0716
            SETB   Rev 20 0x07EE   Rev 22 0x07CF
            SETB   Rev 20 0x0831   Rev 22 0x09D8
    bit 3   CLR    Rev 20 0x0731   Rev 22 0x0718
            SETB   Rev 20 0x07F0   Rev 22 0x07D1
            SETB   Rev 20 0x0833   Rev 22 0x09DA
    bit 4   SETB   Rev 20 0x0840   Rev 22 0x09E5
    bit 5   -- no bit operation in either image --
    bit 6   CLR    Rev 20 0x039E (boot), 0x053E, 0x0962, 0x102B
            SETB   Rev 20 0x0941, 0x102E
            JNB    Rev 20 0x0F32, 0x1028
            (Rev 22: 0x03A2, 0x053D, 0x0883, 0x1023 / 0x0862, 0x1026 /
             0x0F20, 0x1020)
    bit 7   -- no bit operation in either image --

Bits 5 and 7 are never touched bitwise in either image. They can still be set
by the computed stores, so "unused" is not established — only "never
bit-addressed".

## Bits 2 and 3 are the #147 pair

Rev 20 0x07EE / 0x07F0 and Rev 22 0x07CF / 0x07D1 are exactly the sites
recorded in the #147 fix, confirmed here by independent scan. They are also
CLEARED as a pair at Rev 20 0x072F / 0x0731 (Rev 22 0x0716 / 0x0718), and set
again as a pair at Rev 20 0x0831 / 0x0833 (Rev 22 0x09D8 / 0x09DA).

Always moved together, three times in each image, never individually. Whatever
they control is a two-bit field or a pair of matched channel switches — a
single mute bit does not need two.

## Bit 6 and the "48V phantom" claim (#144)

Bit 6 is the only bit of 0x23 that is *tested* (`JNB` at Rev 20 0x0F32 and
0x1028). Its behaviour: cleared at boot (0x039E, part of the same
initialisation run that sets up 0x22 and 0x25), and at 0x1028-0x102E it is
read and then written both ways — the classic toggle idiom:

    1028  JNB 0x1e,0x102e
    102b  CLR 0x1e
    102e  SETB 0x1e

A two-state toggle is consistent with a 48V phantom button and NOT with a
source selector, which cycles three ways. That is suggestive but it is still
only structural: nothing in either image names the pin. #144 stands -- the
"48V phantom" label remains an inference, now with its mechanism documented.

---

# IRAM 0x25 — panel/source state bits

No shift-out call. Read into a register at Rev 20 0x0E90 / Rev 22 0x0E82;
computed stores at Rev 20 0x0534, 0x080C, 0x0968 / Rev 22 0x0533, 0x0889,
0x09B7. This byte is internal state, not a hardware word.

## Boot state — explicitly cleared

    Rev 20 0x03A5-0x03AD   CLR bits 0,1,2,3,4
    Rev 22 0x03A9-0x03B1   CLR bits 0,1,2,3,4

This is what licenses the source-pattern deduction in
`MUX_IRAM22_ANNOTATION.md`: the source state machines start from (0,0), so the
boot mux value 0x76 is the *first* state, not an arbitrary one.

## Bit map

    bit 0   channel 1 source state, low       (with bit 2)
    bit 2   channel 1 source state, high
    bit 1   channel 2 source state, low       (with bit 3)
    bit 3   channel 2 source state, high
    bit 4   feeds 0x22.6; tested very early at Rev 20 0x0076 / Rev 22 0x0071
    bit 5   feeds 0x22.6
    bit 6   mode/branch flag, tested 4x, set 1x, cleared 1x
    bit 7   toggled in two separate places

The two source state machines are exactly symmetric:

    channel 1   Rev 20 0x0E27   bits 0 and 2   -> 0x22 bits [2:0]
    channel 2   Rev 20 0x0E9D   bits 1 and 3   -> 0x22 bits [5:3]

Rev 20 0x0E9D-0x0EBD mirrors 0x0E27-0x0E4A instruction for instruction with
bits 1/3 substituted for 0/2, and the same three patterns emitted onto bits
[5:3]. Rev 22 at 0x0E8F and 0x0E1B.

## Bits 4 and 5 both drive 0x22.6

    0x22.6 = !(0x25.4) && !(0x25.5)

per Rev 20 0x0E52-0x0E61 (channel 1 path) and 0x0EC5-0x0ED2 (channel 2 path),
Rev 22 0x0E46-0x0E55 and 0x0EB7-0x0EC1. Both channel paths compute the SAME
single bit from the same two inputs, so 0x22.6 is a global, not per-channel.

Bit 4 additionally: cleared at boot and at Rev 20 0x0454, set at 0x0466,
tested at 0x0076, 0x0485, 0x049F. The test at 0x0076 sits in the interrupt/
early-reset region, which makes bit 4 the most interesting unknown here.

Bit 5: cleared Rev 20 0x0395 and 0x041C, set 0x04CA.

## Bit 6 — a mode flag

Tested at Rev 20 0x035D, 0x038F, 0x0416, 0x04C4; cleared 0x037B; set 0x0810.
Note 0x038F is the `JB` immediately before the boot init run at 0x0395-0x03AD,
so bit 6 gates whether the boot initialisation is performed — consistent with
a "already initialised" or "mode already selected" latch.

## Bit 7 — two independent toggle sites

    SETB   Rev 20 0x083E, 0x0850, 0x0C8D    Rev 22 0x09E3, 0x09F3, 0x0C77
    CLR    Rev 20 0x084B, 0x0C4F            Rev 22 0x09EE, 0x0C39

Rev 20 0x084B is the address previously logged as "the bare chip-select pulse
at 0x084B" in `FINDING_open_questions.md`. It is not a chip select: it is
`CLR 0x2F`, clearing IRAM 0x25.7, bracketed by SETB at 0x083E and 0x0850.
That open question is answered as to *what the instruction is*; what the bit
means is still open.

---

## Divergences from mboxfw found here

1. `mboxfw/include/mux.h` documents `g_phantom_48v` as mirroring RAM[0x23].6.
   The bit is right and the toggle mechanism is confirmed; the *name* is still
   unproven (#144).
2. Nothing in mboxfw corresponds to IRAM 0x25 bits 4, 5, 6 or 7, nor to
   0x23 bits 0, 1 or 4. mboxfw models 0x23.2/0x23.3 (the #147 pair) and
   0x23.6 only.

## Still open

  * What 0x23 bits 0, 1, 2, 3, 4 physically switch. Bits 2/3 always move as a
    pair; bits 0/1 are set once each, adjacently, in the streaming path.
  * Whether 0x23 bits 5 and 7 are used at all (never bit-addressed, but the
    computed stores could set them).
  * IRAM 0x25 bit 4's role, given the early test at Rev 20 0x0076.
  * The meaning of 0x25.7, toggled around Rev 20 0x083E-0x0850.
