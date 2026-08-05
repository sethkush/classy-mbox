# The host control protocol is fully specified — and #145 was never blocked

2026-07-30, in answer to "so nothing else can be figured out without the Mbox
plugged in?" The answer is no, and the reason is that this project has a **third
independent source** it had not been reading: `reference/mbox1_mixer_quirks.c.snippet`,
the Linux kernel's `snd-usb-audio` quirk for this exact device. Someone else
solved the host-facing half of this problem and the code is sitting in the repo.

It corroborates today's disassembly work on three points and then goes further.

## First, what "independent" does and does not mean here

An earlier draft of this document called this a "third independent source" and
counted the descriptors as one of the three. That is wrong twice, and the
correction matters more than the claim did.

**The descriptor block is not a separate source from the firmware.** Both are
bytes in the same ROM image. "The descriptor says SU bUnitID = 5" agreeing with
"the handler reads 0x25.4" is internal consistency of one artifact, not
corroboration. There are **two** sources here, not three: the ROM, and the
kernel quirk.

**But the ROM/kernel agreement is stronger than it first looks, for a reason
that also creates a new problem.** `decomp/cand/usb_descriptor_block.c` records
that the device **never serves the UAC block** — `GET_DESCRIPTOR(CONFIGURATION)`
returns the vendor-class config, and no `CODE` pointer in `std_get_descriptor`
targets the UAC descriptors. They are dead data.

So the kernel author **could not have read `bUnitID = 5` off the device**. The
`wIndex = 0x500` in the quirk had to come from somewhere else — reverse
engineering Digidesign's own driver, or probing. Two artifacts with no path
between them landed on the same unit ID. That is real corroboration, and it is
the reason to trust the rest of the quirk.

It also raises a question this project should have been asking: **a Selector Unit
that is never advertised is not discoverable.** A class-compliant host has no way
to learn it exists, which is precisely why Linux needs a *quirk* rather than
generic UAC support. `rev20_descriptors_decoded.md`'s "exposed as a standard UAC1
Selector Unit — class-compliant hosts drive it" overstates that: the *handler*
answers UAC1-shaped requests, but nothing tells a host to send them.

For mboxfw, which intends to be genuinely class-compliant, that is a design
decision to make deliberately rather than inherit: serve the UAC descriptors for
real, and the Selector Unit becomes discoverable by any host with no quirk at all.

## Cross-confirmation of today's findings

`FINDING_codec_word_bits_resolved.md` derived, from the dispatcher and
`setup_get_input_source` alone, that IRAM 0x25.4 is a UAC1 Selector Unit
position reported as 1 or 2. The kernel says the same thing from the other side:

    /* Hardware gives 2 possibilities:  ANALOG Source -> 0x01
     *                                  S/PDIF Source -> 0x02 */
    snd_usb_ctl_msg(..., 0x81, USB_DIR_IN|USB_TYPE_CLASS|USB_RECIP_INTERFACE,
                    0x00, 0x500, source, 1);

  * `USB_DIR_IN|USB_TYPE_CLASS|USB_RECIP_INTERFACE` = **0xA1**, which is exactly
    the dispatcher arm at Rev 20 `0x0049` that reaches `setup_get_input_source`.
  * `wIndex = 0x500` = (unit 5 << 8) | AC interface 0, and
    `rev20_descriptors_decoded.md` records **SU bUnitID = 5**,
    `baSourceID=[2 (Analog), 6 (S/PDIF)]`.
  * 1 = analog, 2 = S/PDIF — the polarity this project inferred from work code
    0x05 also selecting the external clock.

Three sources, independently derived, in agreement.

The kernel also states the side effect that had only been inferred here:

    /* NB: Setting the input source to S/PDIF resets the clock source to S/PDIF */

which is work code 0x05 setting 0x25.4 *and* selecting clock mode 1.

## The part that was NOT known: clock source rides the sample-rate control

    static int snd_mbox1_is_spdif_synced(...)
        snd_usb_ctl_msg(..., 0x81, USB_DIR_IN|USB_TYPE_CLASS|USB_RECIP_ENDPOINT,
                        0x100, 0x81, buff, 3);
        /* spdif sync: buff is all zeroes */

    static int snd_mbox1_set_clk_source(chip, rate_or_zero)
        /* Internal -> expects sample rate;  S/PDIF sync -> expects rate = 0 */

So the **standard UAC1 endpoint SAMPLING_FREQ_CONTROL doubles as the clock-source
selector**: write a rate for internal, write **zero** to slave to S/PDIF; read
back zero to mean "currently synced to S/PDIF".

That is visible in the firmware once you know to look for it.
`setup_get_sample_freq` @ Rev 20 `0x008A`:

    008a  MOV DPTR,#0xff2b ; MOVX A,@DPTR ; XRL A,#0x1 ; JZ 0x0095
    0095  LCALL 0x0b3e     ; point at the EP0 IN buffer
    0098  MOV A,0x08       ; the persisted clock mode
    009a  CJNE A,#0x1,0x00ba
    009d  LCALL 0x0b17 ; CLR A ; MOVX @DPTR,A    <-- writes ZERO
    ...   two more zero bytes

**When `RAM[0x08] == 1` — clock mode 1, the external/S/PDIF-slaved mode — the
device reports a sample rate of 0,0,0.** That is precisely the kernel's
"all zeroes means synced". The firmware and the driver agree byte for byte, and
neither was read against the other until now.

## Consequence: task #145 is not blocked on P3.1

#145 has stood as "S/PDIF clock slaving — BLOCKED on P3.1's meaning". It is not.

P3.1 drives *automatic* clock switching — the firmware noticing something and
posting work code 0x0B or 0x0C by itself. The **host-driven** path is completely
specified and independent of P3.1:

| direction | bmReq | bReq | wValue | wIndex | len | meaning |
|---|---|---|---|---|---|---|
| get clock | 0xA2 | 0x81 | 0x0100 | 0x0081 | 3 | 0,0,0 = S/PDIF-synced; else the rate |
| set clock | 0x22 | 0x01 | 0x0100 | 0x0081 | 3 | 0 = slave to S/PDIF; else internal at that rate |
| get source | 0xA1 | 0x81 | 0x0000 | 0x0500 | 1 | 1 = analog, 2 = S/PDIF |
| set source | 0x21 | 0x01 | 0x0000 | 0x0500 | 1 | 1 = analog, 2 = S/PDIF (also forces S/PDIF clock) |

Everything needed to implement S/PDIF slaving in mboxfw — the request encodings,
the magic zero, the unit ID, the side effect, and the clock modes themselves
(already decoded in `FINDING_clock_modes_and_p31.md`) — is now on paper. Automatic
switching on P3.1 can be added later, or never; a class-compliant device whose
clock source is host-selectable is a complete and legitimate design.

## What mboxfw is missing, which a Linux host will actively exercise

> **CORRECTED 2026-08-04 (#177).** Both halves of the paragraph below have
> changed since it was written, in opposite directions. mboxfw's descriptors
> were rewritten on 2026-08-03 and **no longer advertise any Selector Unit** —
> the six mono terminals and two SUs of #159 were replaced by one stereo input
> terminal (`descriptors.c`, and `FINDING_macos_one_input_selector.md` for why).
> And build 0x0020 **does implement the handler**, for unit 5, exactly as stock
> does. So the defect described here is fixed, but the topology claim was stale
> before the fix landed.
>
> The result is stock's own arrangement: the control is answered but not
> advertised, so only a host that already knows the unit ID — i.e. one with the
> kernel quirk, or a deliberate control transfer — can reach it. Whether to make
> it discoverable by serving a real UAC Selector Unit descriptor is #160, still
> open. Note that at `MBOX_PID=0x2000` the quirk does not apply either, which is
> why build 0x0020 also carries a device-recipient vendor alias
> (`TLM_REQ_SET_CLOCK`, `mboxfw/TELEMETRY.md`).

mboxfw ships descriptors ported from stock, so it **advertises Selector Unit 5**
— and has no handler behind it. A kernel with this quirk applied issues all four
requests above during setup and on every resume (`snd_mbox1_*_resume`). Against
mboxfw, the two Selector requests hit no handler.

That is a concrete, testable defect that needs no hardware to find and no
hardware to fix.

## The wider point

The question that prompted this was whether anything was left without the device
on the bench. Two independent sources in this repository had not been read
against the firmware at all: this kernel quirk, and the Digidesign host binaries
(`firmware_stock/strings_i386.txt`, the extracted flasher). "Board question" was
being used to mean "unanswerable", when it should have meant "not answerable
*from the two firmware images*". Those are different claims, and the second one
leaves a lot on the table.
