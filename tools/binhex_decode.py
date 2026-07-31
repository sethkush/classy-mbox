#!/usr/bin/env python3
"""
Decode BinHex 4.0 (.hqx) archives into their data and resource forks.

Written 2026-07-30 because reference/firmware/rev20/*.hqx had sat unopened
for the life of the project -- three archives nobody could look inside,
because Python dropped the `binhex` module in 3.11 and macOS ships no
converter. They decode to StuffIt archives, which `unar` then extracts:

    python3 tools/binhex_decode.py reference/firmware/rev20/*.hqx
    unar -o outdir '<name>.sea.data'

What is in them, so nobody has to wonder again:

  mboxfirmware20.hqx   -> Mbox Firmware Updater Rev20.sea    (classic Mac)
  mboxfirmware20x.hqx  -> Mbox Firmware 20 OS X.sit          (Mach-O ppc)
  mboxusb101.hqx       -> Digidesign_USB_Driver_1.0.1.sea    (PowerPC PEF)

The BinHex payload is 6-bit encoded with a custom alphabet, then RLE
compressed with 0x90 as the run marker (0x90 0x00 escapes a literal 0x90).
Header is: name length, name, version, type, creator, flags, data length,
resource length, CRC -- then the data fork, a CRC, the resource fork, a CRC.
"""
import sys, re
AB = "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr"
def decode(path):
    raw = open(path, 'rb').read().decode('latin-1')
    i = raw.index(':')            # data starts at the first colon
    body = raw[i+1:]
    j = body.rindex(':')
    body = body[:j]
    body = re.sub(r'\s+', '', body)
    bits = 0; nbits = 0; out = bytearray()
    for ch in body:
        v = AB.find(ch)
        if v < 0: continue
        bits = (bits << 6) | v; nbits += 6
        if nbits >= 8:
            nbits -= 8
            out.append((bits >> nbits) & 0xFF)
    # RLE
    res = bytearray(); k = 0
    while k < len(out):
        b = out[k]
        if b == 0x90 and k+1 < len(out):
            n = out[k+1]
            if n == 0: res.append(0x90)
            else: res.extend([res[-1]] * (n-1))
            k += 2
        else:
            res.append(b); k += 1
    # header
    nl = res[0]; name = res[1:1+nl].decode('latin-1', 'replace')
    p = 1 + nl + 1
    typ = res[p:p+4].decode('latin-1','replace'); cre = res[p+4:p+8].decode('latin-1','replace')
    p += 10
    dlen = int.from_bytes(res[p:p+4],'big'); rlen = int.from_bytes(res[p+4:p+8],'big')
    p += 8 + 2
    data = res[p:p+dlen]; p += dlen + 2
    rsrc = res[p:p+rlen]
    return name, typ, cre, data, rsrc
for f in sys.argv[1:]:
    n,t,c,d,r = decode(f)
    print("%-34s name=%-28s type=%s creator=%s data=%d rsrc=%d" % (f.split('/')[-1], n, t, c, len(d), len(r)))
    base = "%s/%s" % (sys.argv[0].rsplit('/',1)[0], n.replace(' ','_').replace('/','_'))
    open(base + ".data", 'wb').write(d); open(base + ".rsrc", 'wb').write(r)
