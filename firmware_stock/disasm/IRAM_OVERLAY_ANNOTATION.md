# IRAM 0x27–0x33 — Keil's overlaid local/parameter area

Evidence: every access site is in `LEDGER_WORKLIST.txt`. Layout context is in
`IRAM_LOW_ANNOTATION.md`.

## Why one address has several jobs

Keil C51 **overlays** the locals and parameters of functions that cannot be
active at the same time: two functions with no call path between them share the
same IRAM bytes. So an address in this range does not have one meaning — it has
one meaning per function that borrows it.

0x2E is the clearest case. It is a `DJNZ` delay counter in one function:

    Rev 20 0x0812  MOV 0x2E,#0xFF        0x0815  DJNZ 0x2E,0x0815
    Rev 20 0x082B  MOV 0x2E,#0xFF        0x082E  DJNZ 0x2E,0x082E
    Rev 20 0x0838  MOV 0x2E,#0xFF        0x083B  DJNZ 0x2E,0x083B
    Rev 20 0x0845  MOV 0x2E,#0xFF        0x0848  DJNZ 0x2E,0x0848

and a CS8427 register number in another (below). Both are correct; they never
run at once. Reading this range as global variables produces nonsense, which is
worth stating because that is how it looks to a first pass.

**Rev 20 0x084B**, previously logged in `FINDING_open_questions.md` as "the bare
chip-select pulse at 0x084B", falls in this run. It is the instruction right
after the fourth `DJNZ` spin — part of a bit-banged sequence paced by these
delays, not a chip select. `IRAM23_IRAM25_ANNOTATION.md` records the same
address separately as `CLR 0x2F`; both readings refer to the same
delay-and-strobe run.

## The (register, value) argument pairs

Three pairs in this range hold a register number and a value, and are then
loaded into R7 and R5 — Keil's first and second argument registers — to call a
two-argument write routine:

    pair          reg in    value in
    0x2C : 0x2D   R7        R5
    0x2E : 0x2F   R7        R5
    0x31 : 0x32   R7        R5

The register numbers are **CS8427 control-port registers**, which is what
identifies the callee. Compare against the map in
`FINDING_cs8427_confirmed.md`:

    Rev 20 0x0502  MOV 0x2C,#0x12   -> CSDATABUF
    Rev 20 0x0589  MOV 0x2C,#0x24
    Rev 20 0x048E  MOV 0x2C,#0x23
    Rev 20 0x04D1  MOV 0x2C,#0x04   with 0x04D4  MOV 0x2D,#0x41
    Rev 20 0x0568  MOV 0x2C,#0x04   with 0x056B  MOV 0x2D,#0x41
    Rev 20 0x0756  MOV 0x31,#0x04   with 0x0759  MOV 0x32,#0x41
    Rev 20 0x0E20  MOV 0x31,#0x04   with 0x0E23  MOV 0x32,#0x40
    Rev 20 0x0858  MOV 0x2E,#0x13   -> UDATABUF
    Rev 20 0x0898  MOV 0x2E,#0x11   -> RECVERRMASK
    Rev 20 0x0864/0x086D/0x0876/0x087F/0x088C/0x0892  MOV 0x2E,#0x04/01/02/03/05/06
        -> CLOCKSOURCE, CONTROL1, CONTROL2, DATAFLOW, SERIALINPUT, SERIALOUTPUT

Register 0x04 is CLOCKSOURCE and bit 6 is RUN, so **`#0x41` is RUN set and
`#0x40` is RUN clear** — the pair at Rev 20 0x0756/0x0759 starts the CS8427 and
the pair at 0x0E20/0x0E23 stops it. Rev 22 has the same two at 0x073D/0x0740
and 0x07A0/0x07A3.

The run at Rev 20 0x0858–0x08CC walks 0x2E through registers 0x13, 0x04, 0x01,
0x02, 0x03, 0x05, 0x06, 0x11 with a value in 0x2F each time: that is the
CS8427 boot initialisation sequence, register by register.

## Per-address accounts

**0x27** — local of the function at Rev 20 0x0A96. Saved from A at 0x0A96, read
at 0x0AEF and 0x0AFF, forced to 1 at 0x0AF3, stored back at 0x0B05. A working
value that is clamped to a minimum of 1.

**0x28, 0x29** — locals of the same function, both decremented in place
(0x0AC4 `DEC 0x28`, 0x0AC0 `DEC 0x29`) and read at 0x0AB8 / 0x0AB4 and 0x0ABE.
A nested countdown pair.

**0x2A** — `MOV 0x2A,#0x00` at Rev 20 0x0A9E only. One write, no read.

**0x2B** — `MOV 0x2B,#0x10` at Rev 20 0x0AA1 only. One write, no read.

**0x2C : 0x2D** — the CS8427 (register, value) pair above. 0x2C is also
complemented in place at 0x04E7 (`XRL 0x2C,#0xFF`) and compared at 0x04FA, so
the same slot serves as a scratch byte in that function.

**0x2E : 0x2F** — CS8427 (register, value) pair, and separately the `DJNZ`
delay counter shown above. 0x2F is also `INC`-ed at Rev 20 0x0800 and read at
0x0805, a third overlaid use.

**0x30** — counter in the streaming region: stored from A at Rev 20 0x072D and
0x07F8, `INC`-ed at 0x07FA, read at 0x07FC. Rev 22 the same shape at
0x0714 / 0x07D9 / 0x07DB / 0x07DD.

**0x31 : 0x32** — the CS8427 CLOCKSOURCE start/stop pair above.

**0x33** — an R7 spill slot, Rev 20 only: `MOV 0x33,R7` at 0x0C45 with callers
listed from eight sites, and `MOV R3,0x33` at 0x0C7C. Rev 22 does not use it,
which together with Rev 22's extra use of 0x07 and the EP0 pointer move from
0x1B:0x1C to 0x1D:0x1E accounts for every low-IRAM difference between the two
images.

## Coverage note

Every address in 0x27–0x33 that either image touches is accounted for, with
every access site enumerated. Four of them (0x2A, 0x2B, and 0x0C and 0x16 in
the other document) have write-only or read-only access sets; where that is the
whole story it is stated as the whole story.
