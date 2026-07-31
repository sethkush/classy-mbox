#!/usr/bin/env python3
"""
P1 waveform gate — execute the image and decode the pins.

Every check this project runs on the two serial shift chains is STATIC. They
read C source (`latch_word_bit_diff.py`), or a table of register values
(`verify_cs8427.py`), or bytes at a cited address (`check_citation_targets.py`).
None of them execute anything, so none of them can see what the chip would
actually receive.

That gap is not hypothetical. `FINDING_delay_calls_elided.md` records SDCC
deleting every CALL SITE of settle_delay() while leaving the body in the image:
the source said one thing, the image did another, and every source-reading gate
agreed with the source. `tools/sim_smoke.sh` even states the hole in its own
header -- "the simulator doesn't model the TAS1020A's USB SFRs or the CS8427 on
P1.3/P1.4" -- and that sentence has been true and unaddressed since it was
written.

So: run the real image in ucSim, break on every write to P1 (SFR 0x90), and
reconstruct both shift chains from the resulting waveform.

    P1.0 codec data   P1.1 codec latch   P1.2 codec clock
    P1.3 CS8427 CCLK  P1.4 CS8427 CDIN
    P1.5 mux clock    P1.6 mux latch     P1.7 mux data

The CS8427's chip select is NOT a port pin -- it is bit 7 of the low byte of
the 16-bit codec word, which reaches the part only after being clocked out on
P1.0/P1.2 and latched by P1.1. So decoding a CS8427 transaction REQUIRES
decoding the codec latch chain first. That coupling is the whole point: it is
exactly the coupling that #166 and #167 were defects in, and a decoder that
gets a transaction out the far end has proved the reset release, the select,
the latch chain and the SPI framing together.

VALIDATION. The decoder is not trusted on its own. It is run against Rev 20 and
Rev 22 -- real stock bytes, real execution -- and mboxfw's transaction list must
come out EQUAL to stock's. A decoder bug that swallows mboxfw's stream would
have to swallow stock's identically to pass, and a divergence is reported as a
diff between two things the same instrument measured the same way.

Usage:  python3 tools/sim_p1_waveform.py [--verbose]
"""

import argparse
import os
import re
import select
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

P1_CODEC_DATA  = 0x01
P1_CODEC_LATCH = 0x02
P1_CODEC_CLK   = 0x04
P1_CS8427_CCLK = 0x08
P1_CS8427_CDIN = 0x10

CS8427_CS_BIT = 0x80        # bit 7 of the codec word's LOW byte, active low

# Bring-up entry points. mboxfw runs from the reset vector so that main()'s
# real ordering is exercised -- #167 was an ordering defect, and entering at
# cs8427_boot_init() directly would have hidden it.
STOCK_ENTRY = {
    "rev20": 0x080B,        # audio_path_reconfig_ext_chips
    "rev22": 0x09B6,
}

EXPECTED = [
    (0x04, 0x00), (0x13, 0x10), (0x04, 0x00), (0x04, 0x40), (0x01, 0x01),
    (0x02, 0x20), (0x03, 0x0C), (0x05, 0x05), (0x06, 0x05), (0x11, 0xFF),
]

RUN_TIMEOUT = 20.0          # seconds to wait for one `run` to stop


# --------------------------------------------------------------------------
# ucSim driver
# --------------------------------------------------------------------------

class Sim:
    """Drive s51 interactively and sample P1 at every write to it.

    Reads through os.read() on the raw fd rather than readline() on the
    text stream. select() reports readiness of the KERNEL pipe, not of
    Python's decoding buffer, so mixing the two silently desynchronises the
    dialogue: readline() drains several lines into the buffer, select() then
    reports "nothing to read", and the next expected line is declared a
    timeout while it is already in hand. This harness lost the entire CS8427
    sequence to exactly that before the fd-level rewrite.
    """

    def __init__(self, ihx):
        self.p = subprocess.Popen(
            ["s51", "-q", str(ihx)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=0,
        )
        self.fd = self.p.stdout.fileno()
        self.buf = ""
        self._drain(0.5)

    def _send(self, cmd):
        self.p.stdin.write(cmd + "\n")
        self.p.stdin.flush()

    def _fill(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return False
        chunk = os.read(self.fd, 65536)
        if not chunk:
            return False
        self.buf += chunk.decode("utf-8", "replace")
        return True

    def _drain(self, timeout):
        while self._fill(timeout):
            pass
        self.buf = ""

    def _read_until(self, pat):
        """Return the first buffered line matching `pat`, or None on timeout."""
        while True:
            while "\n" in self.buf:
                line, self.buf = self.buf.split("\n", 1)
                if pat.search(line):
                    return line
            if not self._fill(RUN_TIMEOUT):
                return None

    def cmd(self, c):
        self._send(c)

    def read_p1(self):
        self._send("dump sfr 0x90 0x90")
        line = self._read_until(re.compile(r"0x90\s+P1:"))
        if line is None:
            return None
        m = re.search(r"(0x[0-9a-f]{2})\s+'", line)
        return int(m.group(1), 16) if m else None

    def run_to_stop(self):
        """Continue; return the PC we stopped at, or None if we never stopped."""
        self._send("run")
        line = self._read_until(re.compile(r"Stop at 0x"))
        if line is None:
            return None
        return int(re.search(r"Stop at 0x([0-9a-f]+)", line).group(1), 16)

    def close(self):
        try:
            self._send("quit")
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


def trace_p1(ihx, entry=None, preset_p1=None, max_writes=200000, want_transactions=12):
    """Return the sequence of P1 values, sampled at every write to P1.

    Stops once `want_transactions` complete CS-framed transactions have been
    seen, which bounds the run without needing a symbol for the routine's end
    -- stock RETs into a garbage stack when entered directly.
    """
    sim = Sim(ihx)
    sim.cmd("break sfr w 0x90")
    if preset_p1 is not None:
        sim.cmd(f"fill sfr 0x90 0x90 0x{preset_p1:02X}")
    if entry is not None:
        sim.cmd(f"pc 0x{entry:04X}")

    initial = sim.read_p1()
    if initial is None:
        sim.close()
        raise RuntimeError("could not read P1 before starting")

    samples = [initial]
    watcher = _CsWatcher(initial)
    n = 0
    while n < max_writes:
        if sim.run_to_stop() is None:
            break
        v = sim.read_p1()
        if v is None:
            break
        samples.append(v)
        watcher.feed(v)
        n += 1
        if watcher.transactions >= want_transactions:
            break
    sim.close()
    return samples


class _CsWatcher:
    """Minimal live counter of completed CS-low periods, used only to bound the run."""

    def __init__(self, p1):
        self.dec = CodecChain(p1)
        self.cs_low = False
        self.transactions = 0

    def feed(self, p1):
        word = self.dec.feed(p1)
        if word is None:
            return
        low = (word & 0xFF)
        asserted = not (low & CS8427_CS_BIT)
        if self.cs_low and not asserted:
            self.transactions += 1
        self.cs_low = asserted


# --------------------------------------------------------------------------
# Chain decoders
# --------------------------------------------------------------------------

class CodecChain:
    """Recover the 16-bit codec word from P1.0 (data) / P1.2 (clock) / P1.1 (latch).

    High byte (IRAM 0x23) first, MSB first, then the low byte (IRAM 0x25);
    the latch pulse publishes the whole word. Ports the shape of Rev 20
    fcn.0x0E62 (Rev 22 fcn.0x0E56) as seen from OUTSIDE the chip.
    """

    def __init__(self, p1):
        self.prev = p1
        self.shift = 0
        self.nbits = 0

    def feed(self, p1):
        """Feed one P1 sample; return the 16-bit word if a latch pulse landed."""
        prev, self.prev = self.prev, p1
        word = None
        if not (prev & P1_CODEC_CLK) and (p1 & P1_CODEC_CLK):
            self.shift = ((self.shift << 1) | (1 if p1 & P1_CODEC_DATA else 0)) & 0xFFFF
            self.nbits += 1
        if not (prev & P1_CODEC_LATCH) and (p1 & P1_CODEC_LATCH):
            word = self.shift
            self.nbits = 0
        return word


class Cs8427Port:
    """Decode CS8427 SPI transactions from P1.3 (CCLK) / P1.4 (CDIN) + the select.

    DS477F5 s9.1: data is clocked in on the RISING edge of CCLK; one CS-low
    period is one continuous shift, byte0 = chip address + R/W, byte1 = MAP
    (bit 7 = auto-increment), every following byte = data.
    """

    def __init__(self, p1, cs_asserted):
        self.prev = p1
        self.cs = cs_asserted
        self.bits = []
        self.transactions = []      # list of (bytes, nbits)

    def feed(self, p1, cs_asserted):
        prev, self.prev = self.prev, p1
        was, self.cs = self.cs, cs_asserted

        if not was and cs_asserted:                 # select falls: new transaction
            self.bits = []
        if was and cs_asserted:
            if not (prev & P1_CS8427_CCLK) and (p1 & P1_CS8427_CCLK):
                self.bits.append(1 if p1 & P1_CS8427_CDIN else 0)
        if was and not cs_asserted:                 # select rises: transaction ends
            b = self.bits
            nbytes = len(b) // 8
            out = [int("".join(map(str, b[i * 8:i * 8 + 8])), 2) for i in range(nbytes)]
            self.transactions.append((out, len(b)))
            self.bits = []


def decode(samples):
    """Full decode of a P1 waveform: codec words published, CS8427 transactions."""
    codec = CodecChain(samples[0])
    port = Cs8427Port(samples[0], False)
    words = []
    at_tx = []          # the codec word in effect as each transaction closed
    cs = False
    current = None
    for v in samples[1:]:
        word = codec.feed(v)
        if word is not None:
            words.append(word)
            current = word
            cs = not (word & CS8427_CS_BIT)
        before = len(port.transactions)
        port.feed(v, cs)
        if len(port.transactions) != before:
            at_tx.append(current)
    return words, port.transactions, at_tx


def register_writes(transactions):
    """(reg, value) pairs from the transactions that carry three bytes."""
    out = []
    for by, _ in transactions:
        if len(by) == 3:
            out.append((by[1], by[2]))
    return out


def word_while_selected(words, transactions):
    """The codec word in effect during the first CS8427 REGISTER WRITE.

    Every other line on that latch -- the external RESET, the mode-5 pair, the
    mono bit -- is published by the same 16 bits that carry the chip select, so
    this one value says what state the rest of the board was in at the moment
    the CS8427 was actually talked to.

    Deliberately NOT "the first word with the select asserted": both images
    publish an all-lines-off word early (mboxfw 0x0000 from codec_init, stock
    0x0040 from its own CLR A / MOV 0x25,A / MOV 0x23,A), and a select bit that
    reads asserted only because the whole word is zero is not a selection.
    """
    for w, by in zip(words, transactions):
        if by:
            return w
    return None


# Divergences between mboxfw's and stock's codec word DURING the CS8427
# bring-up, each with the reason it is accepted. Anything not listed fails.
# Keyed by (mboxfw high byte, stock high byte).
EXPECTED_WORD_DIVERGENCE = {
    (0x10, 0x1C):
        "mboxfw releases the external RESET (0x23.4) with the mode-5 pair "
        "0x23.2/0x23.3 still LOW; stock sets that pair FIRST (Rev 20 SETB 0x1a "
        "/ SETB 0x1b at 0x0831/0x0833, published at 0x0835, Rev 22 at "
        "0x09DC/0x09DE) and only then releases RESET at 0x0840. mboxfw sets "
        "the pair only in streaming_set_rate(), so between boot and the first "
        "host-driven rate change it rests in a state stock never rests in. "
        "streaming.c already argues the pair is a mute / audio-path enable, "
        "not a rate selector. Recorded, NOT fixed: POLICY forbids shipping a "
        "write on 'stock does it' alone, and this one has never been measured. "
        "See FINDING_bringup_waveform.md and task #171.",
}


# --------------------------------------------------------------------------

def bin_to_ihx(blob, path):
    """Intel-HEX a raw stock image so ucSim can load it at 0x0000."""
    lines = []
    for off in range(0, len(blob), 16):
        chunk = blob[off:off + 16]
        rec = [len(chunk), (off >> 8) & 0xFF, off & 0xFF, 0x00] + list(chunk)
        rec.append((-sum(rec)) & 0xFF)
        lines.append(":" + "".join(f"{b:02X}" for b in rec))
    lines.append(":00000001FF")
    path.write_text("\n".join(lines) + "\n")
    return path


def describe(name, words, transactions, at_tx, verbose):
    print(f"{name}:")
    print(f"    codec words published : {len(words)}")
    if verbose:
        for w in words:
            print(f"        0x{w >> 8:02X}{w & 0xFF:02X}   "
                  f"0x23=0x{w >> 8:02X}  0x25=0x{w & 0xFF:02X}")
    print(f"    CS8427 transactions   : {len(transactions)}")
    for by, nbits in transactions:
        if not by:
            print(f"        select pulse, {nbits} clocks  (SPI-mode select, DS477F5 s9)")
        else:
            print(f"        {nbits:3d} clocks  " + " ".join(f"{b:02X}" for b in by))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    tmp = ROOT / "mboxfw" / "build"
    ihx = tmp / "mboxfw.ihx"
    if not ihx.exists():
        print(f"not found: {ihx} (build mboxfw first)", file=sys.stderr)
        return 2

    print("Executing the image and decoding P1. Both shift chains, from the pins.\n")

    results = {}
    samples = trace_p1(ihx)
    results["mboxfw"] = decode(samples)
    describe(f"mboxfw   ({len(samples)} P1 writes traced)",
             *results["mboxfw"], verbose=args.verbose)
    print()

    for name, entry in STOCK_ENTRY.items():
        blob = (ROOT / "firmware_stock" / f"{name}_firmware_code.bin").read_bytes()
        path = bin_to_ihx(blob, tmp / f"_{name}_sim.ihx")

        # Stock has to be entered at its bring-up routine: from the reset
        # vector it stalls, because stock calls this routine from its
        # SET_INTERFACE handlers and ucSim cannot enumerate a USB device.
        #
        # Entering mid-firmware means P1 holds its RESET value, 0xFF, and
        # CCLK is a pin of P1 -- so the first rising edge of the first
        # transaction never happens and the first byte decodes one bit
        # short. That is an artefact of the entry point, not of stock.
        #
        # The fix is a measurement, not a chosen constant: run the image
        # from reset, let its own init drive P1 until it stalls, and use
        # the value ITS OWN CODE left there. Both images leave 0xC1.
        seed = trace_p1(path)[-1]
        samples = trace_p1(path, entry=entry, preset_p1=seed)
        results[name] = decode(samples)
        describe(f"{name}    ({len(samples)} P1 writes traced, entry 0x{entry:04X}, "
                 f"P1 seeded 0x{seed:02X} by its own init)",
                 *results[name], verbose=args.verbose)
        print()

    fails = []

    mine = register_writes(results["mboxfw"][1])
    if mine != EXPECTED:
        fails.append(f"mboxfw register writes {mine} != expected {EXPECTED}")

    for by, nbits in results["mboxfw"][1]:
        if by and by[0] != 0x20:
            fails.append(f"transaction does not open with the chip address 0x20: "
                         f"{[hex(x) for x in by]}")
        if by and nbits != 24:
            fails.append(f"transaction is {nbits} clocks, not 24 -- framing is wrong "
                         f"(an I2C ACK slot or START/STOP would show up here)")

    # The SHAPE of the whole session: one clock count per CS-low period,
    # bare pulses included. Compared against stock rather than against a
    # constant, so it stays a measurement.
    #
    # This replaces an "is there at least one bare select pulse?" check, which
    # a mutation walked straight through: deleting the SPI-select pulse still
    # left one zero-clock period behind, because publishing the all-zero word
    # at codec_init() drives the active-low select low and the next publish
    # raises it again. A pulse that exists only because the word was zero is
    # not the transition DS477F5 s9 asks for. Counting them catches that;
    # asking whether any exist does not.
    my_shape = [n for _, n in results["mboxfw"][1]]
    print(f"CS-low periods, in clocks: mboxfw {my_shape}")
    for name in STOCK_ENTRY:
        shape = [n for _, n in results[name][1]]
        print(f"                           {name}  {shape}")
        if shape != my_shape:
            fails.append(f"CS-low period shape differs from {name}: mboxfw "
                         f"{my_shape} vs {shape}. A missing zero-clock period is "
                         f"a missing SPI-mode select pulse (DS477F5 s9); a "
                         f"changed count is a framing change.")
    print()

    # The rest of the latch, at the moment the CS8427 is first selected.
    my_word = word_while_selected(results["mboxfw"][2],
                                  [by for by, _ in results["mboxfw"][1]])
    print("Codec word in effect while the CS8427 is selected:")
    print(f"    mboxfw  0x{my_word:04X}" if my_word is not None else
          "    mboxfw  never selected the part")
    for name in STOCK_ENTRY:
        w = word_while_selected(results[name][2],
                                [by for by, _ in results[name][1]])
        print(f"    {name}   0x{w:04X}" if w is not None else
              f"    {name}   never selected the part")
        if my_word is None or w is None:
            fails.append(f"no selected-state codec word for "
                         f"{'mboxfw' if my_word is None else name}")
            continue
        if (my_word >> 8) != (w >> 8):
            key = (my_word >> 8, w >> 8)
            if key not in EXPECTED_WORD_DIVERGENCE:
                fails.append(
                    f"codec word high byte diverges from {name} with no recorded "
                    f"reason: mboxfw 0x{my_word >> 8:02X}, {name} 0x{w >> 8:02X}. "
                    f"Every bit of that byte is a control line on the board.")
            else:
                print(f"        recorded divergence 0x{key[0]:02X} vs 0x{key[1]:02X}: "
                      f"{EXPECTED_WORD_DIVERGENCE[key]}")
    print()

    for name in STOCK_ENTRY:
        stock = register_writes(results[name][1])
        if not stock:
            fails.append(f"{name}: decoded no register writes -- the DECODER is "
                         f"suspect, not mboxfw; do not trust the mboxfw result above")
        elif stock != mine:
            fails.append(f"{name} writes {stock} but mboxfw writes {mine}")

    if fails:
        print("FAIL:")
        for f in fails:
            print(f"  - {f}")
        return 1

    print("PASS: mboxfw's P1 waveform decodes to the same CS8427 transactions as "
          "Rev 20 and Rev 22, measured the same way.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
