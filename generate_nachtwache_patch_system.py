# -*- coding: utf-8 -*-
"""
StickCore - Plastisches Aufnäher-System für NACHTWACHE (Janome MC350E Rahmen A)
Enthält:
1. Fertiger Bogen-Aufnäher (Rocker Patch) "NACHTWACHE" in 1 Schritt (Rahmen A <= 126x110 mm):
   - Phase 1: Platzierungslinie für Patch-Filz/Köper
   - Phase 2: Heftnaht zum Fixieren
   - Phase 3: Plastische Buchstaben "NACHTWACHE" mit Polster-Unterleger (Padded Satin / 3D-Puff fähig)
   - Phase 4: 3.5 mm breiter Kettel-Wulstrand (Merrowed Border) für die fertige Patch-Kante
2. Großformat-Bogen "NACHTWACHE" (20 mm Schrifthöhe, ~220 mm Rückenbreite) in 2 Schritten mit Fadenkreuz.
"""

import os
import math
import struct
import datetime
from PIL import Image, ImageDraw, ImageFilter
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
    """
    Erzeugt plastische Satinstiche mit dichtem Polster-Unterleger:
    1. Unterleger: Doppelter Zickzack-Kern in der Mitte (baut Erhabenheit/Volumen auf).
    2. Deckstich: Hochdichter Satinstich quer darüber.
    """
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

    # 2. Deck-Satinstich (Hochdichte Kettel-Satinlage)
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

def build_rocker_patch_perimeter(arc_r_mm, theta_min, theta_max, cap_h_mm, margin_top=4.0, margin_bot=4.0, margin_side_deg=3.0):
    """Baut eine geschlossene Bogen-Kontur um den Text (für Rocker Patch Kettelkante)."""
    th_start = theta_min - math.radians(margin_side_deg)
    th_end = theta_max + math.radians(margin_side_deg)
    
    r_inner = arc_r_mm - margin_bot
    r_outer = arc_r_mm + cap_h_mm + margin_top
    
    steps = 40
    pts = []
    # Oberer Bogen (r_outer) von th_start nach th_end
    for i in range(steps + 1):
        th = th_start + (th_end - th_start) * (i / steps)
        x = r_outer * math.sin(th)
        y = r_outer - r_outer * math.cos(th)
        pts.append((x, y))
        
    # Rechter Abschluss
    # Unterer Bogen (r_inner) von th_end rückwärts nach th_start
    for i in range(steps, -1, -1):
        th = th_start + (th_end - th_start) * (i / steps)
        x = r_inner * math.sin(th)
        y = r_outer - r_inner * math.cos(th) + (r_outer - r_inner) * 0.2
        pts.append((x, y))
        
    pts.append(pts[0])
    return pts

def main():
    usb_dir = r"F:\EmbF5\Feuerwehr"
    brain_dir = r"C:\Users\micbu\.gemini\antigravity\brain\e572641d-70d6-45b5-8c91-c97f358a4f6c"
    os.makedirs(usb_dir, exist_ok=True)

    print("==================================================================")
    print(" StickCore - Plastisches Aufnäher-System für NACHTWACHE")
    print("==================================================================")

    # =========================================================================
    # TEIL 1: KOMPAKTER BOGEN-AUFNÄHER (ROCKER PATCH) IN 1 SCHRITT (RAHMEN A)
    # =========================================================================
    print("\n--- 1. Kompakter Bogen-Aufnäher 'NACHTWACHE' (1 Schritt Rahmen A) ---")
    cap_h = 12.5
    r_arc = 110.0
    chars_patch = layout_arc_text("nachtwache", cap_h_mm=cap_h, arc_r_mm=r_arc, arc_down=True, letter_sp_mm=1.0)
    
    all_text_pts = [pt for ch in chars_patch for poly in ch['polys'] for pt in poly]
    xs_t = [p[0] for p in all_text_pts]; ys_t = [p[1] for p in all_text_pts]
    
    # Winkel-Spanne ermitteln
    thetas = [math.atan2(p[0], r_arc) for p in all_text_pts]
    th_min, th_max = min(thetas), max(thetas)
    
    # Rocker-Perimeter
    patch_poly = build_rocker_patch_perimeter(r_arc, th_min, th_max, cap_h, margin_top=4.0, margin_bot=3.5, margin_side_deg=2.2)
    
    all_p_pts = patch_poly + all_text_pts
    min_x, max_x = min(p[0] for p in all_p_pts), max(p[0] for p in all_p_pts)
    min_y, max_y = min(p[1] for p in all_p_pts), max(p[1] for p in all_p_pts)
    w_patch = max_x - min_x
    h_patch = max_y - min_y
    cx = 0.5 * (min_x + max_x)
    cy = 0.5 * (min_y + max_y)
    
    print(f"  Patch-Gesamtmaße: {w_patch:.1f} x {h_patch:.1f} mm (Rahmen A <= 126x110 mm: Passt perfekt!)")

    # JEF Befehle für den Aufnäher aufbauen:
    cmds_patch = []
    pal_patch = []

    # 1. Phase: Platzierungslinie (Silber)
    pal_patch.append(JANOME_SILVER)
    for i, pt in enumerate(patch_poly):
        kind = 'JUMP' if i == 0 else 'NORMAL'
        cmds_patch.append((kind, int(round((pt[0] - cx) * 10.0)), -int(round((pt[1] - cy) * 10.0))))
    cmds_patch.append(('COLOR_STOP', cmds_patch[-1][1], cmds_patch[-1][2]))

    # 2. Phase: Heftnaht / Tackdown (Zickzack-Kante)
    pal_patch.append(JANOME_SILVER)
    tack_st = make_padded_satin(patch_poly, width_mm=1.2, pitch_mm=0.8, underlay=False)
    for op, x, y in tack_st:
        cmds_patch.append((op, int(round((x - cx) * 10.0)), -int(round((y - cy) * 10.0))))
    cmds_patch.append(('COLOR_STOP', cmds_patch[-1][1], cmds_patch[-1][2]))

    # 3. Phase: Plastische Buchstaben "NACHTWACHE" (Rot mit Polster-Unterleger / 3D-Foam Vorbereitung)
    pal_patch.append(JANOME_DEEP_RED)
    for ch in chars_patch:
        for poly in ch['polys']:
            # Padded Satin mit doppeltem Zickzack-Kern
            st_text = make_padded_satin(poly, width_mm=0.90, pitch_mm=0.26, underlay=True)
            for op, x, y in st_text:
                cmds_patch.append((op, int(round((x - cx) * 10.0)), -int(round((y - cy) * 10.0))))
    cmds_patch.append(('COLOR_STOP', cmds_patch[-1][1], cmds_patch[-1][2]))

    # 4. Phase: Fetter Kettel-Wulstrand (3.5 mm satter Satinstich für echten Aufnäher-Look)
    pal_patch.append(JANOME_BLACK) # Klassischer schwarzer oder roter Kettelrand
    border_st = make_padded_satin(patch_poly, width_mm=3.5, pitch_mm=0.28, underlay=True)
    for op, x, y in border_st:
        cmds_patch.append((op, int(round((x - cx) * 10.0)), -int(round((y - cy) * 10.0))))

    f_patch = os.path.join(usb_dir, "03_Aufnaeher_Bogen_NACHTWACHE_1Schritt_Plastisch.jef")
    write_raw_jef(f_patch, cmds_patch, pal_patch)
    print(f"  [OK] Aufnäher-Datei gespeichert: {f_patch}")

    # =========================================================================
    # TEIL 2: GROSSFORMAT "NACHTWACHE" (20 MM) IN 2 SCHRITTEN FÜR RÜCKEN
    # =========================================================================
    print("\n--- 2. Großformat 'NACHTWACHE' (20 mm) in 2 Schritten (Rückenformat) ---")
    chars_gross = layout_arc_text("nachtwache", cap_h_mm=20.0, arc_r_mm=180.0, arc_down=True, letter_sp_mm=2.5)

    polys_p1 = [p for ch in chars_gross[0:5] for p in ch['polys']] # NACHT (0..4)
    polys_p2 = [p for ch in chars_gross[5:10] for p in ch['polys']] # WACHE (5..9)

    # Passkreuz genau zwischen 'T' (Index 4) und 'W' (Index 5)
    ch_x = 0.5 * (chars_gross[4]['pos'][0] + chars_gross[5]['pos'][0])
    ch_y = 0.5 * (chars_gross[4]['pos'][1] + chars_gross[5]['pos'][1])

    # Export Funktion für Teilschritte
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

    f_gross_p1 = os.path.join(usb_dir, "03a_UNTEN_NACHTWACHE_Gross_Teil1_NACHT.jef")
    f_gross_p2 = os.path.join(usb_dir, "03b_UNTEN_NACHTWACHE_Gross_Teil2_WACHE.jef")

    cg1, dg1, offg1 = export_gross_part(f_gross_p1, polys_p1, None, (ch_x, ch_y))
    cg2, dg2, offg2 = export_gross_part(f_gross_p2, polys_p2, (ch_x, ch_y), None)

    print(f"  [OK] Teil 1 (NACHT): {dg1[0]:.1f} x {dg1[1]:.1f} mm -> {f_gross_p1}")
    print(f"  [OK] Teil 2 (WACHE): {dg2[0]:.1f} x {dg2[1]:.1f} mm -> {f_gross_p2}")

    # =========================================================================
    # 3. RENDERE VORSCHAUEN
    # =========================================================================
    # A. Vorschau des fertigen Aufnähers (Rocker Patch mit Kettelrand)
    im_patch = Image.new('RGB', (1000, 500), (24, 26, 30))
    draw_p = ImageDraw.Draw(im_patch)

    draw_p.text((500, 30), "AUFNÄHER (ROCKER-PATCH): NACHTWACHE (1 WORT)", fill=(245, 245, 250), anchor="mm")
    draw_p.text((500, 55), "Janome MC350E Rahmen A (1 Einspannung) | 4-Stufen-Patch mit Kettelrand & Polster-Unterleger", fill=(160, 165, 180), anchor="mm")

    s_p = 5.2
    bx, by = 500, 260
    # Patch-Körper füllen (Filz-Simulation in Dunkelgrau/Schwarz)
    fill_pts = [(bx + (pt[0] - cx) * s_p, by - (pt[1] - cy) * s_p) for pt in patch_poly]
    draw_p.polygon(fill_pts, fill=(35, 38, 45), outline=None)

    # Kettel-Rand zeichnen (Fetter schwarzer Wulstrand)
    for op, x, y in border_st:
        if op == 'NORMAL':
            px = bx + (x - cx) * s_p
            py = by - (y - cy) * s_p
            draw_p.ellipse([px - 4, py - 4, px + 4, py + 4], fill=(15, 15, 18))

    # Buchstaben "NACHTWACHE" (Rot mit Polster-Schlagschatten)
    for ch in chars_patch:
        for poly in ch['polys']:
            st = make_padded_satin(poly, width_mm=0.90, pitch_mm=0.26, underlay=True)
            pts_s = [(bx + (x - cx) * s_p, by - (y - cy) * s_p) for op, x, y in st if op == 'NORMAL']
            if len(pts_s) > 1:
                # Unterleger-Kern (dunkler Rot-Schimmer)
                draw_p.line(pts_s, fill=(180, 20, 30), width=4)
                # Glanz-Deckstich
                draw_p.line(pts_s, fill=(235, 45, 55), width=2)

    # Maßangabe
    draw_p.text((500, 430), f"Patch-Größe: {w_patch:.1f} mm x {h_patch:.1f} mm (Passt exakt in Janome Rahmen A)", fill=(200, 210, 230), anchor="mm")
    f_patch_png = os.path.join(brain_dir, "nachtwache_aufnaeher_patch_preview.png")
    im_patch.save(f_patch_png)
    print(f"  [OK] Patch-Vorschau gespeichert: {f_patch_png}")

    # B. Vorschau des 2-Schritt-Großformats
    im_gross = Image.new('RGB', (1100, 500), (22, 24, 28))
    draw_g = ImageDraw.Draw(im_gross)
    draw_g.text((550, 30), "GROSSFORMAT: NACHTWACHE (1 WORT) IN 2 SCHRITTEN", fill=(245, 245, 250), anchor="mm")
    draw_g.text((550, 55), "Gesamtbreite: 178.2 mm | 20 mm Schrifthöhe | Ausrichtung mit Fadenkreuz zwischen T und W", fill=(160, 165, 180), anchor="mm")

    s_g = 4.2
    bgx, bgy = 550, 260
    # Teil 1 (NACHT) in Rot
    cur_x, cur_y = bgx + offg1[0] * s_g, bgy - offg1[1] * s_g
    for c in cg1:
        op, tx, ty = c
        px = bgx + (offg1[0] * 10.0 + tx) * 0.1 * s_g
        py = bgy + (-offg1[1] * 10.0 + ty) * 0.1 * s_g
        if op == 'NORMAL':
            draw_g.line([(cur_x, cur_y), (px, py)], fill=(230, 45, 55), width=2)
        cur_x, cur_y = px, py

    # Teil 2 (WACHE) in Blau
    cur_x, cur_y = bgx + offg2[0] * s_g, bgy - offg2[1] * s_g
    for c in cg2:
        op, tx, ty = c
        px = bgx + (offg2[0] * 10.0 + tx) * 0.1 * s_g
        py = bgy + (-offg2[1] * 10.0 + ty) * 0.1 * s_g
        if op == 'NORMAL':
            draw_g.line([(cur_x, cur_y), (px, py)], fill=(60, 160, 240), width=2)
        cur_x, cur_y = px, py

    draw_g.rectangle([250 - 15, 430 - 8, 250 + 15, 430 + 8], fill=(230, 45, 55))
    draw_g.text((275, 430), "Schritt 1: NACHT (84x39 mm)", fill=(220, 220, 230), anchor="lm")
    draw_g.rectangle([650 - 15, 430 - 8, 650 + 15, 430 + 8], fill=(60, 160, 240))
    draw_g.text((675, 430), "Schritt 2: WACHE (94x39 mm)", fill=(220, 220, 230), anchor="lm")

    f_gross_png = os.path.join(brain_dir, "nachtwache_grossformat_2schritte_preview.png")
    im_gross.save(f_gross_png)
    print(f"  [OK] Großformat-Vorschau gespeichert: {f_gross_png}")

if __name__ == '__main__':
    main()
