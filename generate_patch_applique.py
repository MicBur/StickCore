# -*- coding: utf-8 -*-
"""
StickCore - Feuerwehr Kiel Patch & Appliqué Generator
Generiert die perfekten Stickdateien (.jef) für Janome MC350E Rahmen A (<= 126x110 mm):
1. Applique-Verfahren:
   - Vorstich / Außenlinie mit kleiner Stichweite (1.6 mm) zum Positionieren
   - Farbstopp (0x80 0x01): Maschine stoppt, damit der Benutzer das Material auflegen kann
   - Heftstich mit kleiner Stichweite (1.6 mm) zum Fixieren
   - Farbstopp (0x80 0x01): Maschine stoppt zum Beischneiden
   - Kettelrand (2.2 mm breiter dichter Satinstich), der genau über die Außenlinie gestickt wird
2. Schienensprossen (90° transverse Säulenstiche wie Eisenbahnschwellen)
3. Varsity-Schrift in Bögen mit kleiner Stichweite (Bean-Stitch 1.6 mm) und Micro-Kettelrand
"""

import os
import math
import struct
import datetime
from PIL import Image, ImageDraw
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import RecordingPen

# Flags
SF_NORMAL = 0
SF_COLOR_CHANGE = 1
SF_JUMP = 2
SF_END = 4

# Janome Thread Palette Codes
JANOME_BLACK = 1
JANOME_RED = 2
JANOME_DEEP_RED = 13
JANOME_WHITE = 15
JANOME_BLUE = 7
JANOME_SILVER = 10
JANOME_GOLD = 5

def poly_length(pts):
    return sum(math.hypot(pts[i+1][0] - pts[i][0], pts[i+1][1] - pts[i][1]) for i in range(len(pts) - 1))

def sample_poly(pts, s):
    total_L = poly_length(pts)
    target = s * total_L
    acc = 0.0
    for i in range(len(pts) - 1):
        d = math.hypot(pts[i+1][0] - pts[i][0], pts[i+1][1] - pts[i][1])
        if acc + d >= target or i == len(pts) - 2:
            rem = (target - acc) / max(1e-6, d)
            rem = max(0.0, min(1.0, rem))
            return (pts[i][0] + (pts[i+1][0] - pts[i][0]) * rem,
                    pts[i][1] + (pts[i+1][1] - pts[i][1]) * rem)
        acc += d
    return pts[-1]

def make_run_stitch(pts, stitch_len_mm=1.6, color_idx=0):
    """Laufstich / Vorstich mit kleiner Stichweite (z.B. 1.6 mm)."""
    stitches = []
    if len(pts) < 2:
        return stitches
    L = poly_length(pts)
    steps = max(2, int(round(L / stitch_len_mm)))
    for i in range(steps + 1):
        s = i / steps
        p = sample_poly(pts, s)
        flag = SF_JUMP if i == 0 else SF_NORMAL
        stitches.append((p[0], p[1], flag, color_idx))
    return stitches

def make_bean_stitch(pts, stitch_len_mm=1.6, color_idx=0):
    """3-Pass Bean Stitch (Dreifach-Kontur mit kleiner Stichweite 1.6 mm)."""
    stitches = []
    if len(pts) < 2:
        return stitches
    L = poly_length(pts)
    steps = max(2, int(round(L / stitch_len_mm)))
    samples = [sample_poly(pts, i / steps) for i in range(steps + 1)]
    stitches.append((samples[0][0], samples[0][1], SF_JUMP, color_idx))
    for i in range(steps):
        p_next = samples[i+1]
        p_curr = samples[i]
        stitches.append((p_next[0], p_next[1], SF_NORMAL, color_idx)) # Vorwärts
        stitches.append((p_curr[0], p_curr[1], SF_NORMAL, color_idx)) # Rückwärts
        stitches.append((p_next[0], p_next[1], SF_NORMAL, color_idx)) # Vorwärts
    return stitches

def make_kettel_outline(pts, width_mm=2.2, pitch_mm=0.38, color_idx=0):
    """Satin-Kettelrand (Bourdonstich) mittig über eine geschlossene Kontur."""
    stitches = []
    if len(pts) < 2:
        return stitches
    L = poly_length(pts)
    steps = max(4, int(round(L / pitch_mm)))
    outer = True
    for i in range(steps + 1):
        s = i / steps
        p0 = sample_poly(pts, max(0.0, s - 0.005))
        p1 = sample_poly(pts, min(1.0, s + 0.005))
        dx = p1[0] - p0[0]
        dy = p1[1] - p0[1]
        dlen = math.hypot(dx, dy)
        nx, ny = 0.0, 0.0
        if dlen > 1e-6:
            nx = -dy / dlen
            ny = dx / dlen
        p = sample_poly(pts, s)
        sign = (width_mm * 0.5) if outer else (-width_mm * 0.5)
        st_x = p[0] + nx * sign
        st_y = p[1] + ny * sign
        flag = SF_JUMP if i == 0 else SF_NORMAL
        stitches.append((st_x, st_y, flag, color_idx))
        outer = not outer
    return stitches

def make_satin_column(rail_out, rail_in, pitch_mm=0.45, color_idx=0):
    """Schienensprossen (transverse 90°-Säulenstiche zwischen zwei Schienen)."""
    stitches = []
    l_avg = 0.5 * (poly_length(rail_out) + poly_length(rail_in))
    steps = max(2, int(round(l_avg / pitch_mm)))
    outer = True
    for i in range(steps + 1):
        s = i / steps
        p_out = sample_poly(rail_out, s)
        p_in  = sample_poly(rail_in, s)
        pt = p_out if outer else p_in
        flag = SF_JUMP if i == 0 else SF_NORMAL
        stitches.append((pt[0], pt[1], flag, color_idx))
        outer = not outer
    return stitches

# --- Geometrien für Ziffer 30 ---
def get_varsity_zero_rails(W=54.0, H=76.0, w=11.0, c=12.0):
    hw, hh = W * 0.5, H * 0.5
    out_pts = [
        (-hw + c,  hh), ( hw - c,  hh), ( hw,  hh - c), ( hw, -hh + c),
        ( hw - c, -hh), (-hw + c, -hh), (-hw, -hh + c), (-hw,  hh - c), (-hw + c,  hh)
    ]
    inW, inH = W - 2*w, H - 2*w
    inC = max(2.0, c - w)
    ihw, ihh = inW * 0.5, inH * 0.5
    in_pts = [
        (-ihw + inC,  ihh), ( ihw - inC,  ihh), ( ihw,  ihh - inC), ( ihw, -ihh + inC),
        ( ihw - inC, -ihh), (-ihw + inC, -ihh), (-ihw, -ihh + inC), (-ihw,  ihh - inC), (-ihw + inC,  ihh)
    ]
    return out_pts, in_pts

def get_varsity_three_rails(W=50.0, H=76.0, w=11.0, c=12.0):
    hw, hh = W * 0.5, H * 0.5
    rail_out = [
        (-hw,        hh - w - 8.0),
        (-hw,        hh),
        ( hw - c,    hh),
        ( hw,        hh - c),
        ( hw,        8.0),
        ( 6.0,       0.0),
        (-10.0,      0.0),
        ( 6.0,       0.0),
        ( hw,       -8.0),
        ( hw,       -hh + c),
        ( hw - c,   -hh),
        (-hw,       -hh),
        (-hw,       -hh + w + 8.0)
    ]
    rail_in = [
        (-hw + w,    hh - w - 8.0),
        (-hw + w,    hh - w),
        ( hw - w - (c - w), hh - w),
        ( hw - w,    hh - w - (c - w)),
        ( hw - w,    16.0),
        ( 6.0 - w,   0.0),
        (-10.0,      0.0),
        ( 6.0 - w,   0.0),
        ( hw - w,   -16.0),
        ( hw - w,   -hh + w + (c - w)),
        ( hw - w - (c - w), -hh + w),
        (-hw + w,   -hh + w),
        (-hw + w,   -hh + w + 8.0)
    ]
    return rail_out, rail_in

def get_varsity_three_closed(W=50.0, H=76.0, w=11.0, c=12.0):
    r_out, r_in = get_varsity_three_rails(W, H, w, c)
    closed = list(r_out)
    for p in reversed(r_in):
        closed.append(p)
    closed.append(r_out[0])
    return closed

# --- JEF Binary Encoder ---
def write_jef(path, stitches, palette_codes, hoop_w=124.0, hoop_h=108.0):
    if not stitches:
        return False
    xs = [s[0] for s in stitches]
    ys = [s[1] for s in stitches]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    w = max_x - min_x
    h = max_y - min_y

    scale = 1.0
    if w > hoop_w or h > hoop_h:
        scale = min(hoop_w / max(1e-6, w), hoop_h / max(1e-6, h))

    cx = 0.5 * (min_x + max_x)
    cy = 0.5 * (min_y + max_y)

    scaled_stitches = []
    for (x, y, flags, cidx) in stitches:
        sx = (x - cx) * scale
        sy = (y - cy) * scale
        scaled_stitches.append((sx, sy, flags, cidx))

    color_count = max(1, len(palette_codes))
    k_fixed_header = 0x74
    k_version = 0x14
    k_max_delta = 127
    k_byte16 = 0x64
    k_color_attrib = 0x0D

    body = bytearray()
    count_field = 0
    last_x, last_y = 0, 0
    have_last = False
    u_min_x, u_max_x, u_min_y, u_max_y = 0, 0, 0, 0
    have_ext = False

    def encode_move(dx_u, dy_u, ctrl):
        nonlocal count_field
        while abs(dx_u) > k_max_delta or abs(dy_u) > k_max_delta:
            sx = max(-k_max_delta, min(k_max_delta, dx_u))
            sy = max(-k_max_delta, min(k_max_delta, dy_u))
            body.extend(bytes([0x80, 0x02, sx & 0xFF, sy & 0xFF]))
            dx_u -= sx
            dy_u -= sy
            count_field += 2
        if ctrl != 0:
            body.extend(bytes([0x80, ctrl, dx_u & 0xFF, dy_u & 0xFF]))
            count_field += 2
        else:
            body.extend(bytes([dx_u & 0xFF, dy_u & 0xFF]))
            count_field += 1

    for (sx, sy, flags, cidx) in scaled_stitches:
        ux = int(round(sx * 10.0))
        uy = -int(round(sy * 10.0)) # JEF Y is flipped
        if not have_ext:
            u_min_x, u_max_x, u_min_y, u_max_y = ux, ux, uy, uy
            have_ext = True
        else:
            u_min_x = min(u_min_x, ux)
            u_max_x = max(u_max_x, ux)
            u_min_y = min(u_min_y, uy)
            u_max_y = max(u_max_y, uy)

        dx = ux - last_x if have_last else ux
        dy = uy - last_y if have_last else uy

        ctrl = 0
        if flags & SF_COLOR_CHANGE:
            ctrl = 0x01
        elif flags & SF_JUMP:
            ctrl = 0x02

        encode_move(dx, dy, ctrl)
        last_x, last_y = ux, uy
        have_last = True

    body.extend(bytes([0x80, 0x10, 0x00, 0x00]))
    count_field += 1

    header = bytearray()
    header.extend(struct.pack('<i', k_fixed_header + color_count * 8)) # 0x00
    header.extend(struct.pack('<i', k_version))                         # 0x04
    date_str = datetime.datetime.now().strftime("%Y%m%d%H%M%S").encode('ascii').ljust(14, b'0')[:14]
    header.extend(date_str)                                            # 0x08
    header.extend(bytes([k_byte16, 0x00]))                             # 0x16
    header.extend(struct.pack('<i', color_count))                      # 0x18
    header.extend(struct.pack('<i', count_field))                      # 0x1C
    header.extend(struct.pack('<i', 0))                                # 0x20 Hoop A = 0
    header.extend(struct.pack('<i', -u_min_x))                         # 0x24 distLeft
    header.extend(struct.pack('<i',  u_max_y))                         # 0x28 distTop
    header.extend(struct.pack('<i',  u_max_x))                         # 0x2C distRight
    header.extend(struct.pack('<i', -u_min_y))                         # 0x30 distBottom
    for _ in range(16):
        header.extend(struct.pack('<i', -1))                           # 0x34..0x74
    for code in palette_codes:
        header.extend(struct.pack('<i', code))
    for _ in palette_codes:
        header.extend(struct.pack('<i', k_color_attrib))

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(header)
        f.write(body)
    return True

# --- Render-Funktion für schöne realistische Vorschaubilder ---
def render_preview(stitches, palette_rgbs, out_png, size=(800, 800)):
    if not stitches:
        return
    xs = [s[0] for s in stitches]
    ys = [s[1] for s in stitches]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    w = max(1e-4, max_x - min_x)
    h = max(1e-4, max_y - min_y)

    margin = 50
    scale = min((size[0] - 2 * margin) / w, (size[1] - 2 * margin) / h)
    cx = 0.5 * (min_x + max_x)
    cy = 0.5 * (min_y + max_y)

    img = Image.new('RGB', size, (24, 26, 32)) # Elegantes dunkles Carbon/Gewebe
    draw = ImageDraw.Draw(img)

    # Subtiler Rahmen für Hoop A
    hw_px = int(126.0 * scale * 0.5)
    hh_px = int(110.0 * scale * 0.5)
    ox, oy = size[0] // 2, size[1] // 2
    draw.rectangle([ox - hw_px, oy - hh_px, ox + hw_px, oy + hh_px], outline=(45, 50, 60), width=1)

    for i in range(len(stitches) - 1):
        s0 = stitches[i]
        s1 = stitches[i+1]
        if (s1[2] & SF_JUMP) or (s1[2] & SF_COLOR_CHANGE) or math.hypot(s1[0] - s0[0], s1[1] - s0[1]) > 5.0:
            continue
        c_idx = s1[3] % len(palette_rgbs)
        color = palette_rgbs[c_idx]
        x1 = ox + (s0[0] - cx) * scale
        y1 = oy - (s0[1] - cy) * scale # +Y up to screen down
        x2 = ox + (s1[0] - cx) * scale
        y2 = oy - (s1[1] - cy) * scale
        draw.line([(x1, y1), (x2, y2)], fill=color, width=2)

    os.makedirs(os.path.dirname(os.path.abspath(out_png)), exist_ok=True)
    img.save(out_png)

# --- Erstellung der gesamten Feuerwehr-Suite ---
def build_all():
    usb_dir = "F:/EmbF5/Feuerwehr"
    brain_dir = "C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c"
    os.makedirs(usb_dir, exist_ok=True)

    print("=== Starte Generierung der Feuerwehr-Stickdateien mit Appliqué & Kettelung ===")

    # -------------------------------------------------------------
    # 1. MITTE: Ziffer 30 als Applique-Patch
    # -------------------------------------------------------------
    out3_closed = get_varsity_three_closed(50.0, 76.0, 11.0, 12.0)
    out0, in0 = get_varsity_zero_rails(54.0, 76.0, 11.0, 12.0)
    r_out3, r_in3 = get_varsity_three_rails(50.0, 76.0, 11.0, 12.0)

    # Shift-Koordinaten: 3 nach -30 mm, 0 nach +30 mm
    shift3 = -30.0
    shift0 =  30.0

    poly3_shifted = [(p[0] + shift3, p[1]) for p in out3_closed]
    poly0_out_shifted = [(p[0] + shift0, p[1]) for p in out0]
    poly0_in_shifted  = [(p[0] + shift0, p[1]) for p in in0]

    # STUFE 1: Vorstich / Placement Line (Kleine Stichweite 1.6 mm)
    # Markiert die Außenlinie auf dem Trägervlies, damit das Material aufgelegt werden kann!
    st_placement = []
    st_placement.extend(make_run_stitch(poly3_shifted, 1.6, color_idx=0))
    st_placement.extend(make_run_stitch(poly0_out_shifted, 1.6, color_idx=0))
    st_placement.extend(make_run_stitch(poly0_in_shifted, 1.6, color_idx=0))

    # STUFE 2: Heftstich / Tackdown Line (Kleine Stichweite 1.6 mm)
    # Heftet das aufgelegte Patch-Material fest! Vorher Farbstopp.
    st_tackdown = []
    st_tackdown.extend(make_run_stitch(poly3_shifted, 1.6, color_idx=1))
    st_tackdown.extend(make_run_stitch(poly0_out_shifted, 1.6, color_idx=1))
    st_tackdown.extend(make_run_stitch(poly0_in_shifted, 1.6, color_idx=1))
    if st_tackdown:
        p0 = st_tackdown[0]
        st_tackdown[0] = (p0[0], p0[1], SF_COLOR_CHANGE | SF_JUMP, 1)

    # STUFE 3: Dichter Kettelrand (2.2 mm breiter Bourdon-Satinstich)
    # Stickt genau über die Außenlinie und versäubert den Rand!
    st_cover = []
    st_cover.extend(make_kettel_outline(poly3_shifted, 2.2, 0.38, color_idx=2))
    st_cover.extend(make_kettel_outline(poly0_out_shifted, 2.2, 0.38, color_idx=2))
    st_cover.extend(make_kettel_outline(poly0_in_shifted, 2.2, 0.38, color_idx=2))
    if st_cover:
        p0 = st_cover[0]
        st_cover[0] = (p0[0], p0[1], SF_COLOR_CHANGE | SF_JUMP, 2)

    # ZUSATZ: Schienensprossen (90° transverse Satin-Füllung)
    rail_out3_shifted = [(p[0] + shift3, p[1]) for p in r_out3]
    rail_in3_shifted  = [(p[0] + shift3, p[1]) for p in r_in3]
    st_sprossen = []
    st_sprossen.extend(make_satin_column(rail_out3_shifted, rail_in3_shifted, 0.45, color_idx=2))
    st_sprossen.extend(make_satin_column(poly0_out_shifted, poly0_in_shifted, 0.45, color_idx=2))
    if st_sprossen:
        p0 = st_sprossen[0]
        st_sprossen[0] = (p0[0], p0[1], SF_COLOR_CHANGE | SF_JUMP, 2)

    # Datei A: Reiner Appliqué-Patch (Vorstich -> Stopp -> Heftstich -> Stopp -> Kettelrand)
    seq30_applique = []
    seq30_applique.extend(st_placement)
    seq30_applique.extend(st_tackdown)
    seq30_applique.extend(st_cover)

    pal_applique = [JANOME_SILVER, JANOME_BLUE, JANOME_DEEP_RED]
    pal_rgb = [(180, 180, 190), (60, 130, 220), (210, 30, 40)]

    file_app30 = os.path.join(usb_dir, "02_MITTE_Zahl_30_Applique_Patch.jef")
    write_jef(file_app30, seq30_applique, pal_applique)
    render_preview(seq30_applique, pal_rgb, os.path.join(brain_dir, "feuerwehr_30_applique_patch.png"))
    print(f"  [OK] Geschrieben: {file_app30} ({len(seq30_applique)} Stiche)")

    # Datei B: Appliqué-Patch mit Schienensprossen (transverse Satin rungs)
    seq30_app_sprossen = []
    seq30_app_sprossen.extend(st_placement)
    seq30_app_sprossen.extend(st_tackdown)
    seq30_app_sprossen.extend(st_sprossen)
    seq30_app_sprossen.extend(st_cover)

    file_app30_sp = os.path.join(usb_dir, "02_MITTE_Zahl_30_Applique_mit_Sprossen.jef")
    write_jef(file_app30_sp, seq30_app_sprossen, pal_applique)
    render_preview(seq30_app_sprossen, pal_rgb, os.path.join(brain_dir, "feuerwehr_30_applique_sprossen.png"))
    print(f"  [OK] Geschrieben: {file_app30_sp} ({len(seq30_app_sprossen)} Stiche)")

    # Datei C: Reine Kettel-Outlines (ohne Vorstopps, nur rote Bourdon-Ränder)
    seq30_kettel_pure = []
    for s in st_cover:
        seq30_kettel_pure.append((s[0], s[1], SF_JUMP if s == st_cover[0] else (s[2] & ~SF_COLOR_CHANGE), 0))
    file_30_kettel = os.path.join(usb_dir, "02_MITTE_Zahl_30_Kettel_Outlines.jef")
    write_jef(file_30_kettel, seq30_kettel_pure, [JANOME_DEEP_RED])
    render_preview(seq30_kettel_pure, [(210, 30, 40)], os.path.join(brain_dir, "feuerwehr_30_kettel_outlines.png"))
    print(f"  [OK] Geschrieben: {file_30_kettel} ({len(seq30_kettel_pure)} Stiche)")

    # Datei D: Reine Schienensprossen (transverse 90° Satin rungs)
    seq30_sprossen_pure = []
    for s in st_sprossen:
        seq30_sprossen_pure.append((s[0], s[1], SF_JUMP if s == st_sprossen[0] else (s[2] & ~SF_COLOR_CHANGE), 0))
    file_30_sprossen = os.path.join(usb_dir, "02_MITTE_Zahl_30_Satin_Sprossen.jef")
    write_jef(file_30_sprossen, seq30_sprossen_pure, [JANOME_DEEP_RED])
    render_preview(seq30_sprossen_pure, [(210, 30, 40)], os.path.join(brain_dir, "feuerwehr_30_satin_sprossen.png"))
    print(f"  [OK] Geschrieben: {file_30_sprossen} ({len(seq30_sprossen_pure)} Stiche)")

    # -------------------------------------------------------------
    # 2. GROSSE EINZELZIFFERN (100 mm Höhe für solo Hoop A Rahmen)
    # -------------------------------------------------------------
    out3L_closed = get_varsity_three_closed(65.0, 100.0, 14.0, 15.0)
    out0L, in0L   = get_varsity_zero_rails(70.0, 100.0, 14.0, 15.0)

    # 3 Applique
    st3L_p = make_run_stitch(out3L_closed, 1.6, 0)
    st3L_t = make_run_stitch(out3L_closed, 1.6, 1)
    if st3L_t: st3L_t[0] = (st3L_t[0][0], st3L_t[0][1], SF_COLOR_CHANGE | SF_JUMP, 1)
    st3L_c = make_kettel_outline(out3L_closed, 2.4, 0.38, 2)
    if st3L_c: st3L_c[0] = (st3L_c[0][0], st3L_c[0][1], SF_COLOR_CHANGE | SF_JUMP, 2)
    seq3L_app = st3L_p + st3L_t + st3L_c
    file_3L = os.path.join(usb_dir, "02a_MITTE_Gross_Ziffer_3_Applique.jef")
    write_jef(file_3L, seq3L_app, pal_applique)
    print(f"  [OK] Geschrieben: {file_3L} ({len(seq3L_app)} Stiche)")

    # 0 Applique
    st0L_p = make_run_stitch(out0L, 1.6, 0) + make_run_stitch(in0L, 1.6, 0)
    st0L_t = make_run_stitch(out0L, 1.6, 1) + make_run_stitch(in0L, 1.6, 1)
    if st0L_t: st0L_t[0] = (st0L_t[0][0], st0L_t[0][1], SF_COLOR_CHANGE | SF_JUMP, 1)
    st0L_c = make_kettel_outline(out0L, 2.4, 0.38, 2) + make_kettel_outline(in0L, 2.4, 0.38, 2)
    if st0L_c: st0L_c[0] = (st0L_c[0][0], st0L_c[0][1], SF_COLOR_CHANGE | SF_JUMP, 2)
    seq0L_app = st0L_p + st0L_t + st0L_c
    file_0L = os.path.join(usb_dir, "02b_MITTE_Gross_Ziffer_0_Applique.jef")
    write_jef(file_0L, seq0L_app, pal_applique)
    print(f"  [OK] Geschrieben: {file_0L} ({len(seq0L_app)} Stiche)")

    # -------------------------------------------------------------
    # 3. TEXTBÖGEN IN ORIGINAL VARSITY-SCHRIFT
    # -------------------------------------------------------------
    font = TTFont('E:/StickCore/assets/fonts/varsity_regular.ttf')
    glyph_set = font.getGlyphSet()
    hmtx = font['hmtx']
    cmap = font.getBestCmap()

    def get_text_polys_on_arc(text, cap_h_mm, arc_r_mm, arc_down=False, letter_sp_mm=1.4):
        scale = cap_h_mm / 720.0
        sp_units = letter_sp_mm / scale

        items = []
        total_adv = 0.0
        for ch in text:
            if ch == ' ':
                adv = 450.0
                items.append((' ', [], adv))
                total_adv += adv + sp_units
                continue
            gname = cmap.get(ord(ch))
            if not gname or gname not in glyph_set:
                continue
            pen = RecordingPen()
            glyph_set[gname].draw(pen)
            conts = []
            curr = []
            for cmd, args in pen.value:
                if cmd == 'moveTo':
                    if curr: conts.append(curr)
                    curr = [args[0]]
                elif cmd == 'lineTo':
                    curr.append(args[0])
                elif cmd == 'closePath':
                    if curr:
                        if curr[0] != curr[-1]: curr.append(curr[0])
                        conts.append(curr)
                        curr = []
            if curr: conts.append(curr)
            adv = float(hmtx[gname][0])
            items.append((ch, conts, adv))
            total_adv += adv + sp_units
        if items:
            total_adv -= sp_units

        all_polys = []
        cursor_mm = - (total_adv * scale) * 0.5
        for ch, conts, adv in items:
            char_w_mm = adv * scale
            center_s_mm = cursor_mm + char_w_mm * 0.5
            cursor_mm += char_w_mm + letter_sp_mm
            if not conts:
                continue

            theta = center_s_mm / arc_r_mm
            if not arc_down: # ArcUp (convex)
                X = arc_r_mm * math.sin(theta)
                Y = arc_r_mm * math.cos(theta) - arc_r_mm
                rot = -theta
            else: # ArcDown (concave smile)
                X = arc_r_mm * math.sin(theta)
                Y = arc_r_mm - arc_r_mm * math.cos(theta)
                rot = theta

            cos_r = math.cos(rot)
            sin_r = math.sin(rot)
            glyph_center_x = adv * 0.5

            for poly in conts:
                mapped = []
                for (px, py) in poly:
                    gx = (px - glyph_center_x) * scale
                    gy = py * scale
                    rx = gx * cos_r - gy * sin_r + X
                    ry = gx * sin_r + gy * cos_r + Y
                    mapped.append((rx, ry))
                all_polys.append(mapped)
        return all_polys

    def get_text_polys_straight(text, cap_h_mm, letter_sp_mm=2.0):
        scale = cap_h_mm / 720.0
        sp_units = letter_sp_mm / scale
        items = []
        total_adv = 0.0
        for ch in text:
            if ch == ' ':
                adv = 450.0
                items.append((' ', [], adv))
                total_adv += adv + sp_units
                continue
            gname = cmap.get(ord(ch))
            if not gname or gname not in glyph_set:
                continue
            pen = RecordingPen()
            glyph_set[gname].draw(pen)
            conts = []
            curr = []
            for cmd, args in pen.value:
                if cmd == 'moveTo':
                    if curr: conts.append(curr)
                    curr = [args[0]]
                elif cmd == 'lineTo':
                    curr.append(args[0])
                elif cmd == 'closePath':
                    if curr:
                        if curr[0] != curr[-1]: curr.append(curr[0])
                        conts.append(curr)
                        curr = []
            if curr: conts.append(curr)
            adv = float(hmtx[gname][0])
            items.append((ch, conts, adv))
            total_adv += adv + sp_units
        if items:
            total_adv -= sp_units

        all_polys = []
        cursor_mm = - (total_adv * scale) * 0.5
        for ch, conts, adv in items:
            char_w_mm = adv * scale
            cx_mm = cursor_mm + char_w_mm * 0.5
            cursor_mm += char_w_mm + letter_sp_mm
            glyph_center_x = adv * 0.5
            for poly in conts:
                mapped = []
                for (px, py) in poly:
                    gx = (px - glyph_center_x) * scale + cx_mm
                    gy = py * scale - (cap_h_mm * 0.5)
                    mapped.append((gx, gy))
                all_polys.append(mapped)
        return all_polys

    # A) OBEN: "berufsfeuerwehr kiel"
    polys_top = get_text_polys_on_arc("berufsfeuerwehr kiel", cap_h_mm=9.5, arc_r_mm=65.0, arc_down=False, letter_sp_mm=1.4)

    # 1. Kleine Stichweite: Bean Stitch (3-Pass Dreifach-Kontur 1.6 mm)
    seq_top_bean = []
    for poly in polys_top:
        seq_top_bean.extend(make_bean_stitch(poly, 1.6, color_idx=0))
    file_top_bean = os.path.join(usb_dir, "01_OBEN_Bogen_Berufsfeuerwehr_Kiel_KleineStichweite.jef")
    write_jef(file_top_bean, seq_top_bean, [JANOME_DEEP_RED])
    render_preview(seq_top_bean, [(210, 30, 40)], os.path.join(brain_dir, "feuerwehr_bogen_oben_beanstitch.png"))
    print(f"  [OK] Geschrieben: {file_top_bean} ({len(seq_top_bean)} Stiche)")

    # 2. Micro-Kettelrand (0.65 mm feiner Satinstich, 0.28 mm Pitch)
    seq_top_kettel = []
    for poly in polys_top:
        seq_top_kettel.extend(make_kettel_outline(poly, 0.65, 0.28, color_idx=0))
    file_top_kettel = os.path.join(usb_dir, "01_OBEN_Bogen_Berufsfeuerwehr_Kiel_MicroKettel.jef")
    write_jef(file_top_kettel, seq_top_kettel, [JANOME_DEEP_RED])
    file_top_main = os.path.join(usb_dir, "01_OBEN_Bogen_Berufsfeuerwehr_Kiel.jef")
    write_jef(file_top_main, seq_top_kettel, [JANOME_DEEP_RED])
    render_preview(seq_top_kettel, [(210, 30, 40)], os.path.join(brain_dir, "feuerwehr_bogen_oben_stitches.png"))
    print(f"  [OK] Geschrieben: {file_top_kettel} ({len(seq_top_kettel)} Stiche)")

    # 3. Geteilte Großversion (14 mm Schrifthöhe für volle Jackenbreite über 2 Rahmenspannungen)
    polys_top_large = get_text_polys_on_arc("berufsfeuerwehr kiel", cap_h_mm=14.0, arc_r_mm=85.0, arc_down=False, letter_sp_mm=2.0)
    seq_top_L1 = []
    seq_top_L2 = []
    # Passkreuz bei (0, 0)
    crosshair = [(-3.0, 0.0), (3.0, 0.0), (0.0, -3.0), (0.0, 3.0)]
    ch_st = [(p[0], p[1], SF_NORMAL, 0) for p in crosshair]
    ch_st[0] = (crosshair[0][0], crosshair[0][1], SF_JUMP, 0)

    seq_top_L1.extend(ch_st)
    seq_top_L2.extend(ch_st)
    for poly in polys_top_large:
        poly_cx = sum(p[0] for p in poly) / len(poly)
        if poly_cx <= 2.0:
            seq_top_L1.extend(make_kettel_outline(poly, 0.85, 0.30, 0))
        if poly_cx >= -2.0:
            seq_top_L2.extend(make_kettel_outline(poly, 0.85, 0.30, 0))

    file_top_l1 = os.path.join(usb_dir, "01a_OBEN_Gross_Teil1_Links.jef")
    file_top_l2 = os.path.join(usb_dir, "01b_OBEN_Gross_Teil2_Rechts.jef")
    write_jef(file_top_l1, seq_top_L1, [JANOME_DEEP_RED])
    write_jef(file_top_l2, seq_top_L2, [JANOME_DEEP_RED])
    print(f"  [OK] Geschrieben: {file_top_l1} & {file_top_l2}")

    # B) UNTEN: "nordwache ii"
    polys_bottom = get_text_polys_on_arc("nordwache ii", cap_h_mm=10.5, arc_r_mm=65.0, arc_down=True, letter_sp_mm=1.6)

    # 1. Kleine Stichweite: Bean Stitch (3-Pass Dreifach-Kontur 1.6 mm)
    seq_bottom_bean = []
    for poly in polys_bottom:
        seq_bottom_bean.extend(make_bean_stitch(poly, 1.6, color_idx=0))
    file_bot_bean = os.path.join(usb_dir, "03_UNTEN_Bogen_Nordwache_II_KleineStichweite.jef")
    write_jef(file_bot_bean, seq_bottom_bean, [JANOME_DEEP_RED])
    render_preview(seq_bottom_bean, [(210, 30, 40)], os.path.join(brain_dir, "feuerwehr_bogen_unten_beanstitch.png"))
    print(f"  [OK] Geschrieben: {file_bot_bean} ({len(seq_bottom_bean)} Stiche)")

    # 2. Micro-Kettelrand (0.65 mm feiner Satinstich, 0.28 mm Pitch)
    seq_bottom_kettel = []
    for poly in polys_bottom:
        seq_bottom_kettel.extend(make_kettel_outline(poly, 0.65, 0.28, color_idx=0))
    file_bot_kettel = os.path.join(usb_dir, "03_UNTEN_Bogen_Nordwache_II_MicroKettel.jef")
    write_jef(file_bot_kettel, seq_bottom_kettel, [JANOME_DEEP_RED])
    file_bot_main = os.path.join(usb_dir, "03_UNTEN_Bogen_Nordwache_II.jef")
    write_jef(file_bot_main, seq_bottom_kettel, [JANOME_DEEP_RED])
    render_preview(seq_bottom_kettel, [(210, 30, 40)], os.path.join(brain_dir, "feuerwehr_bogen_unten_stitches.png"))
    print(f"  [OK] Geschrieben: {file_bot_kettel} ({len(seq_bottom_kettel)} Stiche)")

    # -------------------------------------------------------------
    # 4. KIELER WAPPEN: Schild als Applique & Kettelrand
    # -------------------------------------------------------------
    w_shield = 72.0
    h_shield = 78.0
    hw = w_shield * 0.5
    hh = h_shield * 0.5
    shield_pts = [
        (-hw,  hh),
        ( hw,  hh),
        ( hw, -hh * 0.2),
        ( hw * 0.9, -hh * 0.55),
        ( hw * 0.55, -hh * 0.85),
        ( 0.0, -hh),
        (-hw * 0.55, -hh * 0.85),
        (-hw * 0.9, -hh * 0.55),
        (-hw, -hh * 0.2),
        (-hw,  hh)
    ]

    nw = 52.0
    nh = 56.0
    nhw = nw * 0.5
    nhh = nh * 0.5
    nessel_pts = [
        (-nhw * 0.9, nhh * 0.8), (-nhw * 0.5, nhh), (0.0, nhh * 0.8), (nhw * 0.5, nhh), (nhw * 0.9, nhh * 0.8),
        (nhw, nhh * 0.3), (nhw * 0.8, -nhh * 0.2), (nhw * 0.6, -nhh * 0.6), (0.0, -nhh * 0.85),
        (-nhw * 0.6, -nhh * 0.6), (-nhw * 0.8, -nhh * 0.2), (-nhw, nhh * 0.3), (-nhw * 0.9, nhh * 0.8)
    ]

    bw = 36.0
    bh = 14.0
    bhw = bw * 0.5
    bhh = bh * 0.5
    boot_pts = [
        (-bhw, bhh * 0.5), (-bhw * 0.7, -bhh), (bhw * 0.7, -bhh), (bhw, bhh * 0.5),
        (bhw * 0.6, bhh * 0.1), (-bhw * 0.6, bhh * 0.1), (-bhw, bhh * 0.5)
    ]

    # Stufe 1: Schild Vorstich (Placement 1.6 mm)
    wap_p = make_run_stitch(shield_pts, 1.6, color_idx=0)
    # Stufe 2: Schild Heftstich (Tackdown 1.6 mm)
    wap_t = make_run_stitch(shield_pts, 1.6, color_idx=1)
    if wap_t: wap_t[0] = (wap_t[0][0], wap_t[0][1], SF_COLOR_CHANGE | SF_JUMP, 1)
    # Stufe 3: Schild Kettelrand (2.2 mm Rot)
    wap_c = make_kettel_outline(shield_pts, 2.2, 0.38, color_idx=2)
    if wap_c: wap_c[0] = (wap_c[0][0], wap_c[0][1], SF_COLOR_CHANGE | SF_JUMP, 2)
    # Stufe 4: Nesselblatt (Weißer Kettelrand 1.6 mm)
    wap_n = make_kettel_outline(nessel_pts, 1.6, 0.38, color_idx=3)
    if wap_n: wap_n[0] = (wap_n[0][0], wap_n[0][1], SF_COLOR_CHANGE | SF_JUMP, 3)
    # Stufe 5: Kieler Boot (Schwarzer Kettelrand 1.6 mm)
    wap_b = make_kettel_outline(boot_pts, 1.6, 0.38, color_idx=4)
    if wap_b: wap_b[0] = (wap_b[0][0], wap_b[0][1], SF_COLOR_CHANGE | SF_JUMP, 4)

    seq_wappen_app = wap_p + wap_t + wap_c + wap_n + wap_b
    pal_wap = [JANOME_SILVER, JANOME_BLUE, JANOME_DEEP_RED, JANOME_WHITE, JANOME_BLACK]
    pal_wap_rgb = [(180, 180, 190), (60, 130, 220), (210, 30, 40), (245, 245, 245), (20, 20, 20)]

    file_wap_app = os.path.join(usb_dir, "04_Kieler_Wappen_Applique_Patch.jef")
    write_jef(file_wap_app, seq_wappen_app, pal_wap)
    render_preview(seq_wappen_app, pal_wap_rgb, os.path.join(brain_dir, "kieler_wappen_applique_patch.png"))
    print(f"  [OK] Geschrieben: {file_wap_app} ({len(seq_wappen_app)} Stiche)")

    # Reine Konturen-Version (ohne Vorstopps)
    seq_wap_kontur = []
    for s in (wap_c + wap_n + wap_b):
        c_code = s[3] - 2 # 0: Red, 1: White, 2: Black
        flag = SF_COLOR_CHANGE if (s[2] & SF_COLOR_CHANGE) else s[2]
        seq_wap_kontur.append((s[0], s[1], flag, c_code))
    file_wap_kettel = os.path.join(usb_dir, "04_Kieler_Wappen_Kettel_Konturen.jef")
    write_jef(file_wap_kettel, seq_wap_kontur, [JANOME_DEEP_RED, JANOME_WHITE, JANOME_BLACK])
    render_preview(seq_wap_kontur, [(210, 30, 40), (245, 245, 245), (20, 20, 20)], os.path.join(brain_dir, "kieler_wappen_kettel_konturen.png"))
    print(f"  [OK] Geschrieben: {file_wap_kettel} ({len(seq_wap_kontur)} Stiche)")

    # -------------------------------------------------------------
    # 5. BRUST "NW" & ÄRMEL "30" IN VARSITY KETTELRAND
    # -------------------------------------------------------------
    polys_nw = get_text_polys_straight("nw", cap_h_mm=42.0, letter_sp_mm=3.0)
    seq_nw = []
    for p in polys_nw:
        seq_nw.extend(make_kettel_outline(p, 2.2, 0.38, 0))
    file_nw = os.path.join(usb_dir, "05_Brust_NW_Varsity_Kettel.jef")
    write_jef(file_nw, seq_nw, [JANOME_DEEP_RED])
    render_preview(seq_nw, [(210, 30, 40)], os.path.join(brain_dir, "feuerwehr_brust_nw_stitches.png"))
    print(f"  [OK] Geschrieben: {file_nw} ({len(seq_nw)} Stiche)")

    polys_aermel = get_text_polys_straight("30", cap_h_mm=52.0, letter_sp_mm=4.0)
    seq_aermel = []
    for p in polys_aermel:
        seq_aermel.extend(make_kettel_outline(p, 2.4, 0.38, 0))
    file_aermel = os.path.join(usb_dir, "06_Aermel_30_Varsity_Kettel.jef")
    write_jef(file_aermel, seq_aermel, [JANOME_DEEP_RED])
    render_preview(seq_aermel, [(210, 30, 40)], os.path.join(brain_dir, "feuerwehr_aermel_30_stitches.png"))
    print(f"  [OK] Geschrieben: {file_aermel} ({len(seq_aermel)} Stiche)")

    print("\n=== ALLE 100% JANOME MC350E RAHMEN A DATEIEN ERFOLGREICH GENERIERT! ===")

if __name__ == '__main__':
    build_all()
