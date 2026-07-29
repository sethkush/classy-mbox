# `ep0_clamp_len_to_wlength` compares a 16-bit length byte-swapped

Rev 20 `0x0D6B`, Rev 22 `0x0DA3`. Found 2026-07-28 while decompiling.

The EP0 transfer length lives in two IRAM bytes. Which is which is not a guess:
`ep0_in_fill_chunk` decrements the pair at Rev 20 `0x0BB0`

    DJNZ 0x09,0x0BBF      ; low byte
    MOV  A,0x0B           ; high byte
    ...
    MOV  0x09,#0xFF       ; borrow
    DEC  0x0B

which is a textbook 16-bit decrement. **IRAM 0x09 is the low byte, 0x0B the
high byte.**

The function then performs two 16-bit comparisons against the SETUP packet's
wLength (`0xFF2E` low, `0xFF2F` high), and they do not agree with each other.

**First comparison — correct.** Clamps the length to wLength. Rev 20 `0x0D70`:

    MOV A,0x0B / SETB C / SUBB A,R7(wLengthH)   ; high bytes first
    ... tie-break on 0x09 vs wLengthL

**Second comparison — byte-swapped.** Sets IRAM bit `0x0D` (0x21.5), the flag
that a short packet is expected. Rev 20 `0x0D96`:

    MOV A,0x09 / CLR C / SUBB A,R7(wLengthL)    ; LOW bytes first, with priority
    ... tie-break on 0x0B vs wLengthH

Comparing the low byte first and only consulting the high byte on a tie treats
the low byte as the more significant one.

The first comparison has already clamped, so `len <= wLength` always holds by
the time the second runs. The flag should therefore be set exactly when
`len < wLength`. It is instead set when `len_lo < wLength_lo`, or the low bytes
tie and `len_hi < wLength_hi`. The two disagree precisely when

    len_hi < wLength_hi   AND   len_lo > wLength_lo

where the flag is wrongly left CLEAR.

**In both images.** The three instruction shapes appear once each per image, at
identical relative offsets (Rev 20 `0x0D70`/`0x0D96`/`0x0DA3`; Rev 22
`0x0DA3`/`0x0DC9`/`0x0DD6`, a uniform +0x33 shift). Rev 22 did not fix it.

**It is reachable by an ordinary host.** EP0 transfers here are descriptors and
small class replies, so the high byte is usually zero on both sides and the
swapped compare degenerates to the correct one. But the standard
GET_DESCRIPTOR(CONFIGURATION) re-read hits it exactly: a host that has already
seen the 9-byte header asks again with a generous wLength, commonly 0x0100.
The configuration is 54 bytes (`wTotalLength = 0x0036`, Rev 20 `0x0672`), so

    len     = 0x0036   (hi 0x00, lo 0x36)
    wLength = 0x0100   (hi 0x01, lo 0x00)

`len < wLength`, so the short-packet flag should be set. The swapped compare
sees `len_lo = 0x36 > wLength_lo = 0x00`, decides on that, and leaves it clear.

Rejecting the earlier draft of this note: it claimed length 0x0100 against
wLength 0x00FF sets the flag. It does not -- the clamp runs first and makes the
two equal, so nothing is set. Any example has to respect `len <= wLength`.

Not yet reproduced as a candidate: the C has to spell the comparison out
byte by byte, since no natural 16-bit comparison compiles to this.
