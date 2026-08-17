#!/usr/bin/env python3
"""Generate an SVG blueprint of the Looper panel at 1:1 with Rack's 100% zoom.

1 SVG unit = 1 module px. pu2px(pu) = pu * (0.508 * 72 / 25.4) = pu * 1.44.
Positions/sizes below are pulled from Looper.cpp / LooperPanelConst and
oldbridge.hpp draw helpers.
"""
import math
import os
import re

S = 0.508 * 72.0 / 25.4  # 1.44
KN0B = 22.67578  # OldBridgeRoundSmallBlackKnob SVG width
JACK = 23.7  # PJ301M port dia
BTN = 15.36  # TL1105 dia
TRIMPOT = 17.85552
LIGHT = 3.0
SCREW = 15.0
OUTRECT = 20.0 * S  # 20pu drawOutRect
OUTR = 2.0 * S  # drawOutRect rounded radius 2pu
TRACKBOX_R = 2.0 * S  # drawRoundRect rounded radius 2pu
DISPLAY_R = 2.0  # fixed px radius (nvgRoundedRect(..., 2))
FG = "#ffffff"
BG = "#777777"
DARK = "#101010"

panel_w = round(358 * S / 15.0) * 15  # 510
panel_h = 380.0
main_right = 78.0 * S  # 112.32


def px(v):
    return v * S


def slugify(s):
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")


def begin(g, name):
    g.append(f'<g class="widget" id="{slugify(name)}" name="{name}" '
             f'inkscape:label="{name}">')


def end(g):
    g.append('</g>')


def centered_text(g, x, y, s, size_pu, bold=False, fill=FG):
    wght = 700 if bold else 400
    g.append(f'<text x="{x:.3f}" y="{y:.3f}" font-family="DejaVu Sans, sans-serif" '
             f'font-size="{px(size_pu):.3f}" font-weight="{wght}" fill="{fill}" '
             f'text-anchor="middle" dominant-baseline="central">{s}</text>')


def knob(g, name, cx, cy, label=None, label_y=None):
    if name:
        begin(g, name)
    g.append(f'<circle cx="{cx:.3f}" cy="{cy:.3f}" r="{KN0B/2:.3f}" fill="#222" '
             f'stroke="{FG}" stroke-width="1.2"/>')
    r_g = 9.0 * S
    g.append(f'<circle cx="{cx:.3f}" cy="{cy:.3f}" r="{r_g:.3f}" fill="none" '
             f'stroke="{FG}" stroke-width="1.2"/>')
    g.append(f'<line x1="{cx:.3f}" y1="{cy:.3f}" x2="{cx:.3f}" y2="{cy - r_g - 2*S:.3f}" '
             f'stroke="{FG}" stroke-width="1.4"/>')
    if label is not None:
        centered_text(g, cx, label_y, label, 5.5)
    if name:
        end(g)


def outrect(g, name, cx, cy, label=None, fill=False):
    if name:
        begin(g, name)
    hw = OUTRECT / 2
    g.append(f'<rect x="{cx-hw:.3f}" y="{cy-hw:.3f}" width="{OUTRECT:.3f}" '
             f'height="{OUTRECT:.3f}" rx="{OUTR:.3f}" '
             f'fill="{"#333" if fill else "none"}" stroke="{FG}" stroke-width="0.8"/>')
    if label is not None:
        centered_text(g, cx, cy - px(13.5), label, 5.5)
    if name:
        end(g)


def jack(g, name, cx, cy, label=None):
    if name:
        begin(g, name)
    r = JACK / 2
    g.append(f'<circle cx="{cx:.3f}" cy="{cy:.3f}" r="{r:.3f}" fill="#111" '
             f'stroke="{FG}" stroke-width="1.4"/>')
    g.append(f'<circle cx="{cx:.3f}" cy="{cy:.3f}" r="{r - 7:.3f}" fill="none" '
             f'stroke="{FG}" stroke-width="1"/>')
    if label is not None:
        centered_text(g, cx, cy - px(13.5), label, 5.5)
    if name:
        end(g)


def track_display(g, name, bx, by, bw, bh, mode="STOP", mode_color="#c8c8c8",
                  left1="AUDIO 48k", right1="1", status=None):
    if name:
        begin(g, name)
    g.append(f'<rect x="{bx:.3f}" y="{by:.3f}" width="{bw:.3f}" height="{bh:.3f}" '
             f'rx="{DISPLAY_R:.3f}" fill="{DARK}" stroke="{FG}" stroke-width="0.5"/>')
    lx = bx + px(1)
    rx = bx + bw - px(1)
    fs = px(6.3)
    y1 = by + px(7)
    y2 = by + px(14)
    y3 = by + px(21)
    for txt, x, y, fill in [
        (left1, lx, y1, FG),
        (right1, rx, y1, FG),
        (mode, lx, y2, mode_color),
        ("SND", rx, y2, FG),
    ]:
        ta = "end" if x == rx else "start"
        g.append(f'<text x="{x:.3f}" y="{y:.3f}" font-family="DejaVu Sans, sans-serif" '
                 f'font-size="{fs:.3f}" fill="{fill}" text-anchor="{ta}" '
                 f'dominant-baseline="central">{txt}</text>')
    if status:
        g.append(f'<text x="{bx + bw/2:.3f}" y="{y3:.3f}" font-family="DejaVu Sans, sans-serif" '
                 f'font-size="{fs:.3f}" fill="#ffc850" text-anchor="middle" '
                 f'dominant-baseline="central">{status}</text>')
    bar_y = by + px(25)
    bar_h = px(3)
    g.append(f'<rect x="{bx + px(1):.3f}" y="{bar_y:.3f}" width="{bw - px(2):.3f}" '
             f'height="{bar_h:.3f}" fill="none" stroke="{FG}" stroke-width="0.3"/>')
    g.append(f'<rect x="{bx + px(1):.3f}" y="{bar_y:.3f}" width="{0.5 * (bw - px(2)):.3f}" '
             f'height="{bar_h:.3f}" fill="#00ff00"/>')
    if name:
        end(g)


def button(g, name, cx, cy, label=None, label_y=None):
    if name:
        begin(g, name)
    hw = BTN / 2
    g.append(f'<rect x="{cx-hw:.3f}" y="{cy-hw:.3f}" width="{BTN:.3f}" '
             f'height="{BTN:.3f}" rx="2.5" fill="#111" stroke="{FG}" stroke-width="1"/>')
    if label is not None:
        centered_text(g, cx, label_y, label, 2.2)
    if name:
        end(g)


def trimpot(g, name, cx, cy, label=None, label_y=None, label_size=5.5):
    if name:
        begin(g, name)
    hw = TRIMPOT / 2
    g.append(f'<circle cx="{cx:.3f}" cy="{cy:.3f}" r="{hw:.3f}" fill="#222" '
             f'stroke="{FG}" stroke-width="1"/>')
    g.append(f'<line x1="{cx:.3f}" y1="{cy:.3f}" x2="{cx:.3f}" y2="{cy-hw+2:.3f}" '
             f'stroke="{FG}" stroke-width="1.2"/>')
    if label is not None:
        centered_text(g, cx, label_y, label, label_size)
    if name:
        end(g)


def light(g, name, cx, cy, color="#ff0000"):
    if name:
        begin(g, name)
    r = LIGHT / 2
    g.append(f'<circle cx="{cx:.3f}" cy="{cy:.3f}" r="{r:.3f}" fill="{color}"/>')
    if name:
        end(g)


def push_button(g, name, cx, cy, w, h, label=None, icon=None, fill="#111"):
    if name:
        begin(g, name)
    bx = cx - px(w) / 2
    by = cy - px(h) / 2
    r = 1.5 * S
    bw = px(w)
    bh = px(h)
    g.append(f'<rect x="{bx:.3f}" y="{by:.3f}" width="{bw:.3f}" height="{bh:.3f}" '
             f'rx="{r:.3f}" fill="#000"/>')
    inpx = 0.5
    ow = bw - 2 * inpx
    g.append(f'<rect x="{bx + inpx:.3f}" y="{by + inpx:.3f}" width="{ow:.3f}" '
             f'height="{bh - 2 * inpx:.3f}" rx="{r * ow / bw:.3f}" fill="url(#btn_{fill[1:]}_cap)"/>')
    innerin = 1.0
    cw = bw - 2 * innerin
    g.append(f'<rect x="{bx + innerin:.3f}" y="{by + innerin:.3f}" width="{cw:.3f}" '
             f'height="{bh - 2 * innerin:.3f}" rx="{r * cw / bw:.3f}" fill="url(#btn_{fill[1:]}_inner)"/>')
    if label:
        centered_text(g, cx, by + px(h) / 2, label, 3.0)
    elif icon:
        design_w = 16.0 if icon in ("playrec", "play") else 10.0
        s = S * w / design_w
        ink = "#222" if fill != "#111" else FG
        ox = 0.0
        gx = bx + ox
        gy = by
        if icon == "mute":
            g.append(f'<rect x="{gx + 2.7033 * s:.3f}" y="{gy + 3.8993 * s:.3f}" '
                     f'width="{1.2933 * s:.3f}" height="{2.6583 * s:.3f}" '
                     f'rx="{0.3813 * s:.3f}" fill="{ink}"/>')
            g.append(f'<polygon points="{gx + 4.1373 * s:.3f},{gy + 4.0436 * s:.3f} '
                     f'{gx + 7.3627 * s:.3f},{gy + 3.3238 * s:.3f} '
                     f'{gx + 7.3627 * s:.3f},{gy + 7.1053 * s:.3f} '
                     f'{gx + 4.1373 * s:.3f},{gy + 6.3854 * s:.3f}" fill="{ink}"/>')
            g.append(f'<line x1="{gx + 4.2885 * s:.3f}" y1="{gy + 2.6729 * s:.3f}" '
                     f'x2="{gx + 7.2115 * s:.3f}" y2="{gy + 7.7562 * s:.3f}" '
                     f'stroke="{ink}" stroke-width="{0.6993 * s:.3f}" '
                     f'stroke-linecap="round"/>')
        elif icon == "playrec":
            g.append(f'<polygon points="{bx + 2.3144 * s:.3f},{by + 10.0182 * s:.3f} '
                     f'{bx + 6.8256 * s:.3f},{by + 8.0 * s:.3f} '
                     f'{bx + 2.3144 * s:.3f},{by + 5.9818 * s:.3f}" fill="{ink}"/>')
            g.append(f'<line x1="{bx + 9.0259 * s:.3f}" y1="{by + 6.185 * s:.3f}" '
                     f'x2="{bx + 6.9741 * s:.3f}" y2="{by + 9.815 * s:.3f}" '
                     f'stroke="{ink}" stroke-width="{0.5129 * s:.3f}" '
                     f'stroke-linecap="round"/>')
            g.append(f'<circle cx="{bx + 11.4886 * s:.3f}" cy="{by + 8.0 * s:.3f}" '
                     f'r="{2.0773 * s:.3f}" fill="{ink}"/>')
        elif icon == "play":
            g.append(f'<polygon points="{bx + 4.5 * s:.3f},{by + 3.5 * s:.3f} '
                     f'{bx + 12.5 * s:.3f},{by + 8.0 * s:.3f} '
                     f'{bx + 4.5 * s:.3f},{by + 12.5 * s:.3f}" fill="{ink}"/>')
        elif icon == "stop":
            g.append(f'<rect x="{gx + 3.25 * s:.3f}" y="{gy + 3.25 * s:.3f}" '
                     f'width="{3.5 * s:.3f}" height="{3.5 * s:.3f}" fill="{ink}"/>')
    if name:
        end(g)


lines = []
W = lines.append

W('<?xml version="1.0" encoding="UTF-8"?>')
W(f'<svg xmlns="http://www.w3.org/2000/svg" '
  f'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
  f'width="{panel_w:.2f}" height="{panel_h:.2f}" '
  f'viewBox="0 0 {panel_w:.2f} {panel_h:.2f}">')
W(f'  <rect width="{panel_w:.2f}" height="{panel_h:.2f}" fill="{BG}"/>')

W('  <defs>')
for gid, c0, c1 in (("btn_111_cap", "#807C7E", "#0A0A0A"),
                    ("btn_111_inner", "#4A4747", "#1F1F1F"),
                    ("btn_ff2020_cap", "#ff8d8d", "#ff2929"),
                    ("btn_ff2020_inner", "#ff6262", "#ff2727")):
    W(f'    <linearGradient id="{gid}" x1="0" y1="0" x2="0" y2="1">'
      f'<stop offset="0" stop-color="{c0}"/><stop offset="1" stop-color="{c1}"/></linearGradient>')
W('  </defs>')

# screws
for sx, sy in [(SCREW, 0), (panel_w - 2 * SCREW, 0), (SCREW, panel_h - SCREW),
               (panel_w - 2 * SCREW, panel_h - SCREW)]:
    W(f'  <g class="screw"><circle cx="{sx+SCREW/2:.2f}" cy="{sy+SCREW/2:.2f}" r="{SCREW/2-1:.2f}" '
      f'fill="#555" stroke="{FG}" stroke-width="1"/></g>')

# main panel divider
W(f'  <line x1="{main_right:.3f}" y1="0" x2="{main_right:.3f}" y2="{panel_h:.2f}" '
  f'stroke="{FG}" stroke-width="1" stroke-dasharray="6 5"/>')

# ---- MAIN PANEL ----
W('  <g id="main-panel">')
W('    <rect x="0" y="0" width="{:.3f}" height="{:.2f}" fill="none" stroke="#aaa" stroke-width="1" />'.format(main_right, panel_h))
mx = px(39)
my = px(14)
centered_text(lines, mx, my, "LOOPER ×5", 12, True)
g = []
knob(g, "BPM knob", px(16.8333), px(44), "BPM", px(31.5))
knob(g, "Count knob", px(41.7222), px(44), "COUNT", px(31.5))
push_button(g, "Master mute", px(63), px(44), 10, 10, icon="mute", fill="#ff2020")
lines.extend("  " + x for x in g)
disp = f'    <g class="widget" id="main_display" name="Main display" inkscape:label="Main display">' \
       f'<rect x="{px(6):.3f}" y="{px(59.1667):.3f}" width="{px(66):.3f}" ' \
       f'height="{px(33):.3f}" rx="{DISPLAY_R:.3f}" fill="{DARK}" stroke="{FG}" stroke-width="0.5"/>' \
       f'<text x="{px(6)+px(66)/2:.3f}" y="{px(59.1667)+px(33)/2:.3f}" font-family="DejaVu Sans, monospace" ' \
       f'font-size="7" fill="{FG}" text-anchor="middle" dominant-baseline="central">DISPLAY</text></g>'
W(disp)
W(f'    <g class="widget" id="input_jacks_frame" name="Input jacks frame" inkscape:label="Input jacks frame">')
W(f'      <rect x="{px(3):.3f}" y="{px(135):.3f}" width="{px(72):.3f}" height="{px(30):.3f}" '
  f'rx="{2*S:.3f}" fill="none" stroke="{FG}" stroke-width="0.8"/>')
W('    </g>')
W(f'    <g class="widget" id="audio_jacks_frame" name="Audio jacks frame" inkscape:label="Audio jacks frame">')
W(f'      <rect x="{px(3):.3f}" y="{px(167):.3f}" width="{px(72):.3f}" height="{px(30):.3f}" '
  f'rx="{2*S:.3f}" fill="none" stroke="{FG}" stroke-width="0.8"/>')
W('    </g>')

g = []
jack(g, "Gate in", px(14.25), px(154.2222), "GATE")
jack(g, "CV in", px(30.75), px(154.2222), "CV")
jack(g, "CV2 in", px(47.25), px(154.2222), "CV2")
jack(g, "Vel in", px(63.75), px(154.2222), "VEL")
jack(g, "Clock in", px(39), px(212.2222), "CLOCK")
push_button(g, "Start/stop button", px(62), px(110.06), 20, 20, icon="play")
push_button(g, "Stop all button", px(34.016), px(115.06), 10, 10, icon="stop")
jack(g, "Audio L in", px(14.25), px(186.2222), "L/MONO")
jack(g, "Audio R in", px(30.75), px(186.2222), "R")
push_button(g, "Reset button", px(11), px(115.06), 10, 10, label="RESET")
knob(g, "In gain knob", px(63.75), px(186.2222), "GAIN", px(173.7222))
jack(g, "Start/stop in", px(39), px(238.2222), "START")
jack(g, "Reset in", px(57), px(238.2222), "RESET")
lines.extend("  " + x for x in g)
W('  </g>')

# ---- TOP OUTPUT ROW ----
W('  <g id="output-row">')
ry = px(28)
for cxl, name, lab in [(152, "Gate out", "GATE"), (180, "CV out", "CV"),
                       (208, "CV2 out", "CV2"), (236, "Vel out", "VEL"),
                       (320, "Audio L out", "L"), (348, "Audio R out", "R")]:
    begin(lines, name)
    outrect(lines, None, px(cxl), ry)
    jack(lines, None, px(cxl), ry, lab)
    end(lines)
knob(lines, "Pan knob", px(264), ry, "PAN", px(15.5))
knob(lines, "Gain knob", px(292), ry, "GAIN", px(15.5))
W('  </g>')

# ---- TRACK STRIPS ----
W('  <g id="track-strips">')
cx_pus = [106, 162, 218, 274, 330]
labels = ["TRACK 1", "TRACK 2", "TRACK 3", "TRACK 4", "TRACK 5"]
for i, (cx, lab) in enumerate(zip(cx_pus, labels)):
    g = []
    begin(g, f"Track {i + 1} strip")
    bx = px(cx - 28)
    by = px(40)
    bw = px(56)
    bh = px(40 + 300.364 / 1.44 - 40)
    g.append(f'<rect x="{bx:.3f}" y="{by:.3f}" width="{bw:.3f}" height="{bh:.3f}" '
             f'rx="{TRACKBOX_R:.3f}" fill="none" stroke="{FG}" stroke-width="0.8"/>')
    tsz = 4.0 * S
    tw = tsz * 0.62 * len(lab)
    plate_x = bx + (bw - (tw + 2 * S)) / 2
    g.append(f'<rect x="{plate_x:.3f}" y="{by:.3f}" width="{tw + 2*S:.3f}" height="{tsz+3:.3f}" '
             f'rx="{1*S:.3f}" fill="{FG}"/>')
    g.append(f'<text x="{bx + bw/2:.3f}" y="{by + (tsz+3)/2:.3f}" font-family="DejaVu Sans, sans-serif" '
             f'font-size="{tsz:.3f}" fill="{BG}" text-anchor="middle" dominant-baseline="central">{lab}</text>')

    cxp = px(cx)
    track_display(g, f"Track {i + 1} display", bx, px(62), px(56), px(28), mode_color="#c8c8c8",
                  left1="AUDIO 48k", right1="1")
    push_button(g, f"Track {i + 1} type button", cxp + px(-16.6), px(108), 10, 10, label="TYPE")
    push_button(g, f"Track {i + 1} mode button", cxp, px(108), 10, 10, label="MODE")
    begin(g, f"Track {i + 1} mute button")
    push_button(g, None, cxp + px(17.6), px(108), 10, 10, icon="mute", fill="#111")
    end(g)
    push_button(g, f"track{i + 1}_button_play_rec", cxp + px(12.7), px(144.3), 20, 20, icon="playrec")
    push_button(g, f"Track {i + 1} stop button", cxp + px(-17.6), px(149.2), 10, 10, icon="stop")
    push_button(g, f"Track {i + 1} clear button", cxp + px(-17.6), px(121.9), 10, 10, label="CLR")
    push_button(g, f"Track {i + 1} undo button", cxp, px(121.9), 10, 10, label="UNDO")
    push_button(g, f"Track {i + 1} redo button", cxp + px(17.6), px(121.9), 10, 10, label="REDO")

    trimpot(g, f"Track {i + 1} rate knob", cxp - px(18.7778), px(50.1667))
    centered_text(g, cxp - px(3.7208), px(50.5347), "RATE", 5.5)
    knob(g, f"Track {i + 1} mix knob", cxp - px(13.6111), px(174.8889))
    knob(g, f"Track {i + 1} pan knob", cxp + px(13.6111), px(174.8889))
    centered_text(g, cxp - px(13.6111), px(162.3889), "MIX", 5.5)
    centered_text(g, cxp + px(13.6111), px(162.3889), "PAN", 5.5)
    for dx, lab in [(-13.6111, "L"), (13.6111, "R")]:
        begin(g, f"Track {i + 1} {lab} out")
        outrect(g, None, cxp + px(dx), px(204.8889))
        jack(g, None, cxp + px(dx), px(204.8889))
        end(g)
    for dx, lab in [(-13.6111, "CV2"), (13.6111, "VEL")]:
        begin(g, f"Track {i + 1} {lab} out")
        outrect(g, None, cxp + px(dx), px(232.8889))
        jack(g, None, cxp + px(dx), px(232.8889))
        end(g)
    end(g)
    lines.extend("  " + x for x in g)
W('  </g>')

# legend
ly = panel_h - 14
W(f'  <g id="legend">')
W(f'    <text x="{panel_w-4:.0f}" y="{ly:.2f}" font-family="DejaVu Sans, sans-serif" font-size="9" '
  f'fill="#ddd" text-anchor="end">Looper panel blueprint — 1:1 @ 100% zoom (pu × {S:g}); '
  f'main panel = 0–78pu (0–{main_right:g}px)</text>')
W('  </g>')
W('</svg>')

out_dir = os.path.join(os.path.dirname(__file__), "..", "blueprint")
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, "looper-panel.svg")

# rotate 5 previous copies before overwriting
for i in range(4, 0, -1):
    old = f"{out_path}.old-{i}"
    new = f"{out_path}.old-{i + 1}"
    if os.path.exists(old):
        os.replace(old, new)
if os.path.exists(out_path):
    os.replace(out_path, f"{out_path}.old-1")

with open(out_path, "w") as f:
    f.write("\n".join(lines) + "\n")
print("wrote", os.path.abspath(out_path))
