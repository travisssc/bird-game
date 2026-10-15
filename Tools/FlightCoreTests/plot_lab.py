#!/usr/bin/env python3
"""Turn flight_lab CSV into an SVG chart sheet (no dependencies).

Usage: flight_lab > lab.csv && python3 plot_lab.py lab.csv lab.svg

For each scenario: a side view (height vs distance flown) and airspeed over time,
with stalled samples marked in red.
"""
import csv
import sys
from collections import OrderedDict

PANEL_W, PANEL_H, PAD = 360, 170, 38
INK, MUTED, GRID, LINE, STALL = "#1f2328", "#6e7781", "#e5e7eb", "#2563eb", "#dc2626"


def nice_range(lo, hi):
    if hi - lo < 1e-6:
        lo, hi = lo - 1, hi + 1
    span = hi - lo
    return lo - 0.05 * span, hi + 0.05 * span


def fmt(v, span):
    return f"{v:.0f}" if span >= 10 else f"{v:.1f}"


def panel(x0, y0, title, xs, ys, flags, xlabel):
    out = []
    xlo, xhi = nice_range(min(xs), max(xs))
    ylo, yhi = nice_range(min(ys), max(ys))
    w, h = PANEL_W - PAD - 10, PANEL_H - PAD - 24
    px = lambda v: x0 + PAD + (v - xlo) / (xhi - xlo) * w
    py = lambda v: y0 + 24 + h - (v - ylo) / (yhi - ylo) * h
    out.append(f'<text x="{x0 + PAD}" y="{y0 + 14}" font-size="13" font-weight="600" fill="{INK}">{title}</text>')
    out.append(f'<rect x="{x0 + PAD}" y="{y0 + 24}" width="{w}" height="{h}" fill="none" stroke="{GRID}"/>')
    for frac in (0.0, 0.5, 1.0):
        yv = ylo + frac * (yhi - ylo)
        out.append(f'<line x1="{x0 + PAD}" x2="{x0 + PAD + w}" y1="{py(yv):.1f}" y2="{py(yv):.1f}" stroke="{GRID}"/>')
        out.append(f'<text x="{x0 + PAD - 4}" y="{py(yv) + 4:.1f}" font-size="10" text-anchor="end" fill="{MUTED}">{fmt(yv, yhi - ylo)}</text>')
        xv = xlo + frac * (xhi - xlo)
        out.append(f'<text x="{px(xv):.1f}" y="{y0 + 24 + h + 13}" font-size="10" text-anchor="middle" fill="{MUTED}">{fmt(xv, xhi - xlo)}</text>')
    out.append(f'<text x="{x0 + PAD + w}" y="{y0 + PANEL_H - 2}" font-size="10" text-anchor="end" fill="{MUTED}">{xlabel}</text>')
    pts = " ".join(f"{px(a):.1f},{py(b):.1f}" for a, b in zip(xs, ys))
    out.append(f'<polyline points="{pts}" fill="none" stroke="{LINE}" stroke-width="2"/>')
    for a, b, f in zip(xs, ys, flags):
        if f:
            out.append(f'<circle cx="{px(a):.1f}" cy="{py(b):.1f}" r="2.5" fill="{STALL}"/>')
    return out


def main(src, dst):
    data = OrderedDict()
    with open(src) as f:
        for r in csv.DictReader(f):
            data.setdefault(r["scenario"], []).append(r)

    width = 2 * PANEL_W + 20
    height = 40 + len(data) * (PANEL_H + 10)
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'font-family="system-ui, sans-serif"><rect width="100%" height="100%" fill="#ffffff"/>',
           f'<text x="10" y="24" font-size="16" font-weight="700" fill="{INK}">Flight lab: crow defaults '
           f'(red dots = stalled)</text>']
    for i, (name, rows) in enumerate(data.items()):
        y0 = 40 + i * (PANEL_H + 10)
        dist = [(float(r["x"]) ** 2 + float(r["y"]) ** 2) ** 0.5 for r in rows]
        z = [float(r["z"]) for r in rows]
        t = [float(r["t"]) for r in rows]
        v = [float(r["airspeed"]) for r in rows]
        st = [r["stalled"] == "1" for r in rows]
        out += panel(10, y0, f"{name}: height (m)", dist, z, st, "distance flown (m)")
        out += panel(10 + PANEL_W + 10, y0, f"{name}: airspeed (m/s)", t, v, st, "time (s)")
    out.append("</svg>")
    with open(dst, "w") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
