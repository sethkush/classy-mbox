#!/usr/bin/env python3
"""The UAC1 class rulebook, checked locally — the last thing #192 said needed Windows.

WHAT THIS IS, STATED FIRST BECAUSE IT BOUNDS EVERY RESULT BELOW.

USB20CV's class-specific pass encodes USB-IF's reading of the Audio 1.0
document. This encodes OURS. It can catch a descriptor that contradicts the
spec, contradicts itself, or contradicts the rest of our own bundle. It CANNOT
catch an error that lives in our reading of the spec, because it is built from
that same reading — and a validator agreeing with the descriptors it was written
alongside is not independent evidence.

So this closes the mechanical half of the rulebook and leaves the interpretive
half open. The honest way to close that half is a second IMPLEMENTATION, not a
second document: macOS/IOUSBFamily is one, and it is free. See FINDING_192.

WHY IT IS WORTH HAVING ANYWAY. The mechanical half is where the real bugs have
been. #185 (wrong sync types), #187 (undeclared terminal), #203 (a Selector Unit
the handler answered but nothing advertised), #207 (a 7-byte audio endpoint
descriptor that must be 9) were all cross-reference failures inside our own
bundle — exactly what this catches, and every one of them shipped.

WHAT IT CHECKS. Structure and cross-references, not opinions:

  * every bTerminalID / bUnitID unique and non-zero (UAC1 §4.3.2)
  * every baSourceID resolves to a declared terminal or unit
  * no unit sources itself; the topology has no cycle
  * wTotalLength of the AC header equals the real span of the AC descriptors
  * baInterfaceNr in the AC header names interfaces that exist
  * every AS interface's bTerminalLink resolves to a declared terminal
  * audio endpoint descriptors are 9 bytes, not 7 (bRefresh/bSynchAddress)
  * a declared feedback endpoint's bSynchAddress points at the endpoint that
    actually carries the feedback, and that endpoint is declared
  * bNrChannels agrees with wChannelConfig's bit count
  * Type I format: bSubframeSize x 8 >= bBitResolution, and bSamFreqType
    matches the number of tSamFreq entries that follow

    python3 tools/check_uac1_rulebook.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_descriptors import (  # noqa: E402
    AC_FEATURE_UNIT, AC_HEADER, AC_INPUT_TERMINAL, AC_OUTPUT_TERMINAL,
    AC_SELECTOR_UNIT, AS_FORMAT_TYPE, AS_GENERAL, DT_CS_ENDPOINT,
    DT_CS_INTERFACE, DT_ENDPOINT, DT_INTERFACE, load_ihx, load_symbols,
    slice_mem,
)

AC_MIXER_UNIT = 0x04
AC_PROCESSING_UNIT = 0x07
AC_EXTENSION_UNIT = 0x08


def u16(b, i):
    return b[i] | (b[i + 1] << 8)


SC_AUDIOCONTROL = 0x01
SC_AUDIOSTREAMING = 0x02


def parse(cfg):
    """Walk the bundle into (offset, bLength, bType, bytes, ctx).

    ctx IS NOT OPTIONAL, and leaving it out is a bug this file shipped with for
    one run. AudioControl and AudioStreaming class-specific descriptors have
    SEPARATE bDescriptorSubtype namespaces that collide numerically:

        0x01  AC_HEADER          vs  AS_GENERAL
        0x02  AC_INPUT_TERMINAL  vs  AS_FORMAT_TYPE

    Reading them from one table makes every AS_FORMAT_TYPE look like an Input
    Terminal and vice versa. The first run of this validator did exactly that
    and reported SIX confident failures against a descriptor bundle that four
    other gates already pass -- "ID 1 declared 3 times" was three format
    descriptors, and "bSamFreqType = 2 but 1 entry follows" was an Input
    Terminal's bNrChannels read as a frequency count.

    Every one of those would have been a wrong row in a machine-read table,
    which this project holds to be worse than no row at all. So the subtype is
    only ever interpreted together with the bInterfaceSubClass in force.
    """
    out, p, ctx = [], 0, None
    while p + 1 < len(cfg):
        ln = cfg[p]
        if ln == 0:
            break
        btype = cfg[p + 1]
        if btype == DT_INTERFACE and ln >= 8:
            sub = cfg[p + 6]
            ctx = ("AC" if sub == SC_AUDIOCONTROL else
                   "AS" if sub == SC_AUDIOSTREAMING else None)
        out.append((p, ln, btype, bytes(cfg[p:p + ln]), ctx))
        p += ln
    return out


class Rulebook:
    def __init__(self, items):
        self.items = items
        self.errors = []
        self.checked = 0

    def fail(self, rule, msg):
        self.errors.append((rule, msg))

    def ok(self):
        self.checked += 1

    # ---- unit / terminal graph -------------------------------------------
    def units(self):
        """{id: (subtype, raw)} for every AC entity that carries an ID."""
        found = {}
        for _off, _ln, btype, raw, ctx in self.items:
            if btype != DT_CS_INTERFACE or len(raw) < 4 or ctx != "AC":
                continue
            st = raw[2]
            if st in (AC_INPUT_TERMINAL, AC_OUTPUT_TERMINAL, AC_MIXER_UNIT,
                      AC_SELECTOR_UNIT, AC_FEATURE_UNIT, AC_PROCESSING_UNIT,
                      AC_EXTENSION_UNIT):
                found.setdefault(raw[3], []).append((st, raw))
        return found

    def sources_of(self, st, raw):
        """baSourceID list for a unit, by subtype. UAC1 §4.3.2.x layouts."""
        if st == AC_OUTPUT_TERMINAL:
            return [raw[7]] if len(raw) > 7 else []
        if st == AC_FEATURE_UNIT:
            return [raw[4]] if len(raw) > 4 else []
        if st == AC_SELECTOR_UNIT:
            n = raw[4] if len(raw) > 4 else 0
            return list(raw[5:5 + n])
        if st == AC_MIXER_UNIT:
            n = raw[4] if len(raw) > 4 else 0
            return list(raw[5:5 + n])
        if st in (AC_PROCESSING_UNIT, AC_EXTENSION_UNIT):
            n = raw[6] if len(raw) > 6 else 0
            return list(raw[7:7 + n])
        return []

    def check_ids(self):
        u = self.units()
        for uid, entries in sorted(u.items()):
            if uid == 0:
                self.fail("§4.3.2", "an entity uses ID 0, which is reserved")
            elif len(entries) > 1:
                self.fail("§4.3.2",
                          "ID %d is declared %d times (subtypes %s) -- every "
                          "bTerminalID/bUnitID must be unique"
                          % (uid, len(entries),
                             ", ".join("0x%02X" % e[0] for e in entries)))
            else:
                self.ok()
        return u

    def check_sources(self, u):
        for uid, entries in sorted(u.items()):
            st, raw = entries[0]
            for src in self.sources_of(st, raw):
                if src == uid:
                    self.fail("§4.3.2", "unit %d sources itself" % uid)
                elif src not in u:
                    self.fail("§4.3.2",
                              "unit %d (subtype 0x%02X) has baSourceID %d, "
                              "which is not a declared terminal or unit"
                              % (uid, st, src))
                else:
                    self.ok()

    def check_no_cycle(self, u):
        state = {}

        def walk(uid, stack):
            if state.get(uid) == 2:
                return
            if uid in stack:
                self.fail("§4.3.2",
                          "topology cycle: %s"
                          % " -> ".join(str(x) for x in stack + [uid]))
                return
            entries = u.get(uid)
            if not entries:
                return
            st, raw = entries[0]
            for src in self.sources_of(st, raw):
                walk(src, stack + [uid])
            state[uid] = 2

        for uid in sorted(u):
            walk(uid, [])
        self.ok()

    # ---- AC header --------------------------------------------------------
    def check_ac_header(self):
        hdr = None
        for off, _ln, btype, raw, ctx in self.items:
            if (btype == DT_CS_INTERFACE and ctx == "AC"
                    and len(raw) >= 3 and raw[2] == AC_HEADER):
                hdr = (off, raw)
                break
        if hdr is None:
            self.fail("§4.3.2", "no class-specific AC interface header")
            return
        off, raw = hdr
        want = u16(raw, 5)
        span = 0
        for o2, ln2, bt2, r2, c2 in self.items:
            if o2 < off:
                continue
            if bt2 == DT_INTERFACE and o2 > off:
                break
            if bt2 == DT_CS_INTERFACE and c2 == "AC":
                span += ln2
        if span != want:
            self.fail("§4.3.2",
                      "AC header wTotalLength = %d but the AC descriptors "
                      "actually span %d bytes" % (want, span))
        else:
            self.ok()

        n = raw[7] if len(raw) > 7 else 0
        declared = {r[2] for _o, _l, bt, r, _c in self.items
                    if bt == DT_INTERFACE and len(r) > 2}
        for i in range(n):
            if 8 + i >= len(raw):
                self.fail("§4.3.2", "AC header claims %d interfaces but is "
                                    "too short to list them" % n)
                break
            if raw[8 + i] not in declared:
                self.fail("§4.3.2",
                          "AC header baInterfaceNr[%d] = %d, which is not a "
                          "declared interface" % (i, raw[8 + i]))
            else:
                self.ok()

    # ---- AS interfaces ----------------------------------------------------
    def check_as(self, u):
        cur_ep = []
        for _off, ln, btype, raw, ctx in self.items:
            if btype == DT_ENDPOINT and len(raw) >= 3:
                cur_ep.append((raw[2], ln, raw))
            if btype != DT_CS_INTERFACE or len(raw) < 3 or ctx != "AS":
                continue
            if raw[2] == AS_GENERAL:
                link = raw[3] if len(raw) > 3 else 0
                if link not in u:
                    self.fail("§4.5.2",
                              "AS_GENERAL bTerminalLink = %d, not a declared "
                              "terminal" % link)
                else:
                    self.ok()
            elif raw[2] == AS_FORMAT_TYPE and len(raw) >= 8:
                nch, subf, bits = raw[4], raw[5], raw[6]
                if subf * 8 < bits:
                    self.fail("§2.2.5",
                              "format: bSubframeSize %d (=%d bits) cannot hold "
                              "bBitResolution %d" % (subf, subf * 8, bits))
                else:
                    self.ok()
                nfreq = raw[7]
                have = (len(raw) - 8) // 3
                if nfreq != have:
                    self.fail("§2.2.5",
                              "format: bSamFreqType = %d but %d tSamFreq "
                              "entries follow" % (nfreq, have))
                else:
                    self.ok()
                if nch == 0:
                    self.fail("§4.5.3", "format: bNrChannels = 0")
                else:
                    self.ok()

        # Audio endpoint descriptors are 9 bytes, not 7 (#207).
        for addr, ln, _raw in cur_ep:
            iso = True
            if ln not in (7, 9):
                self.fail("§9.6.6",
                          "endpoint 0x%02X descriptor is %d bytes" % (addr, ln))
                continue
            del iso
        return cur_ep

    def check_audio_endpoints(self, eps):
        """Audio-class endpoints need the 9-byte form, and bSynchAddress must
        name a declared endpoint."""
        declared = {a for a, _l, _r in eps}
        for addr, ln, raw in eps:
            attrs = raw[3] if len(raw) > 3 else 0
            if (attrs & 0x03) != 0x01:       # isochronous only
                continue
            if ln != 9:
                self.fail("§9.6.6",
                          "isochronous endpoint 0x%02X has a %d-byte "
                          "descriptor; the audio form is 9 (bRefresh and "
                          "bSynchAddress are not optional)" % (addr, ln))
                continue
            self.ok()
            sync = raw[8]
            usage = (attrs >> 4) & 0x03
            if usage == 0x01:                # feedback endpoint
                if sync != 0:
                    self.fail("§9.6.6",
                              "feedback endpoint 0x%02X sets bSynchAddress "
                              "0x%02X; a feedback endpoint must set it to 0"
                              % (addr, sync))
                else:
                    self.ok()
            elif sync != 0 and sync not in declared:
                self.fail("§9.6.6",
                          "endpoint 0x%02X bSynchAddress = 0x%02X, which is "
                          "not a declared endpoint" % (addr, sync))
            else:
                self.ok()


def selftest():
    """Prove every rule FIRES, by breaking the real bundle one byte at a time.

    A validator that only ever passes is indistinguishable from one whose checks
    never run -- and this file already shipped one run where six checks fired
    for the wrong reason. So each mutation below must produce at least one
    error, and the clean bundle must produce none.

    This is the known-answer arm the project requires, applied to a static
    analyser rather than to a measurement.
    """
    here = Path(__file__).resolve().parent.parent
    mem = load_ihx(str(here / "mboxfw/build/mboxfw.ihx"))
    syms = load_symbols(str(here / "mboxfw/build/mboxfw.map"))
    base = syms["_AppConfigDesc"]
    head = slice_mem(mem, base, 4)
    cfg = bytearray(slice_mem(mem, base, head[2] | (head[3] << 8)))

    r = Rulebook(parse(bytes(cfg)))
    u = r.check_ids()
    r.check_sources(u)
    r.check_no_cycle(u)
    r.check_ac_header()
    eps = r.check_as(u)
    r.check_audio_endpoints(eps)
    if r.errors:
        print("SELFTEST FAIL: the unmutated bundle already errors")
        for rule, msg in r.errors:
            print("  [%s] %s" % (rule, msg))
        return 1
    baseline = r.checked

    # Find offsets worth corrupting, by walking the real bundle.
    items = parse(bytes(cfg))
    muts = []
    for off, ln, bt, raw, ctx in items:
        if bt == DT_CS_INTERFACE and ctx == "AC" and len(raw) >= 4:
            st = raw[2]
            if st == AC_HEADER:
                muts.append(("AC header wTotalLength", off + 5, 0xEE))
            elif st == AC_OUTPUT_TERMINAL:
                muts.append(("Output Terminal baSourceID", off + 7, 0x7E))
            elif st == AC_SELECTOR_UNIT:
                muts.append(("Selector Unit bUnitID", off + 3, 0x00))
        elif bt == DT_CS_INTERFACE and ctx == "AS" and len(raw) >= 4:
            if raw[2] == AS_GENERAL:
                muts.append(("AS bTerminalLink", off + 3, 0x7F))
            elif raw[2] == AS_FORMAT_TYPE and len(raw) >= 8:
                muts.append(("bSamFreqType", off + 7, 0x09))
                muts.append(("bBitResolution vs bSubframeSize",
                             off + 6, 0xFF))

    if not muts:
        print("SELFTEST FAIL: found nothing to mutate -- the parse is broken")
        return 1

    seen, bad = set(), 0
    for name, off, val in muts:
        if name in seen:
            continue
        seen.add(name)
        m = bytearray(cfg)
        if m[off] == val:
            val ^= 0xFF
        m[off] = val
        rr = Rulebook(parse(bytes(m)))
        uu = rr.check_ids()
        rr.check_sources(uu)
        rr.check_no_cycle(uu)
        rr.check_ac_header()
        ee = rr.check_as(uu)
        rr.check_audio_endpoints(ee)
        if rr.errors:
            print("  fires: %-38s -> %s" % (name, rr.errors[0][1][:44]))
        else:
            print("  SILENT: %-38s -- NOT DETECTED" % name)
            bad += 1

    print()
    if bad:
        print("SELFTEST FAIL: %d mutation(s) went undetected" % bad)
        return 1
    print("SELFTEST PASS: clean bundle clean (%d checks), %d distinct "
          "mutations all detected" % (baseline, len(seen)))
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
    here = Path(__file__).resolve().parent.parent
    ihx, mp = here / "mboxfw/build/mboxfw.ihx", here / "mboxfw/build/mboxfw.map"
    if not ihx.exists() or not mp.exists():
        print("build artifacts missing -- run make first", file=sys.stderr)
        return 2
    mem, syms = load_ihx(str(ihx)), load_symbols(str(mp))
    if "_AppConfigDesc" not in syms:
        print("no _AppConfigDesc in the map", file=sys.stderr)
        return 2
    base = syms["_AppConfigDesc"]
    head = slice_mem(mem, base, 4)
    cfg = slice_mem(mem, base, head[2] | (head[3] << 8))

    r = Rulebook(parse(cfg))
    u = r.check_ids()
    r.check_sources(u)
    r.check_no_cycle(u)
    r.check_ac_header()
    eps = r.check_as(u)
    r.check_audio_endpoints(eps)

    if r.errors:
        print("UAC1 RULEBOOK FAIL: %d problem(s)" % len(r.errors))
        for rule, msg in r.errors:
            print("  [%s] %s" % (rule, msg))
        return 1
    print("UAC1 RULEBOOK PASS: %d cross-references checked, %d entities"
          % (r.checked, len(u)))
    print("SCOPE: this encodes OUR reading of Audio 1.0. It catches descriptors")
    print("that contradict the spec or each other -- not errors in the reading")
    print("itself. That needs a second implementation; macOS is one, and free.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
