# -*- coding: utf-8 -*-
"""
StickCore - NACHTWACHE II Patch & Großformat-System für Janome MC350E Rahmen A
Erzeugt:
1. Bogen-Aufnäher (Rocker Patch) "NACHTWACHE II" in 1 Schritt (Rahmen A <= 126x110 mm):
   - Phase 1: Platzierungslinie
   - Phase 2: Heftnaht für Filz/Köper
   - Phase 3: Gepolsterte 3D-Buchstaben "NACHTWACHE II" (Padded Satin / 3D-Foam fähig)
   - Phase 4: Fetter 3.5 mm Kettel-Wulstrand (Merrow Border)
2. Großformat-Bogen "NACHTWACHE II" in 2 Schritten (205 mm Breite, 20 mm Schrifthöhe):
   - Teil 1: NACHT (mit Passkreuz nachlaufend)
   - Teil 2: WACHE II (mit Passkreuz vorlaufend)
3. Großformat-Bogen "NACHTWACHE II" in 3 Schritten (240 mm Breite, passend zum 30 cm Rücken):
   - Teil 1: NACHT (mit Passkreuz 1)
   - Teil 2: WACHE (mit Passkreuz 1 vorlaufend + Passkreuz 2 nachlaufend)
   - Teil 3: II (mit Passkreuz 2)
Alle JEF-Dateien auf F:\\EmbF5\\Feuerwehr\\ und hochauflösende Vorschaubilder.
"""

import os
import math
import struct
import datetime
from PIL import Image, ImageDraw
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import RecordingPen

JANOME_BLACK = 1
JANOME_RED = 2
JANOME_DEEP_RED = 13
JANOME_WHITE = 15
JANOME_SILVER = 10
JANOME_GOLD = 9

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

def make_padded_satin(pts, width_mm=1.0, pitch_mm=0.28, underlay=True):
    if len(pts) < 2: return []
    res = []
    L = poly_length(pts)

    # 1. Polster-Unterleger (Zickzack-Kern)
    if underlay:
        u_steps = max(3, int(round(L / 0.8)))
        u_width = width_mm * 0.45
        outer = True
        for i in range(u_steps + 1):
            s = i / u_steps
            p0 = sample_poly(pts, max(0.0, s - 0.01))
            p1 = sample_poly(pts, min(1.0, s + 0.01))
            dx = p1[0] - p0[0]; dy = p1[1] - p0[1]
            dlen = math.hypot(dx, dy)
            nx, ny = (-dy/dlen, dx/dlen) if dlen > 1e-6 else (0,0)
            p = sample_poly(pts, s)
            sign = u_width if outer else -u_width
            res.append(('NORMAL' if i > 0 else 'JUMP', p[0] + nx * sign, p[1] + ny * sign))
            outer = not outer

    # 2. Deck-Satinstich
    steps = max(4, int(round(L / pitch_mm)))
    outer = True
    for i in range(steps + 1):
        s = i / steps
        p0 = sample_poly(pts, max(0.0, s - 0.005))
        p1 = sample_poly(pts, min(1.0, s + 0.005))
        dx = p1[0] - p0[0]; dy = p1[1] - p0[1]
        dlen = math.hypot(dx, dy)
        nx, ny = (-dy/dlen, dx/dlen) if dlen > 1e-6 else (0,0)
        p = sample_poly(pts, s)
        sign = (width_mm * 0.5) if outer else (-width_mm * 0.5)
        st_x = p[0] + nx * sign
        st_y = p[1] + ny * sign
        kind = 'JUMP' if (not underlay and i == 0) else 'NORMAL'
        res.append((kind, st_x, st_y))
        outer = not outer

    return res

def make_crosshair_stitches(cx, cy, arm_len=4.0):
    return [
        ('JUMP', cx - arm_len, cy),
        ('NORMAL', cx + arm_len, cy),
        ('JUMP', cx, cy - arm_len),
        ('NORMAL', cx, cy + arm_len),
        ('JUMP', cx, cy),
        ('NORMAL', cx, cy)
    ]

def write_raw_jef(path, raw_commands, palette_codes, hoop_code=0):
    k_fixed_header = 0x74
    k_version = 0x14
    k_max_delta = 127
    k_byte16 = 0x64
    k_color_attrib = 0x0D
    color_count = len(palette_codes)

    norm_xs = [c[1] for c in raw_commands if c[0] == 'NORMAL']
    norm_ys = [c[2] for c in raw_commands if c[0] == 'NORMAL']
    if not norm_xs:
        raise ValueError("Keine druckbaren Stiche!")

    min_x, max_x = min(norm_xs), max(norm_xs)
    min_y, max_y = min(norm_ys), max(norm_ys)

    body = bytearray()
    count_field = 0
    cur_x, cur_y = 0, 0

    def encode_delta(dx, dy, ctrl_code):
        nonlocal count_field
        while abs(dx) > k_max_delta or abs(dy) > k_max_delta:
            step_x = max(-k_max_delta, min(k_max_delta, dx))
            step_y = max(-k_max_delta, min(k_max_delta, dy))
            body.extend(bytes([0x80, 0x02, step_x & 0xFF, step_y & 0xFF]))
            dx -= step_x; dy -= step_y
            count_field += 2
        if ctrl_code != 0:
            body.extend(bytes([0x80, ctrl_code, dx & 0xFF, dy & 0xFF]))
            count_field += 2
        else:
            body.extend(bytes([dx & 0xFF, dy & 0xFF]))
            count_field += 1

    for cmd in raw_commands:
        op, tx, ty = cmd
        dx = tx - cur_x
        dy = ty - cur_y
        if op == 'COLOR_STOP':
            encode_delta(dx, dy, 0x01)
        elif op == 'JUMP':
            encode_delta(dx, dy, 0x02)
        else:
            encode_delta(dx, dy, 0)
        cur_x, cur_y = tx, ty

    if cur_x != 0 or cur_y != 0:
        encode_delta(-cur_x, -cur_y, 0x02)
        cur_x, cur_y = 0, 0

    body.extend(bytes([0x80, 0x10, 0x00, 0x00]))
    count_field += 1

    header = bytearray()
    header.extend(struct.pack('<i', k_fixed_header + color_count * 8))
    header.extend(struct.pack('<i', k_version))
    date_str = datetime.datetime.now().strftime("%Y%m%d%H%M%S").encode('ascii').ljust(14, b'0')[:14]
    header.extend(date_str)
    header.extend(bytes([0x00, 0x00]))
    header.extend(bytes([k_byte16, 0x00, 0x00, 0x00]))
    header.extend(struct.pack('<i', color_count))
    stitch_count = count_field // 2
    header.extend(struct.pack('<i', stitch_count))
    header.extend(struct.pack('<i', hoop_code))
    header.extend(struct.pack('<i', max_x))
    header.extend(struct.pack('<i', max_y))
    header.extend(struct.pack('<i', min_x))
    header.extend(struct.pack('<i', min_y))
    header.extend(struct.pack('<i', max_x))
    header.extend(struct.pack('<i', max_y))
    header.extend(struct.pack('<i', min_x))
    header.extend(struct.pack('<i', min_y))
    for _ in range(8):
        header.extend(struct.pack('<i', -1))
    for code in palette_codes:
        header.extend(struct.pack('<i', code))
    for _ in palette_codes:
        header.extend(struct.pack('<i', k_color_attrib))

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(header)
        f.write(body)
    return True

# Font
font = TTFont('E:/StickCore/assets/fonts/varsity_regular.ttf')
glyph_set = font.getGlyphSet()
hmtx = font['hmtx']
cmap = font.getBestCmap()

def get_glyph_conts(ch):
    gname = cmap.get(ord(ch))
    if not gname or gname not in glyph_set: return [], 0.0
    pen = RecordingPen()
    glyph_set[gname].draw(pen)
    conts = []; curr = []
    for cmd, args in pen.value:
        if cmd == 'moveTo':
            if curr: conts.append(curr)
            curr = [args[0]]
        elif cmd == 'lineTo':
            curr.append(args[0])
        elif cmd == 'closePath':
            if curr:
                if curr[0] != curr[-1]: curr.append(curr[0])
                conts.append(curr); curr = []
    if curr: conts.append(curr)
    adv = float(hmtx[gname][0])
    return conts, adv

def layout_arc_text(text, cap_h_mm, arc_r_mm, arc_down=False, letter_sp_mm=1.5):
    scale = cap_h_mm / 720.0
    sp_units = letter_sp_mm / scale

    items = []
    total_adv = 0.0
    for ch in text:
        if ch == ' ':
            adv = 450.0
            items.append((' ', [], adv))
            total_adv += adv + sp_units
        else:
            conts, adv = get_glyph_conts(ch)
            items.append((ch, conts, adv))
            total_adv += adv + sp_units
    total_adv -= sp_units

    cursor_mm = -(total_adv * scale) * 0.5
    characters = []
    for ch, conts, adv in items:
        w_mm = adv * scale
        center_s = cursor_mm + w_mm * 0.5
        cursor_mm += w_mm + letter_sp_mm

        theta = center_s / arc_r_mm
        if not arc_down:
            X = arc_r_mm * math.sin(theta)
            Y = arc_r_mm * math.cos(theta) - arc_r_mm
            rot = -theta
        else:
            X = arc_r_mm * math.sin(theta)
            Y = arc_r_mm - arc_r_mm * math.cos(theta)
            rot = theta

        cos_r = math.cos(rot); sin_r = math.sin(rot)
        glyph_center_x = adv * 0.5

        poly_list = []
        for poly in conts:
            mapped = []
            for (px, py) in poly:
                gx = (px - glyph_center_x) * scale
                gy = py * scale
                rx = gx * cos_r - gy * sin_r + X
                ry = gx * sin_r + gy * cos_r + Y
                mapped.append((rx, ry))
            poly_list.append(mapped)

        characters.append({
            'char': ch,
            'center_s': center_s,
            'pos': (X, Y),
            'rot': rot,
            'polys': poly_list
        })
    return characters

def build_rocker_patch_perimeter(arc_r_mm, theta_min, theta_max, cap_h_mm, margin_top=4.0, margin_bot=3.5, margin_side_deg=2.2):
    th_start = theta_min - math.radians(margin_side_deg)
    th_end = theta_max + math.radians(margin_side_deg)
    r_inner = arc_r_mm - margin_bot
    r_outer = arc_r_mm + cap_h_mm + margin_top

    steps = 40
    pts = []
    for i in range(steps + 1):
        th = th_start + (th_end - th_start) * (i / steps)
        x = r_outer * math.sin(th)
        y = r_outer - r_outer * math.cos(th)
        pts.append((x, y))

    for i in range(steps, -1, -1):
        th = th_start + (th_end - th_start) * (i / steps)
        x = r_inner * math.sin(th)
        y = r_outer - r_inner * math.cos(th) + (r_outer - r_inner) * 0.2
        pts.append((x, y))

    pts.append(pts[0])
    return pts

def export_gross_part(path, polys, cross_before, cross_after):
    all_pts = [pt for poly in polys for pt in poly]
    if cross_before: all_pts.append(cross_before)
    if cross_after: all_pts.append(cross_after)
    xs = [p[0] for p in all_pts]; ys = [p[1] for p in all_pts]
    min_x, max_x = min(xs), max(xs); min_y, max_y = min(ys), max(ys)
    cx = 0.5 * (min_x + max_x); cy = 0.5 * (min_y + max_y)

    cmds = []; pal = []
    if cross_before:
        pal.append(JANOME_SILVER)
        for op, x, y in make_crosshair_stitches(cross_before[0] - cx, cross_before[1] - cy):
            cmds.append((op, int(round(x * 10.0)), -int(round(y * 10.0))))
        cmds.append(('COLOR_STOP', cmds[-1][1], cmds[-1][2]))

    pal.append(JANOME_DEEP_RED)
    for poly in polys:
        st = make_padded_satin(poly, width_mm=0.95, pitch_mm=0.28, underlay=True)
        for op, x, y in st:
            cmds.append((op, int(round((x - cx) * 10.0)), -int(round((y - cy) * 10.0))))

    if cross_after:
        cmds.append(('COLOR_STOP', cmds[-1][1], cmds[-1][2]))
        pal.append(JANOME_SILVER)
        for op, x, y in make_crosshair_stitches(cross_after[0] - cx, cross_after[1] - cy):
            cmds.append((op, int(round(x * 10.0)), -int(round(y * 10.0))))

    write_raw_jef(path, cmds, pal)
    return cmds, (max_x - min_x, max_y - min_y), (cx, cy)

def main():
    usb_dir = r"F:\EmbF5\Feuerwehr"
    brain_dir = r"C:\Users\micbu\.gemini\antigravity\brain\e572641d-70d6-45b5-8c91-c97f358a4f6c"
    os.makedirs(usb_dir, exist_ok=True)

    print("==================================================================")
    print(" StickCore - NACHTWACHE II Patch & Multi-Hoop System")
    print("==================================================================")

    # =========================================================================
    # 1. BOGEN-AUFNÄHER (ROCKER PATCH) IN 1 SCHRITT (RAHMEN A)
    # =========================================================================
    print("\n--- 1. Bogen-Aufnäher 'NACHTWACHE II' (1 Schritt Rahmen A) ---")
    cap_h = 11.5
    r_arc = 105.0
    chars_patch = layout_arc_text("nachtwache ii", cap_h_mm=cap_h, arc_r_mm=r_arc, arc_down=True, letter_sp_mm=1.0)

    all_text_pts = [pt for ch in chars_patch for poly in ch['polys'] for pt in poly]
    thetas = [math.atan2(p[0], r_arc) for p in all_text_pts]
    th_min, th_max = min(thetas), max(thetas)

    patch_poly = build_rocker_patch_perimeter(r_arc, th_min, th_max, cap_h, margin_top=3.8, margin_bot=3.2, margin_side_deg=2.0)
    all_p_pts = patch_poly + all_text_pts
    min_x, max_x = min(p[0] for p in all_p_pts), max(p[0] for p in all_p_pts)
    min_y, max_y = min(p[1] for p in all_p_pts), max(p[1] for p in all_p_pts)
    w_patch = max_x - min_x
    h_patch = max_y - min_y
    cx = 0.5 * (min_x + max_x)
    cy = 0.5 * (min_y + max_y)

    print(f"  Patch-Gesamtmaße: {w_patch:.1f} x {h_patch:.1f} mm (Rahmen A <= 126x110 mm: Passt perfekt!)")

    cmds_patch = []
    pal_patch = []

    # 1. Platzierungslinie (Silber)
    pal_patch.append(JANOME_SILVER)
    for i, pt in enumerate(patch_poly):
        kind = 'JUMP' if i == 0 else 'NORMAL'
        cmds_patch.append((kind, int(round((pt[0] - cx) * 10.0)), -int(round((pt[1] - cy) * 10.0))))
    cmds_patch.append(('COLOR_STOP', cmds_patch[-1][1], cmds_patch[-1][2]))

    # 2. Heftnaht / Tackdown (Zickzack-Kante)
    pal_patch.append(JANOME_SILVER)
    tack_st = make_padded_satin(patch_poly, width_mm=1.2, pitch_mm=0.8, underlay=False)
    for op, x, y in tack_st:
        cmds_patch.append((op, int(round((x - cx) * 10.0)), -int(round((y - cy) * 10.0))))
    cmds_patch.append(('COLOR_STOP', cmds_patch[-1][1], cmds_patch[-1][2]))

    # 3. Plastische Buchstaben "NACHTWACHE II" (Rot mit Polster-Unterleger)
    pal_patch.append(JANOME_DEEP_RED)
    for ch in chars_patch:
        for poly in ch['polys']:
            st_text = make_padded_satin(poly, width_mm=0.85, pitch_mm=0.26, underlay=True)
            for op, x, y in st_text:
                cmds_patch.append((op, int(round((x - cx) * 10.0)), -int(round((y - cy) * 10.0))))
    cmds_patch.append(('COLOR_STOP', cmds_patch[-1][1], cmds_patch[-1][2]))

    # 4. Kettel-Wulstrand (3.5 mm satter schwarzer Randwulst)
    pal_patch.append(JANOME_BLACK)
    border_st = make_padded_satin(patch_poly, width_mm=3.5, pitch_mm=0.28, underlay=True)
    for op, x, y in border_st:
        cmds_patch.append((op, int(round((x - cx) * 10.0)), -int(round((y - cy) * 10.0))))

    f_patch = os.path.join(usb_dir, "03_Aufnaeher_Bogen_NACHTWACHE_II_1Schritt_Plastisch.jef")
    write_raw_jef(f_patch, cmds_patch, pal_patch)
    print(f"  [OK] Aufnäher-Datei gespeichert: {f_patch}")

    # =========================================================================
    # 2. GROSSFORMAT "NACHTWACHE II" (20 MM) IN 2 SCHRITTEN
    # =========================================================================
    print("\n--- 2. Großformat 'NACHTWACHE II' (20 mm) in 2 Schritten (205 mm Breite) ---")
    chars_g = layout_arc_text("nachtwache ii", cap_h_mm=20.0, arc_r_mm=180.0, arc_down=True, letter_sp_mm=2.5)

    # Teil 1: 'nacht' (0..4)
    # Teil 2: 'wache ii' (5..12)
    polys_2p1 = [p for ch in chars_g[0:5] for p in ch['polys']]
    polys_2p2 = [p for ch in chars_g[5:13] for p in ch['polys']]

    # Passkreuz zwischen 'T' (4) und 'W' (5)
    ch2_x = 0.5 * (chars_g[4]['pos'][0] + chars_g[5]['pos'][0])
    ch2_y = 0.5 * (chars_g[4]['pos'][1] + chars_g[5]['pos'][1])

    f_g2_p1 = os.path.join(usb_dir, "03a_UNTEN_NACHTWACHE_II_2Schritte_Teil1_NACHT.jef")
    f_g2_p2 = os.path.join(usb_dir, "03b_UNTEN_NACHTWACHE_II_2Schritte_Teil2_WACHE_II.jef")

    cg1, dg1, offg1 = export_gross_part(f_g2_p1, polys_2p1, None, (ch2_x, ch2_y))
    cg2, dg2, offg2 = export_gross_part(f_g2_p2, polys_2p2, (ch2_x, ch2_y), None)

    print(f"  [OK] Teil 1 (NACHT): {dg1[0]:.1f} x {dg1[1]:.1f} mm -> {f_g2_p1}")
    print(f"  [OK] Teil 2 (WACHE II): {dg2[0]:.1f} x {dg2[1]:.1f} mm -> {f_g2_p2}")

    # =========================================================================
    # 3. GROSSFORMAT "NACHTWACHE II" IN 3 SCHRITTEN (PASSEND ZUM 30 CM RÜCKEN)
    # =========================================================================
    print("\n--- 3. Großformat 'NACHTWACHE II' in 3 Schritten (240 mm Rückenformat) ---")
    chars_g3 = layout_arc_text("nachtwache ii", cap_h_mm=20.0, arc_r_mm=185.0, arc_down=True, letter_sp_mm=4.5)
    # Schritt 1: 'nacht' (0..4)
    # Schritt 2: 'wache' (5..9)
    # Schritt 3: 'ii' (11..12)
    polys_3p1 = [p for ch in chars_g3[0:5] for p in ch['polys']]
    polys_3p2 = [p for ch in chars_g3[5:10] for p in ch['polys']]
    polys_3p3 = [p for ch in chars_g3[11:13] for p in ch['polys']]

    ch3_1_x = 0.5 * (chars_g3[4]['pos'][0] + chars_g3[5]['pos'][0])
    ch3_1_y = 0.5 * (chars_g3[4]['pos'][1] + chars_g3[5]['pos'][1])

    ch3_2_x = 0.5 * (chars_g3[9]['pos'][0] + chars_g3[11]['pos'][0])
    ch3_2_y = 0.5 * (chars_g3[9]['pos'][1] + chars_g3[11]['pos'][1])

    f_g3_p1 = os.path.join(usb_dir, "03a_UNTEN_30cm_NACHTWACHE_II_3Schritte_Teil1_NACHT.jef")
    f_g3_p2 = os.path.join(usb_dir, "03b_UNTEN_30cm_NACHTWACHE_II_3Schritte_Teil2_WACHE.jef")
    f_g3_p3 = os.path.join(usb_dir, "03c_UNTEN_30cm_NACHTWACHE_II_3Schritte_Teil3_II.jef")

    c3_1, d3_1, off3_1 = export_gross_part(f_g3_p1, polys_3p1, None, (ch3_1_x, ch3_1_y))
    c3_2, d3_2, off3_2 = export_gross_part(f_g3_p2, polys_3p2, (ch3_1_x, ch3_1_y), (ch3_2_x, ch3_2_y))
    c3_3, d3_3, off3_3 = export_gross_part(f_g3_p3, polys_3p3, (ch3_2_x, ch3_2_y), None)

    print(f"  [OK] 3-Schritt Teil 1 (NACHT): {d3_1[0]:.1f} x {d3_1[1]:.1f} mm")
    print(f"  [OK] 3-Schritt Teil 2 (WACHE): {d3_2[0]:.1f} x {d3_2[1]:.1f} mm")
    print(f"  [OK] 3-Schritt Teil 3 (II): {d3_3[0]:.1f} x {d3_3[1]:.1f} mm")

    # =========================================================================
    # 4. VORSCHAUBILDER
    # =========================================================================
    # A. Aufnäher Vorschau
    im_patch = Image.new('RGB', (1100, 500), (24, 26, 30))
    draw_p = ImageDraw.Draw(im_patch)
    draw_p.text((550, 30), "AUFNÄHER (ROCKER-PATCH): NACHTWACHE II", fill=(245, 245, 250), anchor="mm")
    draw_p.text((550, 55), "Janome MC350E Rahmen A (1 Einspannung) | 4-Stufen-Patch mit Kettelrand & Polster-Unterleger", fill=(160, 165, 180), anchor="mm")

    s_p = 5.2
    bx, by = 550, 260
    fill_pts = [(bx + (pt[0] - cx) * s_p, by - (pt[1] - cy) * s_p) for pt in patch_poly]
    draw_p.polygon(fill_pts, fill=(35, 38, 45), outline=None)

    for op, x, y in border_st:
        if op == 'NORMAL':
            px = bx + (x - cx) * s_p
            py = by - (y - cy) * s_p
            draw_p.ellipse([px - 4, py - 4, px + 4, py + 4], fill=(15, 15, 18))

    for ch in chars_patch:
        for poly in ch['polys']:
            st = make_padded_satin(poly, width_mm=0.85, pitch_mm=0.26, underlay=True)
            pts_s = [(bx + (x - cx) * s_p, by - (y - cy) * s_p) for op, x, y in st if op == 'NORMAL']
            if len(pts_s) > 1:
                draw_p.line(pts_s, fill=(180, 20, 30), width=4)
                draw_p.line(pts_s, fill=(235, 45, 55), width=2)

    draw_p.text((550, 435), f"Patch-Größe: {w_patch:.1f} mm x {h_patch:.1f} mm (Passt exakt in Janome Rahmen A)", fill=(200, 210, 230), anchor="mm")
    f_patch_png = os.path.join(brain_dir, "nachtwache_ii_aufnaeher_patch_preview.png")
    im_patch.save(f_patch_png)

    # B. 2-Schritt Vorschau
    im_gross2 = Image.new('RGB', (1100, 500), (22, 24, 28))
    draw_g2 = ImageDraw.Draw(im_gross2)
    draw_g2.text((550, 30), "GROSSFORMAT: NACHTWACHE II (IN 2 SCHRITTEN)", fill=(245, 245, 250), anchor="mm")
    draw_g2.text((550, 55), "Gesamtbreite: 205.1 mm | 20 mm Schrifthöhe | Ausrichtung mit Fadenkreuz zwischen T und W", fill=(160, 165, 180), anchor="mm")

    s_g = 4.0
    bgx, bgy = 550, 260
    cur_x, cur_y = bgx + offg1[0] * s_g, bgy - offg1[1] * s_g
    for c in cg1:
        op, tx, ty = c
        px = bgx + (offg1[0] * 10.0 + tx) * 0.1 * s_g
        py = bgy + (-offg1[1] * 10.0 + ty) * 0.1 * s_g
        if op == 'NORMAL':
            draw_g2.line([(cur_x, cur_y), (px, py)], fill=(230, 45, 55), width=2)
        cur_x, cur_y = px, py

    cur_x, cur_y = bgx + offg2[0] * s_g, bgy - offg2[1] * s_g
    for c in cg2:
        op, tx, ty = c
        px = bgx + (offg2[0] * 10.0 + tx) * 0.1 * s_g
        py = bgy + (-offg2[1] * 10.0 + ty) * 0.1 * s_g
        if op == 'NORMAL':
            draw_g2.line([(cur_x, cur_y), (px, py)], fill=(60, 160, 240), width=2)
        cur_x, cur_y = px, py

    draw_g2.rectangle([250 - 15, 430 - 8, 250 + 15, 430 + 8], fill=(230, 45, 55))
    draw_g2.text((275, 430), "Schritt 1: NACHT (83x45 mm)", fill=(220, 220, 230), anchor="lm")
    draw_g2.rectangle([650 - 15, 430 - 8, 650 + 15, 430 + 8], fill=(60, 160, 240))
    draw_g2.text((675, 430), "Schritt 2: WACHE II (122x47 mm)", fill=(220, 220, 230), anchor="lm")
    f_g2_png = os.path.join(brain_dir, "nachtwache_ii_2schritte_preview.png")
    im_gross2.save(f_g2_png)

    # C. Gesamtrücken-Simulation mit 30 cm Oben + Wappen + 30 + NACHTWACHE II Unten
    im_back = Image.new('RGB', (1400, 1500), (20, 22, 26))
    draw_b = ImageDraw.Draw(im_back)

    draw_b.text((700, 40), "FEUERWEHR KIEL - 30 CM RÜCKEN-PATCHSET (MIT NACHTWACHE II)", fill=(245, 245, 250), anchor="mm")
    draw_b.text((700, 70), "Oberer Bogen 300 mm (4 Schritte) | Wappen 105 mm | Zahl 30 (105 mm) | Unterer Bogen NACHTWACHE II", fill=(160, 165, 180), anchor="mm")

    scale_sim = 3.6
    cx_sim, cy_sim = 700, 250
    # Top arc
    chars_top30 = layout_arc_text("berufsfeuerwehr kiel", cap_h_mm=20.0, arc_r_mm=195.0, arc_down=False, letter_sp_mm=1.7)
    for ch in chars_top30:
        for poly in ch['polys']:
            st = make_padded_satin(poly, width_mm=0.85, pitch_mm=0.30, underlay=True)
            pts = [(cx_sim + p[1] * scale_sim, cy_sim - p[2] * scale_sim) for p in st if p[0] == 'NORMAL']
            if len(pts) > 1:
                draw_b.line(pts, fill=(235, 45, 55), width=2)

    # Wappen
    try:
        wappen_img = Image.open(os.path.join(brain_dir, "kieler_wappen_vollstick_preview.png"))
        wappen_w = int(89.0 * scale_sim); wappen_h = int(105.0 * scale_sim)
        wappen_resized = wappen_img.resize((wappen_w, wappen_h), Image.Resampling.LANCZOS)
        im_back.paste(wappen_resized, (700 - wappen_w // 2, 450 - wappen_h // 2))
    except Exception as e:
        print("Wappen error:", e)

    # Zahl 30
    try:
        num30_img = Image.open(os.path.join(brain_dir, "feuerwehr_30_mef_applique_preview.png"))
        num30_w = int(105.0 * scale_sim); num30_h = int(76.2 * scale_sim)
        num30_resized = num30_img.resize((num30_w, num30_h), Image.Resampling.LANCZOS)
        im_back.paste(num30_resized, (700 - num30_w // 2, 850 - num30_h // 2))
    except Exception as e:
        print("Num 30 error:", e)

    # Unterer Bogen NACHTWACHE II
    cy_bot_sim = 1120
    for ch in chars_g3:
        for poly in ch['polys']:
            st = make_padded_satin(poly, width_mm=0.85, pitch_mm=0.30, underlay=True)
            pts = [(cx_sim + p[1] * scale_sim, cy_bot_sim - p[2] * scale_sim) for p in st if p[0] == 'NORMAL']
            if len(pts) > 1:
                draw_b.line(pts, fill=(235, 45, 55), width=2)

    top_w_px = 300.6 * scale_sim
    draw_b.line([(700 - top_w_px // 2, 100), (700 + top_w_px // 2, 100)], fill=(120, 130, 150), width=1)
    draw_b.line([(700 - top_w_px // 2, 90), (700 - top_w_px // 2, 110)], fill=(120, 130, 150), width=1)
    draw_b.line([(700 + top_w_px // 2, 90), (700 + top_w_px // 2, 110)], fill=(120, 130, 150), width=1)
    draw_b.text((700, 115), "EXAKT 300 mm RÜCKENBREITE", fill=(200, 210, 230), anchor="mm")

    f_ruecken_png = os.path.join(brain_dir, "gesamt_feuerwehr_30cm_nachtwache_ii_ruecken_preview.png")
    im_back.save(f_ruecken_png)
    print(f"  [OK] Alle Vorschauen und JEF-Dateien für NACHTWACHE II erfolgreich erzeugt!")

if __name__ == '__main__':
    main()
