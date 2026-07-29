# CS8427 register reference

`alsa_cs8427.h` is `include/sound/cs8427.h` from the Linux kernel, fetched
2026-07-28 from
https://raw.githubusercontent.com/torvalds/linux/master/include/sound/cs8427.h
(GPL-2.0-or-later, per its SPDX header).

Cirrus's own PDF is not redistributable here and every mirror refused
automated download (Mouser served bot-protection HTML, statics.cirrus.com
returned 403). The ALSA header is a better fit anyway: it is the register map
expressed as named constants with bit meanings, which is exactly what is needed
to decode the writes the Mbox firmware makes, and it is a source this project
can quote.

It is a secondary source. Where a claim matters, it is stated as "ALSA's
CS8427 header names this ..." rather than "the datasheet says". The
identification of the part itself no longer rests on inference -- see
FINDING_cs8427_confirmed.md.
