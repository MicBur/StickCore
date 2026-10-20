# -*- coding: utf-8 -*-
"""
StickCore - Komplett-Projekt-Export für Janome MC350E USB-Stick
Erstellt das gesamte Rücken-/Ärmel-Projekt mit Fadenkreuzen (Passkreuzen)
im 8.3 DOS-Dateinamen-Format für Rahmen A (126 x 110 mm):

1. OBERER BOGEN: "BERUFSFEUERWEHR KIEL" (24.0 mm Varsity, 3D-Plastisch, +Fadenkreuz)
   - BF30_1.JEF  (Schritt 1: BERUFS + Passkreuz 1 nachlaufend)
   - BF30_2.JEF  (Schritt 2: Passkreuz 1 vorlaufend + FEUER + Passkreuz 2 nachlaufend)
   - BF30_3.JEF  (Schritt 3: Passkreuz 2 vorlaufend + WEHR + Passkreuz 3 nachlaufend)
   - BF30_4.JEF  (Schritt 4: Passkreuz 3 vorlaufend + KIEL)
   - BF30_ALL.JEF (Komplett-Vorschau Oberer Bogen)

2. MITTE / ÄRMEL-WAPPEN:
   - WWWAP.JEF   (Ärmel-/Brustwappen 66.2 x 70.3 mm, 15.126 Stiche, 3 Farben)
   - KIEL_VOL.JEF, KIEL_KTL.JEF, KIEL_BLK.JEF, 30_MITTE.JEF

3. UNTERER GEGENBOGEN: "NACHTWACHE II" (24.0 mm Varsity, 3D-Plastisch, +Fadenkreuz)
   - NW30_1.JEF  (Schritt 1: NACHT + Passkreuz 1 nachlaufend)
   - NW30_2.JEF  (Schritt 2: Passkreuz 1 vorlaufend + WACH + Passkreuz 2 nachlaufend)
   - NW30_3.JEF  (Schritt 3: Passkreuz 2 vorlaufend + E II)
   - NW30_ALL.JEF (Komplett-Vorschau Unterer Gegenbogen NACHTWACHE II)

   Zusätzlich als Variante mit arabischer Ziffer "NACHTWACHE 2":
   - NW2_1.JEF   (Schritt 1: NACHT + Passkreuz 1 nachlaufend)
   - NW2_2.JEF   (Schritt 2: Passkreuz 1 vorlaufend + WACH + Passkreuz 2 nachlaufend)
   - NW2_3.JEF   (Schritt 3: Passkreuz 2 vorlaufend + E 2)
   - NW2_ALL.JEF (Komplett-Vorschau Unterer Gegenbogen NACHTWACHE 2)

Zielorte:
- F:\\ (Wurzelverzeichnis USB-Stick)
- F:\\fw\\
- F:\\EmbF5\\fw\\
- F:\\EmbF5\\MyDesign\\
- F:\\EMB\\MyDesign\\
- F:\\MyDesign\\
- E:\\StickCore\\output\\fw\\
- E:\\StickCore\\output\\EmbF5\\MyDesign\\
"""

import os
import math
import shutil
import struct
import datetime
from PIL import Image, ImageDraw
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import RecordingPen

JANOME_DEEP_RED = 13   # Janome Katalogcode für Feuerwehrrot

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

def make_secure_crosshair_stitches(cx, cy, arm_len=4.0, step_mm=1.0):
    """
    Erzeugt ein vibrations- und zugfest verriegeltes Passkreuz (8x8 mm).
    - Kleine 1.0 mm Steppstiche (zieht sich nicht mehr heraus!)
    - 3-fache Start- und End-Verriegelung im Zentrum (Tie-in / Tie-off)
    - Doppelter Hin- und Rückweg auf X- und Y-Achse
    """
    cmds = []
    cmds.append(('JUMP', cx, cy))

    # 1. Start-Verriegelung (3 Mikrostiche 0.4 mm im Zentrum)
    cmds.append(('NORMAL', cx + 0.4, cy))
    cmds.append(('NORMAL', cx - 0.4, cy))
    cmds.append(('NORMAL', cx, cy))

    steps = max(2, int(round(arm_len / step_mm)))
    actual_step = arm_len / steps

    # 2. X-Achse: Zentrum -> +X -> -X -> Zentrum in 1.0 mm Schritten
    for s in range(1, steps + 1):
        cmds.append(('NORMAL', cx + s * actual_step, cy))
    for s in range(steps - 1, -steps - 1, -1):
        cmds.append(('NORMAL', cx + s * actual_step, cy))
    for s in range(-steps + 1, 1):
        cmds.append(('NORMAL', cx + s * actual_step, cy))

    # 3. Y-Achse: Zentrum -> +Y -> -Y -> Zentrum in 1.0 mm Schritten
    for s in range(1, steps + 1):
        cmds.append(('NORMAL', cx, cy + s * actual_step))
    for s in range(steps - 1, -steps - 1, -1):
        cmds.append(('NORMAL', cx, cy + s * actual_step))
    for s in range(-steps + 1, 1):
        cmds.append(('NORMAL', cx, cy + s * actual_step))

    # 4. End-Verriegelung (3 Mikrostiche im Zentrum)
    cmds.append(('NORMAL', cx, cy + 0.4))
    cmds.append(('NORMAL', cx, cy - 0.4))
    cmds.append(('NORMAL', cx, cy))

    return cmds

def write_verified_jef(path, raw_commands, palette_codes, hoop_code=0):
    """
    Schreibt eine 100% standardkonforme Janome MC350E JEF-Datei.
    """
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

    # Design-Abschluss: End-Befehl
    body.extend(bytes([0x80, 0x10, 0x00, 0x00]))
    count_field += 1

    header = bytearray()
    header.extend(struct.pack('<i', k_fixed_header + color_count * 8))  # 0x00
    header.extend(struct.pack('<i', k_version))                         # 0x04
    date_str = datetime.datetime.now().strftime("%Y%m%d%H%M%S").encode('ascii').ljust(14, b'0')[:14]
    header.extend(date_str)                                             # 0x08..0x15
    header.append(k_byte16)                                             # 0x16 (0x64)
    header.append(0x00)                                                 # 0x17 (0x00)
    header.extend(struct.pack('<i', color_count))                       # 0x18
    header.extend(struct.pack('<i', count_field))                       # 0x1C
    header.extend(struct.pack('<i', hoop_code))                         # 0x20
    header.extend(struct.pack('<i', -min_x))                            # 0x24 distLeft
    header.extend(struct.pack('<i',  max_y))                            # 0x28 distTop
    header.extend(struct.pack('<i',  max_x))                            # 0x2C distRight
    header.extend(struct.pack('<i', -min_y))                            # 0x30 distBottom
    for _ in range(16):                                                 # 0x34..0x73 (16 x -1)
        header.extend(struct.pack('<i', -1))
    for code in palette_codes:                                          # 0x74
        header.extend(struct.pack('<i', code))
    for _ in palette_codes:
        header.extend(struct.pack('<i', k_color_attrib))

    parent = os.path.dirname(os.path.abspath(path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, 'wb') as f:
        f.write(header)
        f.write(body)
    return True

# Font laden
font = TTFont('E:/StickCore/assets/fonts/varsity_regular.ttf')
glyph_set = font.getGlyphSet()
hmtx = font['hmtx']
cmap = font.getBestCmap()

def get_glyph_conts(ch):
    gname = cmap.get(ord(ch))
    if not gname or gname not in glyph_set:
        return [], 0.0
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
    # In varsity_regular.ttf haben Ziffern ('0'..'9') 3 Doppel-Konturen;
    # Kontur 2 (verschoben um -39,-39) hat exakt dieselbe y=0..622 Einzelkontur-Höhe wie 'a'..'z'.
    if ch.isdigit() and len(conts) >= 3:
        single_cont = [(x - 39.0, y - 39.0) for (x, y) in conts[2]]
        return [single_cont], adv - 78.0
    return conts, adv

def layout_arc_text(text, cap_h_mm=24.0, arc_r_mm=210.0, arc_down=False, letter_sp_mm=1.5):
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
        left_s = cursor_mm
        right_s = cursor_mm + w_mm
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
            'left_s': left_s,
            'right_s': right_s,
            'center_s': center_s,
            'pos': (X, Y),
            'rot': rot,
            'polys': poly_list
        })
    return characters

def get_gap_pos(c_left, c_right, arc_r_mm=234.0, arc_down=True):
    """Berechnet den exakten Mittelpunkt der Lücke zwischen zwei Buchstaben auf der Bogen-Grundlinie."""
    gap_s = 0.5 * (c_left['right_s'] + c_right['left_s'])
    theta = gap_s / arc_r_mm
    if not arc_down:
        return (arc_r_mm * math.sin(theta), arc_r_mm * math.cos(theta) - arc_r_mm)
    else:
        return (arc_r_mm * math.sin(theta), arc_r_mm - arc_r_mm * math.cos(theta))

def make_kettel_outline(pts, width_mm=2.4, pitch_mm=0.35):
    """Erzeugt einen gleichmäßigen Satinstich / Zickzack-Kettelrand entlang eines Pfades."""
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

def build_plastic_char_stitches(char_item):
    """
    Stickt jeden Buchstaben als Varsity-Outline mit doppelter Lage für massiven 3D-Relief-Effekt:
    1. Pass 1: Vorstick / Unterleger-Raupe (Breite 2.4 mm, Pitch 0.48 mm)
    2. Pass 2: Deckraupe mit größerer Stichweite (Breite 3.5 mm, Stichabstand 0.34 mm)
    """
    cmds = []
    for poly in char_item['polys']:
        st_under = make_kettel_outline(poly, width_mm=2.4, pitch_mm=0.48)
        cmds.extend(st_under)
        st_cover = make_kettel_outline(poly, width_mm=3.5, pitch_mm=0.34)
        cmds.extend(st_cover)
    return cmds

def build_part_commands(chars_in_part, crosshairs_before=[], crosshairs_after=[]):
    """
    Erzeugt die vollständige Stichfolge für einen Teilschritt:
    - Vorlaufende Passkreuze (1.0 mm Stiche + Mittenverriegelung)
    - Buchstabe für Buchstabe plastisch: 2.4 mm Unterleger + 3.5 mm Deckraupe (0.34 mm)
    - Nachlaufende Passkreuze
    - ALLES in 1 Garnfarbe (Feuerwehrrot, kein Farbwechsel)
    """
    uncentered_cmds = []

    for ch in crosshairs_before:
        for op, x, y in make_secure_crosshair_stitches(ch[0], ch[1], arm_len=4.0, step_mm=1.0):
            uncentered_cmds.append((op, x, y))

    for c in chars_in_part:
        if not c['polys']:
            continue
        char_raw_stitches = build_plastic_char_stitches(c)
        for op, x, y in char_raw_stitches:
            uncentered_cmds.append((op, x, y))

    for ch in crosshairs_after:
        for op, x, y in make_secure_crosshair_stitches(ch[0], ch[1], arm_len=4.0, step_mm=1.0):
            uncentered_cmds.append((op, x, y))

    norm_xs = [c[1] for c in uncentered_cmds if c[0] == 'NORMAL']
    norm_ys = [c[2] for c in uncentered_cmds if c[0] == 'NORMAL']
    min_x, max_x = min(norm_xs), max(norm_xs)
    min_y, max_y = min(norm_ys), max(norm_ys)
    w_mm = max_x - min_x
    h_mm = max_y - min_y
    cx = 0.5 * (min_x + max_x)
    cy = 0.5 * (min_y + max_y)

    final_cmds = []
    for op, x, y in uncentered_cmds:
        final_cmds.append((op, int(round((x - cx) * 10.0)), int(round((y - cy) * 10.0))))

    pal = [JANOME_DEEP_RED]
    return final_cmds, pal, (w_mm, h_mm), (cx, cy)

def parse_jef_stitches(jef_path):
    with open(jef_path, 'rb') as f:
        data = f.read()
    st_offset, = struct.unpack('<I', data[0:4])
    color_count, = struct.unpack('<I', data[24:28])
    pal_codes = [struct.unpack('<I', data[0x74 + i*4 : 0x74 + (i+1)*4])[0] for i in range(color_count)]
    pos = st_offset; x, y = 0, 0; pts = []; color_idx = 0
    while pos < len(data):
        b0 = data[pos]; b1 = data[pos+1]; pos += 2
        if b0 == 0x80:
            if b1 == 0x01:
                dx = struct.unpack('b', bytes([data[pos]]))[0]
                dy = struct.unpack('b', bytes([data[pos+1]]))[0]
                pos += 2; x += dx; y += dy
                color_idx += 1
                pts.append(('COLOR_STOP', x, y, color_idx))
            elif b1 == 0x02:
                dx = struct.unpack('b', bytes([data[pos]]))[0]
                dy = struct.unpack('b', bytes([data[pos+1]]))[0]
                pos += 2; x += dx; y += dy
                pts.append(('JUMP', x, y, color_idx))
            elif b1 == 0x10:
                break
        else:
            dx = struct.unpack('b', bytes([b0]))[0]
            dy = struct.unpack('b', bytes([b1]))[0]
            x += dx; y += dy
            pts.append(('NORMAL', x, y, color_idx))
    return pts, pal_codes

def main():
    print("=========================================================================")
    print(" StickCore - Komplett-Projekt Export (Oberbogen + Wappen + Gegenbogen)")
    print("=========================================================================")

    potential_dirs = [
        r"F:\\",
        r"F:\fw",
        r"F:\EmbF5\fw",
        r"F:\EmbF5\MyDesign",
        r"F:\Embf5\MyDesign",
        r"F:\EMB\MyDesign",
        r"F:\MyDesign",
        r"E:\StickCore\output\fw",
        r"E:\StickCore\output\EmbF5\MyDesign"
    ]
    target_dirs = []
    seen_norm = set()
    for d in potential_dirs:
        drive = os.path.splitdrive(d)[0]
        if not drive or os.path.exists(drive + "\\"):
            os.makedirs(d, exist_ok=True)
            norm_d = os.path.normcase(os.path.abspath(d))
            if norm_d not in seen_norm:
                seen_norm.add(norm_d)
                target_dirs.append(d)
        else:
            print(f"  [INFO] Laufwerk {drive} aktuell nicht verbunden. Überspringe {d}.")

    # Descriptive folders (where long filenames are also saved for PC clarity)
    long_name_dirs = [d for d in target_dirs if os.path.basename(os.path.normpath(d)).lower() == 'fw']

    # =========================================================================
    # 1. OBERER BOGEN: "BERUFSFEUERWEHR KIEL" (24.0 mm, R=210 mm, arc_down=False)
    # =========================================================================
    chars_top30 = layout_arc_text(
        "berufsfeuerwehr kiel",
        cap_h_mm=24.0,
        arc_r_mm=210.0,
        arc_down=False,
        letter_sp_mm=1.5
    )

    top_part1 = chars_top30[0:6]    # BERUFS
    top_part2 = chars_top30[6:11]   # FEUER
    top_part3 = chars_top30[11:15]  # WEHR
    top_part4 = chars_top30[16:20]  # KIEL

    top_ch1 = (
        0.5 * (chars_top30[5]['pos'][0] + chars_top30[6]['pos'][0]),
        0.5 * (chars_top30[5]['pos'][1] + chars_top30[6]['pos'][1])
    )
    top_ch2 = (
        0.5 * (chars_top30[10]['pos'][0] + chars_top30[11]['pos'][0]),
        0.5 * (chars_top30[10]['pos'][1] + chars_top30[11]['pos'][1])
    )
    top_ch3 = (
        0.5 * (chars_top30[14]['pos'][0] + chars_top30[16]['pos'][0]),
        0.5 * (chars_top30[14]['pos'][1] + chars_top30[16]['pos'][1])
    )

    top_steps = [
        ("BF30_1.JEF", "01_OBEN_Bogen_Berufsfeuerwehr_Kiel_Teil_1_BERUFS.JEF", top_part1, [], [top_ch1], "1. BERUFS", 0),
        ("BF30_2.JEF", "01_OBEN_Bogen_Berufsfeuerwehr_Kiel_Teil_2_FEUER.JEF",  top_part2, [top_ch1], [top_ch2], "2. FEUER", 0),
        ("BF30_3.JEF", "01_OBEN_Bogen_Berufsfeuerwehr_Kiel_Teil_3_WEHR.JEF",   top_part3, [top_ch2], [top_ch3], "3. WEHR", 0),
        ("BF30_4.JEF", "01_OBEN_Bogen_Berufsfeuerwehr_Kiel_Teil_4_KIEL.JEF",   top_part4, [top_ch3], [], "4. KIEL", 0),
    ]

    print("\n--- [1/3] OBERER BOGEN: BERUFSFEUERWEHR KIEL (24 mm Varsity + Fadenkreuz) ---")
    top_offsets = []
    for fname, long_name, ch_list, ch_before, ch_after, label, h_code in top_steps:
        cmds, pal, dims, center_xy = build_part_commands(ch_list, ch_before, ch_after)
        top_offsets.append(center_xy)
        fits = "PASST (Rahmen A)" if (dims[0] <= 126.0 and dims[1] <= 110.0) else "GROSS"
        print(f"  {fname:12s} ({label:12s}): {dims[0]:5.1f} x {dims[1]:5.1f} mm | {len(cmds):4d} Stiche | {fits}")
        for d in target_dirs:
            write_verified_jef(os.path.join(d, fname), cmds, pal, hoop_code=h_code)
        for d in long_name_dirs:
            write_verified_jef(os.path.join(d, long_name), cmds, pal, hoop_code=h_code)

    # Komplett-Vorschau Oberer Bogen (BF30_ALL.JEF)
    cmds_top_all, pal_top_all, dims_top_all, _ = build_part_commands(
        [c for c in chars_top30 if c['polys']], [], [top_ch1, top_ch2, top_ch3]
    )
    for d in target_dirs:
        write_verified_jef(os.path.join(d, "BF30_ALL.JEF"), cmds_top_all, pal_top_all, hoop_code=2)

    # =========================================================================
    # 2. UNTERER GEGENBOGEN: "NACHTWACHE II" (24.0 mm, R=234 mm, arc_down=True)
    #    Konzentrisch zum oberen Bogen (Oberkante Buchstaben R=210 mm, Grundlinie R=234 mm)
    # =========================================================================
    chars_bot_ii = layout_arc_text(
        "nachtwache ii",
        cap_h_mm=24.0,
        arc_r_mm=234.0,
        arc_down=True,
        letter_sp_mm=4.2
    )

    bot_ii_part1 = chars_bot_ii[0:5]                           # NACHT
    bot_ii_part2 = chars_bot_ii[5:9]                           # WACH
    bot_ii_part3 = [chars_bot_ii[9], chars_bot_ii[11], chars_bot_ii[12]]  # E II

    bot_ii_ch1 = get_gap_pos(chars_bot_ii[4], chars_bot_ii[5], arc_r_mm=234.0, arc_down=True)
    bot_ii_ch2 = get_gap_pos(chars_bot_ii[8], chars_bot_ii[9], arc_r_mm=234.0, arc_down=True)

    bot_ii_steps = [
        ("NW30_1.JEF", "03_UNTEN_Gegenbogen_Nachtwache_II_Teil_1_NACHT.JEF", bot_ii_part1, [], [bot_ii_ch1], "1. NACHT", 0),
        ("NW30_2.JEF", "03_UNTEN_Gegenbogen_Nachtwache_II_Teil_2_WACH.JEF",  bot_ii_part2, [bot_ii_ch1], [bot_ii_ch2], "2. WACH", 0),
        ("NW30_3.JEF", "03_UNTEN_Gegenbogen_Nachtwache_II_Teil_3_E_II.JEF",  bot_ii_part3, [bot_ii_ch2], [], "3. E II", 0),
    ]

    print("\n--- [2/3] UNTERER GEGENBOGEN: NACHTWACHE II (24 mm Varsity + Fadenkreuz) ---")
    bot_ii_offsets = []
    for fname, long_name, ch_list, ch_before, ch_after, label, h_code in bot_ii_steps:
        cmds, pal, dims, center_xy = build_part_commands(ch_list, ch_before, ch_after)
        bot_ii_offsets.append(center_xy)
        fits = "PASST (Rahmen A)" if (dims[0] <= 126.0 and dims[1] <= 110.0) else "GROSS"
        print(f"  {fname:12s} ({label:12s}): {dims[0]:5.1f} x {dims[1]:5.1f} mm | {len(cmds):4d} Stiche | {fits}")
        for d in target_dirs:
            write_verified_jef(os.path.join(d, fname), cmds, pal, hoop_code=h_code)
        for d in long_name_dirs:
            write_verified_jef(os.path.join(d, long_name), cmds, pal, hoop_code=h_code)

    # Komplett-Vorschau Unterer Gegenbogen NACHTWACHE II (NW30_ALL.JEF)
    cmds_bot_ii_all, pal_bot_ii_all, dims_bot_ii_all, _ = build_part_commands(
        [c for c in chars_bot_ii if c['polys']], [], [bot_ii_ch1, bot_ii_ch2]
    )
    for d in target_dirs:
        write_verified_jef(os.path.join(d, "NW30_ALL.JEF"), cmds_bot_ii_all, pal_bot_ii_all, hoop_code=2)

    # Zusätzlich "NACHTWACHE 2" (mit arabischer 2) als NW2_1..3.JEF bereitstellen
    chars_bot_2 = layout_arc_text(
        "nachtwache 2",
        cap_h_mm=24.0,
        arc_r_mm=234.0,
        arc_down=True,
        letter_sp_mm=4.2
    )
    bot_2_part1 = chars_bot_2[0:5]                 # NACHT
    bot_2_part2 = chars_bot_2[5:9]                 # WACH
    bot_2_part3 = [chars_bot_2[9], chars_bot_2[11]] # E 2
    bot_2_ch1 = get_gap_pos(chars_bot_2[4], chars_bot_2[5], arc_r_mm=234.0, arc_down=True)
    bot_2_ch2 = get_gap_pos(chars_bot_2[8], chars_bot_2[9], arc_r_mm=234.0, arc_down=True)
    bot_2_steps = [
        ("NW2_1.JEF", "03_UNTEN_Gegenbogen_Nachtwache_2_Teil_1_NACHT.JEF", bot_2_part1, [], [bot_2_ch1], "1. NACHT", 0),
        ("NW2_2.JEF", "03_UNTEN_Gegenbogen_Nachtwache_2_Teil_2_WACH.JEF",  bot_2_part2, [bot_2_ch1], [bot_2_ch2], "2. WACH", 0),
        ("NW2_3.JEF", "03_UNTEN_Gegenbogen_Nachtwache_2_Teil_3_E_2.JEF",   bot_2_part3, [bot_2_ch2], [], "3. E 2", 0),
    ]
    print("\n--- [Optional] UNTERER GEGENBOGEN: NACHTWACHE 2 (Arabische 2) ---")
    for fname, long_name, ch_list, ch_before, ch_after, label, h_code in bot_2_steps:
        cmds, pal, dims, _ = build_part_commands(ch_list, ch_before, ch_after)
        print(f"  {fname:12s} ({label:12s}): {dims[0]:5.1f} x {dims[1]:5.1f} mm | {len(cmds):4d} Stiche")
        for d in target_dirs:
            write_verified_jef(os.path.join(d, fname), cmds, pal, hoop_code=h_code)
        for d in long_name_dirs:
            write_verified_jef(os.path.join(d, long_name), cmds, pal, hoop_code=h_code)

    cmds_bot_2_all, pal_bot_2_all, _, _ = build_part_commands(
        [c for c in chars_bot_2 if c['polys']], [], [bot_2_ch1, bot_2_ch2]
    )
    for d in target_dirs:
        write_verified_jef(os.path.join(d, "NW2_ALL.JEF"), cmds_bot_2_all, pal_bot_2_all, hoop_code=2)

    # =========================================================================
    # 3. MITTE / ÄRMEL-WAPPEN: WWWAP.JEF + KIEL WAPPEN + 30 MITTE KOPIEREN
    # =========================================================================
    print("\n--- [3/3] ÄRMEL- UND BRUST-WAPPEN (WWWAP.JEF & MITTE-DATEIEN) SYNCHRONISIEREN ---")
    wappen_files = [
        (r"E:\StickCore\output\fw\WWWAP.JEF", "WWWAP.JEF", "02_MITTE_Aermelwappen_WWWAP.JEF"),
        (r"E:\StickCore\output\fw\KIEL_VOL.JEF", "KIEL_VOL.JEF", None),
        (r"E:\StickCore\output\fw\KIEL_KTL.JEF", "KIEL_KTL.JEF", None),
        (r"E:\StickCore\output\fw\KIEL_BLK.JEF", "KIEL_BLK.JEF", None),
        (r"E:\StickCore\output\fw\30_MITTE.JEF", "30_MITTE.JEF", None),
    ]
    for src_path, short_name, extra_long_name in wappen_files:
        if os.path.exists(src_path):
            for d in target_dirs:
                dst = os.path.join(d, short_name)
                if os.path.normcase(os.path.abspath(src_path)) != os.path.normcase(os.path.abspath(dst)):
                    shutil.copy2(src_path, dst)
            if extra_long_name:
                for d in long_name_dirs:
                    dst_long = os.path.join(d, extra_long_name)
                    if os.path.normcase(os.path.abspath(src_path)) != os.path.normcase(os.path.abspath(dst_long)):
                        shutil.copy2(src_path, dst_long)
            print(f"  [OK] {short_name} -> in alle {len(target_dirs)} Zielordner (inkl. USB-Stick) kopiert.")

    # =========================================================================
    # 4. VORSCHAU-BILDER DES GESAMTEN PROJEKTS RENDERN
    # =========================================================================
    brain_dir = r"C:\Users\micbu\.gemini\antigravity\brain\e572641d-70d6-45b5-8c91-c97f358a4f6c"
    os.makedirs(brain_dir, exist_ok=True)
    src_verify_dir = r"E:\StickCore\output\fw"

    # A) Detail-Vorschau Gegenbogen NACHTWACHE II
    im_nw = Image.new('RGB', (1600, 720), (20, 22, 26))
    draw_nw = ImageDraw.Draw(im_nw)
    draw_nw.text((800, 30), "UNTERER GEGENBOGEN (24 MM VARSITY): NACHTWACHE II", fill=(245, 245, 250), anchor="mm")
    draw_nw.text((800, 58), "Exakt gleiche Schrift & Größe wie BERUFSFEUERWEHR KIEL | 3D-Plastisch (2.4mm + 3.5mm @ 0.34mm) | Verriegeltes 1mm Fadenkreuz", fill=(160, 165, 180), anchor="mm")

    colors_nw = [
        (235, 45, 55),   # 1. NACHT (Rot)
        (45, 165, 245),  # 2. WACH (Blau)
        (245, 185, 35),  # 3. E II (Gold)
    ]
    scale_nw = 4.2
    base_nw_x, base_nw_y = 800, 460

    for idx, (fname, _, _, _, _, lbl, _) in enumerate(bot_ii_steps):
        col = colors_nw[idx]
        jef_file = os.path.join(src_verify_dir, fname)
        st_pts, _ = parse_jef_stitches(jef_file)
        cx_off, cy_off = bot_ii_offsets[idx]

        for i in range(len(st_pts) - 1):
            op1, x1, y1, _ = st_pts[i]
            op2, x2, y2, _ = st_pts[i+1]
            if op2 == 'NORMAL':
                p1_scr = (base_nw_x + (cx_off * 10.0 + x1) * 0.1 * scale_nw,
                          base_nw_y - (cy_off * 10.0 + y1) * 0.1 * scale_nw)
                p2_scr = (base_nw_x + (cx_off * 10.0 + x2) * 0.1 * scale_nw,
                          base_nw_y - (cy_off * 10.0 + y2) * 0.1 * scale_nw)
                draw_nw.line([p1_scr, p2_scr], fill=col, width=2)

        leg_x = 240 + idx * 420
        draw_nw.rectangle([leg_x - 15, 660 - 8, leg_x + 15, 660 + 8], fill=col)
        draw_nw.text((leg_x + 25, 660), f"{fname}: {lbl}", fill=(220, 220, 230), anchor="lm")

    # Markiere die beiden Passkreuze im Bild
    for ch_idx, (chx, chy) in enumerate([bot_ii_ch1, bot_ii_ch2], start=1):
        px = base_nw_x + chx * scale_nw
        py = base_nw_y - chy * scale_nw
        r_mark = 22
        draw_nw.ellipse([px - r_mark, py - r_mark, px + r_mark, py + r_mark], outline=(80, 255, 120), width=2)
        draw_nw.text((px, py + 34), f"Fadenkreuz {ch_idx}", fill=(80, 255, 120), anchor="mm")

    out_nw_png = os.path.join(brain_dir, "fw_nw30_gegenbogen_preview.png")
    im_nw.save(out_nw_png)

    # B) Gesamtprojekt-Übersicht (Oberer Bogen + WWWAP.JEF Wappen + Unterer Gegenbogen NACHTWACHE II)
    im_proj = Image.new('RGB', (1600, 1300), (18, 20, 24))
    draw_proj = ImageDraw.Draw(im_proj)
    draw_proj.text((800, 32), "KOMPLETT-PROJEKT MIT FADENKREUZ (JANOME MC350E - RAHMEN A)", fill=(245, 245, 250), anchor="mm")
    draw_proj.text((800, 60), "Oben: BERUFSFEUERWEHR KIEL (BF30_1..4.JEF)  |  Mitte: Ärmelwappen (WWWAP.JEF)  |  Unten: NACHTWACHE II (NW30_1..3.JEF)", fill=(165, 175, 195), anchor="mm")

    scale_p = 3.5
    center_px = 800
    top_arc_origin_y = 260    # Scheitelpunkt oberer Bogen (Y=0 in top_arc coords)
    wappen_origin_y  = 640    # Zentrum WWWAP.JEF
    bot_arc_origin_y = 1040   # Scheitelpunkt unterer Bogen (Y=0 in bot_arc coords)

    # 1. Zeichne Oberen Bogen (BF30_1..4.JEF)
    colors_top = [(235, 50, 55), (55, 170, 245), (245, 190, 40), (70, 215, 115)]
    for idx, (fname, _, _, _, _, lbl, _) in enumerate(top_steps):
        col = colors_top[idx]
        st_pts, _ = parse_jef_stitches(os.path.join(src_verify_dir, fname))
        cx_off, cy_off = top_offsets[idx]
        for i in range(len(st_pts) - 1):
            op1, x1, y1, _ = st_pts[i]
            op2, x2, y2, _ = st_pts[i+1]
            if op2 == 'NORMAL':
                p1 = (center_px + (cx_off * 10.0 + x1) * 0.1 * scale_p,
                      top_arc_origin_y - (cy_off * 10.0 + y1) * 0.1 * scale_p)
                p2 = (center_px + (cx_off * 10.0 + x2) * 0.1 * scale_p,
                      top_arc_origin_y - (cy_off * 10.0 + y2) * 0.1 * scale_p)
                draw_proj.line([p1, p2], fill=col, width=2)

    for ch_idx, (chx, chy) in enumerate([top_ch1, top_ch2, top_ch3], start=1):
        px = center_px + chx * scale_p
        py = top_arc_origin_y - chy * scale_p
        draw_proj.ellipse([px - 18, py - 18, px + 18, py + 18], outline=(80, 255, 120), width=2)
        draw_proj.text((px, py - 26), f"FK {ch_idx}", fill=(80, 255, 120), anchor="mm")

    # 2. Zeichne Ärmelwappen WWWAP.JEF in der Mitte
    wwwap_path = os.path.join(src_verify_dir, "WWWAP.JEF")
    if os.path.exists(wwwap_path):
        st_w, _ = parse_jef_stitches(wwwap_path)
        w_colors = [(220, 35, 45), (242, 242, 246), (35, 38, 44)]
        for i in range(len(st_w) - 1):
            op1, x1, y1, c1 = st_w[i]
            op2, x2, y2, c2 = st_w[i+1]
            if op2 == 'NORMAL':
                col = w_colors[c2 % len(w_colors)]
                p1 = (center_px + x1 * 0.1 * scale_p, wappen_origin_y - y1 * 0.1 * scale_p)
                p2 = (center_px + x2 * 0.1 * scale_p, wappen_origin_y - y2 * 0.1 * scale_p)
                draw_proj.line([p1, p2], fill=col, width=1)
        draw_proj.text((center_px, wappen_origin_y + 155), "WWWAP.JEF (Ärmel-/Brustwappen: 66.2 x 70.3 mm, 15.126 Stiche)", fill=(220, 225, 235), anchor="mm")

    # 3. Zeichne Unteren Gegenbogen (NW30_1..3.JEF)
    for idx, (fname, _, _, _, _, lbl, _) in enumerate(bot_ii_steps):
        col = colors_nw[idx]
        st_pts, _ = parse_jef_stitches(os.path.join(src_verify_dir, fname))
        cx_off, cy_off = bot_ii_offsets[idx]
        for i in range(len(st_pts) - 1):
            op1, x1, y1, _ = st_pts[i]
            op2, x2, y2, _ = st_pts[i+1]
            if op2 == 'NORMAL':
                p1 = (center_px + (cx_off * 10.0 + x1) * 0.1 * scale_p,
                      bot_arc_origin_y - (cy_off * 10.0 + y1) * 0.1 * scale_p)
                p2 = (center_px + (cx_off * 10.0 + x2) * 0.1 * scale_p,
                      bot_arc_origin_y - (cy_off * 10.0 + y2) * 0.1 * scale_p)
                draw_proj.line([p1, p2], fill=col, width=2)

    for ch_idx, (chx, chy) in enumerate([bot_ii_ch1, bot_ii_ch2], start=1):
        px = center_px + chx * scale_p
        py = bot_arc_origin_y - chy * scale_p
        draw_proj.ellipse([px - 18, py - 18, px + 18, py + 18], outline=(80, 255, 120), width=2)
        draw_proj.text((px, py + 26), f"FK {ch_idx}", fill=(80, 255, 120), anchor="mm")

    # Legende unten
    draw_proj.text((800, 1175), "OBERER BOGEN:   BF30_1.JEF (1. BERUFS)  ->  BF30_2.JEF (2. FEUER)  ->  BF30_3.JEF (3. WEHR)  ->  BF30_4.JEF (4. KIEL)", fill=(230, 230, 240), anchor="mm")
    draw_proj.text((800, 1210), "MITTE WAPPEN:   WWWAP.JEF (Ärmelwappen 66x70 mm)   |   KIEL_VOL.JEF / KIEL_KTL.JEF   |   30_MITTE.JEF (Zahl 30)", fill=(230, 230, 240), anchor="mm")
    draw_proj.text((800, 1245), "UNTERER BOGEN:  NW30_1.JEF (1. NACHT)   ->  NW30_2.JEF (2. WACH)   ->  NW30_3.JEF (3. E II)  [+ NW2_1..3.JEF für 'NACHTWACHE 2']", fill=(230, 230, 240), anchor="mm")

    out_proj_png = os.path.join(brain_dir, "fw_complete_project_preview.png")
    im_proj.save(out_proj_png)

    # Kopiere Vorschaubilder auch auf den Stick in F:\fw und E:\StickCore\output\fw
    for d in long_name_dirs:
        shutil.copy2(out_proj_png, os.path.join(d, "PROJEKT_KOMPLETT_UEBERSICHT.png"))
        shutil.copy2(out_nw_png, os.path.join(d, "GEGENBOGEN_NACHTWACHE_II_DETAIL.png"))

    print(f"\n  [OK] Detail-Vorschau Gegenbogen: {out_nw_png}")
    print(f"  [OK] Komplett-Projekt-Vorschau:  {out_proj_png}")
    print("\n[FERTIG] Gesamtes Projekt erfolgreich auf USB-Stick (F:\\) und Festplatte gespeichert!")

if __name__ == '__main__':
    main()
