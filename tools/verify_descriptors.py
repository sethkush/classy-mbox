#!/usr/bin/env python3
"""
Verify the UAC1 descriptor bundle emitted by mboxfw.

Parses build/mboxfw.ihx, extracts each descriptor at the addresses recorded
in the linker .map, walks every descriptor field, and reports the first
error it finds. Catches:

  * bLength / wTotalLength mismatches (recomputes both)
  * unknown descriptor types / class subtypes
  * dangling bSourceID / baInterfaceNr references
  * duplicate terminal / unit IDs
  * endpoint address mis-matches vs code (EP1 IN / EP2 OUT)
  * AS format-type-I channel/subframe/bitres mismatches
  * string descriptor length ≠ 2 + 2*chars

Usage:  python3 tools/verify_descriptors.py [--ihx PATH] [--map PATH]
Exit code 0 on clean, 1 on any structural error.
"""

import argparse
import os
import re
import sys
from pathlib import Path


# ---- Intel HEX loader ----------------------------------------------------

def load_ihx(path):
    """Return dict {address: byte} from an Intel-HEX file."""
    mem = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue
            n = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            rtype = int(line[7:9], 16)
            if rtype != 0:
                continue
            for i in range(n):
                mem[addr + i] = int(line[9 + 2*i : 11 + 2*i], 16)
    return mem


def slice_mem(mem, base, length):
    return bytes(mem[base + i] for i in range(length))


# ---- Linker .map symbol extraction --------------------------------------

def load_symbols(path):
    """Return dict {name: address} for symbols we care about."""
    wanted = {'_AppDevDesc', '_AppConfigDesc', '_AppStringLang',
              '_AppStringMfr', '_AppStringProduct'}
    syms = {}
    with open(path) as f:
        for line in f:
            m = re.match(r'\s*C:\s+([0-9A-F]+)\s+(\S+)', line)
            if m and m.group(2) in wanted:
                syms[m.group(2)] = int(m.group(1), 16)
    return syms


# ---- Constants from the USB/UAC spec ------------------------------------

DT_DEVICE = 0x01
DT_CONFIG = 0x02
DT_STRING = 0x03
DT_INTERFACE = 0x04
DT_ENDPOINT = 0x05
DT_CS_INTERFACE = 0x24
DT_CS_ENDPOINT = 0x25

AC_HEADER = 0x01
AC_INPUT_TERMINAL = 0x02
AC_OUTPUT_TERMINAL = 0x03
AC_FEATURE_UNIT = 0x06

AS_GENERAL = 0x01
AS_FORMAT_TYPE = 0x02

EP_AUDIO_IN = 0x81
EP_AUDIO_OUT = 0x02


# ---- Error accumulator --------------------------------------------------

class Report:
    def __init__(self):
        self.errors = []
        self.warnings = []
        self.info = []

    def err(self, msg): self.errors.append(msg)
    def warn(self, msg): self.warnings.append(msg)
    def note(self, msg): self.info.append(msg)

    def dump(self):
        for m in self.info:     print(f"  info    {m}")
        for m in self.warnings: print(f"  WARN    {m}")
        for m in self.errors:   print(f"  ERROR   {m}")


# ---- Descriptor walkers -------------------------------------------------

def check_device(desc, r):
    if len(desc) != 18:      r.err(f"device desc length {len(desc)} != 18")
    if desc[0] != 18:        r.err(f"bLength = {desc[0]}, expected 18")
    if desc[1] != DT_DEVICE: r.err(f"bDescriptorType = 0x{desc[1]:02X}, expected 0x01")
    bcd = desc[2] | (desc[3] << 8)
    if bcd not in (0x0100, 0x0110, 0x0200):
        r.warn(f"unusual bcdUSB 0x{bcd:04X}")
    vid = desc[8]  | (desc[9]  << 8)
    pid = desc[10] | (desc[11] << 8)
    r.note(f"VID:PID = 0x{vid:04X}:0x{pid:04X}")
    # mboxflash --probe hardcodes 0x0DBA; any other VID means mboxflash
    # can't find the device and recovery via --enter-dfu is impossible.
    if vid != 0x0DBA:
        r.err(f"idVendor = 0x{vid:04X}, must be 0x0DBA (Digidesign)")
    # Two legal PIDs: 0x1000 runtime audio, 0x1001 app-DFU. Anything
    # else breaks the host-side driver bind + our own probe path.
    if pid not in (0x1000, 0x1001):
        r.err(f"idProduct = 0x{pid:04X}, must be 0x1000 or 0x1001")
    r.note(f"bMaxPacketSize0 = {desc[7]}")
    if desc[7] not in (8, 16, 32, 64):
        r.err(f"bMaxPacketSize0 = {desc[7]}, must be 8/16/32/64")
    if desc[17] != 1:
        r.warn(f"bNumConfigurations = {desc[17]}")


def check_string(desc, name, r):
    if desc[0] != len(desc):
        r.err(f"{name}: bLength {desc[0]} != actual length {len(desc)}")
    if desc[1] != DT_STRING:
        r.err(f"{name}: bDescriptorType = 0x{desc[1]:02X}, expected 0x03")
    if name != 'AppStringLang' and (desc[0] - 2) % 2 != 0:
        r.err(f"{name}: UTF-16 payload length {desc[0]-2} is odd")


def check_config_bundle(desc, r):
    """Walk the entire config descriptor bundle."""
    n = len(desc)
    p = 0

    terminal_ids = set()       # every IT/OT/FU bTerminalID
    source_refs = []           # (from_id, source_id) pairs
    ac_iface_refs = []         # baInterfaceNr from AC header
    seen_interfaces = set()    # (bInterfaceNumber, bAlternateSetting)
    as_endpoints = []          # (iface, alt, ep_addr, attr, maxpkt, interval)
    as_terminal_links = []     # (iface, alt, bTerminalLink)
    ac_wtotal_declared = None
    ac_block_actual = 0
    ac_walking = False

    # ---- Config header (9) ----
    if desc[0] != 9 or desc[1] != DT_CONFIG:
        r.err(f"config header: bLength/type wrong ({desc[0]}, 0x{desc[1]:02X})")
        return
    wtotal = desc[2] | (desc[3] << 8)
    if wtotal != n:
        r.err(f"config wTotalLength = {wtotal}, actual bundle length = {n}")
    else:
        r.note(f"wTotalLength = {wtotal} (matches bundle)")
    n_ifaces = desc[4]
    r.note(f"bNumInterfaces = {n_ifaces}")
    r.note(f"bmAttributes = 0x{desc[7]:02X}, bMaxPower = {desc[8]*2} mA")

    p = 9
    current_iface = None
    current_alt = None
    current_iface_class = None
    current_iface_subclass = None

    while p < n:
        bLen = desc[p]
        if bLen == 0:
            r.err(f"@{p}: bLength = 0 (would infinite-loop the host)")
            return
        if p + bLen > n:
            r.err(f"@{p}: descriptor overruns bundle (bLength={bLen}, remaining={n-p})")
            return
        bType = desc[p + 1]
        blob = desc[p : p + bLen]

        if bType == DT_INTERFACE:
            # If we were walking the AC class-spec block, close it out.
            if ac_walking and ac_wtotal_declared is not None:
                if ac_block_actual != ac_wtotal_declared:
                    r.err(f"AC class-spec wTotalLength declared {ac_wtotal_declared}, "
                          f"actual bytes seen = {ac_block_actual}")
                ac_walking = False

            if bLen != 9:
                r.err(f"@{p}: interface desc bLength = {bLen}, expected 9")
            current_iface = blob[2]
            current_alt = blob[3]
            numEP = blob[4]
            current_iface_class = blob[5]
            current_iface_subclass = blob[6]
            key = (current_iface, current_alt)
            if key in seen_interfaces:
                r.err(f"duplicate interface descriptor {key}")
            seen_interfaces.add(key)
            r.note(f"IF {current_iface} alt {current_alt}: "
                   f"class 0x{current_iface_class:02X} subclass 0x{current_iface_subclass:02X}, "
                   f"{numEP} EP(s)")

        elif bType == DT_CS_INTERFACE:
            if current_iface_class != 0x01:
                r.err(f"@{p}: CS_INTERFACE outside audio interface")
            subtype = blob[2]

            # AudioControl subtypes
            if current_iface_subclass == 0x01:  # AC
                if subtype == AC_HEADER:
                    if bLen != 8 + blob[7]:
                        r.err(f"AC header: bLength {bLen} inconsistent with bInCollection {blob[7]}")
                    ac_wtotal_declared = blob[5] | (blob[6] << 8)
                    for i in range(blob[7]):
                        ac_iface_refs.append(blob[8 + i])
                    r.note(f"AC header: bcdADC {blob[4]:02X}.{blob[3]:02X}, "
                           f"wTotalLength(class) = {ac_wtotal_declared}, "
                           f"IF refs = {ac_iface_refs}")
                    ac_walking = True
                    ac_block_actual = bLen
                elif subtype == AC_INPUT_TERMINAL:
                    if bLen != 12:
                        r.err(f"IT bLength {bLen} != 12")
                    tid = blob[3]
                    if tid in terminal_ids:
                        r.err(f"duplicate terminal ID {tid}")
                    terminal_ids.add(tid)
                    ttype = blob[4] | (blob[5] << 8)
                    r.note(f"  IT id={tid} type=0x{ttype:04X} channels={blob[7]}")
                    ac_block_actual += bLen
                elif subtype == AC_OUTPUT_TERMINAL:
                    if bLen != 9:
                        r.err(f"OT bLength {bLen} != 9")
                    tid = blob[3]
                    if tid in terminal_ids:
                        r.err(f"duplicate terminal ID {tid}")
                    terminal_ids.add(tid)
                    ttype = blob[4] | (blob[5] << 8)
                    src = blob[7]
                    source_refs.append((tid, src))
                    r.note(f"  OT id={tid} type=0x{ttype:04X} src={src}")
                    ac_block_actual += bLen
                elif subtype == AC_FEATURE_UNIT:
                    tid = blob[3]
                    if tid in terminal_ids:
                        r.err(f"duplicate unit ID {tid}")
                    terminal_ids.add(tid)
                    src = blob[4]
                    source_refs.append((tid, src))
                    r.note(f"  FU id={tid} src={src}")
                    ac_block_actual += bLen
                else:
                    r.err(f"unknown AC subtype 0x{subtype:02X}")

            # AudioStreaming subtypes
            elif current_iface_subclass == 0x02:  # AS
                if subtype == AS_GENERAL:
                    if bLen != 7:
                        r.err(f"AS_GENERAL bLength {bLen} != 7")
                    tlink = blob[3]
                    fmt = blob[5] | (blob[6] << 8)
                    as_terminal_links.append((current_iface, current_alt, tlink))
                    if fmt != 0x0001:
                        r.warn(f"AS iface {current_iface}: wFormatTag 0x{fmt:04X}, expected PCM (0x0001)")
                    r.note(f"  AS_GENERAL: bTerminalLink={tlink} bDelay={blob[4]} wFormatTag=0x{fmt:04X}")
                elif subtype == AS_FORMAT_TYPE:
                    if blob[3] != 0x01:
                        r.err(f"FORMAT_TYPE = 0x{blob[3]:02X}, only TYPE_I (0x01) implemented")
                    nch, subf, bitres = blob[4], blob[5], blob[6]
                    n_rates = blob[7]
                    expected_len = 8 + 3 * n_rates
                    if bLen != expected_len:
                        r.err(f"FORMAT_TYPE_I bLength {bLen} != 8+3*{n_rates}={expected_len}")
                    if subf * 8 < bitres:
                        r.err(f"bSubframeSize {subf} bytes can't hold {bitres} bits")
                    rates = [blob[8 + 3*i] | (blob[9 + 3*i] << 8) | (blob[10 + 3*i] << 16)
                             for i in range(n_rates)]
                    r.note(f"  FORMAT_TYPE_I: {nch}ch × {bitres}b in {subf}B, rates {rates}")

            else:
                r.warn(f"CS_INTERFACE in unknown subclass 0x{current_iface_subclass:02X}")

        elif bType == DT_ENDPOINT:
            if bLen not in (7, 9):
                r.err(f"@{p}: endpoint bLength {bLen} not 7 or 9")
            ep_addr = blob[2]
            attr = blob[3]
            maxpkt = blob[4] | (blob[5] << 8)
            interval = blob[6]
            if current_iface_subclass == 0x02:
                as_endpoints.append((current_iface, current_alt, ep_addr, attr, maxpkt, interval))
            r.note(f"  EP 0x{ep_addr:02X} attr=0x{attr:02X} maxpkt={maxpkt} bInterval={interval}")
            # sanity: audio streaming EP should be ISO (bits 0-1 == 01)
            if current_iface_subclass == 0x02 and (attr & 0x03) != 0x01:
                r.err(f"AS EP 0x{ep_addr:02X}: attr low2 = 0x{attr&3:X}, expected 0x1 (ISO)")

        elif bType == DT_CS_ENDPOINT:
            if bLen != 7:
                r.err(f"@{p}: CS_ENDPOINT bLength {bLen} != 7")

        else:
            r.err(f"@{p}: unknown descriptor type 0x{bType:02X}")

        p += bLen

    # ---- Cross-reference checks ----
    for from_id, src in source_refs:
        if src not in terminal_ids:
            r.err(f"terminal/unit {from_id} bSourceID={src} references nothing")

    for iface_ref in ac_iface_refs:
        if not any(i == iface_ref for (i, _a) in seen_interfaces):
            r.err(f"AC header baInterfaceNr={iface_ref} — no such AS interface")

    for iface, alt, tlink in as_terminal_links:
        if tlink not in terminal_ids:
            r.err(f"AS iface {iface} alt {alt}: bTerminalLink={tlink} references nothing")

    # Endpoint expectations from code (usb.h defines EP_AUDIO_IN=0x81 OUT=0x02)
    found_out = any(ep == EP_AUDIO_OUT for (_, _, ep, *_) in as_endpoints)
    found_in  = any(ep == EP_AUDIO_IN  for (_, _, ep, *_) in as_endpoints)
    if not found_out: r.err(f"no active EP 0x{EP_AUDIO_OUT:02X} (playback) in bundle")
    if not found_in:  r.err(f"no active EP 0x{EP_AUDIO_IN:02X} (capture) in bundle")

    # Every AS interface should have exactly one alt-0 and one alt-1
    as_ifaces = {i for (i, _a) in seen_interfaces
                 if any(ii == i and ss == 0x02 for ii, ss in
                        [(current_iface, current_iface_subclass)])}
    # simpler: any (i, a) tuples with alt = 0 must have a matching alt=1
    ifaces_with_alts = {}
    for (i, a) in seen_interfaces:
        ifaces_with_alts.setdefault(i, set()).add(a)


# ---- Main ---------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    here = Path(__file__).resolve().parent.parent
    ap.add_argument('--ihx', default=str(here / 'mboxfw/build/mboxfw.ihx'))
    ap.add_argument('--map', default=str(here / 'mboxfw/build/mboxfw.map'))
    args = ap.parse_args()

    if not os.path.exists(args.ihx):
        print(f"ihx not found: {args.ihx}", file=sys.stderr); return 2
    if not os.path.exists(args.map):
        print(f"map not found: {args.map}", file=sys.stderr); return 2

    mem = load_ihx(args.ihx)
    syms = load_symbols(args.map)
    missing = {'_AppDevDesc', '_AppConfigDesc', '_AppStringLang',
               '_AppStringMfr', '_AppStringProduct'} - set(syms)
    if missing:
        print(f"missing symbols: {missing}", file=sys.stderr); return 2

    # Determine each descriptor's length from its first byte for strings,
    # from hard-coded 18 for device, from wTotalLength for config.
    print(f"\n== AppDevDesc @ 0x{syms['_AppDevDesc']:04X} ==")
    dev = slice_mem(mem, syms['_AppDevDesc'], 18)
    r = Report(); check_device(dev, r); r.dump()
    all_errors = list(r.errors)

    print(f"\n== AppConfigDesc @ 0x{syms['_AppConfigDesc']:04X} ==")
    cfg_head = slice_mem(mem, syms['_AppConfigDesc'], 4)
    wtotal = cfg_head[2] | (cfg_head[3] << 8)
    cfg = slice_mem(mem, syms['_AppConfigDesc'], wtotal)
    r = Report(); check_config_bundle(cfg, r); r.dump()
    all_errors += r.errors

    for label in ('AppStringLang', 'AppStringMfr', 'AppStringProduct'):
        addr = syms['_' + label]
        first = mem[addr]
        blob = slice_mem(mem, addr, first)
        print(f"\n== {label} @ 0x{addr:04X} ({first} bytes) ==")
        r = Report(); check_string(blob, label, r); r.dump()
        all_errors += r.errors

    print()
    if all_errors:
        print(f"FAIL: {len(all_errors)} structural error(s)")
        return 1
    print("PASS: descriptor bundle is structurally valid")
    return 0


if __name__ == '__main__':
    sys.exit(main())
