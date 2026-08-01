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
P1_MUX_CLK     = 0x20
P1_MUX_LATCH   = 0x40
P1_MUX_DATA    = 0x80

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

    def __init__(self, ihx, mirror=0x23):
        self.mirror = mirror
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

    def sample(self):
        """(P1 value, cycles since reset, IRAM 0x23), or (None, None, None).

        Both commands go out before either reply is read: the cost of a
        sample is a pipe round trip, not the simulation, so pipelining the
        pair keeps the cycle counts nearly free. Timing matters here because
        the delays BETWEEN these writes are load-bearing -- see
        FINDING_delay_calls_elided.md, where they silently became zero.
        """
        self._send("dump sfr 0x90 0x90")
        self._send(f"dump iram 0x{self.mirror:02x} 0x{self.mirror:02x}")
        self._send("state")
        line = self._read_until(re.compile(r"0x90\s+P1:"))
        if line is None:
            return None, None, None
        m = re.search(r"(0x[0-9a-f]{2})\s+'", line)
        p1 = int(m.group(1), 16) if m else None
        line = self._read_until(re.compile(rf"^0x{self.mirror:02x}\s"))
        iram23 = None
        if line is not None:
            m = re.search(rf"^0x{self.mirror:02x}\s+([0-9a-f]{{2}})", line)
            iram23 = int(m.group(1), 16) if m else None
        line = self._read_until(re.compile(r"clks\)"))
        if line is None:
            return p1, None, iram23
        m = re.search(r"\(([0-9]+) clks\)", line)
        return p1, (int(m.group(1)) if m else None), iram23

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


def mirror_address(default=0x23):
    """Where SDCC actually put g_codec_state_23.

    The NAME encodes stock's address, IRAM 0x23. SDCC puts mboxfw's copy
    wherever it likes -- 0x0A at the time of writing. Sampling 0x23 because
    the variable is called `_23` reads an unrelated byte and reports a
    phantom mono mismatch on a correct image, which is exactly what it did.
    Resolve it from the linker map, the same lesson sim_smoke.sh learned
    when it anchored to a compiler-generated label.
    """
    m = ROOT / "mboxfw" / "build" / "mboxfw.map"
    if not m.exists():
        return default
    for line in m.read_text().splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[-2] == "_g_codec_state_23":
            return int(parts[-3], 16)
        if "_g_codec_state_23" in line:
            for i, tok in enumerate(parts):
                if tok == "_g_codec_state_23" and i:
                    return int(parts[i - 1], 16)
    return default


def trace_p1(ihx, entry=None, preset_p1=None, mirror=0x23,
             max_writes=200000, want_transactions=12):
    """Return the sequence of P1 values, sampled at every write to P1.

    Stops once `want_transactions` complete CS-framed transactions have been
    seen, which bounds the run without needing a symbol for the routine's end
    -- stock RETs into a garbage stack when entered directly.
    """
    sim = Sim(ihx, mirror=mirror)
    sim.cmd("break sfr w 0x90")
    if preset_p1 is not None:
        sim.cmd(f"fill sfr 0x90 0x90 0x{preset_p1:02X}")
    if entry is not None:
        sim.cmd(f"pc 0x{entry:04X}")

    initial, t0, m0 = sim.sample()
    if initial is None:
        sim.close()
        raise RuntimeError("could not read P1 before starting")

    samples = [initial]
    clocks = [t0 or 0]
    iram23 = [m0 or 0]
    watcher = _CsWatcher(initial)
    n = 0
    while n < max_writes:
        if sim.run_to_stop() is None:
            break
        v, t, m = sim.sample()
        if v is None:
            break
        samples.append(v)
        clocks.append(t if t is not None else clocks[-1])
        iram23.append(m if m is not None else iram23[-1])
        watcher.feed(v)
        n += 1
        if watcher.transactions >= want_transactions:
            break
    sim.close()
    return samples, clocks, iram23


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


class MuxChain:
    """Recover the panel / source-mux word from P1.7 (data) / P1.5 (clock) / P1.6 (latch).

    The third chain on the same port, and the one this harness threw away
    until it was extended.

    EIGHT clocked bits, not nine. The project's shorthand has been "IRAM 0x22
    plus mono as a ninth bit", and read literally that is wrong -- stock's
    `shiftreg8_commit_p1_7_6_5` (Rev 20 0x0F0C) clocks 0x22 eight times and
    then, at 0x0F32, drives DATA to the mono value and raises LATCH without
    another clock:

        0f32  JNB 0x1e,0x0f39    ; 0x1e = IRAM 0x23.6, mono
        0f35  ORL P1,#0xC0       ; DATA high AND LATCH high in one write, RET
        0f39  ANL P1,#0x7F / ORL P1,#0x40 / ANL P1,#0xBF

    So mono is a ninth OUTPUT presented at the latch edge, not a ninth shifted
    bit. That distinction is why this records the data line AT the latch: it is
    the only place the mono line is observable, and it is the end of the path
    that moved out of `__bit g_mono` and into the codec word on 2026-07-31.
    """

    def __init__(self, p1):
        self.prev = p1
        self.shift = 0
        self.nbits = 0
        self.words = []         # (nbits, value, mono_at_latch) per latch pulse

    def feed(self, p1):
        prev, self.prev = self.prev, p1
        latched = None
        if not (prev & P1_MUX_CLK) and (p1 & P1_MUX_CLK):
            self.shift = (self.shift << 1) | (1 if p1 & P1_MUX_DATA else 0)
            self.nbits += 1
        if not (prev & P1_MUX_LATCH) and (p1 & P1_MUX_LATCH):
            latched = (self.nbits, self.shift, bool(p1 & P1_MUX_DATA))
            self.words.append(latched)
            self.shift = 0
            self.nbits = 0
        return latched


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


def decode(samples, clocks=None, iram23=None):
    """Full decode of a P1 waveform: all three chains, with cycle stamps."""
    codec = CodecChain(samples[0])
    port = Cs8427Port(samples[0], False)
    mux = MuxChain(samples[0])
    words = []
    at_tx = []          # the codec word in effect as each transaction closed
    tx_clk = []         # cycle stamp at which each transaction closed
    tx_start = []       # cycle stamp at which each transaction's select fell
    open_clk = None
    mono_pairs = []
    cs = False
    current = None
    for i, v in enumerate(samples[1:], start=1):
        word = codec.feed(v)
        if word is not None:
            words.append(word)
            current = word
            cs = not (word & CS8427_CS_BIT)
        before = len(port.transactions)
        port.feed(v, cs)
        latched = mux.feed(v)
        if latched is not None:
            # The mono line the panel chain published, against IRAM 0x23 read
            # straight out of the simulator at that instant.
            #
            # The first version compared against the last PUBLISHED codec
            # word instead, and that was unsound: both of mboxfw's boot-time
            # mux latches happen before the first codec-word publish, so the
            # comparison ran against a default and reported a phantom
            # mismatch on a correct image. The mirror is only observable by
            # reading it -- the waveform cannot carry a value that has not
            # been shifted out yet.
            mono_pairs.append((latched[2], iram23[i] if iram23 else None))
        if len(port.transactions) != before:
            at_tx.append(current)
            tx_clk.append(clocks[i] if clocks else 0)
            tx_start.append(open_clk)
            open_clk = None
        if cs and open_clk is None:
            open_clk = clocks[i] if clocks else 0
        if not cs:
            open_clk = None
    return {
        "words": words,
        "transactions": port.transactions,
        "at_tx": at_tx,
        "mux": mux.words,
        "tx_end_clk": tx_clk,
        "tx_start_clk": tx_start,
        "mono_pairs": mono_pairs,
    }


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


def describe(name, r, verbose):
    words, transactions = r["words"], r["transactions"]
    print(f"{name}:")
    print(f"    codec words published : {len(words)}")
    if verbose:
        for w in words:
            print(f"        0x{w >> 8:02X}{w & 0xFF:02X}   "
                  f"0x23=0x{w >> 8:02X}  0x25=0x{w & 0xFF:02X}")
    print(f"    mux words published   : {len(r['mux'])}"
          + (f"  ({r['mux'][0][0]} clocked bits each)" if r["mux"] else ""))
    if verbose:
        for n, w, mono in r["mux"]:
            print(f"        {n} bits  0x{w:02X}  mono-at-latch={int(mono)}")
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
    stock_mux = {}
    mirror = mirror_address()
    print(f"g_codec_state_23 resolved to IRAM 0x{mirror:02X} from the map\n")
    samples, clocks, iram23 = trace_p1(ihx, mirror=mirror)
    results["mboxfw"] = decode(samples, clocks, iram23)
    describe(f"mboxfw   ({len(samples)} P1 writes traced)",
             results["mboxfw"], verbose=args.verbose)
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
        reset_samples, reset_clocks, reset_iram = trace_p1(path)
        seed = reset_samples[-1]
        # The reset run is also the ONLY place stock's panel chain is
        # observable: by the time it reaches the bring-up routine it has
        # already published its mux word, so the entry-point trace shows
        # none. Keep the reset run's decode as the mux reference rather than
        # reasoning about a chain nothing measured.
        stock_mux[name] = decode(reset_samples, reset_clocks, reset_iram)["mux"]
        samples, clocks, iram23 = trace_p1(path, entry=entry, preset_p1=seed)
        results[name] = decode(samples, clocks, iram23)
        describe(f"{name}    ({len(samples)} P1 writes traced, entry 0x{entry:04X}, "
                 f"P1 seeded 0x{seed:02X} by its own init)",
                 results[name], verbose=args.verbose)
        print()

    fails = []

    mine = register_writes(results["mboxfw"]["transactions"])
    if mine != EXPECTED:
        fails.append(f"mboxfw register writes {mine} != expected {EXPECTED}")

    for by, nbits in results["mboxfw"]["transactions"]:
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
    my_shape = [n for _, n in results["mboxfw"]["transactions"]]
    print(f"CS-low periods, in clocks: mboxfw {my_shape}")
    for name in STOCK_ENTRY:
        shape = [n for _, n in results[name]["transactions"]]
        print(f"                           {name}  {shape}")
        if shape != my_shape:
            fails.append(f"CS-low period shape differs from {name}: mboxfw "
                         f"{my_shape} vs {shape}. A missing zero-clock period is "
                         f"a missing SPI-mode select pulse (DS477F5 s9); a "
                         f"changed count is a framing change.")
    print()

    # The rest of the latch, at the moment the CS8427 is first selected.
    my_word = word_while_selected(results["mboxfw"]["at_tx"],
                                  [by for by, _ in results["mboxfw"]["transactions"]])
    print("Codec word in effect while the CS8427 is selected:")
    print(f"    mboxfw  0x{my_word:04X}" if my_word is not None else
          "    mboxfw  never selected the part")
    for name in STOCK_ENTRY:
        w = word_while_selected(results[name]["at_tx"],
                                [by for by, _ in results[name]["transactions"]])
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

    # ---- the third chain -------------------------------------------------
    # Nine bits per latch, not eight: IRAM 0x22 plus mono appended by
    # mux_write's tail. A chain that shifted the wrong count would put every
    # panel line one position out -- the same class of defect as the CS8427's
    # framing, and equally unchecked until this decoder existed.
    my_mux = sorted({n for n, _, _ in results["mboxfw"]["mux"]})
    print(f"Mux/panel chain, clocked bits per latch: mboxfw {my_mux}")
    for name in STOCK_ENTRY:
        theirs = sorted({n for n, _, _ in stock_mux[name]})
        print(f"                                        {name}  {theirs}")
        if not theirs:
            fails.append(f"{name}: decoded no mux words even from the reset run "
                         f"-- the mux DECODER is suspect, not mboxfw")
        elif my_mux != theirs:
            fails.append(f"mux chain clocks {my_mux} bits per latch, {name} "
                         f"clocks {theirs}. Every panel line lands in the wrong "
                         f"position on a count mismatch.")

    # The mono line is published by the PANEL chain but stored in the CODEC
    # word (IRAM 0x23.6). Until 2026-07-31 mboxfw kept it in a separate
    # `__bit g_mono`, so the panel chain got the right value and the codec
    # word's bit 6 was always 0. Checking one against the other at the latch
    # edge is what makes that class of drift impossible to reintroduce
    # quietly -- latch_word_bit_diff.py proves the codec bit gets SET, not
    # that the value reaching the panel latch agrees with it.
    pairs = results["mboxfw"]["mono_pairs"]
    bad = [(m, w) for m, w in pairs if w is None or m != bool(w & 0x40)]
    if not pairs:
        fails.append("no mux latch edges seen, so the mono cross-check never "
                     "ran. A check that cannot fire is not a check.")
    print(f"    mono at latch vs codec word 0x23.6: "
          f"{len(pairs)} latches, {len(bad)} disagree")
    if bad:
        fails.append(f"the mono line published on the panel chain disagrees with "
                     f"the codec word's bit 0x23.6 at {len(bad)} latch edges, e.g. "
                     f"panel={int(bad[0][0])} IRAM 0x23={bad[0][1]}. The two "
                     f"chains have drifted apart again.")
    print()

    # ---- the delays ------------------------------------------------------
    # FINDING_delay_calls_elided.md is the case where SDCC deleted every CALL
    # SITE of settle_delay() and left the body in the image, so the listing
    # gave no hint. verify_reachability.py catches the total-deletion case.
    # It cannot catch a delay that is PRESENT but wrong -- a counter that
    # shrank, a `volatile` that got dropped from one of two copies. Cycles
    # between transactions can.
    #
    # The bar is deliberately not equality: SDCC's loop and Keil's `DJNZ
    # 0x2e` do not cost the same, so requiring stock's exact count would
    # encode the compiler rather than the requirement. What is being defended
    # is that the gaps did not collapse.
    my_gaps = [b - a for a, b in zip(results["mboxfw"]["tx_end_clk"],
                                     results["mboxfw"]["tx_start_clk"][1:])]
    print(f"Cycles between CS8427 transactions: mboxfw {my_gaps}")
    stock_gaps = {}
    for name in STOCK_ENTRY:
        gaps = [b - a for a, b in zip(results[name]["tx_end_clk"],
                                      results[name]["tx_start_clk"][1:])]
        stock_gaps[name] = gaps
        print(f"                                    {name}  {gaps}")
    stock_floor = min((min(g) for g in stock_gaps.values() if g), default=None)
    if my_gaps and stock_floor:
        # Stock's own shortest gap, and not a fraction of it. The first
        # version of this used stock_floor // 2 on the reasoning that the bar
        # should be generous about compiler differences -- and the mutation
        # that drops `volatile` from settle_delay() (the exact
        # FINDING_delay_calls_elided.md failure) collapsed mboxfw from 19044
        # cycles to 3672 and sailed straight under a bar of 2682. A generous
        # bar calibrated against nothing is not a bar.
        #
        # "At least as long as the vendor firmware waits" is a requirement
        # that can be defended; mboxfw clears it by 3.5x, so the headroom is
        # real and a drop below it is a regression worth stopping for.
        bar = stock_floor
        floor = min(my_gaps)
        worst = my_gaps.index(floor)
        print(f"    shortest mboxfw gap {floor}, stock {stock_floor}, bar {bar}")
        if floor < bar:
            fails.append(
                f"a settle delay collapsed to {floor} cycles (after transaction "
                f"{worst}); the bar is stock's own shortest gap, {bar}. "
                f"This is the "
                f"FINDING_delay_calls_elided.md failure mode: the call site is "
                f"gone or the counter shrank, and the source still reads right.")
    print()

    for name in STOCK_ENTRY:
        stock = register_writes(results[name]["transactions"])
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
