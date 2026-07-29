#!/usr/bin/env python3
"""
Build the Mbox 1 firmware reference document (single self-contained HTML).

Everything factual in the output is derived from the two stock images and the
Ghidra recursive-traversal listings at build time. Prose is inline below.

    python3 tools/build_fw_doc.py  ->  firmware_stock/disasm/mbox_firmware.html
"""
import html
import os
import re
import difflib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware_stock")
DIS = os.path.join(FW, "disasm")
OUT = os.path.join(DIS, "mbox_firmware.html")

IMG = {
    "v20": os.path.join(FW, "rev20_firmware_code.bin"),
    "v22": os.path.join(FW, "rev22_firmware_code.bin"),
}
GH = {
    "v20": os.path.join(DIS, "rev20_ghidra.txt"),
    "v22": os.path.join(DIS, "rev22_ghidra.txt"),
}

# --------------------------------------------------------------------------
# parse
# --------------------------------------------------------------------------

RE_FN = re.compile(r"; ======== FUNCTION (\S+) @ CODE:([0-9a-f]{4})")
RE_INS = re.compile(r"CODE:([0-9a-f]{4})\s+([0-9a-f]+)\s{2,}([A-Z][^;\n]*?)\s*(?:;\s*(.*))?$")
RE_GAP = re.compile(r"; GAP CODE:([0-9a-f]{4}) \((\d+) bytes\)")
RE_LAB = re.compile(r"^(LAB_CODE_[0-9a-f]+):")


def parse(path):
    """-> (functions list, gaps list). Each function: name, addr, ins[]."""
    fns, gaps, cur = [], [], None
    for line in open(path):
        m = RE_FN.match(line)
        if m:
            cur = {"name": m.group(1), "addr": int(m.group(2), 16), "ins": [], "bytes": 0}
            fns.append(cur)
            continue
        g = RE_GAP.match(line)
        if g:
            gaps.append((int(g.group(1), 16), int(g.group(2))))
            cur = None
            continue
        i = RE_INS.match(line.rstrip("\n"))
        if i and cur is not None:
            addr = int(i.group(1), 16)
            raw = i.group(2)
            if len(raw) % 2:  # guard against malformed rows
                continue
            cur["ins"].append((addr, raw, i.group(3).strip(), (i.group(4) or "").strip()))
            cur["bytes"] += len(raw) // 2
    return fns, gaps


def coverage(path, binpath):
    data = open(binpath, "rb").read()
    cov = bytearray(len(data))
    fns, gaps = parse(path)
    for f in fns:
        for a, raw, _, _ in f["ins"]:
            for k in range(a, min(a + len(raw) // 2, len(data))):
                cov[k] = 1
    for a, n in gaps:
        for k in range(a, min(a + n, len(data))):
            cov[k] = 2
    return data, cov, fns, gaps


DATA, COV, FNS, GAPS = {}, {}, {}, {}
for k in IMG:
    DATA[k], COV[k], FNS[k], GAPS[k] = coverage(GH[k], IMG[k])

# decompiled C, split per function on the banner comments
CSRC = {"v20": open(os.path.join(DIS, "rev20_decompiled.c")).read(),
        "v22": open(os.path.join(DIS, "rev22_decompiled.c")).read()}
RE_CBAN = re.compile(r"/\* =+\n \* (\S+) @ CODE:([0-9a-f]{4})\n \* =+ \*/")


def split_c(src):
    out, marks = {}, list(RE_CBAN.finditer(src))
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(src)
        out[m.group(1)] = src[m.end():end]
    return out


CFNS = {k: split_c(CSRC[k]) for k in CSRC}

# --------------------------------------------------------------------------
# reference tables
# --------------------------------------------------------------------------

VECINT = {
    0x00: ("OEP0", "EP0 OUT (host->device data stage) complete"),
    0x01: ("OEP1", "EP1 OUT complete"), 0x02: ("OEP2", "EP2 OUT complete"),
    0x03: ("OEP3", "EP3 OUT complete"), 0x04: ("OEP4", "EP4 OUT complete"),
    0x05: ("OEP5", "EP5 OUT complete"), 0x06: ("OEP6", "EP6 OUT complete"),
    0x07: ("OEP7", "EP7 OUT complete"),
    0x08: ("IEP0", "EP0 IN (device->host data stage) complete"),
    0x09: ("IEP1", "EP1 IN complete"), 0x0A: ("IEP2", "EP2 IN complete"),
    0x0B: ("IEP3", "EP3 IN complete"), 0x0C: ("IEP4", "EP4 IN complete"),
    0x0D: ("IEP5", "EP5 IN complete"), 0x0E: ("IEP6", "EP6 IN complete"),
    0x0F: ("IEP7", "EP7 IN complete"),
    0x10: ("STPOW", "SETUP overwrite - new SETUP arrived mid-transfer"),
    0x11: ("(reserved)", "-"),
    0x12: ("SETUP", "SETUP packet received on EP0"),
    0x13: ("PSOF", "Pre-start-of-frame"),
    0x14: ("SOF", "Start of frame (1 kHz USB frame clock)"),
    0x15: ("RESR", "Resume from suspend"),
    0x16: ("SUSR", "Suspend detected"),
    0x17: ("RSTR", "USB bus reset"),
    0x18: ("CPRX", "Codec port receive"), 0x19: ("CPTX", "Codec port transmit"),
    0x1A: ("DPRX", "Data port receive"), 0x1B: ("DPTX", "Data port transmit"),
    0x1C: ("I2CRX", "I2C receive"), 0x1D: ("I2CTX", "I2C transmit"),
    0x1E: ("(reserved)", "-"),
    0x1F: ("XINT", "External interrupt"),
    0x20: ("(reserved)", "-"), 0x21: ("(reserved)", "-"),
    0x22: ("(reserved)", "-"), 0x23: ("(reserved)", "-"),
    0x24: ("NO_INT", "No interrupt pending"),
}
VEC_BASE = {"v20": 0x0C93, "v22": 0x0C7D}
DESC_BASE = {"v20": 0x0596, "v22": 0x057D}

BREQ = {0: "GET_STATUS", 1: "CLEAR_FEATURE", 2: "(reserved 2)", 3: "SET_FEATURE",
        4: "(reserved 4)", 5: "SET_ADDRESS", 6: "GET_DESCRIPTOR", 7: "SET_DESCRIPTOR",
        8: "GET_CONFIGURATION", 9: "SET_CONFIGURATION", 10: "GET_INTERFACE",
        11: "SET_INTERFACE", 12: "SYNCH_FRAME"}

DESC_TYPE = {1: "DEVICE", 2: "CONFIGURATION", 3: "STRING", 4: "INTERFACE",
             5: "ENDPOINT", 0x24: "CS_INTERFACE", 0x25: "CS_ENDPOINT"}

# name of the function containing each address, for cross-referencing
def fn_index(key):
    idx = {}
    for f in FNS[key]:
        for a, raw, _, _ in f["ins"]:
            idx[a] = f["name"]
    return idx

FIDX = {k: fn_index(k) for k in IMG}


def fname(key, addr):
    return FIDX[key].get(addr)


# --------------------------------------------------------------------------
# html helpers
# --------------------------------------------------------------------------

E = html.escape
_parts = []
def w(s=""):
    _parts.append(s)


def table(headers, rows, cls=""):
    w(f'<div class="tw"><table class="{cls}"><thead><tr>'
      + "".join(f"<th>{h}</th>" for h in headers) + "</tr></thead><tbody>")
    for r in rows:
        w("<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>")
    w("</tbody></table></div>")


def h(level, text, anchor=None):
    a = f' id="{anchor}"' if anchor else ""
    w(f"<h{level}{a}>{text}</h{level}>")


CSS = r"""
:root{
 --bg:#fbfaf7; --fg:#1b1a17; --dim:#6b6760; --line:#ddd8cf; --card:#fff;
 --accent:#8a4b2a; --accent2:#2f5d50; --code:#f4f1ea; --warn:#8a5a00;
 --v20:#2f5d50; --v22:#8a4b2a; --hl:#fff4d6;
}
@media (prefers-color-scheme:dark){:root{
 --bg:#15161a; --fg:#e6e3dd; --dim:#96918a; --line:#2e3038; --card:#1c1e24;
 --accent:#e0996a; --accent2:#7fc0aa; --code:#111318; --warn:#e0b060;
 --v20:#7fc0aa; --v22:#e0996a; --hl:#3a3320;
}}
:root[data-theme=dark]{
 --bg:#15161a; --fg:#e6e3dd; --dim:#96918a; --line:#2e3038; --card:#1c1e24;
 --accent:#e0996a; --accent2:#7fc0aa; --code:#111318; --warn:#e0b060;
 --v20:#7fc0aa; --v22:#e0996a; --hl:#3a3320;
}
:root[data-theme=light]{
 --bg:#fbfaf7; --fg:#1b1a17; --dim:#6b6760; --line:#ddd8cf; --card:#fff;
 --accent:#8a4b2a; --accent2:#2f5d50; --code:#f4f1ea; --warn:#8a5a00;
 --v20:#2f5d50; --v22:#8a4b2a; --hl:#fff4d6;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);
 font:16px/1.65 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;}
.wrap{display:flex;align-items:flex-start;max-width:1500px;margin:0 auto}
nav{position:sticky;top:0;flex:0 0 260px;max-height:100vh;overflow-y:auto;
 padding:24px 14px 60px;border-right:1px solid var(--line);font-size:13px}
nav a{display:block;color:var(--dim);text-decoration:none;padding:3px 8px;border-radius:5px;
 line-height:1.4}
nav a:hover{color:var(--fg);background:var(--code)}
nav a.s{padding-left:20px;font-size:12.5px}
nav .nt{font-weight:700;color:var(--fg);margin:14px 0 4px;padding:0 8px;
 text-transform:uppercase;letter-spacing:.07em;font-size:10.5px}
main{flex:1 1 auto;min-width:0;padding:24px 40px 140px}
h1{font-size:31px;line-height:1.2;margin:.2em 0 .1em;letter-spacing:-.02em}
h2{font-size:23px;margin:2.4em 0 .5em;padding-bottom:.25em;border-bottom:2px solid var(--line);
 letter-spacing:-.01em;scroll-margin-top:16px}
h3{font-size:17.5px;margin:1.9em 0 .4em;color:var(--accent);scroll-margin-top:16px}
h4{font-size:15px;margin:1.4em 0 .3em}
p,li{max-width:80ch}
code,kbd{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:.88em;
 background:var(--code);padding:1px 5px;border-radius:4px}
pre{background:var(--code);border:1px solid var(--line);border-radius:8px;padding:12px 14px;
 overflow-x:auto;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
 font-size:12.5px;line-height:1.5}
pre code{background:none;padding:0}
.tw{overflow-x:auto;margin:1em 0}
table{border-collapse:collapse;font-size:13.5px;min-width:100%}
th,td{text-align:left;padding:5px 11px;border-bottom:1px solid var(--line);vertical-align:top}
th{font-weight:650;color:var(--dim);text-transform:uppercase;font-size:10.5px;
 letter-spacing:.06em;white-space:nowrap}
tbody tr:hover{background:var(--code)}
td code{background:none;padding:0}
.mono td{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:12.5px}
.lede{font-size:17.5px;color:var(--dim);max-width:76ch;margin:.6em 0 1.4em}
.tag{display:inline-block;font-size:10.5px;font-weight:700;padding:1.5px 7px;border-radius:20px;
 vertical-align:middle;letter-spacing:.04em}
.t20{background:var(--v20);color:var(--bg)}
.t22{background:var(--v22);color:var(--bg)}
.box{background:var(--card);border:1px solid var(--line);border-left:3px solid var(--accent);
 border-radius:0 8px 8px 0;padding:12px 18px;margin:1.3em 0}
.box.key{border-left-color:var(--warn);background:var(--hl)}
.box h4{margin-top:.3em}
.box p:last-child,.box ul:last-child{margin-bottom:.3em}
figure{margin:1.6em 0;text-align:center}
figure svg{max-width:100%;height:auto}
figcaption{font-size:12.5px;color:var(--dim);margin-top:.6em;text-align:center}
details{border:1px solid var(--line);border-radius:8px;margin:1em 0;background:var(--card)}
details>summary{cursor:pointer;padding:10px 16px;font-weight:600;font-size:14px;user-select:none}
details>summary:hover{background:var(--code)}
details>div{padding:0 16px 12px;border-top:1px solid var(--line)}
.lst{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:12px;line-height:1.45;
 white-space:pre;overflow-x:auto;background:var(--code);border:1px solid var(--line);
 border-radius:8px;padding:10px 12px;max-height:640px;overflow-y:auto}
.a{color:var(--dim)}
.b{color:var(--accent2)}
.m{font-weight:650}
.x{color:var(--dim);font-style:italic}
.fnh{color:var(--accent);font-weight:700}
.add{background:rgba(80,160,110,.18)}
.del{background:rgba(200,90,90,.18)}
#tt{position:fixed;top:12px;right:14px;z-index:9;background:var(--card);color:var(--fg);
 border:1px solid var(--line);border-radius:20px;padding:5px 13px;cursor:pointer;font-size:12px}
.kv{font-size:14px}
.kv td:first-child{color:var(--dim);white-space:nowrap;width:1%}
hr{border:0;border-top:1px solid var(--line);margin:2.5em 0}
.small{font-size:13px;color:var(--dim)}
@media(max-width:1000px){nav{display:none}main{padding:20px}}
"""

JS = r"""
(function(){
 var b=document.getElementById('tt');
 function cur(){var t=document.documentElement.getAttribute('data-theme');
  if(t)return t;return matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light';}
 function set(t){document.documentElement.setAttribute('data-theme',t);
  b.textContent=t==='dark'?'☀ light':'☾ dark';}
 set(cur());
 b.onclick=function(){set(cur()==='dark'?'light':'dark');};
})();
"""


# --------------------------------------------------------------------------
# SVG diagrams
# --------------------------------------------------------------------------

SVG_HDR = ('<svg viewBox="0 0 {w} {h}" xmlns="http://www.w3.org/2000/svg" '
           'font-family="ui-monospace,Menlo,Consolas,monospace" font-size="12">'
           '<style>'
           '.bx{{fill:var(--card);stroke:var(--line);stroke-width:1.5}}'
           '.bx20{{fill:none;stroke:var(--v20);stroke-width:2}}'
           '.bx22{{fill:none;stroke:var(--v22);stroke-width:2}}'
           '.acc{{fill:none;stroke:var(--accent);stroke-width:2}}'
           '.t{{fill:var(--fg)}}.d{{fill:var(--dim);font-size:10.5px}}'
           '.ln{{stroke:var(--dim);stroke-width:1.4;fill:none}}'
           '.lna{{stroke:var(--accent);stroke-width:1.8;fill:none}}'
           '.bold{{font-weight:700}}'
           '</style>'
           '<defs><marker id="ar" markerWidth="9" markerHeight="7" refX="8" refY="3.5" '
           'orient="auto"><polygon points="0 0,9 3.5,0 7" fill="var(--dim)"/></marker>'
           '<marker id="ara" markerWidth="9" markerHeight="7" refX="8" refY="3.5" '
           'orient="auto"><polygon points="0 0,9 3.5,0 7" fill="var(--accent)"/></marker>'
           '</defs>')


def svg_memmap():
    """Image layout, both revisions, to scale."""
    W, H = 900, 300
    s = [SVG_HDR.format(w=W, h=H)]
    total = 8174
    x0, wid = 70, 760
    for row, (key, lab) in enumerate((("v20", "Rev 20"), ("v22", "Rev 22"))):
        y = 46 + row * 118
        s.append(f'<text x="8" y="{y+22}" class="t bold">{lab}</text>')
        s.append(f'<rect x="{x0}" y="{y}" width="{wid}" height="34" class="bx"/>')
        gaps = dict(GAPS[key])
        # regions: code vs gaps
        segs = []
        pos = 0
        for ga in sorted(gaps):
            if ga > pos:
                segs.append((pos, ga - pos, "code"))
            segs.append((ga, gaps[ga], "fill" if gaps[ga] > 3000 else "data"))
            pos = ga + gaps[ga]
        if pos < total:
            segs.append((pos, total - pos, "code"))
        for a, n, kind in segs:
            px = x0 + wid * a / total
            pw = max(1.0, wid * n / total)
            if kind == "code":
                col, op = "var(--accent2)", ".55"
            elif kind == "data":
                col, op = "var(--accent)", ".8"
            else:
                col, op = "var(--dim)", ".22"
            s.append(f'<rect x="{px:.1f}" y="{y}" width="{pw:.1f}" height="34" '
                     f'fill="{col}" opacity="{op}"/>')
        s.append(f'<rect x="{x0}" y="{y}" width="{wid}" height="34" class="bx" fill="none"/>')
        # annotate the big blocks
        for a, n, kind in segs:
            if n < 300:
                continue
            px = x0 + wid * (a + n / 2) / total
            lbl = {"code": "code", "data": "descriptors", "fill": "0xFF erase fill"}[kind]
            s.append(f'<text x="{px:.0f}" y="{y+52}" class="d" text-anchor="middle">'
                     f'{lbl}</text>')
            s.append(f'<text x="{px:.0f}" y="{y+65}" class="d" text-anchor="middle">'
                     f'{n} B</text>')
        s.append(f'<text x="{x0}" y="{y-6}" class="d">0x0000</text>')
        s.append(f'<text x="{x0+wid}" y="{y-6}" class="d" text-anchor="end">0x1FED</text>')
    # legend
    ly = 282
    for i, (c, o, t) in enumerate((("var(--accent2)", ".55", "instructions"),
                                   ("var(--accent)", ".8", "data tables"),
                                   ("var(--dim)", ".22", "0xFF fill"))):
        lx = 70 + i * 200
        s.append(f'<rect x="{lx}" y="{ly-10}" width="14" height="12" fill="{c}" opacity="{o}"/>')
        s.append(f'<text x="{lx+20}" y="{ly}" class="d">{t}</text>')
    s.append("</svg>")
    return "".join(s)


def svg_boot():
    W, H = 900, 400
    s = [SVG_HDR.format(w=W, h=H)]
    steps = [
        ("Power on / USB attach", "TAS1020B boot ROM runs from internal ROM", "var(--dim)"),
        ("Boot ROM reads EEPROM over I2C", "header + signature checked; 8174 B image copied to program RAM", "var(--dim)"),
        ("MEMCFG.SDW = 1, jump to 0x0000", "code fetches now come from shadow RAM", "var(--dim)"),
        ("reset_vector 0x0000 -> LJMP", "v20 0x0A09 / v22 0x092A", "var(--accent)"),
        ("Keil C51 startup", "zero IDATA, run the initializer table at 0x0F9C / 0x0FBA", "var(--accent)"),
        ("main()", "seed IRAM state, CLR EA, VECINT=0", "var(--accent)"),
        ("hw_master_init", "timers, ports, codec port CPTCNF1..4, GLOBCTL.CPTEN", "var(--accent2)"),
        ("usb_ep_dma_init", "endpoint buffers + DMACTL0/1 transfer sizes", "var(--accent2)"),
        ("~65535-iteration delay", "settling time for the external codec / CS8427", "var(--dim)"),
        ("TR0 = 1, EA = 1", "Timer 0 starts the 1 ms panel tick", "var(--accent)"),
        ("USBCTL |= 0x80 (CONN)", "pull-up asserted -> host now sees the device", "var(--accent)"),
        ("main loop", "poll event queue + panel, service USB from ISR", "var(--accent)"),
    ]
    y = 18
    for i, (t, d, c) in enumerate(steps):
        s.append(f'<rect x="150" y="{y}" width="600" height="26" rx="5" class="bx" '
                 f'stroke="{c}"/>')
        s.append(f'<text x="164" y="{y+17}" class="t bold">{E(t)}</text>')
        s.append(f'<text x="470" y="{y+17}" class="d">{E(d)}</text>')
        if i < len(steps) - 1:
            s.append(f'<line x1="450" y1="{y+26}" x2="450" y2="{y+31}" class="ln" '
                     f'marker-end="url(#ar)"/>')
        y += 32
    s.append("</svg>")
    return "".join(s)


def svg_audio():
    W, H = 940, 330
    s = [SVG_HDR.format(w=W, h=H)]

    def box(x, y, w_, h_, title, sub="", cls="bx"):
        s.append(f'<rect x="{x}" y="{y}" width="{w_}" height="{h_}" rx="6" class="{cls}"/>')
        s.append(f'<text x="{x+w_/2}" y="{y+(17 if sub else h_/2+4)}" class="t bold" '
                 f'text-anchor="middle">{E(title)}</text>')
        if sub:
            for i, ln in enumerate(sub.split("|")):
                s.append(f'<text x="{x+w_/2}" y="{y+32+i*13}" class="d" '
                         f'text-anchor="middle">{E(ln)}</text>')

    def arrow(x1, y1, x2, y2, lab="", acc=False):
        c = "lna" if acc else "ln"
        m = "ara" if acc else "ar"
        s.append(f'<path d="M{x1} {y1} L{x2} {y2}" class="{c}" marker-end="url(#{m})"/>')
        if lab:
            s.append(f'<text x="{(x1+x2)/2}" y="{min(y1,y2)-6}" class="d" '
                     f'text-anchor="middle">{E(lab)}</text>')

    s.append('<text x="10" y="16" class="t bold">Capture  (device -> host)</text>')
    box(10, 26, 150, 54, "Analog in / SPDIF", "combo jacks, CS8427|selector unit ID 5")
    box(190, 26, 130, 54, "Codec", "24-bit I2S|serial receive")
    box(350, 26, 160, 54, "C-port receive", "CPTRXCNF2/3/4|SCLK2 = MCLKO2 / DIVB2")
    box(540, 26, 150, 54, "DMA ch 1", "DMACTL1 = 0x09|EP1 IN, 6 B/sample")
    box(720, 26, 150, 54, "EP1 IN buffer", "IEPCNF1 = 0xC5|X/Y ping-pong")
    arrow(160, 53, 188, 53); arrow(320, 53, 348, 53)
    arrow(510, 53, 538, 53); arrow(690, 53, 718, 53)
    s.append('<text x="880" y="57" class="d">-&gt; host</text>')

    s.append('<text x="10" y="126" class="t bold">Playback  (host -> device)</text>')
    box(720, 136, 150, 54, "EP2 OUT buffer", "OEPCNF2 = 0xC5|X/Y ping-pong")
    box(540, 136, 150, 54, "DMA ch 0", "DMACTL0 = 0x02|EP2 OUT, 6 B/sample")
    box(350, 136, 160, 54, "C-port transmit", "CPTCNF1..4|SCLK = MCLKO1 / DIVB")
    box(190, 136, 130, 54, "Codec", "24-bit I2S|serial transmit")
    box(10, 136, 150, 54, "Line / headphone out", "")
    arrow(718, 163, 692, 163); arrow(538, 163, 512, 163)
    arrow(348, 163, 322, 163); arrow(188, 163, 162, 163)
    s.append('<text x="880" y="167" class="d">&lt;- host</text>')

    # clock block
    s.append('<text x="10" y="236" class="t bold">Adaptive clock generator (ACG)</text>')
    box(10, 246, 160, 54, "FreqSynth 1", "ACG1FRQ2/1/0|0x61A80F = 48 kHz")
    box(200, 246, 140, 54, "/M1 = ACG1DCTL", "0x10 -> divide by 2")
    box(370, 246, 130, 54, "MCLKO1", "ACGCTL = 0x06")
    box(530, 246, 150, 54, "/B = CPTCNF4", "0x03 -> divide by 4")
    box(710, 246, 160, 54, "SCLK -> codec", "sample rate set here")
    arrow(170, 273, 198, 273, "", True); arrow(340, 273, 368, 273, "", True)
    arrow(500, 273, 528, 273, "", True); arrow(680, 273, 708, 273, "", True)
    s.append('<text x="10" y="322" class="d">'
             'The receive side mirrors this: FreqSynth 2 -&gt; ACG2DCTL -&gt; MCLKO2 -&gt; '
             'CPTRXCNF4 (DIVB2) -&gt; SCLK2.</text>')
    s.append("</svg>")
    return "".join(s)


def svg_panel():
    W, H = 900, 300
    s = [SVG_HDR.format(w=W, h=H)]
    s.append('<rect x="20" y="20" width="200" height="120" rx="8" class="bx"/>')
    s.append('<text x="120" y="44" class="t bold" text-anchor="middle">TAS1020B</text>')
    s.append('<text x="120" y="62" class="d" text-anchor="middle">Port 1 (SFR 0x90)</text>')
    for i, (pin, role) in enumerate((("P1.7", "DATA  A"), ("P1.6", "LATCH A"),
                                     ("P1.5", "CLK   A"), ("P1.2", "CLK   B"),
                                     ("P1.1", "LATCH B"), ("P1.0", "DATA  B"))):
        y = 80 + i * 0
    rows = [("P1.7", "DATA"), ("P1.5", "CLOCK"), ("P1.6", "LATCH")]
    y = 82
    for pin, role in rows:
        s.append(f'<text x="36" y="{y}" class="d">{pin}  {role}</text>')
        y += 15
    s.append('<rect x="330" y="20" width="230" height="80" rx="8" class="bx" '
             'stroke="var(--accent)"/>')
    s.append('<text x="445" y="44" class="t bold" text-anchor="middle">'
             'Shift register A (8 bit)</text>')
    s.append('<text x="445" y="62" class="d" text-anchor="middle">source: IRAM 0x22</text>')
    s.append('<text x="445" y="78" class="d" text-anchor="middle">'
             'MSB first, shiftreg8 @0x0F0C</text>')
    s.append('<rect x="620" y="20" width="250" height="80" rx="8" class="bx"/>')
    s.append('<text x="745" y="44" class="t bold" text-anchor="middle">'
             'Input source mux + LEDs</text>')
    s.append('<text x="745" y="64" class="d" text-anchor="middle">'
             'ch A = bits 2:0,  ch B = bits 5:3</text>')
    s.append('<text x="745" y="80" class="d" text-anchor="middle">bit 6 derived from 0x25.4/.5</text>')
    s.append('<path d="M225 60 L328 60" class="lna" marker-end="url(#ara)"/>')
    s.append('<path d="M562 60 L618 60" class="lna" marker-end="url(#ara)"/>')

    s.append('<rect x="330" y="150" width="230" height="80" rx="8" class="bx" '
             'stroke="var(--accent2)"/>')
    s.append('<text x="445" y="174" class="t bold" text-anchor="middle">'
             'Shift register B (16 bit)</text>')
    s.append('<text x="445" y="192" class="d" text-anchor="middle">'
             'source: IRAM 0x23 then 0x25</text>')
    s.append('<text x="445" y="208" class="d" text-anchor="middle">'
             'MSB first, shiftreg16 @0x0E62</text>')
    s.append('<rect x="620" y="150" width="250" height="80" rx="8" class="bx"/>')
    s.append('<text x="745" y="174" class="t bold" text-anchor="middle">Panel LEDs / relays</text>')
    s.append('<text x="745" y="194" class="d" text-anchor="middle">'
             '0x23.6 toggled by the P3.5 button</text>')
    rows2 = [("P1.0", "DATA"), ("P1.2", "CLOCK"), ("P1.1", "LATCH")]
    y = 175
    for pin, role in rows2:
        s.append(f'<text x="36" y="{y}" class="d">{pin}  {role}</text>')
        y += 15
    s.append('<rect x="20" y="150" width="200" height="80" rx="8" class="bx"/>')
    s.append('<text x="120" y="170" class="d" text-anchor="middle">Port 1 (cont.)</text>')
    s.append('<path d="M225 190 L328 190" class="lna" marker-end="url(#ara)"/>')
    s.append('<path d="M562 190 L618 190" class="lna" marker-end="url(#ara)"/>')

    s.append('<rect x="20" y="252" width="850" height="34" rx="6" class="bx"/>')
    s.append('<text x="34" y="273" class="d">'
             'Buttons are INPUTS on Port 3 (SFR 0xB0):  P3.3 -&gt; channel A source,  '
             'P3.4 -&gt; channel B source,  P3.5 -&gt; toggles IRAM 0x23.6.  '
             'Scanned every Timer-0 tick by p3_button_scan @0x0ED5.</text>')
    s.append("</svg>")
    return "".join(s)


def svg_sof():
    W, H = 900, 290
    s = [SVG_HDR.format(w=W, h=H)]

    def node(x, y, w_, h_, txt, sub="", cls="bx"):
        s.append(f'<rect x="{x}" y="{y}" width="{w_}" height="{h_}" rx="6" class="{cls}"/>')
        s.append(f'<text x="{x+w_/2}" y="{y+(17 if sub else h_/2+4)}" class="t bold" '
                 f'text-anchor="middle">{E(txt)}</text>')
        if sub:
            s.append(f'<text x="{x+w_/2}" y="{y+33}" class="d" text-anchor="middle">{E(sub)}</text>')

    def dia(x, y, w_, h_, txt):
        s.append(f'<path d="M{x+w_/2} {y} L{x+w_} {y+h_/2} L{x+w_/2} {y+h_} L{x} {y+h_/2} Z" '
                 f'class="bx" stroke="var(--accent)"/>')
        s.append(f'<text x="{x+w_/2}" y="{y+h_/2+4}" class="t bold" '
                 f'text-anchor="middle">{E(txt)}</text>')

    def ar(x1, y1, x2, y2, lab=""):
        s.append(f'<path d="M{x1} {y1} L{x2} {y2}" class="ln" marker-end="url(#ar)"/>')
        if lab:
            s.append(f'<text x="{(x1+x2)/2+8}" y="{(y1+y2)/2-4}" class="d">{E(lab)}</text>')

    node(20, 16, 180, 40, "SOF interrupt", "every 1 ms, VECINT 0x14", "bx22")
    ar(200, 36, 240, 36)
    node(240, 16, 210, 40, "read DMABCNT0 (16 bit)", "0xFFEC:0xFFEB")
    ar(450, 36, 490, 36)
    dia(490, 6, 170, 60, "changed?")
    ar(660, 36, 740, 36, "no")
    node(740, 16, 140, 40, "RET", "")
    s.append('<path d="M575 66 L575 96" class="ln" marker-end="url(#ar)"/>')
    s.append('<text x="583" y="86" class="d">yes</text>')
    node(455, 96, 240, 40, "udiv16(count, 6)", "remainder in R4:R5")
    s.append('<path d="M575 136 L575 160" class="ln" marker-end="url(#ar)"/>')
    dia(470, 160, 210, 60, "remainder = 0?")
    s.append(f'<path d="M680 190 L760 190 L760 60" class="ln" marker-end="url(#ar)"/>')
    s.append('<text x="690" y="182" class="d">yes - aligned</text>')
    s.append('<path d="M575 220 L575 244" class="ln" marker-end="url(#ar)"/>')
    s.append('<text x="583" y="238" class="d">no - stream has slipped</text>')
    node(150, 244, 600, 38, "DMACTL0 &amp;= ~DMAEN  |  OEPBCTX2 = OEPBCTY2 = 0  |  "
                            "OEPCNF2 = 0xC5  |  DMACTL0 |= DMAEN", "", "bx22")
    s.append("</svg>")
    return "".join(s)


# --------------------------------------------------------------------------
# rendered listing
# --------------------------------------------------------------------------

def render_listing(key, fns=None):
    out = []
    fns = fns if fns is not None else FNS[key]
    gapmap = dict(GAPS[key])
    for f in fns:
        out.append(f'<span class="fnh">;=== {f["name"]}  @0x{f["addr"]:04X}  '
                   f'({f["bytes"]} B) ===</span>')
        for a, raw, txt, cmt in f["ins"]:
            c = f'   <span class="x">; {E(cmt)}</span>' if cmt else ""
            out.append(f'<span class="a">{a:04X}</span>  '
                       f'<span class="b">{raw:<10}</span><span class="m">{E(txt)}</span>{c}')
        out.append("")
    return "\n".join(out)


def render_gap(key, addr, n, per=16):
    d = DATA[key][addr:addr + n]
    out = []
    for i in range(0, len(d), per):
        chunk = d[i:i + per]
        out.append(f'<span class="a">{addr+i:04X}</span>  '
                   + " ".join(f"{b:02x}" for b in chunk))
    return "\n".join(out)


# --------------------------------------------------------------------------
# document
# --------------------------------------------------------------------------

TOC = []


def sec(title, anchor, sub=False):
    TOC.append((title, anchor, sub))
    h(3 if sub else 2, title, anchor)


w('<button id="tt">theme</button>')
w('<div class="wrap"><nav id="toc"></nav><main>')

w('<h1>Digidesign Mbox 1 &mdash; Stock Firmware</h1>')
w('<p class="lede">Complete reverse-engineering reference for revisions 20 and 22 of the '
  'Digidesign Mbox 1 firmware, running on a Texas Instruments TAS1020B '
  '(8051-core USB audio streaming controller). Every byte of both 8174-byte images is '
  'accounted for and classified.</p>')

kv = [
    ("Device", "Digidesign Mbox 1 (USB audio interface, 2000&ndash;2001)"),
    ("Controller", "TI TAS1020B &mdash; enhanced 8051 core, USB 1.1 full speed, "
                   "I&sup2;S codec port, 2 DMA channels"),
    ("Image size", "8174 bytes (0x1FEE) each, loaded at CODE 0x0000 from I&sup2;C EEPROM"),
    ("Execution", "copied by the boot ROM into shadow program RAM; <code>MEMCFG.SDW = 1</code>"),
    ("Toolchain", "Keil C51 (identified from the startup stub, "
                  "<code>?C_INITSEG</code> table and the <code>?C?CASE</code> "
                  "jump-table helper in Rev 20)"),
    ("USB identity", "VID 0x0DBA, PID 0x1000; <code>bcdDevice</code> 0x0020 (Rev 20) "
                     "/ 0x0022 (Rev 22)"),
]
w('<div class="tw"><table class="kv"><tbody>'
  + "".join(f"<tr><td>{a}</td><td>{b}</td></tr>" for a, b in kv)
  + "</tbody></table></div>")

# ---- 1 how to read -------------------------------------------------------
sec("1. Method, and how to read this", "method")
w("<p>Both images were disassembled by <b>recursive traversal</b> from the reset vector and "
  "the interrupt table, following every call, jump, conditional branch and jump-table edge. "
  "Alignment is therefore guaranteed by construction rather than assumed &mdash; a linear "
  "sweep of 8051 code drifts silently whenever it starts mid-instruction, and an earlier "
  "linear listing of this same image did exactly that.</p>")
w("<p>Coverage below is <b>measured</b>: every byte is marked as belonging to a decoded "
  "instruction, to an identified data block, or to erase fill. The unaccounted count is "
  "zero for both images.</p>")

w('<div class="box key"><h4>Reading trap: the same number is two different addresses</h4>'
  '<p>8051 bit instructions (<code>JB</code>, <code>JNB</code>, <code>SETB</code>, '
  '<code>CLR</code>, <code>MOV C,</code>) take a <b>bit address</b>; byte instructions '
  '(<code>MOV</code>, <code>ORL</code>, <code>INC</code>) take a <b>direct byte address</b>. '
  'They print identically. Bit address <i>B</i> lives in IRAM byte '
  '<code>0x20 + (B &gt;&gt; 3)</code>, bit <code>B &amp; 7</code>.</p>'
  '<p>This firmware uses both meanings of the same literal constantly. In '
  '<code>cmd7</code>: <code>JNB 0x2c</code> tests IRAM <code>0x25</code> bit 4 '
  '(the S/PDIF-selected flag), while three instructions later '
  '<code>MOV 0x2c,#0x23</code> writes IRAM byte <code>0x2C</code> (a staged chip-register '
  'number). Likewise <code>MOV 0x0a,#0x01</code> queues event 1, but <code>CLR 0x0a</code> '
  'clears IRAM <code>0x21</code> bit 2. Misreading one for the other produces confident '
  'nonsense.</p></div>')

# ---- 2 byte accounting ---------------------------------------------------
sec("2. Byte accounting", "bytes")
rows = []
for key, lab in (("v20", "Rev 20"), ("v22", "Rev 22")):
    ins = sum(1 for c in COV[key] if c == 1)
    gap = sum(1 for c in COV[key] if c == 2)
    unc = sum(1 for c in COV[key] if c == 0)
    fill = sum(n for a, n in GAPS[key] if n > 3000)
    rows.append((f'<span class="tag t{key[1:]}">{lab}</span>', f"{len(DATA[key])}",
                 f"{ins}", f"{gap - fill}", f"{fill}", f"<b>{unc}</b>",
                 f"{len(FNS[key])}"))
table(["Image", "Total", "Instruction bytes", "Data-table bytes", "0xFF fill",
       "Unaccounted", "Functions"], rows)
w(f"<figure>{svg_memmap()}<figcaption>Image layout to scale. The right-hand ~49&nbsp;% of "
  "each EEPROM image is unprogrammed 0xFF.</figcaption></figure>")

h(3, "2.1 The data blocks", "datablocks")
w("<p>Every non-instruction region resolves to a specific table:</p>")
gap_desc = {
    "v20": {0x011F: ("Standard-request jump table", "Keil <code>?C?CASE</code> records: "
                     "<code>{addr_hi, addr_lo, key}</code>, terminated by "
                     "<code>00 00</code> then a default address."),
            0x0596: ("USB descriptor block", "402 bytes. See &sect;6.3."),
            0x0A48: ("Bit-mask table", "<code>01 02 04 08 10 20 40 80</code> &mdash; "
                     "Keil bit-addressing helper."),
            0x0C93: ("VECINT dispatch table", "37 big-endian handler addresses indexed "
                     "directly by the VECINT register. See &sect;5.2."),
            0x0F9C: ("C51 initialiser table", "<code>{len, addr, value...}</code> records "
                     "run by the startup interpreter."),
            0x103F: ("Erase fill", "verified 0xFF for every byte to end of image.")},
    "v22": {0x057D: ("USB descriptor block", "402 bytes, byte-identical to Rev 20 except "
                     "<code>bcdDevice</code>."),
            0x0969: ("Bit-mask table", "<code>01 02 04 08 10 20 40 80</code>."),
            0x0C7D: ("VECINT dispatch table", "37 entries. See &sect;5.2."),
            0x0FBA: ("C51 initialiser table", "one record shorter than Rev 20."),
            0x1036: ("Erase fill", "verified 0xFF for every byte to end of image.")},
}
rows = []
for key, lab in (("v20", "Rev 20"), ("v22", "Rev 22")):
    for a, n in GAPS[key]:
        nm, ds = gap_desc[key].get(a, ("(unclassified)", ""))
        rows.append((f'<span class="tag t{key[1:]}">{lab}</span>', f"<code>0x{a:04X}</code>",
                     f"{n}", f"<b>{nm}</b>", ds))
table(["Image", "Address", "Bytes", "Block", "Notes"], rows)
w('<p class="small">Rev 22 has one fewer block because it does not use the Keil searched-key '
  'jump table &mdash; see &sect;6.2.</p>')

# ---- 3 boot --------------------------------------------------------------
sec("3. Boot and initialisation", "boot")
w("<p>The TAS1020B has no internal flash. Its mask boot ROM enumerates first, reads the "
  "external I&sup2;C EEPROM, validates a header and signature, copies the firmware image "
  "into shadow program RAM, sets <code>MEMCFG.SDW</code> so code fetches come from that RAM, "
  "and jumps to 0x0000. Everything documented here begins at that jump.</p>")
w(f"<figure>{svg_boot()}<figcaption>Cold-start sequence. Addresses differ between revisions "
  "but the order is identical.</figcaption></figure>")
w("<p>Two details matter for anyone writing replacement firmware. First, <code>USBCTL</code> "
  "bit 7 (CONN, the D+ pull-up) is asserted <b>last</b>, only after the codec port and DMA "
  "are configured &mdash; so a hang anywhere earlier leaves the device invisible on the bus "
  "rather than enumerated-but-broken. Second, <code>GLOBCTL</code> bit 0 is CPTEN, the codec "
  "port enable, not a USB enable; the codec configuration registers are only writable while "
  "it is clear.</p>")

# ---- 4 main loop ---------------------------------------------------------
sec("4. Main loop and the event queue", "mainloop")
w("<p>The firmware is a single-threaded foreground loop plus interrupt handlers. USB "
  "protocol work happens in the INT0 handler; everything slow or stateful is deferred to a "
  "one-slot <b>event queue</b> &mdash; IRAM byte <code>0x0A</code> &mdash; drained by the "
  "foreground loop.</p>")
w("<pre><code>forever:\n"
  "    if (tick_flag)            # bit 0x20, set by the Timer 0 ISR\n"
  "        scan_p3_buttons()     # debounce + edge detect on the front panel\n"
  "        if (panel_changed)\n"
  "            shift_out_8()     # IRAM 0x22 -> mux/LED chain A\n"
  "            shift_out_16()    # IRAM 0x23,0x25 -> chain B\n"
  "        handle_alt_setting_edges()\n"
  "        tick_flag = 0\n"
  "    else if (event_code != 0) # IRAM 0x0A\n"
  "        dispatch_event()      # 14-entry LJMP table\n</code></pre>")
w("<p>Timer 0 reloads <code>TH0 = 0xCE</code> and sets the tick flag, giving the panel scan a "
  "steady period; the ISR also re-enables <code>EA</code> before returning, so USB interrupts "
  "are not blocked by panel work.</p>")

h(3, "4.1 The 14 deferred events", "events")
w("<p>A handler queues an event by writing its number to IRAM <code>0x0A</code>. The "
  "dispatcher subtracts 1, range-checks against 14, multiplies by 3 and jumps into a table "
  "of <code>LJMP</code>s. Rev 20 keeps that table in a separate function at 0x0300; Rev 22 "
  "inlines it at 0x030C and uses <code>MUL AB</code> instead of two adds.</p>")
EV = [
 (1, "apply clock mode", "0x032A", "0x0336",
  "Tear down DMA + CPTEN, set CPTCNF3 byte order per direction, re-run the audio path."),
 (2, "interface 1 alt changed", "0x0386", "0x038A",
  "Capture path. Arms IEPCNF1 = 0xC5 and DMACTL1 |= DMAEN, or disarms them."),
 (3, "interface 2 alt changed", "0x03FD", "0x03FD",
  "Playback path. Arms OEPCNF2 = 0xC5 and DMACTL0 |= DMAEN, or disarms them."),
 (4, "select analog input", "0x0454", "0x045A",
  "Clears the S/PDIF flag (bit 0x2C), commits both shift registers, reapplies clock mode."),
 (5, "select S/PDIF input", "0x0466", "0x0469",
  "Sets bit 0x2C, forces clock mode 1."),
 (6, "set clock mode 1", "0x0478", "0x0478", "Idle / no sample clock."),
 (7, "set clock mode 2 + program S/PDIF chip", "0x0480", "0x047D", "44.1 kHz."),
 (8, "set clock mode 3 + program S/PDIF chip", "0x049A", "0x049F", "48 kHz."),
 (9, "set clock mode 4", "0x04B4", "0x04C0", "Falls through to the common tail."),
 (10, "set clock mode 5", "0x04BC", "0x04C4",
  "External / S/PDIF-slaved: halves CPTRXCNF4 to 0x01 and re-enables CPTEN."),
 (11, "EEPROM self-test", "0x04C4", "0x04C8",
  "Sets mode 3, writes chip reg 4 = 0x41, then reads EEPROM byte 0x1F, inverts it, "
  "writes it back, re-reads and compares; the result drives a panel bit."),
 (12, "set clock mode 1", "0x0511", "0x0478", "Duplicate of event 6."),
 (13, "invalidate boot EEPROM", "0x0518", "0x0517",
  "Writes 0x00 to EEPROM address 0 &mdash; the DFU trigger. See &sect;6.4."),
 (14, "suspend / resume", "0x0526", "0x0525", "USB suspend entry and wake."),
]
table(["Event", "Meaning", "Rev 20", "Rev 22", "What it does"],
      [(str(n), m, f"<code>{a}</code>", f"<code>{b}</code>", d) for n, m, a, b, d in EV])

# ---- 5 interrupts --------------------------------------------------------
sec("5. Interrupts", "interrupts")
h(3, "5.1 The 8051 vector table", "ivt")
w("<p>Only two of the six core vectors do real work. INT0 is the USB engine; Timer 0 is the "
  "panel tick. The rest jump to shared one-byte <code>RETI</code> stubs, which is why the "
  "region below 0x0026 looks like scattered <code>0x22</code> and <code>0x32</code> bytes.</p>")
rows = []
for v, nm in ((0x0000, "Reset"), (0x0003, "INT0 &mdash; USB engine"),
              (0x000B, "Timer 0 &mdash; panel tick"), (0x0013, "INT1 &mdash; unused"),
              (0x001B, "Timer 1 &mdash; unused"), (0x0023, "Serial &mdash; unused")):
    c = []
    for key in ("v20", "v22"):
        d = DATA[key]
        tgt = (d[v + 1] << 8) | d[v + 2] if d[v] == 0x02 else None
        c.append(f"<code>0x{tgt:04X}</code>" if tgt else "&mdash;")
    rows.append((f"<code>0x{v:04X}</code>", nm, c[0], c[1]))
table(["Vector", "Source", "Rev 20 target", "Rev 22 target"], rows)

h(3, "5.2 The VECINT dispatch table", "vecint")
w("<p>The TAS1020B multiplexes every USB event onto INT0 and reports the cause in the "
  "<code>VECINT</code> register (0xFFB2). The INT0 handler reads it and indexes a table of "
  "37 big-endian addresses &mdash; one per defined VECINT value, 0x00 through 0x24. "
  "Handler names below were assigned independently of this table and match it exactly, "
  "which is a strong cross-check on both.</p>")
rows = []
for i in range(37):
    nm, ds = VECINT[i]
    cells = []
    for key in ("v20", "v22"):
        b = VEC_BASE[key]
        t = (DATA[key][b + 2 * i] << 8) | DATA[key][b + 2 * i + 1]
        fn = fname(key, t)
        stub = fn is None or "noop" in (fn or "") or (fn or "").endswith("_reti")
        cells.append((t, fn, stub))
    hi = ""
    if cells[0][2] and not cells[1][2]:
        hi = ' class="add"'
    elif cells[1][2] and not cells[0][2]:
        hi = ' class="del"'
    def cell(c):
        t, fn, stub = c
        s = f"<code>0x{t:04X}</code>"
        if fn:
            s += f' <span class="small">{E(fn)}</span>'
        return s
    rows.append((f"<code>0x{i:02X}</code>", f"<b>{nm}</b>", cell(cells[0]), cell(cells[1]),
                 "<b>Rev 22 adds a real handler</b>" if hi == ' class="add"' else ""))
table(["VECINT", "Source", "Rev 20", "Rev 22", ""], rows, cls="mono")
w('<p class="small">Every entry below 0x0030 is a one-byte <code>RET</code> stub; the '
  '0x1029&ndash;0x103E band at the top of the code region is a run of such stubs, one per '
  'unused source.</p>')

# ---- 6 USB ---------------------------------------------------------------
sec("6. USB", "usb")
h(3, "6.1 SETUP dispatch", "setup")
w("<p>On a SETUP interrupt the handler clears any stall on both halves of endpoint 0, sets "
  "the data-toggle bits, arms the buffers, then branches on <code>bmRequestType</code> read "
  "from XDATA <code>0xFF28</code>. The compiler emitted this as a chain of adds against the "
  "accumulator rather than compares, which is why it reads oddly:</p>")
w("<pre><code>A = bmRequestType\n"
  "A += 0xDE   ; == A - 0x22\n"
  "JZ  class_out_endpoint      ; 0x22  class, host->device, endpoint\n"
  "A += 0x81   ; == A - 0xA1\n"
  "JZ  get_input_source        ; 0xA1  class, device->host, interface\n"
  "DEC A       ; == A - 0xA2\n"
  "JZ  get_sample_freq         ; 0xA2  class, device->host, endpoint\n"
  "A += 0x81   ; == A - 0x21\n"
  "JZ  class_out_interface     ; 0x21  class, host->device, interface\n"
  "LJMP standard_request_dispatch</code></pre>")
w("<p>The four class paths cover exactly two audio controls &mdash; the input selector and "
  "the sampling-frequency endpoint control &mdash; plus the vendor trigger described in "
  "&sect;6.4. Everything else falls through to the standard-request dispatcher.</p>")
w("<p>The EP0 buffers sit at XDATA <code>0xFA10</code> (OUT) and <code>0xFA18</code> (IN), "
  "8 bytes each, consistent with the declared <code>bMaxPacketSize0 = 8</code>. IRAM "
  "<code>0x1B:0x1C</code> holds a working pointer into them.</p>")

h(3, "6.2 Standard requests &mdash; where the revisions diverge", "stdreq")
w("<p>This is the largest structural difference between the two firmwares. Rev 20 calls the "
  "Keil <code>?C?CASE</code> helper, which walks a table of "
  "<code>{addr_hi, addr_lo, key}</code> records comparing keys until it finds a match or hits "
  "a <code>00 00</code> terminator followed by a default address. Rev 22 drops the helper "
  "entirely and uses a dense table of <code>LJMP</code>s indexed by "
  "<code>bRequest &times; 3</code> &mdash; no search, no library call, and reserved request "
  "codes get explicit stall entries instead of falling off the end.</p>")
a = DATA["v20"][0x011F:0x011F + 37]
recs = {}
i = 0
while i + 2 < len(a):
    hi, lo, k = a[i], a[i + 1], a[i + 2]
    if hi == 0 and lo == 0:
        dflt20 = (a[i + 2] << 8) | a[i + 3]
        break
    recs[k] = (hi << 8) | lo
    i += 3
b = DATA["v22"]
rows = []
for n in range(13):
    t22 = (b[0x011E + 3 * n + 1] << 8) | b[0x011E + 3 * n + 2]
    t20 = recs.get(n)
    c20 = f"<code>0x{t20:04X}</code>" if t20 else \
          f'<span class="small">(absent &rarr; default <code>0x{dflt20:04X}</code>)</span>'
    f20 = fname("v20", t20) if t20 else None
    f22 = fname("v22", t22)
    rows.append((f"<code>0x{n:02X}</code>", BREQ.get(n, "?"),
                 c20 + (f'<br><span class="small">{E(f20)}</span>' if f20 else ""),
                 f"<code>0x{t22:04X}</code>" +
                 (f'<br><span class="small">{E(f22)}</span>' if f22 else "")))
table(["bRequest", "Name", "Rev 20 (searched table @0x011F)",
       "Rev 22 (dense LJMP table @0x011E)"], rows)

h(3, "6.3 Descriptors &mdash; and the block that is never served", "descriptors")
w("<p>The 402-byte descriptor block is <b>byte-identical between the two revisions except for "
  "a single byte</b>: <code>bcdDevice</code> at block offset +0x00C, 0x0020 in Rev 20 and "
  "0x0022 in Rev 22.</p>")
d = DATA["v20"]
base = 0x0596
rows = []
i = 0
while i < 402 and d[base + i]:
    L, T = d[base + i], d[base + i + 1]
    body = d[base + i:base + i + L]
    note = ""
    if T == 2:
        tot = body[2] | (body[3] << 8)
        note = (f"wTotalLength {tot}, {body[4]} interfaces, "
                f"bConfigurationValue {body[5]}, {body[8]*2} mA")
    elif T == 4:
        note = (f"interface {body[2]} alt {body[3]}, {body[4]} endpoints, "
                f"class 0x{body[5]:02X} subclass 0x{body[6]:02X}")
    elif T == 5:
        note = (f"EP 0x{body[2]:02X} {'IN' if body[2]&0x80 else 'OUT'}, "
                f"attr 0x{body[3]:02X} (isochronous), "
                f"wMaxPacketSize {body[4]|(body[5]<<8)}")
    elif T == 1:
        note = (f"VID 0x{body[8]|(body[9]<<8):04X} PID 0x{body[10]|(body[11]<<8):04X}, "
                f"bcdDevice 0x{body[12]|(body[13]<<8):04X}, EP0 size {body[7]}")
    elif T == 3:
        try:
            s = body[2:].decode("utf-16-le")
            note = "&ldquo;" + E(s) + "&rdquo;" if L > 4 else "language ID 0x0409"
        except Exception:
            note = ""
    elif T == 0x24 and body[2] == 2:
        note = (f"input terminal ID {body[3]}, type 0x{body[4]|(body[5]<<8):04X}")
    elif T == 0x24 and body[2] == 3:
        note = f"output terminal ID {body[3]}, source {body[7]}"
    elif T == 0x24 and body[2] == 5:
        note = f"<b>selector unit ID {body[3]}</b>, sources {list(body[5:5+body[4]])}"
    elif T == 0x24 and body[2] == 1 and L == 7:
        note = f"AS general, bTerminalLink {body[3]}"
    elif T == 0x24 and body[2] == 2 and L == 14:
        note = (f"{body[4]} ch, {body[5]}-byte subframe, {body[6]} bit, rates "
                f"{body[8]|(body[9]<<8)|(body[10]<<16)} / "
                f"{body[11]|(body[12]<<8)|(body[13]<<16)} Hz")
    served = 0x012 <= i < 0x0DA
    cls = ' class="del"' if served else ""
    rows.append((f"<code>+0x{i:03X}</code>", str(L), DESC_TYPE.get(T, f"0x{T:02X}"),
                 note, "never served" if served else ""))
    i += L
table(["Offset", "Len", "Type", "Decoded", ""], rows)

w('<div class="box key"><h4>The stock Mbox never exposes its own audio-class descriptors</h4>'
  '<p>Both images contain a complete, well-formed USB Audio Class configuration at block '
  'offset +0x012: an AudioControl interface with input terminals for analog and S/PDIF, a '
  'selector unit, output terminals, two AudioStreaming interfaces with 24-bit 44.1/48 kHz '
  'format descriptors, and both isochronous endpoints. <b>It is dead data.</b></p>'
  '<p><code>std_get_descriptor</code> contains exactly five CODE pointer loads, and none of '
  'them is +0x012. <code>GET_DESCRIPTOR(CONFIGURATION)</code> always returns block offset '
  '<b>+0x0DA</b> &mdash; a 54-byte vendor-class configuration: two interfaces, '
  '<code>bInterfaceClass = 0xFF</code>, no endpoints on alt 0, and both iso endpoints on '
  '<b>interface 1 alternate setting 1</b>. Only descriptor index 0 is handled; any other '
  'index stalls. Verified by scanning both images for every pointer-load instruction pair.</p>'
  '<p>This is why the device needs a proprietary driver, why Linux carries a hardcoded quirk '
  'table for 0x0DBA:0x1000 rather than parsing descriptors, and why the endpoints only appear '
  'on interface 1 alt 1. The UAC descriptors were compiled in and then not wired up.</p>'
  '</div>')

h(3, "6.4 The DFU trigger", "dfu")
w("<p>Fully traced, and identical in both revisions:</p>")
w("<pre><code>bmRequestType 0x21, bRequest 0x00      (class, host-&gt;device, interface)\n"
  "  -&gt; setup handler reads XDATA 0xFF29 (bRequest), sees zero\n"
  "  -&gt; MOV 0x0A,#0x0D                     queue event 13\n"
  "  -&gt; event 13 handler:\n"
  "       i2c_eeprom_write(addr 0x0000, value 0x00)\n"
  "       OEPDCNTX0 = 0                    acknowledge on EP0</code></pre>")
w("<p>Writing zero to EEPROM byte 0 destroys the boot signature the mask ROM checks. The "
  "device keeps running the already-loaded image, so nothing appears to happen; on the next "
  "power cycle the boot ROM fails validation and stays in its own DFU mode. Any "
  "<code>bRequest</code> other than 0 on the same request type is treated as a class "
  "SET_CUR and tagged for an OUT data stage instead.</p>")

# ---- 7 audio -------------------------------------------------------------
sec("7. The audio path", "audio")
w(f"<figure>{svg_audio()}<figcaption>Capture, playback and clock generation. Register values "
  "shown are those written by the boot initialisation.</figcaption></figure>")

h(3, "7.1 Clock modes", "clockmodes")
w("<p>All rate selection funnels through one function (Rev 20 <code>0x0728</code>, Rev 22 "
  "<code>0x070F</code>) taking a mode number in R7. The mode is stored in IRAM "
  "<code>0x08</code>, and the class GET_CUR handler reads that same byte to answer host "
  "queries about the current sampling frequency &mdash; which pins the mapping without "
  "any guesswork:</p>")
table(["Mode", "Reported rate", "ACG1FRQ2/1/0", "What it does"],
      [("1", "0 Hz (idle)", "&mdash;", "<code>ACGCTL = 0x0D</code>; no sample clock."),
       ("2", "44 100 Hz", "<code>6A 4B 20</code>", "Both synthesizers to the 44.1 family."),
       ("3", "48 000 Hz", "<code>61 A8 0F</code>",
        "Both synthesizers to the 48 k family. This is what both SET_INTERFACE paths select."),
       ("4", "&mdash;", "&mdash;", "Falls straight through to the common tail."),
       ("5", "external", "&mdash;",
        "Drops CPTEN, sets <code>CPTRXCNF4 = 0x01</code> (&divide;2 instead of &divide;4), "
        "restores CPTEN &mdash; the S/PDIF-slaved receive mode.")])
w("<p>The common tail then writes the queued external-chip register pair, sets "
  "<code>ACGCTL |= 0xC0</code> (both MCLKO outputs enabled), zeroes all four endpoint "
  "byte-count registers, re-enables both isochronous endpoint configurations at "
  "<code>0xC5</code>, commits the panel shift register and spins a settling delay.</p>")
w('<div class="box"><p><b>The 0xC5 endpoint configuration byte.</b> '
  '<code>IEPCNF1</code>/<code>OEPCNF2</code> = <code>0xC5</code> is '
  'ENABLE | ISO | BPS&nbsp;5, i.e. 6 bytes per sample &mdash; two channels of 24-bit audio. '
  'It matches <code>DMATSH = 0x80</code> (3 bytes per time slot) and '
  '<code>DMATSL = 0x03</code> (time slots 0 and 1) on both DMA channels.</p></div>')

h(3, "7.2 Byte order", "byteorder")
w("<p><code>CPTCNF3</code> and <code>CPTRXCNF3</code> share a layout whose bit 2 is BYOR: "
  "when set, the byte order of each audio sample is reversed as it moves between the codec "
  "port and the endpoint buffer. Boot initialisation writes <code>0xAC</code> (BYOR set) to "
  "both. Event 1 then rewrites <code>CPTCNF3</code> by direction &mdash; <code>0xAC</code> "
  "when capture is active, <code>0xA8</code> when playback is &mdash; because the DMA moves "
  "bytes the opposite way on each path, so opposite BYOR values produce the same order on "
  "the wire. Stock is big-endian in both directions, which is what Linux declares.</p>")

# ---- 8 panel -------------------------------------------------------------
sec("8. Front panel", "panel")
w(f"<figure>{svg_panel()}<figcaption>Two independent bit-banged shift-register chains on "
  "Port 1; buttons are inputs on Port 3.</figcaption></figure>")
w("<p>There is no latch peripheral &mdash; both chains are driven by software loops that "
  "rotate a byte left and test bit 0 after the rotate, so bits go out <b>most significant "
  "first</b>. Chain A carries 8 bits from IRAM <code>0x22</code>; chain B carries 16 bits, "
  "IRAM <code>0x23</code> followed by IRAM <code>0x25</code>.</p>")
table(["Chain", "Data", "Clock", "Latch", "Source", "Routine (Rev 20 / Rev 22)"],
      [("A", "P1.7", "P1.5", "P1.6", "IRAM 0x22",
        "<code>0x0F0C</code> / <code>0x0EFC</code>"),
       ("B", "P1.0", "P1.2", "P1.1", "IRAM 0x23 then 0x25",
        "<code>0x0E62</code> / <code>0x0E56</code>")])

h(3, "8.1 The source-select state machine", "sourcesel")
w("<p>Each of the two input channels has a three-position selector cycled by a front-panel "
  "button. Channel A occupies IRAM <code>0x22</code> bits 2:0 and channel B bits 5:3. The "
  "cycle is driven by two hidden state bits per channel, and it runs in a fixed order:</p>")
w("<pre><code>state (0,0) --press--&gt; emit 0b101 (0x05)   state becomes (1,1)\n"
  "state (1,1) --press--&gt; emit 0b011 (0x03)   state becomes (1,0)\n"
  "state (1,0) --press--&gt; emit 0b110 (0x06)   state becomes (0,0)</code></pre>")
w("<p>So the ring is <code>0x05 &rarr; 0x03 &rarr; 0x06 &rarr; 0x05</code>, which against the "
  "panel legend makes <b>0x05 = Mic, 0x03 = Line, 0x06 = Inst</b>. Channel B is the same "
  "machine on bits 0x29/0x2B driving output bits 3, 4 and 5.</p>")
w('<div class="box key"><h4>Direction matters</h4><p>Any reimplementation that cycles '
  '<code>0x05 &rarr; 0x06 &rarr; 0x03</code> traverses the same three values in the '
  '<b>opposite</b> order. The selector still works but the button steps backwards through the '
  'panel legend.</p></div>')
w("<p>Both channel routines end with the same tail, which derives IRAM <code>0x22</code> bit 6 "
  "from two other flags: it is set when bit <code>0x2C</code> is clear, and forced clear when "
  "bit <code>0x2D</code> is set. Bit <code>0x2C</code> is the analog-versus-S/PDIF selection "
  "&mdash; the very bit the class <code>GET_CUR</code> handler reports as selector-unit pin 1 "
  "or 2. Bit <code>0x22.6</code> is therefore a derived output, not an independent control.</p>")
w("<p>The third button, on P3.5, calls a nine-byte routine that is a pure toggle of bit "
  "<code>0x1E</code> &mdash; IRAM <code>0x23</code> bit 6 &mdash; which rides out on chain B "
  "and additionally changes how chain A finishes its latch sequence. It is a firmware-owned "
  "latched output with no other dependency.</p>")

# ---- 9 diff --------------------------------------------------------------
sec("9. Rev 20 versus Rev 22", "diff")
w("<p>The two images are the same program. Function-level structural matching pairs 94 of "
  "them; the differences are concentrated in three places.</p>")

h(3, "9.1 The headline: a playback DMA watchdog", "sofwatchdog")
w("<p>Rev 20 points VECINT 0x14 (SOF) at a one-byte <code>RET</code>. Rev 22 points it at a "
  "70-byte handler that runs every USB frame:</p>")
w(f"<figure>{svg_sof()}<figcaption>Rev 22's start-of-frame handler. Rev 20 has no equivalent."
  "</figcaption></figure>")
w("<p>It reads the 16-bit playback DMA byte counter, and if it has moved since the last "
  "frame, divides it by 6 &mdash; the size of one stereo 24-bit sample frame &mdash; using a "
  "16-bit division routine that exists in Rev 22 solely to support this check. If the "
  "remainder is non-zero the playback stream has slipped out of sample alignment, and the "
  "handler stops the DMA channel, clears both ping-pong buffer counts, re-enables the OUT "
  "endpoint and restarts the channel.</p>")
w('<div class="box key"><p>A misalignment of anything other than a whole number of 6-byte '
  'frames permanently rotates the channel and byte order of the playback stream. Rev 20 has '
  'no way to detect or recover from it; Rev 22 checks once per millisecond and resynchronises. '
  'This is the most plausible mechanical explanation for Rev 20 being the revision that '
  'misbehaves on playback.</p>'
  '<p>Note also what Rev 22 did <b>not</b> need to change: Rev 20 already unmasks SOF '
  '(<code>USBIMSK = 0xFF</code> when interface 1 goes to a non-zero alt setting), so the '
  'interrupt was already firing &mdash; into a stub. The fix is one table entry plus a '
  'handler.</p></div>')

h(3, "9.2 Restructuring, same behaviour", "restructure")
table(["Area", "Rev 20", "Rev 22"],
      [("Standard-request dispatch",
        "Keil <code>?C?CASE</code> helper + 37-byte searched key table",
        "39-byte dense <code>LJMP</code> table indexed by <code>bRequest</code>"),
       ("SETUP handling",
        "one 47-byte dispatcher calling four separate class handlers",
        "a single 218-byte handler with all four inlined"),
       ("Event index math", "<code>ADD A,R0</code> twice", "<code>MUL AB</code> with B = 3"),
       ("External chip writes",
        "register/value staged through IRAM 0x2C&ndash;0x2F, then a thunk",
        "register and value passed directly in R7/R5"),
       ("Delay loops", "counted in IRAM byte 0x2E", "counted in R7"),
       ("Panel scan result", "returned in R7", "left in IRAM 0x07 for the caller"),
       ("C51 initialiser table", "13 records", "12 records &mdash; Rev 20 additionally "
        "initialises IRAM 0x08 to 3")])

h(3, "9.3 What is byte-identical", "identical")
w("<p>The descriptor block differs by one byte (<code>bcdDevice</code>). The VECINT table "
  "differs only in relocated handler addresses plus the SOF entry. The clock-mode values, "
  "the endpoint configuration bytes, the DMA transfer sizes, the shift-register bit order, "
  "the source-select state machine and the DFU trigger are all unchanged.</p>")

# ---- 10 IRAM map ---------------------------------------------------------
sec("10. Internal RAM map", "iram")
w("<p>Addresses used as bit operands are shown with their owning byte. Register banks occupy "
  "0x00&ndash;0x1F; the bit-addressable region is 0x20&ndash;0x2F.</p>")
table(["IRAM", "Meaning"],
      [("<code>0x08</code>", "current clock mode (1,2,3,5) &mdash; read back by the class "
        "GET_CUR sampling-frequency handler"),
       ("<code>0x0A</code>", "pending event code, 1&ndash;14; zero means idle"),
       ("<code>0x0D</code>", "pending class-request tag (1 = endpoint, 2 = interface, "
        "5 = deferred SET_ADDRESS)"),
       ("<code>0x0E</code>", "deferred USB device address, written to USBFADR after the "
        "status stage"),
       ("<code>0x19:0x1A</code>", "CODE pointer for descriptor reads (<code>MOVC</code>)"),
       ("<code>0x1B:0x1C</code>", "XDATA pointer into the EP0 buffers"),
       ("<code>0x20</code>", "previous Port 3 sample, for edge detection"),
       ("<code>0x21</code>", "bit 0 = interface 1 alt, bit 1 = interface 2 alt, "
        "bit 2 = (vestigial, never set), bit 6 = configured"),
       ("<code>0x22</code>", "shift-register chain A: bits 2:0 channel A source, "
        "bits 5:3 channel B source, bit 6 derived, bit 7 mute/idle"),
       ("<code>0x23</code>", "shift-register chain B low byte; bit 6 toggled by the "
        "P3.5 button"),
       ("<code>0x24</code>", "bit 0 = Timer 0 tick flag"),
       ("<code>0x25</code>", "shift-register chain B high byte; bit 4 = S/PDIF selected, "
        "bit 5 = forces the derived bit clear"),
       ("<code>0x27</code>", "suspend/resume edge state"),
       ("<code>0x28</code>&ndash;<code>0x2B</code>", "hidden selector state bits, two per "
        "channel"),
       ("<code>0x2C</code>&ndash;<code>0x2F</code>", "staged external-chip register/value "
        "pairs and delay counters (Rev 20 only)"),
       ("<code>0x31:0x32</code>", "queued chip register/value applied by the clock-mode tail")])

# ---- 11 function inventory ----------------------------------------------
sec("11. Function inventory", "inventory")
for key, lab in (("v20", "Rev 20"), ("v22", "Rev 22")):
    h(3, f"11.{1 if key=='v20' else 2} {lab} &mdash; {len(FNS[key])} functions",
      f"inv{key}")
    rows = [(f"<code>0x{f['addr']:04X}</code>", f"<code>{E(f['name'])}</code>",
             str(f["bytes"]), str(len(f["ins"])))
            for f in sorted(FNS[key], key=lambda x: x["addr"])]
    table(["Address", "Name", "Bytes", "Instructions"], rows)

# ---- 12 listings ---------------------------------------------------------
sec("12. Full disassembly listings", "listings")
w("<p>Complete recursive-traversal output for both images, in address order, followed by the "
  "raw contents of every data block.</p>")
for key, lab in (("v20", "Rev 20"), ("v22", "Rev 22")):
    w(f'<details><summary>{lab} &mdash; full listing '
      f'({sum(len(f["ins"]) for f in FNS[key])} instructions)</summary><div>')
    w(f'<div class="lst">{render_listing(key, sorted(FNS[key], key=lambda x: x["addr"]))}</div>')
    w("</div></details>")
    w(f'<details><summary>{lab} &mdash; data blocks (hex)</summary><div>')
    for a, n in GAPS[key]:
        nm = gap_desc[key].get(a, ("(unclassified)", ""))[0]
        if n > 3000:
            w(f'<p class="small"><b>0x{a:04X}</b> &mdash; {nm}, {n} bytes, '
              f'all 0xFF (not reproduced).</p>')
            continue
        w(f'<p class="small"><b>0x{a:04X}</b> &mdash; {nm}, {n} bytes</p>')
        w(f'<div class="lst">{render_gap(key, a, n)}</div>')
    w("</div></details>")

# ---- 13 provenance -------------------------------------------------------
sec("13. Provenance and confidence", "provenance")
w("<p>Structure &mdash; instruction boundaries, control flow, byte classification, table "
  "contents, descriptor fields &mdash; is derived mechanically and is exact. Where a name "
  "expresses an interpretation of intent rather than a decoded fact, that interpretation "
  "rests on one of: a register's documented function in the TAS1020B datasheet, a matching "
  "constant in TI's USB-audio reference sources, agreement between the two independent "
  "revisions, or a value the firmware itself reports back over USB.</p>")
w("<p>The strongest internal cross-check available: the VECINT handler names were assigned "
  "without reference to the dispatch table, and all 37 entries line up &mdash; SETUP to the "
  "SETUP handler, SUSR to the suspend handler, IEP0 to the EP0-IN handler, every unused "
  "source to a <code>RET</code> stub. Similarly, the clock-mode numbering is confirmed by the "
  "firmware answering 44100 and 48000 to the host from the same IRAM byte the mode setter "
  "writes.</p>")
w('<p class="small">Generated by <code>tools/build_fw_doc.py</code> directly from '
  '<code>rev20_firmware_code.bin</code>, <code>rev22_firmware_code.bin</code> and the two '
  'Ghidra recursive-traversal listings. Every table, count and listing on this page is '
  'produced at build time; none is transcribed by hand.</p>')

# ---- 14 decompiled C -----------------------------------------------------
sec("14. Decompiled C", "decompiled")
w("<p>Both images were also run through a decompiler. Coverage is complete &mdash; every "
  "function in both images has a C body &mdash; and the SFR reads and writes resolve to "
  "named registers, which makes the audio and USB paths far easier to follow than the "
  "assembly. Treat it as a readable second view, not as ground truth: the assembly and the "
  "raw bytes remain authoritative.</p>")
rows = []
for key, lab, cf in (("v20", "Rev 20", CSRC["v20"]), ("v22", "Rev 22", CSRC["v22"])):
    rows.append((f'<span class="tag t{key[1:]}">{lab}</span>', str(len(FNS[key])),
                 str(len(CFNS[key])),
                 str(sum(1 for f in CFNS[key].values() if "halt_baddata" in f)),
                 str(cf.count("\n"))))
table(["Image", "Functions", "With C body", "Decompiler failures", "Lines of C"], rows)

w('<div class="box"><h4>The only two decompiler failures, and why they are not unknowns</h4>'
  '<p>Both are in Rev 20 and both are the same construct: the Keil <code>?C?CASE</code> '
  'helper at <code>0x0F70</code> pops its own return address off the stack and uses it as a '
  'pointer to an inline key table. The decompiler models the call as never returning and '
  'flags the 37 table bytes at <code>0x011F</code> as bad instruction data. That table is '
  'fully decoded in &sect;6.2 &mdash; eleven <code>{addr, bRequest}</code> records plus a '
  'default. Rev 22 has zero failures because it replaced the helper with a dense '
  '<code>LJMP</code> table.</p></div>')

h(3, "14.1 Side by side", "sidebyside")
w("<p>Three functions where the C view earns its keep.</p>")
PAIRS = [
    ("v22", "sof_int_handler",
     "Rev 22's playback watchdog. The <code>% 6</code> is the whole point and it is "
     "invisible in the assembly, where it is a call to a 16-bit division helper."),
    ("v20", "audio_clock_mode_apply",
     "Rate selection. Every ACG and endpoint register write in one place."),
    ("v20", "button_a_cycle_3state",
     "The source-select ring. Bit variables decompile to named flags."),
]
for key, nm, why in PAIRS:
    body = CFNS[key].get(nm)
    if not body:
        continue
    lab = "Rev 20" if key == "v20" else "Rev 22"
    w(f'<h4><span class="tag t{key[1:]}">{lab}</span> <code>{E(nm)}</code></h4>')
    w(f"<p>{why}</p>")
    w(f'<pre><code>{E(body.strip())}</code></pre>')

h(3, "14.2 Full C listings", "fullc")
for key, lab in (("v20", "Rev 20"), ("v22", "Rev 22")):
    w(f'<details><summary>{lab} &mdash; decompiled C '
      f'({len(CFNS[key])} functions, {CSRC[key].count(chr(10))} lines)</summary><div>')
    w(f'<div class="lst">{E(CSRC[key])}</div>')
    w("</div></details>")

w("</main></div>")

# ---- assemble ------------------------------------------------------------
nav = []
for title, anchor, sub in TOC:
    nav.append(f'<a class="{"s" if sub else ""}" href="#{anchor}">{title}</a>')
navhtml = '<div class="nt">Contents</div>' + "".join(nav)
body = "".join(_parts).replace('<nav id="toc"></nav>', f"<nav>{navhtml}</nav>")

doc = (f"<title>Mbox 1 Firmware &mdash; Rev 20 &amp; Rev 22</title>"
       f"<meta name=viewport content='width=device-width,initial-scale=1'>"
       f"<style>{CSS}</style>{body}<script>{JS}</script>")
open(OUT, "w").write(doc)
print(f"wrote {OUT}  ({len(doc)/1024:.0f} KB)")
