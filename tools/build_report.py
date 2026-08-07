#!/usr/bin/env python3
"""Build the bench measurement report page from the measured CSVs."""
import csv
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
OUT = os.path.join(HERE, "mbox_bench_report.html")


def load(name):
    with open(os.path.join(DATA, name)) as f:
        return [
            {k: (v if k in ("hz",) else v) for k, v in row.items()}
            for row in csv.DictReader(f)
        ]


def fnum(v):
    try:
        x = float(v)
        return None if math.isnan(x) else x
    except Exception:
        return None


freq = load("freq.csv")
level = load("level.csv")

W, H = 760, 300
PL, PR, PT, PB = 62, 18, 18, 42


def xlog(hz, lo, hi):
    return PL + (math.log10(hz) - math.log10(lo)) / (
        math.log10(hi) - math.log10(lo)
    ) * (W - PL - PR)


def xlin(v, lo, hi):
    return PL + (v - lo) / (hi - lo) * (W - PL - PR)


def yv(v, lo, hi):
    return PT + (hi - v) / (hi - lo) * (H - PT - PB)


def chart(series, xticks, yticks, xmap, ymap, ylabel, xlabel, bands=None):
    """series: list of dicts(points=[(x,y)], cls=..., dots=bool)"""
    p = []
    p.append(
        '<svg class="chart" viewBox="0 0 %d %d" role="img" '
        'preserveAspectRatio="xMidYMid meet">' % (W, H)
    )
    if bands:
        for b in bands:
            x0, x1 = xmap(b["from"]), xmap(b["to"])
            p.append(
                '<rect class="band" x="%.1f" y="%d" width="%.1f" height="%d"/>'
                % (x0, PT, x1 - x0, H - PT - PB)
            )
            p.append(
                '<text class="bandlabel" x="%.1f" y="%d">%s</text>'
                % (x0 + 6, PT + 16, b["label"])
            )
    for v, lab in yticks:
        y = ymap(v)
        p.append(
            '<line class="grid" x1="%d" y1="%.1f" x2="%d" y2="%.1f"/>'
            % (PL, y, W - PR, y)
        )
        p.append('<text class="tick ty" x="%d" y="%.1f">%s</text>' % (PL - 8, y + 4, lab))
    for v, lab in xticks:
        x = xmap(v)
        p.append(
            '<line class="grid vg" x1="%.1f" y1="%d" x2="%.1f" y2="%d"/>'
            % (x, PT, x, H - PB)
        )
        p.append(
            '<text class="tick tx" x="%.1f" y="%d">%s</text>' % (x, H - PB + 20, lab)
        )
    p.append(
        '<line class="axis" x1="%d" y1="%d" x2="%d" y2="%d"/>' % (PL, PT, PL, H - PB)
    )
    p.append(
        '<line class="axis" x1="%d" y1="%d" x2="%d" y2="%d"/>'
        % (PL, H - PB, W - PR, H - PB)
    )
    for s in series:
        pts = " ".join("%.2f,%.2f" % (xmap(x), ymap(y)) for x, y in s["points"])
        p.append('<polyline class="trace %s" points="%s"/>' % (s.get("cls", ""), pts))
        if s.get("dots", True):
            for x, y in s["points"]:
                p.append(
                    '<circle class="dot %s" cx="%.2f" cy="%.2f" r="2.6"/>'
                    % (s.get("cls", ""), xmap(x), ymap(y))
                )
    p.append(
        '<text class="axlabel" x="%d" y="%d">%s</text>'
        % (PL, PT - 4, ylabel)
    )
    p.append(
        '<text class="axlabel axr" x="%d" y="%d">%s</text>'
        % (W - PR, H - 6, xlabel)
    )
    p.append("</svg>")
    return "\n".join(p)


# ---------------------------------------------------------------- frequency
FLO, FHI = 18, 22000
fx = [(20, "20"), (50, "50"), (100, "100"), (200, "200"), (500, "500"),
      (1000, "1k"), (2000, "2k"), (5000, "5k"), (10000, "10k"), (20000, "20k")]

fr_pts = [(float(r["hz"]), fnum(r["fund_db"])) for r in freq]
base = fr_pts[0][1]
fr_rel = [(h, v - base) for h, v in fr_pts]
lo, hi = -0.5, 0.5
fy = [(v, "%+.2f" % v) for v in (-0.4, -0.2, 0.0, 0.2, 0.4)]
c_fr = chart(
    [{"points": fr_rel, "cls": "t1"}],
    fx, fy,
    lambda v: xlog(v, FLO, FHI),
    lambda v: yv(v, lo, hi),
    "dB, relative to 20 Hz", "Hz",
)

# ---------------------------------------------------------------- THD v freq
thd_pts = [
    (float(r["hz"]), fnum(r["thd_db"]))
    for r in freq
    if fnum(r["thd_db"]) is not None
]
tlo, thi = -100, -45
ty = [(v, "%d" % v) for v in (-50, -60, -70, -80, -90, -100)]
c_thd = chart(
    [{"points": thd_pts, "cls": "t2"}],
    fx, ty,
    lambda v: xlog(v, FLO, FHI),
    lambda v: yv(v, tlo, thi),
    "THD, dB below fundamental", "Hz",
    bands=[{"from": 18, "to": 200, "label": "LF skirt — not distortion"}],
)

# ---------------------------------------------------------------- THD v level
lv = [(fnum(r["fund_db"]), fnum(r["thd_db"])) for r in level]
lv = sorted([p for p in lv if p[0] is not None], key=lambda p: p[0])
llo, lhi = -85, -15
lx = [(v, "%d" % v) for v in (-80, -70, -60, -50, -40, -30, -20)]
ly = [(v, "%d" % v) for v in (-30, -45, -60, -75, -90)]
c_lvl = chart(
    [{"points": lv, "cls": "t3"}],
    lx, ly,
    lambda v: xlin(v, llo, lhi),
    lambda v: yv(v, -95, -25),
    "THD, dB below fundamental", "capture level, dBFS",
)

# ---------------------------------------------------------------- linearity
lin = [(math.log10(fnum(r["amp"])) * 20, fnum(r["fund_db"])) for r in level]
lin = sorted(lin, key=lambda p: p[0])
nx = [(v, "%d" % v) for v in (-60, -50, -40, -30, -20, -10, 0)]
ny = [(v, "%d" % v) for v in (-80, -60, -40, -20)]
c_lin = chart(
    [{"points": lin, "cls": "t4"}],
    nx, ny,
    lambda v: xlin(v, -62, 2),
    lambda v: yv(v, -85, -15),
    "capture, dBFS", "playback, dBFS",
)

lat_rows = [
    ("64", "512", "172.19", "6177"),
    ("128", "1024", "172.21", "6573"),
    ("256", "2048", "172.25", "6164"),
    ("512", "4096", "172.44*", "5693"),
    ("1024", "8192", "172.30", "6203"),
]

freq_rows = "".join(
    "<tr><td>%s</td><td>%.2f</td><td>%s</td><td>%.2f</td></tr>"
    % (
        r["hz"],
        fnum(r["fund_db"]),
        ("%.4f" % fnum(r["thd_pct"])) if fnum(r["thd_pct"]) is not None else "—",
        fnum(r["noise_db"]),
    )
    for r in freq
)

level_rows = "".join(
    "<tr><td>%.4f</td><td>%.2f</td><td>%.4f</td><td>%.2f</td></tr>"
    % (fnum(r["amp"]), fnum(r["fund_db"]), fnum(r["thd_pct"]), fnum(r["thd_db"]))
    for r in level
)

lat_html = "".join(
    "<tr><td>%s</td><td>%s</td><td>%s</td><td>%s×</td></tr>" % r for r in lat_rows
)

HTML = """<title>Mbox 1 — bench measurements at minimum gain</title>
<style>
:root{
  --paper:#f4f6f7; --card:#ffffff; --ink:#10161c; --body:#39434e;
  --muted:#6a7580; --rule:#d7dee2; --rule2:#e8edef;
  --accent:#0d7382; --accent2:#8a5a12; --good:#2c6e49;
  --t1:#0d7382; --t2:#8a5a12; --t3:#7a3560; --t4:#2c6e49;
  --band:rgba(138,90,18,.10);
}
@media (prefers-color-scheme:dark){
  :root{
    --paper:#0c1116; --card:#141b22; --ink:#e8eef2; --body:#b3c0cb;
    --muted:#7f8d99; --rule:#26313a; --rule2:#1c252d;
    --accent:#3fb6c6; --accent2:#d59a3c; --good:#5fc08a;
    --t1:#3fb6c6; --t2:#d59a3c; --t3:#cf87b4; --t4:#5fc08a;
    --band:rgba(213,154,60,.13);
  }
}
:root[data-theme="dark"]{
  --paper:#0c1116; --card:#141b22; --ink:#e8eef2; --body:#b3c0cb;
  --muted:#7f8d99; --rule:#26313a; --rule2:#1c252d;
  --accent:#3fb6c6; --accent2:#d59a3c; --good:#5fc08a;
  --t1:#3fb6c6; --t2:#d59a3c; --t3:#cf87b4; --t4:#5fc08a;
  --band:rgba(213,154,60,.13);
}
:root[data-theme="light"]{
  --paper:#f4f6f7; --card:#ffffff; --ink:#10161c; --body:#39434e;
  --muted:#6a7580; --rule:#d7dee2; --rule2:#e8edef;
  --accent:#0d7382; --accent2:#8a5a12; --good:#2c6e49;
  --t1:#0d7382; --t2:#8a5a12; --t3:#7a3560; --t4:#2c6e49;
  --band:rgba(138,90,18,.10);
}
*{box-sizing:border-box}
body{
  margin:0; background:var(--paper); color:var(--body);
  font:16px/1.62 ui-sans-serif,-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  -webkit-font-smoothing:antialiased;
}
.mono{font-family:ui-monospace,"SF Mono",SFMono-Regular,Menlo,Consolas,monospace}
.wrap{max-width:860px;margin:0 auto;padding:56px 24px 96px}
header{border-bottom:2px solid var(--ink);padding-bottom:22px;margin-bottom:34px}
.eyebrow{
  font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace;
  font-size:11.5px;letter-spacing:.14em;text-transform:uppercase;
  color:var(--accent);margin:0 0 12px
}
h1{
  font-size:clamp(28px,4.6vw,42px);line-height:1.1;margin:0 0 12px;
  color:var(--ink);font-weight:640;letter-spacing:-.02em;text-wrap:balance
}
.sub{margin:0;color:var(--muted);max-width:62ch;font-size:15.5px}
h2{
  font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace;
  font-size:12.5px;letter-spacing:.15em;text-transform:uppercase;
  color:var(--ink);margin:52px 0 6px;padding-bottom:8px;
  border-bottom:1px solid var(--rule);font-weight:650
}
h3{font-size:17px;color:var(--ink);margin:30px 0 8px;font-weight:620}
p{margin:12px 0;max-width:66ch}
a{color:var(--accent)}
.readout{
  display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));
  gap:1px;background:var(--rule);border:1px solid var(--rule);
  margin:30px 0 8px
}
.cell{background:var(--card);padding:16px 18px}
.cell .k{
  font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace;
  font-size:10.5px;letter-spacing:.11em;text-transform:uppercase;color:var(--muted)
}
.cell .v{
  font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace;
  font-size:25px;color:var(--ink);margin-top:7px;font-variant-numeric:tabular-nums;
  letter-spacing:-.02em
}
.cell .u{font-size:13px;color:var(--muted);margin-left:3px}
.figure{
  background:var(--card);border:1px solid var(--rule);padding:16px 14px 10px;
  margin:22px 0;overflow-x:auto
}
.figure figcaption{
  font-size:13.5px;color:var(--muted);padding:10px 6px 2px;
  border-top:1px solid var(--rule2);margin-top:8px
}
svg.chart{display:block;width:100%;min-width:600px;height:auto}
.grid{stroke:var(--rule2);stroke-width:1}
.vg{stroke-dasharray:2 3}
.axis{stroke:var(--rule);stroke-width:1.5}
.tick{
  font-family:ui-monospace,Menlo,monospace;font-size:10.5px;fill:var(--muted);
  font-variant-numeric:tabular-nums
}
.ty{text-anchor:end}.tx{text-anchor:middle}
.axlabel{
  font-family:ui-monospace,Menlo,monospace;font-size:10.5px;fill:var(--muted);
  letter-spacing:.06em
}
.axr{text-anchor:end}
.trace{fill:none;stroke-width:2;stroke-linejoin:round;stroke-linecap:round}
.t1{stroke:var(--t1)}.t2{stroke:var(--t2)}.t3{stroke:var(--t3)}.t4{stroke:var(--t4)}
circle.dot{stroke:none}
circle.t1{fill:var(--t1)}circle.t2{fill:var(--t2)}
circle.t3{fill:var(--t3)}circle.t4{fill:var(--t4)}
.band{fill:var(--band)}
.bandlabel{
  font-family:ui-monospace,Menlo,monospace;font-size:10.5px;fill:var(--accent2);
  letter-spacing:.05em
}
table{
  width:100%;border-collapse:collapse;margin:16px 0;font-size:13.5px;
  font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace;
  font-variant-numeric:tabular-nums
}
th,td{text-align:right;padding:6px 10px;border-bottom:1px solid var(--rule2)}
th:first-child,td:first-child{text-align:left}
thead th{
  color:var(--muted);font-weight:500;font-size:10.5px;letter-spacing:.09em;
  text-transform:uppercase;border-bottom:1px solid var(--rule)
}
.scroll{overflow-x:auto;border:1px solid var(--rule);background:var(--card);padding:2px 12px}
.note{
  border-left:3px solid var(--accent2);background:var(--card);
  padding:14px 18px;margin:22px 0
}
.note .t{
  font-family:ui-monospace,Menlo,monospace;font-size:11px;letter-spacing:.12em;
  text-transform:uppercase;color:var(--accent2);margin-bottom:6px
}
.note p{margin:8px 0}
.k-v{display:grid;grid-template-columns:auto 1fr;gap:4px 18px;margin:14px 0;
  font-size:14.5px}
.k-v dt{color:var(--muted);font-family:ui-monospace,Menlo,monospace;font-size:12.5px}
.k-v dd{margin:0;color:var(--body)}
footer{
  margin-top:60px;padding-top:20px;border-top:1px solid var(--rule);
  color:var(--muted);font-size:13px
}
</style>

<div class="wrap">
<header>
  <p class="eyebrow">Bench report · unit A · build 0x0038 · 2026-08-06</p>
  <h1>Mbox 1 loopback: latency, response and distortion at minimum gain</h1>
  <p class="sub">Round-trip measurements over the analog self-loop, with the
  front-panel gain dials at minimum. Every figure describes the whole loop —
  DAC, analog out, cable, line in, ADC — not any one stage.</p>
</header>

<div class="readout">
  <div class="cell"><div class="k">Round trip</div>
    <div class="v">3.59<span class="u">ms</span></div></div>
  <div class="cell"><div class="k">Response 20–20k</div>
    <div class="v">±0.16<span class="u">dB</span></div></div>
  <div class="cell"><div class="k">THD @ 1 kHz</div>
    <div class="v">0.0041<span class="u">%</span></div></div>
  <div class="cell"><div class="k">Noise floor</div>
    <div class="v">−107<span class="u">dBFS</span></div></div>
  <div class="cell"><div class="k">Loop gain</div>
    <div class="v">−20.2<span class="u">dB</span></div></div>
</div>

<h2>What was measured, and what it cannot say</h2>
<p>The signal path is unit A's self-loop: <span class="mono">line out 2 →
line source 2</span> over a TRS cable. That path was chosen over the crossed
inter-unit cables for one reason — playback and capture then share a single
crystal. The two units differ by about 4.4&nbsp;ppm, which over a one-second
analysis window smears a spectrum into skirts that read as distortion.</p>
<p>Because the loop contains both converters, <strong>no figure here separates
the output stage from the input stage.</strong> Splitting them needs an
external generator or analyser, which this bench does not have.</p>
<dl class="k-v">
  <dt>source</dt><dd>LINE on both channels, set over EP0 rather than the panel</dd>
  <dt>gain</dt><dd>minimum, both channels, unchanged throughout</dd>
  <dt>format</dt><dd>48 kHz, 24-bit, S24_3LE, 2 ch</dd>
  <dt>window</dt><dd>1.000 s coherent, rectangular, centred in a 2 s capture</dd>
</dl>

<h2>Round-trip latency</h2>
<p>Measured by linking the playback and capture substreams with
<span class="mono">snd_pcm_link()</span>, so both start on one hardware trigger
and capture frame 0 <em>is</em> playback frame 0. Latency is then a subtraction
of two frame indices rather than an inference. A Hann-windowed 2 kHz burst is
located by cross-correlation, giving sub-sample resolution.</p>
<p><strong>172.2–172.5 frames — 3.59 ms</strong> — and it does not move across a
16× range of buffer sizes. That flatness is the evidence that this is hardware
and USB transport, not software buffering: the ALSA buffer sets how far ahead
you must write, not the frame-to-frame relationship.</p>
<div class="scroll">
<table>
<thead><tr><th>period</th><th>buffer</th><th>round trip, frames</th>
<th>peak / next peak</th></tr></thead>
<tbody>%LAT%</tbody>
</table>
</div>
<p class="mono" style="font-size:12.5px;color:var(--muted)">* one run of this
config first read 156.27 frames — exactly 16 low — and did not reproduce across
four repeats. Recorded, unexplained, not averaged in.</p>

<h2>Frequency response</h2>
<div class="figure">
%CFR%
<figcaption>Relative to 20 Hz. Full vertical span is 1 dB — the trace occupies
a third of it.</figcaption>
</div>
<p>Flat within <strong>±0.16 dB from 20 Hz to 20 kHz</strong>, drifting
gently <em>upward</em> with frequency (−26.23 dB at 20 Hz, −25.92 dB at 20 kHz).
There is no low-end roll-off and no anti-alias droop at the top, which is
mildly surprising at 48 kHz — a reconstruction filter and an input
anti-alias filter both sit near 22 kHz and would normally show as a dip. A
slight analog HF lift appears to offset them.</p>

<h2>Distortion against frequency</h2>
<div class="figure">
%CTHD%
<figcaption>THD from harmonics 2–10. Above 12.5 kHz no harmonic fits below
Nyquist, so THD is undefined and the trace ends.</figcaption>
</div>
<p>From roughly 300 Hz up, THD falls steadily with frequency — <strong>0.0041%
at 1 kHz, 0.0016% at 8 kHz</strong>. That is genuinely low for a 2002 bus-powered
interface measured through both of its converters at once.</p>

<div class="note">
  <div class="t">The shaded region is not distortion</div>
  <p>Below about 200 Hz the THD column rises to 0.21% at 20 Hz, scaling almost
  exactly as 1/f. That is an artifact of the measurement, and the check that
  settles it is this: a smooth 1/f skirt centred on DC exists in the capture
  <em>with no signal playing at all</em>, at −79 dBFS.</p>
  <p>For a 20 Hz tone the harmonic bins land directly on that skirt, to within
  0.3 dB — 40 Hz predicted −83.3 and measured −83.34, 60 Hz predicted −86.9
  and measured −86.65, 80 Hz predicted −89.4 and measured −89.37. Those bins
  are the skirt, not harmonics of the tone.</p>
  <p>It is not mains hum: there is no 50 or 60 Hz peak anywhere. And it is not
  lost coherence, because with a 1 kHz tone the bins either side of the
  fundamental sit at −130 to −140 dBFS. <strong>True LF distortion is below the
  skirt and this method cannot reach it.</strong> The shaded figures are an
  upper bound.</p>
</div>

<h2>Distortion and linearity against level</h2>
<div class="figure">
%CLVL%
<figcaption>THD at 1 kHz against capture level. The minimum sits near
−26 dBFS; the rise to the right is the loop compressing near its ceiling.</figcaption>
</div>
<p>THD is lowest at <strong>0.0041% around −26 dBFS</strong> capture. Below that
it climbs as the harmonics sink toward a fixed noise floor. Above it, driving
the loop to full-scale playback pushes THD to 0.0346% — a sevenfold rise in the
last 6 dB, which is the analog path running out of headroom rather than
anything digital.</p>
<div class="figure">
%CLIN%
<figcaption>Capture level against playback level. A straight line of slope 1
across 60 dB.</figcaption>
</div>
<p>Linearity is essentially perfect: every 3 dB step at the source produces a
3 dB step at the capture, over the full 60 dB swept. The offset between the two
is the loop's gain.</p>

<h3>The gain dials are costing 20 dB of converter range</h3>
<p>At minimum gain, a <em>full-scale</em> playback signal reaches the ADC at only
<strong>−20.2 dBFS</strong>. The top 20 dB of the converter is unreachable — a
recording made at this setting throws away more than three bits before anything
digital happens. That is the dials doing their job, not a fault, but it does
mean every absolute level in the bench notes sits 20 dB below where the
converter would like to be.</p>

<h2>Full data</h2>
<h3>Frequency sweep, playback amplitude 0.5012</h3>
<div class="scroll">
<table>
<thead><tr><th>Hz</th><th>level, dBFS</th><th>THD, %</th>
<th>noise, dBFS/bin</th></tr></thead>
<tbody>%FREQROWS%</tbody>
</table>
</div>
<h3>Level sweep at 1 kHz</h3>
<div class="scroll">
<table>
<thead><tr><th>playback amp</th><th>capture, dBFS</th><th>THD, %</th>
<th>THD, dB</th></tr></thead>
<tbody>%LEVELROWS%</tbody>
</table>
</div>

<h2>What would come next</h2>
<p>Every number here is one point on the gain control's travel — the bottom of
it. Repeating the 1 kHz level sweep at marked dial positions would turn this
into a family of curves and answer the question the manual raises but does not
address: how much of the line input's distortion and noise is the preamp's,
and where on the dial the loop stops being limited by headroom and starts
being limited by the gain stage.</p>
<p>That needs someone at the bench, because nothing in the firmware can read
the dial position — there is no control ADC anywhere in the design, in
mboxfw or in either stock image.</p>

<footer>
Unit A, serial RK10874600Q, firmware build 0x0038. Self-loop
<span class="mono">out2 → src2</span>, TRS. Source LINE, gain minimum.
Measurement code: <span class="mono">rtlat.c</span>,
<span class="mono">sweep.py</span>.
</footer>
</div>
"""

HTML = (
    HTML.replace("%CFR%", c_fr)
    .replace("%CTHD%", c_thd)
    .replace("%CLVL%", c_lvl)
    .replace("%CLIN%", c_lin)
    .replace("%LAT%", lat_html)
    .replace("%FREQROWS%", freq_rows)
    .replace("%LEVELROWS%", level_rows)
)

with open(OUT, "w") as f:
    f.write(HTML)
print("wrote", OUT, len(HTML), "bytes")
