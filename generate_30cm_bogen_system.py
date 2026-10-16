# -*- coding: utf-8 -*-
"""
StickCore - 30 cm (300 mm) Bogenschrift-System für Janome MC350E (Rahmen A <= 126x110 mm)
Erzeugt den offiziellen 30 cm Feuerwehr-Rückenbogen:
1. OBERER BOGEN: "BERUFSFEUERWEHR KIEL" in 4 Schritten (300 mm Bogenbreite, 20 mm Schrifthöhe)
   - Teil 1: BERUFS (mit Passkreuz 1 nachlaufend)
   - Teil 2: FEUER (mit Passkreuz 1 vorlaufend + Passkreuz 2 nachlaufend)
   - Teil 3: WEHR (mit Passkreuz 2 vorlaufend + Passkreuz 3 nachlaufend)
   - Teil 4: KIEL (mit Passkreuz 3 vorlaufend)
2. UNTERER BOGEN: "NORDWACHE II" im passenden Rückenformat (260 mm Breite, 20 mm Schrifthöhe)
   - Teil 1: NORD (mit Passkreuz 1 nachlaufend)
   - Teil 2: WACHE (mit Passkreuz 1 vorlaufend + Passkreuz 2 nachlaufend)
   - Teil 3: II (mit Passkreuz 2 vorlaufend)
Alle Teildateien halten die Janome Rahmen-A-Grenze (<= 126x110 mm) strikt ein.
"""

import os
import math
import struct
import datetime
from PIL import Image, ImageDraw, ImageFont
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

def make_kettel_outline(pts, width_mm=0.85, pitch_mm=0.30):
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
    """Exportiert Teilschritt zentriert im Rahmen A mit Passkreuzen und Farbstopps."""
    all_pts = [pt for poly in stitch_items for pt in poly]
    if crosshairs_before:
        all_pts.extend(crosshairs_before)
    if crosshairs_after:
        all_pts.extend(crosshairs_after)

    xs = [p[0] for p in all_pts]
    ys = [p[1] for p in all_pts]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    w_mm = max_x - min_x
    h_mm = max_y - min_y
    cx = 0.5 * (min_x + max_x)
    cy = 0.5 * (min_y + max_y)

    if w_mm > width_cap or h_mm > height_cap:
        print(f"  [WARNUNG] Teil überschreitet Rahmen A ({w_mm:.1f}x{h_mm:.1f} mm > {width_cap}x{height_cap} mm): {os.path.basename(path)}")
    else:
        print(f"  [OK] Teil passt perfekt in Rahmen A: {w_mm:.1f}x{h_mm:.1f} mm -> {os.path.basename(path)}")

    cmds = []
    pal = []

    # 1. Vorab-Passkreuze (zum Einrichten auf den vorherigen Schritt)
    if crosshairs_before:
        pal.append(JANOME_SILVER) # Kontrastfaden für Passkreuz
        for ch in crosshairs_before:
            ch_rel_x = ch[0] - cx
            ch_rel_y = ch[1] - cy
            for op, x, y in make_crosshair_stitches(ch_rel_x, ch_rel_y, arm_len=4.0):
                cmds.append((op, int(round(x * 10.0)), -int(round(y * 10.0))))
        # Farbstopp: Maschine stoppt nach Passkreuz!
        last_p = cmds[-1]
        cmds.append(('COLOR_STOP', last_p[1], last_p[2]))

    # 2. Hauptbuchstaben (Janome Tiefrot)
    pal.append(JANOME_DEEP_RED)
    for poly in stitch_items:
        st = make_kettel_outline(poly, width_mm=0.85, pitch_mm=0.30)
        for op, x, y in st:
            cmds.append((op, int(round((x - cx) * 10.0)), -int(round((y - cy) * 10.0))))

    # 3. Nachlauf-Passkreuze (für den nächsten Schritt)
    if crosshairs_after:
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

def render_multihoop_preview(steps_info, out_png, title, subtitle="Mehrfach-Rahmenspannung Janome MC350E (Rahmen A)"):
    im = Image.new('RGB', (1400, 750), (22, 24, 28))
    draw = ImageDraw.Draw(im)

    draw.text((700, 30), title, fill=(245, 245, 250), anchor="mm")
    draw.text((700, 58), subtitle, fill=(160, 165, 180), anchor="mm")

    colors_step = [
        (230, 45, 55),   # Schritt 1: Rot
        (60, 160, 240),  # Schritt 2: Blau
        (245, 180, 40),  # Schritt 3: Gelb/Gold
        (65, 205, 110)   # Schritt 4: Grün
    ]
    scale = 3.8
    base_x, base_y = 700, 380

    for idx, step in enumerate(steps_info):
        col = colors_step[idx % len(colors_step)]
        cmds = step['cmds']
        cx_off, cy_off = step['offset']

        cur_x, cur_y = base_x + cx_off * scale, base_y - cy_off * scale
        for c in cmds:
            op, tx, ty = c
            px = base_x + (cx_off * 10.0 + tx) * 0.1 * scale
            py = base_y + (-cy_off * 10.0 + ty) * 0.1 * scale
            if op == 'NORMAL':
                draw.line([(cur_x, cur_y), (px, py)], fill=col, width=2)
            cur_x, cur_y = px, py

        # Legende
        leg_y = 690
        n_steps = len(steps_info)
        spacing = 1200 // n_steps
        leg_x = 100 + idx * spacing + spacing // 2
        draw.rectangle([leg_x - 15, leg_y - 8, leg_x + 15, leg_y + 8], fill=col)
        draw.text((leg_x + 25, leg_y), step['label'], fill=(220, 220, 230), anchor="lm")

    os.makedirs(os.path.dirname(os.path.abspath(out_png)), exist_ok=True)
    im.save(out_png)

def main():
    print("==================================================================")
    print(" StickCore - 30 cm Bogenschrift-System für Janome MC350E")
    print("==================================================================")

    usb_dir = r"F:\EmbF5\Feuerwehr"
    brain_dir = r"C:\Users\micbu\.gemini\antigravity\brain\e572641d-70d6-45b5-8c91-c97f358a4f6c"
    os.makedirs(usb_dir, exist_ok=True)

    # =========================================================================
    # 1. OBERER BOGEN 30 CM: "BERUFSFEUERWEHR KIEL" IN 4 SCHRITTEN
    # =========================================================================
    print("\n--- 1. Oberer Bogen 30 cm (BERUFSFEUERWEHR KIEL) in 4 Schritten ---")
    chars_top30 = layout_arc_text("berufsfeuerwehr kiel", cap_h_mm=20.0, arc_r_mm=195.0, arc_down=False, letter_sp_mm=1.7)

    all_pts_top = [pt for ch in chars_top30 for poly in ch['polys'] for pt in poly]
    xs_top = [p[0] for p in all_pts_top]
    ys_top = [p[1] for p in all_pts_top]
    w_top = max(xs_top) - min(xs_top)
    h_top = max(ys_top) - min(ys_top)
    print(f"  Gesamter 30-cm-Bogen: {w_top:.1f} mm Breite x {h_top:.1f} mm Wölbungshöhe (Schrifthöhe: 20 mm)")

    # 4 Schritte an natürlichen Wortgrenzen:
    # Schritt 1: 'berufs' (0..5)
    # Schritt 2: 'feuer'  (6..10)
    # Schritt 3: 'wehr'   (11..14)
    # Schritt 4: 'kiel'   (16..19)
    polys_top_p1 = [p for ch in chars_top30[0:6] for p in ch['polys']]
    polys_top_p2 = [p for ch in chars_top30[6:11] for p in ch['polys']]
    polys_top_p3 = [p for ch in chars_top30[11:15] for p in ch['polys']]
    polys_top_p4 = [p for ch in chars_top30[16:20] for p in ch['polys']]

    # Passkreuz 1 (zwischen berufs und feuer)
    ch1_x = 0.5 * (chars_top30[5]['pos'][0] + chars_top30[6]['pos'][0])
    ch1_y = 0.5 * (chars_top30[5]['pos'][1] + chars_top30[6]['pos'][1])

    # Passkreuz 2 (zwischen feuer und wehr)
    ch2_x = 0.5 * (chars_top30[10]['pos'][0] + chars_top30[11]['pos'][0])
    ch2_y = 0.5 * (chars_top30[10]['pos'][1] + chars_top30[11]['pos'][1])

    # Passkreuz 3 (zwischen wehr und kiel)
    ch3_x = 0.5 * (chars_top30[14]['pos'][0] + chars_top30[16]['pos'][0])
    ch3_y = 0.5 * (chars_top30[14]['pos'][1] + chars_top30[16]['pos'][1])

    f_top4_p1 = os.path.join(usb_dir, "01a_OBEN_30cm_4Schritte_Teil1_BERUFS.jef")
    f_top4_p2 = os.path.join(usb_dir, "01b_OBEN_30cm_4Schritte_Teil2_FEUER.jef")
    f_top4_p3 = os.path.join(usb_dir, "01c_OBEN_30cm_4Schritte_Teil3_WEHR.jef")
    f_top4_p4 = os.path.join(usb_dir, "01d_OBEN_30cm_4Schritte_Teil4_KIEL.jef")

    c4_1, d4_1, off4_1 = export_hoop_part(f_top4_p1, polys_top_p1, crosshairs_before=[], crosshairs_after=[(ch1_x, ch1_y)])
    c4_2, d4_2, off4_2 = export_hoop_part(f_top4_p2, polys_top_p2, crosshairs_before=[(ch1_x, ch1_y)], crosshairs_after=[(ch2_x, ch2_y)])
    c4_3, d4_3, off4_3 = export_hoop_part(f_top4_p3, polys_top_p3, crosshairs_before=[(ch2_x, ch2_y)], crosshairs_after=[(ch3_x, ch3_y)])
    c4_4, d4_4, off4_4 = export_hoop_part(f_top4_p4, polys_top_p4, crosshairs_before=[(ch3_x, ch3_y)], crosshairs_after=[])

    render_multihoop_preview([
        {'cmds': c4_1, 'offset': off4_1, 'label': '1. BERUFS (86x65 mm)'},
        {'cmds': c4_2, 'offset': off4_2, 'label': '2. FEUER (78x28 mm)'},
        {'cmds': c4_3, 'offset': off4_3, 'label': '3. WEHR (75x37 mm)'},
        {'cmds': c4_4, 'offset': off4_4, 'label': '4. KIEL (48x47 mm)'}
    ], os.path.join(brain_dir, "feuerwehr_bogen_oben_30cm_4schritte_preview.png"),
       "OBERER BOGEN 30 CM: BERUFSFEUERWEHR KIEL (IN 4 SCHRITTEN)",
       "Gesamtbreite: 300.6 mm | Janome MC350E Rahmen A (<= 126x110 mm) mit 3 Passkreuzen")

    # =========================================================================
    # 2. UNTERER BOGEN: "NORDWACHE II" IN 3 SCHRITTEN (24 CM RÜCKENFORMAT)
    # =========================================================================
    print("\n--- 2. Unterer Bogen (NORDWACHE II) in 3 Schritten (24 cm Rückenformat) ---")
    chars_bot26 = layout_arc_text("nord wache ii", cap_h_mm=20.0, arc_r_mm=185.0, arc_down=True, letter_sp_mm=5.5)

    all_pts_bot = [pt for ch in chars_bot26 for poly in ch['polys'] for pt in poly]
    xs_bot = [p[0] for p in all_pts_bot]
    ys_bot = [p[1] for p in all_pts_bot]
    w_bot = max(xs_bot) - min(xs_bot)
    h_bot = max(ys_bot) - min(ys_bot)
    print(f"  Gesamter unterer Bogen: {w_bot:.1f} mm Breite x {h_bot:.1f} mm Höhe")

    # 3 Schritte:
    # Schritt 1: 'nord'  (0..4 -> 0,1,2,3)
    # Schritt 2: 'wache' (5..10 -> 5,6,7,8,9)
    # Schritt 3: 'ii'    (11..13 -> 11,12)
    polys_bot_p1 = [p for ch in chars_bot26[0:4] for p in ch['polys']]
    polys_bot_p2 = [p for ch in chars_bot26[5:10] for p in ch['polys']]
    polys_bot_p3 = [p for ch in chars_bot26[11:13] for p in ch['polys']]

    # Passkreuz Bot 1 (zwischen nord und wache)
    chb1_x = 0.5 * (chars_bot26[3]['pos'][0] + chars_bot26[5]['pos'][0])
    chb1_y = 0.5 * (chars_bot26[3]['pos'][1] + chars_bot26[5]['pos'][1])

    # Passkreuz Bot 2 (zwischen wache und ii)
    chb2_x = 0.5 * (chars_bot26[9]['pos'][0] + chars_bot26[11]['pos'][0])
    chb2_y = 0.5 * (chars_bot26[9]['pos'][1] + chars_bot26[11]['pos'][1])

    f_bot3_p1 = os.path.join(usb_dir, "03a_UNTEN_30cm_3Schritte_Teil1_NORD.jef")
    f_bot3_p2 = os.path.join(usb_dir, "03b_UNTEN_30cm_3Schritte_Teil2_WACHE.jef")
    f_bot3_p3 = os.path.join(usb_dir, "03c_UNTEN_30cm_3Schritte_Teil3_II.jef")

    cb_1, db_1, offb_1 = export_hoop_part(f_bot3_p1, polys_bot_p1, crosshairs_before=[], crosshairs_after=[(chb1_x, chb1_y)])
    cb_2, db_2, offb_2 = export_hoop_part(f_bot3_p2, polys_bot_p2, crosshairs_before=[(chb1_x, chb1_y)], crosshairs_after=[(chb2_x, chb2_y)])
    cb_3, db_3, offb_3 = export_hoop_part(f_bot3_p3, polys_bot_p3, crosshairs_before=[(chb2_x, chb2_y)], crosshairs_after=[])

    render_multihoop_preview([
        {'cmds': cb_1, 'offset': offb_1, 'label': '1. NORD (66x38 mm)'},
        {'cmds': cb_2, 'offset': offb_2, 'label': '2. WACHE (95x24 mm)'},
        {'cmds': cb_3, 'offset': offb_3, 'label': '3. II (34x36 mm)'}
    ], os.path.join(brain_dir, "feuerwehr_bogen_unten_30cm_3schritte_preview.png"),
       "UNTERER BOGEN GROSSFORMAT: NORDWACHE II (IN 3 SCHRITTEN)",
       f"Gesamtbreite: {w_bot:.1f} mm | Janome MC350E Rahmen A mit 2 Passkreuzen")

    # =========================================================================
    # 3. RÜCKEN-GESAMTANSICHT (30 CM BOGEN + WAPPEN + 30 + UNTERBOGEN)
    # =========================================================================
    print("\n--- 3. Rendere 30 cm Rücken-Gesamtsimulation ---")
    im_back = Image.new('RGB', (1400, 1500), (20, 22, 26))
    draw_b = ImageDraw.Draw(im_back)

    draw_b.text((700, 40), "FEUERWEHR KIEL - 30 CM RÜCKEN-PATCHSET (JANOME MC350E RAHMEN A)", fill=(245, 245, 250), anchor="mm")
    draw_b.text((700, 70), "Oberer Bogen 300 mm (4 Schritte) | Wappen 105 mm | Zahl 30 (105 mm) | Unterer Bogen 260 mm (3 Schritte)", fill=(160, 165, 180), anchor="mm")

    # Oberer Bogen bei Y=200
    scale_sim = 3.6
    cx_sim, cy_sim = 700, 250
    for ch in chars_top30:
        for poly in ch['polys']:
            st = make_kettel_outline(poly, width_mm=0.85, pitch_mm=0.30)
            pts = [(cx_sim + p[1] * scale_sim, cy_sim - p[2] * scale_sim) for p in st if p[0] == 'NORMAL']
            if len(pts) > 1:
                draw_b.line(pts, fill=(235, 45, 55), width=2)

    # Kieler Wappen bei Y=470 (Skalierung auf 89x105 mm)
    try:
        wappen_img = Image.open(os.path.join(brain_dir, "kieler_wappen_vollstick_preview.png"))
        wappen_w = int(89.0 * scale_sim)
        wappen_h = int(105.0 * scale_sim)
        wappen_resized = wappen_img.resize((wappen_w, wappen_h), Image.Resampling.LANCZOS)
        im_back.paste(wappen_resized, (700 - wappen_w // 2, 450 - wappen_h // 2))
    except Exception as e:
        print("  Wappenbild konnte nicht geladen werden:", e)

    # Zahl 30 bei Y=850 (Original mef 105x76 mm)
    try:
        num30_img = Image.open(os.path.join(brain_dir, "feuerwehr_30_mef_applique_preview.png"))
        num30_w = int(105.0 * scale_sim)
        num30_h = int(76.2 * scale_sim)
        num30_resized = num30_img.resize((num30_w, num30_h), Image.Resampling.LANCZOS)
        im_back.paste(num30_resized, (700 - num30_w // 2, 850 - num30_h // 2))
    except Exception as e:
        print("  Zahl 30 Bild konnte nicht geladen werden:", e)

    # Unterer Bogen bei Y=1150
    cy_bot_sim = 1120
    for ch in chars_bot26:
        for poly in ch['polys']:
            st = make_kettel_outline(poly, width_mm=0.85, pitch_mm=0.30)
            pts = [(cx_sim + p[1] * scale_sim, cy_bot_sim - p[2] * scale_sim) for p in st if p[0] == 'NORMAL']
            if len(pts) > 1:
                draw_b.line(pts, fill=(235, 45, 55), width=2)

    # Maßlinien einzeichnen (300 mm Bogenbreite)
    top_w_px = w_top * scale_sim
    draw_b.line([(700 - top_w_px // 2, 100), (700 + top_w_px // 2, 100)], fill=(120, 130, 150), width=1)
    draw_b.line([(700 - top_w_px // 2, 90), (700 - top_w_px // 2, 110)], fill=(120, 130, 150), width=1)
    draw_b.line([(700 + top_w_px // 2, 90), (700 + top_w_px // 2, 110)], fill=(120, 130, 150), width=1)
    draw_b.text((700, 115), f"EXAKT {w_top:.1f} mm (ca. 30 cm)", fill=(200, 210, 230), anchor="mm")

    im_back.save(os.path.join(brain_dir, "gesamt_feuerwehr_30cm_ruecken_preview.png"))
    print(f"  [OK] Rücken-Simulation gespeichert: {os.path.join(brain_dir, 'gesamt_feuerwehr_30cm_ruecken_preview.png')}")
    print("\n[FERTIG] Alle 30-cm-Dateien erfolgreich generiert!")

if __name__ == '__main__':
    main()
