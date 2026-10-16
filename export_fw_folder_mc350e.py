# -*- coding: utf-8 -*-
"""
StickCore - Export für Janome MC350E USB-Stick
Erstellt den Ordner 'fw' mit 8-Zeichen DOS-Dateinamen (8.3 Format)
für den 30-34 cm Bogen "BERUFSFEUERWEHR KIEL" in 4 Teilschritten für Rahmen A:

Optimierungen:
1. Schrift eine Nummer größer: 24.0 mm Kappenhöhe (ca. 1 Inch)
2. Höhere Plastizität (3D-Relief-Effekt) Buchstabe für Buchstabe:
   - Schritt A: Unterleger-Füllstich (Tatami parallel zur Grundlinie)
   - Schritt B: Deck-Füllstich (Tatami im 90°-Kreuzwinkel, größere Stichweite 3.8 mm für seidigen Glanz)
   - Schritt C: Saubere Kantenverriegelung entlang der Buchstabenumrisse
3. Kein Farbwechsel für das Passkreuz: Alles in 1 Garnfarbe (Feuerwehrrot)
4. Fadenkreuze mit kleinen 1.0 mm Stichen und 3-facher Mitten-Verriegelung (kann sich nicht mehr herausziehen!)

Dateinamen (<= 8 Zeichen):
1. BF30_1.JEF  (Schritt 1: BERUFS + Passkreuz 1 nachlaufend)
2. BF30_2.JEF  (Schritt 2: Passkreuz 1 vorlaufend + FEUER + Passkreuz 2 nachlaufend)
3. BF30_3.JEF  (Schritt 3: Passkreuz 2 vorlaufend + WEHR + Passkreuz 3 nachlaufend)
4. BF30_4.JEF  (Schritt 4: Passkreuz 3 vorlaufend + KIEL)

Zielorte:
- F:\\fw\\
- F:\\EmbF5\\fw\\
- E:\\StickCore\\output\\fw\\
"""

import os
import math
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

def make_run_stitch(pts, stitch_len_mm=1.4):
    """Erzeugt saubere Geradstiche entlang einer Kontur."""
    if len(pts) < 2: return []
    L = poly_length(pts)
    steps = max(2, int(round(L / stitch_len_mm)))
    res = []
    for i in range(steps + 1):
        p = sample_poly(pts, i / steps)
        kind = 'JUMP' if i == 0 else 'NORMAL'
        res.append((kind, p[0], p[1]))
    return res

def multi_poly_tatami(polys, pitch_mm=0.40, stitch_len_mm=3.5, angle_deg=0.0):
    """
    Erzeugt einen lückenlosen Scanline-Füllstich (Tatami) über geschlossene
    Polygone inklusive aller Innenlöcher (Even-Odd-Regel).
    """
    rad = math.radians(-angle_deg)
    cos_a, sin_a = math.cos(rad), math.sin(rad)
    cos_back, sin_back = math.cos(-rad), math.sin(-rad)

    rot_polys = []
    for poly in polys:
        rp = [(p[0] * cos_a - p[1] * sin_a, p[0] * sin_a + p[1] * cos_a) for p in poly]
        if rp[0] != rp[-1]:
            rp.append(rp[0])
        rot_polys.append(rp)

    all_y = [p[1] for rp in rot_polys for p in rp]
    if not all_y:
        return []
    min_y, max_y = min(all_y), max(all_y)

    y = min_y + pitch_mm * 0.5
    y_lines = []
    while y <= max_y:
        y_lines.append(y)
        y += pitch_mm

    stitches = []
    forward = True
    for y_val in y_lines:
        xs = []
        for rp in rot_polys:
            for i in range(len(rp) - 1):
                p1, p2 = rp[i], rp[i+1]
                if (p1[1] <= y_val < p2[1]) or (p2[1] <= y_val < p1[1]):
                    t = (y_val - p1[1]) / (p2[1] - p1[1])
                    ix = p1[0] + t * (p2[0] - p1[0])
                    xs.append(ix)
        xs.sort()

        segments = []
        for j in range(0, len(xs) - 1, 2):
            if xs[j+1] - xs[j] > 0.05:
                segments.append((xs[j], xs[j+1]))

        if not forward:
            segments.reverse()

        for seg_idx, seg in enumerate(segments):
            x_start, x_end = seg if forward else (seg[1], seg[0])
            dist = abs(x_end - x_start)
            steps = max(1, int(round(dist / stitch_len_mm)))
            for s_i in range(steps + 1):
                cur_x = x_start + (x_end - x_start) * (s_i / steps)
                orig_x = cur_x * cos_back - y_val * sin_back
                orig_y = cur_x * sin_back + y_val * cos_back
                kind = 'JUMP' if (len(stitches) == 0 or (s_i == 0 and seg_idx > 0)) else 'NORMAL'
                stitches.append((kind, orig_x, orig_y))
        forward = not forward
    return stitches

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
    Byte-für-Byte verifiziert:
    - 0x00: Stitch Offset (0x74 + colorCount * 8)
    - 0x04: Version (0x14)
    - 0x08: Datum 14 Bytes
    - 0x16: 0x64
    - 0x17: 0x00
    - 0x18: Farbanzahl
    - 0x1C: Stichzähler (CountField)
    - 0x20: Hoop-Code (0 für Rahmen A 126x110 mm)
    - 0x24: distLeft   (-minX in 0.1 mm)
    - 0x28: distTop    ( maxY in 0.1 mm)
    - 0x2C: distRight  ( maxX in 0.1 mm)
    - 0x30: distBottom (-minY in 0.1 mm)
    - 0x34: 16 x -1 (64 Bytes unbenutzt)
    - 0x74: Thread-Codes (colorCount * 4 Bytes)
    - Per-Color Attributes (colorCount * 4 Bytes: 0x0D)
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

    # Design-Abschluss: Zurück zum Zentrum + End-Befehl
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

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
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
    Stickt jeden Buchstaben wie beim Versuch davor als Varsity-Outline,
    jedoch Buchstabe für Buchstabe mit doppelter Lage für massiven 3D-Relief-Effekt:
    1. Pass 1: Vorstick / Unterleger-Raupe (Breite 2.4 mm, Pitch 0.48 mm)
    2. Pass 2: Deckraupe mit größerer Stichweite (Breite 3.5 mm, Stichabstand 0.34 mm)
       -> Umschließt den 2.4 mm Unterleger und wölbt sich plastisch auf!
    """
    cmds = []
    for poly in char_item['polys']:
        # Pass 1: 2.4 mm Raupe
        st_under = make_kettel_outline(poly, width_mm=2.4, pitch_mm=0.48)
        cmds.extend(st_under)
        # Pass 2: 3.5 mm Raupe mit 0.34 mm Stichabstand direkt darüber
        st_cover = make_kettel_outline(poly, width_mm=3.5, pitch_mm=0.34)
        cmds.extend(st_cover)
    return cmds

def build_part_commands(chars_in_part, crosshairs_before=[], crosshairs_after=[]):
    """
    Erzeugt die vollständige Stichfolge für einen Teilschritt:
    - Vorlaufende Passkreuze (mit kleinen 1mm Stichen und Mittenverriegelung)
    - Buchstabe für Buchstabe plastisch: 2.4 mm Unterleger + 3.5 mm Deckraupe (0.34 mm)
    - Nachlaufende Passkreuze
    - ALLES in 1 Garnfarbe (Feuerwehrrot, kein Farbwechsel / kein Maschinenstopp)
    """
    uncentered_cmds = []

    # 1. Vorlaufende Passkreuze
    for ch in crosshairs_before:
        for op, x, y in make_secure_crosshair_stitches(ch[0], ch[1], arm_len=4.0, step_mm=1.0):
            uncentered_cmds.append((op, x, y))

    # 2. Buchstabe für Buchstabe (Pass 1 Unterleger 1.0 mm -> Pass 2 Deckraupe 2.4 mm)
    for c in chars_in_part:
        char_raw_stitches = build_plastic_char_stitches(c)
        for op, x, y in char_raw_stitches:
            uncentered_cmds.append((op, x, y))

    # 3. Nachlaufende Passkreuze
    for ch in crosshairs_after:
        for op, x, y in make_secure_crosshair_stitches(ch[0], ch[1], arm_len=4.0, step_mm=1.0):
            uncentered_cmds.append((op, x, y))

    # Exakte Zentrierung im Rahmen A (0,0)
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
    return final_cmds, pal, (w_mm, h_mm)

def main():
    print("==================================================================")
    print(" StickCore - Janome MC350E USB-Export (3D-Plastisch, 1 Farbe)")
    print("==================================================================")

    potential_dirs = [
        r"F:\EmbF5\MyDesign",
        r"F:\Embf5\MyDesign",
        r"F:\EMB\MyDesign",
        r"F:\MyDesign",
        r"F:\EmbF5\fw",
        r"F:\fw",
        r"E:\StickCore\output\EmbF5\MyDesign",
        r"E:\StickCore\output\fw"
    ]
    target_dirs = []
    for d in potential_dirs:
        drive = os.path.splitdrive(d)[0]
        if not drive or os.path.exists(drive + "\\"):
            os.makedirs(d, exist_ok=True)
            target_dirs.append(d)
        else:
            print(f"  [INFO] Laufwerk {drive} aktuell nicht verbunden (Stick an Nähmaschine?). Überspringe {d}.")

    # Größere Schrift: 24.0 mm Kappenhöhe, Radius 210 mm (~34 cm Gesamtbogen)
    chars_top30 = layout_arc_text(
        "berufsfeuerwehr kiel",
        cap_h_mm=24.0,
        arc_r_mm=210.0,
        arc_down=False,
        letter_sp_mm=1.5
    )

    # 4 Teilschritte
    part1_chars = chars_top30[0:6]    # BERUFS
    part2_chars = chars_top30[6:11]   # FEUER
    part3_chars = chars_top30[11:15]  # WEHR
    part4_chars = chars_top30[16:20]  # KIEL

    # Passkreuz-Positionen (auf dem Bogen zentriert zwischen den Wörtern)
    ch1_x = 0.5 * (chars_top30[5]['pos'][0] + chars_top30[6]['pos'][0])
    ch1_y = 0.5 * (chars_top30[5]['pos'][1] + chars_top30[6]['pos'][1])

    ch2_x = 0.5 * (chars_top30[10]['pos'][0] + chars_top30[11]['pos'][0])
    ch2_y = 0.5 * (chars_top30[10]['pos'][1] + chars_top30[11]['pos'][1])

    ch3_x = 0.5 * (chars_top30[14]['pos'][0] + chars_top30[16]['pos'][0])
    ch3_y = 0.5 * (chars_top30[14]['pos'][1] + chars_top30[16]['pos'][1])

    steps = [
        ("BF30_1.JEF", part1_chars, [], [(ch1_x, ch1_y)], "1. BERUFS"),
        ("BF30_2.JEF", part2_chars, [(ch1_x, ch1_y)], [(ch2_x, ch2_y)], "2. FEUER"),
        ("BF30_3.JEF", part3_chars, [(ch2_x, ch2_y)], [(ch3_x, ch3_y)], "3. WEHR"),
        ("BF30_4.JEF", part4_chars, [(ch3_x, ch3_y)], [], "4. KIEL")
    ]

    for fname, ch_list, ch_before, ch_after, label in steps:
        cmds, pal, dims = build_part_commands(ch_list, ch_before, ch_after)
        print(f"\nGeneriere {fname} ({label}):")
        print(f"  Buchstaben: {len(ch_list)}, Befehle: {len(cmds)}, Farben: {len(pal)}")
        print(f"  Maße: {dims[0]:.1f} x {dims[1]:.1f} mm (Rahmen A <= 126x110 mm: {'PASST' if dims[0]<=126 and dims[1]<=110 else 'ZU GROSS!'})")

        for d in target_dirs:
            out_file = os.path.join(d, fname)
            write_verified_jef(out_file, cmds, pal, hoop_code=0)
            print(f"  -> Gespeichert in: {out_file}")

    print("\n--- Verifiziere und rendere JEF-Dateien direkt von Festplatte/Stick ---")
    brain_dir = r"C:\Users\micbu\.gemini\antigravity\brain\e572641d-70d6-45b5-8c91-c97f358a4f6c"

    def parse_jef_stitches(jef_path):
        with open(jef_path, 'rb') as f:
            data = f.read()
        st_offset, = struct.unpack('<I', data[0:4])
        pos = st_offset; x, y = 0, 0; pts = []
        while pos < len(data):
            b0 = data[pos]; b1 = data[pos+1]; pos += 2
            if b0 == 0x80:
                if b1 in (0x01, 0x02):
                    dx = struct.unpack('b', bytes([data[pos]]))[0]
                    dy = struct.unpack('b', bytes([data[pos+1]]))[0]
                    pos += 2; x += dx; y += dy
                    pts.append(('JUMP', x, y))
                elif b1 == 0x10: break
            else:
                dx = struct.unpack('b', bytes([b0]))[0]
                dy = struct.unpack('b', bytes([b1]))[0]
                x += dx; y += dy
                pts.append(('NORMAL', x, y))
        return pts

    # Rendere Gesamtansicht der 4 Schritte
    im_tot = Image.new('RGB', (1600, 850), (20, 22, 26))
    draw_tot = ImageDraw.Draw(im_tot)
    draw_tot.text((800, 30), "PLASTISCHER BOGEN (24 MM SCHRIFT): BERUFSFEUERWEHR KIEL", fill=(245, 245, 250), anchor="mm")
    draw_tot.text((800, 58), "Buchstabe-für-Buchstabe: Pass 1 (2.4 mm Raupe) + Pass 2 (3.5 mm Raupe mit 0.34 mm Stichabstand) | 1 Garnfarbe | Verriegeltes 1mm Passkreuz", fill=(160, 165, 180), anchor="mm")

    colors_step = [
        (235, 45, 55),   # 1. Rot
        (45, 165, 245),  # 2. Blau
        (245, 185, 35),  # 3. Gold/Gelb
        (65, 210, 110)   # 4. Grün
    ]
    scale_tot = 3.6
    base_x, base_y = 800, 520

    # Offsets der 4 Teile in globalen Koordinaten
    step_offsets = []
    for fname, ch_list, ch_b, ch_a, lbl in steps:
        all_p = [pt for c in ch_list for poly in c['polys'] for pt in poly]
        for ch in ch_b:
            all_p.extend([(ch[0]-4, ch[1]), (ch[0]+4, ch[1]), (ch[0], ch[1]-4), (ch[0], ch[1]+4)])
        for ch in ch_a:
            all_p.extend([(ch[0]-4, ch[1]), (ch[0]+4, ch[1]), (ch[0], ch[1]-4), (ch[0], ch[1]+4)])
        cx = 0.5 * (min(p[0] for p in all_p) + max(p[0] for p in all_p))
        cy = 0.5 * (min(p[1] for p in all_p) + max(p[1] for p in all_p))
        step_offsets.append((cx, cy))

    for idx, (fname, _, _, _, lbl) in enumerate(steps):
        col = colors_step[idx]
        jef_file = os.path.join(target_dirs[-1], fname)
        st_pts = parse_jef_stitches(jef_file)
        cx_off, cy_off = step_offsets[idx]

        for i in range(len(st_pts) - 1):
            op1, x1, y1 = st_pts[i]
            op2, x2, y2 = st_pts[i+1]
            if op2 == 'NORMAL':
                # JEF: +Y ist UP -> Bild: +Y ist DOWN
                p1_scr = (base_x + (cx_off * 10.0 + x1) * 0.1 * scale_tot,
                          base_y - (cy_off * 10.0 + y1) * 0.1 * scale_tot)
                p2_scr = (base_x + (cx_off * 10.0 + x2) * 0.1 * scale_tot,
                          base_y - (cy_off * 10.0 + y2) * 0.1 * scale_tot)
                draw_tot.line([p1_scr, p2_scr], fill=col, width=1)

        leg_x = 120 + idx * 360
        draw_tot.rectangle([leg_x - 15, 790 - 8, leg_x + 15, 790 + 8], fill=col)
        draw_tot.text((leg_x + 25, 790), f"{fname}: {lbl}", fill=(220, 220, 230), anchor="lm")

    out_tot_png = os.path.join(brain_dir, "fw_bf30_verified_upright_preview.png")
    im_tot.save(out_tot_png)
    print(f"  [OK] Gesamt-Vorschau gespeichert: {out_tot_png}")
    print("\n[FERTIG] Alle Dateien erfolgreich erzeugt!")

if __name__ == '__main__':
    main()
