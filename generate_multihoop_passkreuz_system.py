# -*- coding: utf-8 -*-
"""
StickCore - Multi-Hoop Rahmenspannungs-System mit Passkreuzen für Janome MC350E
Ermöglicht große Schriftzüge über 2 bis 3 Rahmenspannungen im Rahmen A (<= 126x110 mm):
1. Bogen Oben (BERUFSFEUERWEHR KIEL):
   - 2 Schritte (14 mm Schrifthöhe, ~215 mm Gesamtbreite)
   - 3 Schritte (16 mm Schrifthöhe, ~250 mm Monumentalbreite)
2. Gerade FEUERWEHR:
   - 2 Schritte (1-Inch College Outline, 25.7 mm Schrifthöhe)
   - 3 Schritte (1.5-Inch College Outline, 38.3 mm Schrifthöhe)
3. Bogen Unten (NORDWACHE II):
   - 2 Schritte (14 mm Schrifthöhe)
Mit exakten Passkreuzen (Fadenkreuz 8x8 mm) und Kontroll-Stopps (0x80 0x01).
"""

import os
import math
import struct
import datetime
from PIL import Image, ImageDraw
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import RecordingPen

# Janome Palette
JANOME_BLACK = 1
JANOME_RED = 2
JANOME_DEEP_RED = 13
JANOME_WHITE = 15
JANOME_BLUE = 7
JANOME_SILVER = 10

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

def make_kettel_outline(pts, width_mm=0.75, pitch_mm=0.30):
    if len(pts) < 2: return []
    L = poly_length(pts)
    steps = max(4, int(round(L / pitch_mm)))
    res = []
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
        kind = 'JUMP' if i == 0 else 'NORMAL'
        res.append((kind, st_x, st_y))
        outer = not outer
    return res

def make_crosshair_stitches(cx, cy, arm_len=4.0):
    """Erzeugt ein exaktes 8x8 mm Fadenkreuz mit Zentrierpunkt."""
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
    header.extend(bytes([k_byte16, 0x00]))
    header.extend(struct.pack('<i', color_count))
    header.extend(struct.pack('<i', count_field))
    header.extend(struct.pack('<i', hoop_code))
    header.extend(struct.pack('<i', -min_x))
    header.extend(struct.pack('<i',  max_y))
    header.extend(struct.pack('<i',  max_x))
    header.extend(struct.pack('<i', -min_y))
    for _ in range(16):
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

# --- Font Vorbereitung ---
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

def export_hoop_part(path, stitch_items, crosshairs_before=[], crosshairs_after=[], width_cap=124.0, height_cap=108.0):
    """
    Exportiert einen Teilschritt zentriert im Rahmen A mit Passkreuzen.
    stitch_items: Liste von Polygonen für die Buchstaben
    crosshairs_before: Liste von (cx, cy) Passkreuzen, die vorab gestickt werden (inkl. Farbstopp)
    crosshairs_after: Liste von (cx, cy) Passkreuzen, die nach den Buchstaben gestickt werden
    """
    # Sammle alle Punkte für gemeinsame Schwerpunkt-Zentrierung
    all_pts = []
    for poly in stitch_items:
        for p in poly:
            all_pts.append(p)
    for ch in (crosshairs_before + crosshairs_after):
        all_pts.append(ch)

    min_x, max_x = min(p[0] for p in all_pts), max(p[0] for p in all_pts)
    min_y, max_y = min(p[1] for p in all_pts), max(p[1] for p in all_pts)

    cx = 0.5 * (min_x + max_x)
    cy = 0.5 * (min_y + max_y)

    w_mm = max_x - min_x
    h_mm = max_y - min_y
    if w_mm > width_cap or h_mm > height_cap:
        raise ValueError(f"Teil ({w_mm:.1f} x {h_mm:.1f} mm) ueberschreitet Rahmen A ({width_cap}x{height_cap} mm)!")

    cmds = []
    pal = []

    # 1. Vorab-Passkreuze (z.B. bei Schritt 2 und 3 zum exakten Einrichten)
    if crosshairs_before:
        pal.append(JANOME_SILVER) # Helle Kontrastfarbe für Fadenkreuz
        for ch in crosshairs_before:
            ch_rel_x = ch[0] - cx
            ch_rel_y = ch[1] - cy
            for op, x, y in make_crosshair_stitches(ch_rel_x, ch_rel_y, arm_len=4.0):
                cmds.append((op, int(round(x * 10.0)), -int(round(y * 10.0))))
        # Farbstopp: Maschine stoppt nach Passkreuz, damit Benutzer Ausrichtung prüfen kann!
        last_p = cmds[-1]
        cmds.append(('COLOR_STOP', last_p[1], last_p[2]))

    # 2. Buchstaben-Stiche (Rot)
    pal.append(JANOME_DEEP_RED)
    for poly in stitch_items:
        st = make_kettel_outline(poly, width_mm=0.85, pitch_mm=0.30)
        for op, x, y in st:
            cmds.append((op, int(round((x - cx) * 10.0)), -int(round((y - cy) * 10.0))))

    # 3. Nachlauf-Passkreuze (z.B. bei Schritt 1 und 2 für den nächsten Schritt)
    if crosshairs_after:
        # Farbstopp vor Passkreuz oder direkt anhängen
        last_p = cmds[-1]
        cmds.append(('COLOR_STOP', last_p[1], last_p[2]))
        pal.append(JANOME_SILVER)
        for ch in crosshairs_after:
            ch_rel_x = ch[0] - cx
            ch_rel_y = ch[1] - cy
            for op, x, y in make_crosshair_stitches(ch_rel_x, ch_rel_y, arm_len=4.0):
                cmds.append((op, int(round(x * 10.0)), -int(round(y * 10.0))))

    write_raw_jef(path, cmds, pal)
    return cmds, (w_mm, h_mm), (cx, cy)

def render_multihoop_preview(steps_info, out_png, title):
    """
    Rendert eine Gesamtansicht, die zeigt wie die Teile per Fadenkreuz zusammenpassen.
    steps_info: Liste von {'cmds': cmds, 'offset': (cx, cy), 'label': 'Teil X'}
    """
    im = Image.new('RGB', (1200, 700), (25, 25, 30))
    draw = ImageDraw.Draw(im)

    draw.text((600, 30), title, fill=(245, 245, 250), anchor="mm")
    draw.text((600, 55), "Mehrfach-Rahmenspannung Janome MC350E (Rahmen A) mit Passkreuz-Ausrichtung", fill=(160, 165, 180), anchor="mm")

    colors_step = [(230, 45, 55), (60, 160, 240), (245, 180, 40)]
    scale = 4.2
    base_x, base_y = 600, 350

    for idx, step in enumerate(steps_info):
        col = colors_step[idx % len(colors_step)]
        cmds = step['cmds']
        cx_off, cy_off = step['offset']

        cur_x, cur_y = base_x + cx_off * scale, base_y - cy_off * scale
        for c in cmds:
            op, tx, ty = c
            # ty in cmds is already -y*10
            px = base_x + (cx_off * 10.0 + tx) * 0.1 * scale
            py = base_y + (-cy_off * 10.0 + ty) * 0.1 * scale
            if op == 'NORMAL':
                draw.line([(cur_x, cur_y), (px, py)], fill=col, width=2)
            cur_x, cur_y = px, py

        # Legende
        leg_y = 640
        leg_x = 250 + idx * 350
        draw.rectangle([leg_x - 15, leg_y - 8, leg_x + 15, leg_y + 8], fill=col)
        draw.text((leg_x + 25, leg_y), step['label'], fill=(220, 220, 230), anchor="lm")

    os.makedirs(os.path.dirname(os.path.abspath(out_png)), exist_ok=True)
    im.save(out_png)

def main():
    print("==================================================================")
    print(" StickCore - Multi-Hoop Rahmenspannungs-System mit Passkreuzen")
    print("==================================================================")

    usb_dir = r"F:\EmbF5\Feuerwehr"
    brain_dir = r"C:\Users\micbu\.gemini\antigravity\brain\e572641d-70d6-45b5-8c91-c97f358a4f6c"
    os.makedirs(usb_dir, exist_ok=True)

    # -------------------------------------------------------------
    # 1. BOGEN OBEN IN 2 SCHRITTEN (14 mm Schrifthöhe, ~215 mm Bogenbreite)
    # -------------------------------------------------------------
    print("\n--- 1. Bogen Oben (BERUFSFEUERWEHR KIEL) in 2 Schritten (14 mm) ---")
    chars_top14 = layout_arc_text("berufsfeuerwehr kiel", cap_h_mm=14.0, arc_r_mm=100.0, arc_down=False, letter_sp_mm=1.8)

    # Teilung:
    # Schritt 1: 'berufsfeuer' (Index 0..10)
    # Schritt 2: 'wehr kiel'   (Index 11..19)
    polys_p1 = [p for ch in chars_top14[:11] for p in ch['polys']]
    polys_p2 = [p for ch in chars_top14[11:] for p in ch['polys']]

    # Passkreuz an der Nahtstelle zwischen 'feuer' und 'wehr':
    p_last1 = chars_top14[10]['pos'] # Ende von feuer
    p_first2 = chars_top14[11]['pos'] # Anfang von wehr
    ch_x1 = 0.5 * (p_last1[0] + p_first2[0])
    ch_y1 = 0.5 * (p_last1[1] + p_first2[1])

    f_top2_p1 = os.path.join(usb_dir, "01a_OBEN_2Schritte_Teil1_Links_BERUFSFEUER.jef")
    f_top2_p2 = os.path.join(usb_dir, "01b_OBEN_2Schritte_Teil2_Rechts_WEHR_KIEL.jef")

    c1, d1, off1 = export_hoop_part(f_top2_p1, polys_p1, crosshairs_before=[], crosshairs_after=[(ch_x1, ch_y1)])
    c2, d2, off2 = export_hoop_part(f_top2_p2, polys_p2, crosshairs_before=[(ch_x1, ch_y1)], crosshairs_after=[])

    print(f"  [OK] Schritt 1 (Links): {d1[0]:.1f} x {d1[1]:.1f} mm -> {f_top2_p1}")
    print(f"  [OK] Schritt 2 (Rechts): {d2[0]:.1f} x {d2[1]:.1f} mm -> {f_top2_p2}")

    render_multihoop_preview([
        {'cmds': c1, 'offset': off1, 'label': 'Schritt 1 (Links): BERUFSFEUER'},
        {'cmds': c2, 'offset': off2, 'label': 'Schritt 2 (Rechts): WEHR KIEL'}
    ], os.path.join(brain_dir, "feuerwehr_bogen_oben_2schritte_preview.png"), "OBERER BOGEN IN 2 SCHRITTEN (14 mm Großformat)")

    # -------------------------------------------------------------
    # 2. BOGEN OBEN IN 3 SCHRITTEN (14 mm Großformat, ~220 mm Bogenbreite)
    # -------------------------------------------------------------
    print("\n--- 2. Bogen Oben (BERUFSFEUERWEHR KIEL) in 3 Schritten (14 mm) ---")
    chars_top16 = layout_arc_text("berufsfeuerwehr kiel", cap_h_mm=14.0, arc_r_mm=100.0, arc_down=False, letter_sp_mm=1.3)

    # 3 Teile:
    # Teil 1: 'berufs'     (0..5)
    # Teil 2: 'feuerwehr'  (6..14)
    # Teil 3: 'kiel'       (16..19)
    polys_3p1 = [p for ch in chars_top16[0:6] for p in ch['polys']]
    polys_3p2 = [p for ch in chars_top16[6:15] for p in ch['polys']]
    polys_3p3 = [p for ch in chars_top16[16:20] for p in ch['polys']]

    # Passkreuz 1 (zwischen berufs und feuerwehr)
    ch1_x = 0.5 * (chars_top16[5]['pos'][0] + chars_top16[6]['pos'][0])
    ch1_y = 0.5 * (chars_top16[5]['pos'][1] + chars_top16[6]['pos'][1])

    # Passkreuz 2 (zwischen feuerwehr und kiel)
    ch2_x = 0.5 * (chars_top16[14]['pos'][0] + chars_top16[16]['pos'][0])
    ch2_y = 0.5 * (chars_top16[14]['pos'][1] + chars_top16[16]['pos'][1])

    f_top3_p1 = os.path.join(usb_dir, "01a_OBEN_3Schritte_Teil1_BERUFS.jef")
    f_top3_p2 = os.path.join(usb_dir, "01b_OBEN_3Schritte_Teil2_FEUERWEHR.jef")
    f_top3_p3 = os.path.join(usb_dir, "01c_OBEN_3Schritte_Teil3_KIEL.jef")

    c3_1, d3_1, off3_1 = export_hoop_part(f_top3_p1, polys_3p1, crosshairs_before=[], crosshairs_after=[(ch1_x, ch1_y)])
    c3_2, d3_2, off3_2 = export_hoop_part(f_top3_p2, polys_3p2, crosshairs_before=[(ch1_x, ch1_y)], crosshairs_after=[(ch2_x, ch2_y)])
    c3_3, d3_3, off3_3 = export_hoop_part(f_top3_p3, polys_3p3, crosshairs_before=[(ch2_x, ch2_y)], crosshairs_after=[])

    print(f"  [OK] Schritt 1 (BERUFS): {d3_1[0]:.1f} x {d3_1[1]:.1f} mm")
    print(f"  [OK] Schritt 2 (FEUERWEHR): {d3_2[0]:.1f} x {d3_2[1]:.1f} mm")
    print(f"  [OK] Schritt 3 (KIEL): {d3_3[0]:.1f} x {d3_3[1]:.1f} mm")

    render_multihoop_preview([
        {'cmds': c3_1, 'offset': off3_1, 'label': 'Schritt 1: BERUFS'},
        {'cmds': c3_2, 'offset': off3_2, 'label': 'Schritt 2: FEUERWEHR'},
        {'cmds': c3_3, 'offset': off3_3, 'label': 'Schritt 3: KIEL'}
    ], os.path.join(brain_dir, "feuerwehr_bogen_oben_3schritte_preview.png"), "OBERER BOGEN IN 3 SCHRITTEN (14 mm Großformat)")

    # -------------------------------------------------------------
    # 3. GERADE FEUERWEHR IN 2 SCHRITTEN (1-INCH COLLEGE OUTLINE)
    # -------------------------------------------------------------
    print("\n--- 3. Gerade FEUERWEHR in 2 Schritten (1-Inch College Outline) ---")
    col_dir = r"C:\Users\micbu\Downloads\mef\College_Outline_part1\JEF\1in"

    from build_complete_filled_wappen_and_upright_college import assemble_text_upright
    letters_f1 = [
        os.path.join(col_dir, "F_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(col_dir, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(col_dir, "U_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(col_dir, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(col_dir, "R_CollegeOutl_1in_NinviaStore.jef"),
    ]
    letters_f2 = [
        os.path.join(col_dir, "W_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(col_dir, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(col_dir, "H_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(col_dir, "R_CollegeOutl_1in_NinviaStore.jef"),
    ]

    cmds_f1, (w_f1, h_f1) = assemble_text_upright(letters_f1, gap_mm=2.5)
    cmds_f2, (w_f2, h_f2) = assemble_text_upright(letters_f2, gap_mm=2.5)

    # Passkreuz bei x = rechter Rand von FEUER, y = 0
    # Teil 1: FEUER zentriert + Passkreuz rechts
    cmds_f1_with_ch = list(cmds_f1)
    ch_right_x = int(round((w_f1 * 0.5 + 4.0) * 10.0))
    cmds_f1_with_ch.append(('COLOR_STOP', cmds_f1_with_ch[-1][1], cmds_f1_with_ch[-1][2]))
    for op, x, y in make_crosshair_stitches(ch_right_x, 0, arm_len=3.5 * 10.0):
        cmds_f1_with_ch.append((op, int(round(x)), int(round(y))))

    f_fw2_p1 = os.path.join(usb_dir, "01a_FEUERWEHR_2Schritte_Teil1_FEUER_1in.jef")
    write_raw_jef(f_fw2_p1, cmds_f1_with_ch, [JANOME_DEEP_RED, JANOME_SILVER])

    # Teil 2: Passkreuz links vorab + WEHR zentriert
    cmds_f2_with_ch = []
    ch_left_x = int(round((-w_f2 * 0.5 - 4.0) * 10.0))
    for op, x, y in make_crosshair_stitches(ch_left_x, 0, arm_len=3.5 * 10.0):
        cmds_f2_with_ch.append((op, int(round(x)), int(round(y))))
    cmds_f2_with_ch.append(('COLOR_STOP', ch_left_x, 0))
    cmds_f2_with_ch.extend(cmds_f2)

    f_fw2_p2 = os.path.join(usb_dir, "01b_FEUERWEHR_2Schritte_Teil2_WEHR_1in.jef")
    write_raw_jef(f_fw2_p2, cmds_f2_with_ch, [JANOME_SILVER, JANOME_DEEP_RED])
    print(f"  [OK] FEUERWEHR Schritt 1 (FEUER): {w_f1:.1f} x {h_f1:.1f} mm")
    print(f"  [OK] FEUERWEHR Schritt 2 (WEHR): {w_f2:.1f} x {h_f2:.1f} mm")

    render_multihoop_preview([
        {'cmds': cmds_f1_with_ch, 'offset': (-58.0, 0), 'label': 'Schritt 1: FEUER (mit Passkreuz)'},
        {'cmds': cmds_f2_with_ch, 'offset': ( 58.0, 0), 'label': 'Schritt 2: WEHR (mit Passkreuz)'}
    ], os.path.join(brain_dir, "feuerwehr_college_2schritte_preview.png"), "FEUERWEHR (1-INCH) IN 2 SCHRITTEN MIT PASSKREUZ")

    # -------------------------------------------------------------
    # 4. BOGEN UNTEN IN 2 SCHRITTEN (14 mm Schrifthöhe)
    # -------------------------------------------------------------
    print("\n--- 4. Bogen Unten (NORDWACHE II) in 2 Schritten (14 mm) ---")
    chars_bot14 = layout_arc_text("nordwache ii", cap_h_mm=14.0, arc_r_mm=95.0, arc_down=True, letter_sp_mm=1.8)

    # Teilung: 'nord' (0..3) und 'wache ii' (4..11)
    polys_b1 = [p for ch in chars_bot14[:4] for p in ch['polys']]
    polys_b2 = [p for ch in chars_bot14[4:] for p in ch['polys']]

    ch_b_x = 0.5 * (chars_bot14[3]['pos'][0] + chars_bot14[4]['pos'][0])
    ch_b_y = 0.5 * (chars_bot14[3]['pos'][1] + chars_bot14[4]['pos'][1])

    f_bot2_p1 = os.path.join(usb_dir, "03a_UNTEN_2Schritte_Teil1_Links_NORD.jef")
    f_bot2_p2 = os.path.join(usb_dir, "03b_UNTEN_2Schritte_Teil2_Rechts_WACHE_II.jef")

    cb1, db1, ob1 = export_hoop_part(f_bot2_p1, polys_b1, crosshairs_before=[], crosshairs_after=[(ch_b_x, ch_b_y)])
    cb2, db2, ob2 = export_hoop_part(f_bot2_p2, polys_b2, crosshairs_before=[(ch_b_x, ch_b_y)], crosshairs_after=[])

    print(f"  [OK] Schritt 1 (NORD): {db1[0]:.1f} x {db1[1]:.1f} mm")
    print(f"  [OK] Schritt 2 (WACHE II): {db2[0]:.1f} x {db2[1]:.1f} mm")

    render_multihoop_preview([
        {'cmds': cb1, 'offset': ob1, 'label': 'Schritt 1: NORD'},
        {'cmds': cb2, 'offset': ob2, 'label': 'Schritt 2: WACHE II'}
    ], os.path.join(brain_dir, "feuerwehr_bogen_unten_2schritte_preview.png"), "UNTERER BOGEN IN 2 SCHRITTEN (14 mm Großformat)")

    print("\n=== ALLE MULTI-HOOP DATEIEN MIT PASSKREUZEN ERFOLGREICH GENERIERT! ===")

if __name__ == '__main__':
    main()
