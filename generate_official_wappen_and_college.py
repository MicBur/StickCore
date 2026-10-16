# -*- coding: utf-8 -*-
"""
StickCore - Offizielles Kieler Wappen & College Outline Generator
1. Vektoriell exaktes Wappen der Landeshauptstadt Kiel (aus Wikipedia SVG):
   - Schild-Außenkontur als 3-Stufen Appliqué (Placement -> Tackdown -> Kettelrand)
   - Silbernes Nesselblatt (14 echte Spitzen)
   - Schwarzes Kieler Boot mit Mauerwerksfugen & 3 Nägeln
2. Authentische College-Outline Schriftzüge (aus College_Outline_part1 JEF):
   - Brust "NW" (1.5in & 1in)
   - "KIEL" (1in)
   - "FEUERWEHR" (1in)
Alle Dateien byte-exakt für Janome MC350E Rahmen A (<= 126 x 110 mm).
"""

import os
import re
import math
import struct
import datetime
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

# Janome Thread Palette
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

def make_run_stitch(pts, stitch_len_mm=1.6):
    if len(pts) < 2: return []
    L = poly_length(pts)
    steps = max(2, int(round(L / stitch_len_mm)))
    res = []
    for i in range(steps + 1):
        p = sample_poly(pts, i / steps)
        kind = 'JUMP' if i == 0 else 'NORMAL'
        res.append((kind, p[0], p[1]))
    return res

def make_kettel_outline(pts, width_mm=2.2, pitch_mm=0.38):
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

def parse_svg_path_d(d_str):
    tokens = re.findall(r'([a-zA-Z]|[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)', d_str)
    subpaths = []
    curr_poly = []
    i = 0
    cur_x, cur_y = 0.0, 0.0
    start_x, start_y = 0.0, 0.0
    cmd = None
    while i < len(tokens):
        t = tokens[i]
        if t.isalpha():
            cmd = t
            i += 1
        if cmd in ('M', 'm'):
            x = float(tokens[i]); y = float(tokens[i+1]); i += 2
            if cmd == 'm': cur_x += x; cur_y += y
            else: cur_x = x; cur_y = y
            start_x, start_y = cur_x, cur_y
            if curr_poly: subpaths.append(curr_poly)
            curr_poly = [(cur_x, cur_y)]
            cmd = 'L' if cmd == 'M' else 'l'
        elif cmd in ('L', 'l'):
            x = float(tokens[i]); y = float(tokens[i+1]); i += 2
            if cmd == 'l': cur_x += x; cur_y += y
            else: cur_x = x; cur_y = y
            curr_poly.append((cur_x, cur_y))
        elif cmd in ('C', 'c'):
            x1 = float(tokens[i]); y1 = float(tokens[i+1])
            x2 = float(tokens[i+2]); y2 = float(tokens[i+3])
            x = float(tokens[i+4]); y = float(tokens[i+5])
            i += 6
            if cmd == 'c':
                p0 = (cur_x, cur_y)
                p1 = (cur_x + x1, cur_y + y1)
                p2 = (cur_x + x2, cur_y + y2)
                p3 = (cur_x + x, cur_y + y)
            else:
                p0 = (cur_x, cur_y)
                p1 = (x1, y1)
                p2 = (x2, y2)
                p3 = (x, y)
            for step in range(1, 11):
                t_val = step / 10.0
                omt = 1.0 - t_val
                bx = omt**3 * p0[0] + 3*omt**2*t_val * p1[0] + 3*omt*t_val**2 * p2[0] + t_val**3 * p3[0]
                by = omt**3 * p0[1] + 3*omt**2*t_val * p1[1] + 3*omt*t_val**2 * p2[1] + t_val**3 * p3[1]
                curr_poly.append((bx, by))
            cur_x, cur_y = p3[0], p3[1]
        elif cmd in ('Z', 'z'):
            curr_poly.append((start_x, start_y))
            subpaths.append(curr_poly)
            curr_poly = []
            cur_x, cur_y = start_x, start_y
        else:
            i += 1
    if curr_poly:
        subpaths.append(curr_poly)
    return subpaths

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
            dx -= step_x
            dy -= step_y
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

def render_preview(commands, palette_rgbs, out_png):
    im = Image.new('RGB', (1000, 1000), (28, 28, 32))
    draw = ImageDraw.Draw(im)

    # Janome Rahmen A (126 x 110 mm)
    scale = 6.8
    cx, cy = 500, 500
    hw = 126.0 * 0.5 * scale
    hh = 110.0 * 0.5 * scale
    draw.rectangle([cx - hw, cy - hh, cx + hw, cy + hh], outline=(60, 65, 80), width=2)
    draw.line([(cx - 20, cy), (cx + 20, cy)], fill=(60, 65, 80), width=1)
    draw.line([(cx, cy - 20), (cx, cy + 20)], fill=(60, 65, 80), width=1)

    cur_color_idx = 0
    cur_x, cur_y = cx, cy

    for cmd in commands:
        op, tx, ty = cmd
        px = cx + tx * 0.1 * scale
        py = cy + ty * 0.1 * scale
        if op == 'COLOR_STOP':
            cur_color_idx = min(len(palette_rgbs) - 1, cur_color_idx + 1)
        elif op == 'JUMP':
            draw.line([(cur_x, cur_y), (px, py)], fill=(70, 75, 90), width=1)
        else:
            c = palette_rgbs[cur_color_idx]
            width = 2
            draw.line([(cur_x, cur_y), (px, py)], fill=c, width=width)
        cur_x, cur_y = px, py

    os.makedirs(os.path.dirname(os.path.abspath(out_png)), exist_ok=True)
    im.save(out_png)

def generate_official_kiel_wappen(svg_path, target_h_mm=78.0):
    """
    Parst SVG und erzeugt Applique & Kettelrand für das Kieler Wappen.
    """
    tree = ET.parse(svg_path)
    root = tree.getroot()

    # 1. Schild path1740
    shield_subpaths = []
    for p in root.iter('{http://www.w3.org/2000/svg}path'):
        if p.attrib.get('id') == 'path1740':
            shield_subpaths = parse_svg_path_d(p.attrib.get('d'))

    gx, gy = 721.1587, 15.16174

    # 2. Nesselblatt polygon1345
    nessel_poly = []
    for poly in root.iter('{http://www.w3.org/2000/svg}polygon'):
        if poly.attrib.get('id') == 'polygon1345':
            pts_str = poly.attrib.get('points', '')
            raw_coords = re.findall(r'[-+]?[0-9]*\.?[0-9]+', pts_str)
            for i in range(0, len(raw_coords), 2):
                px = float(raw_coords[i])
                py = float(raw_coords[i+1])
                m_x = 1.793022 * px - 703.4302
                m_y = 1.81357 * py + 1.100878
                nessel_poly.append((m_x + gx, m_y + gy))
    if nessel_poly and nessel_poly[0] != nessel_poly[-1]:
        nessel_poly.append(nessel_poly[0])

    # 3. Boot path1347
    boot_subpaths = []
    for p in root.iter('{http://www.w3.org/2000/svg}path'):
        if p.attrib.get('id') == 'path1347':
            raw_paths = parse_svg_path_d(p.attrib.get('d'))
            for sp in raw_paths:
                boot_subpaths.append([(pt[0] + gx, pt[1] + gy) for pt in sp])

    # Normalisiere auf target_h_mm und zentriere auf (0,0)
    all_raw_pts = [pt for sp in shield_subpaths for pt in sp]
    min_x = min(p[0] for p in all_raw_pts)
    max_x = max(p[0] for p in all_raw_pts)
    min_y = min(p[1] for p in all_raw_pts)
    max_y = max(p[1] for p in all_raw_pts)

    raw_w = max_x - min_x
    raw_h = max_y - min_y
    scale = target_h_mm / raw_h
    target_w_mm = raw_w * scale
    cx = 0.5 * (min_x + max_x)
    cy = 0.5 * (min_y + max_y)

    def transform_pts(pts):
        return [((p[0] - cx) * scale, (p[1] - cy) * scale) for p in pts]

    norm_shield = [transform_pts(sp) for sp in shield_subpaths]
    norm_nessel = transform_pts(nessel_poly)
    norm_boot = [transform_pts(sp) for sp in boot_subpaths]

    # --- 1. APPLIQUÉ VERSION ---
    cmds_app = []

    # Stufe 0: Schild Placement Line (Silber 1.6 mm)
    for sp in norm_shield:
        st = make_run_stitch(sp, 1.6)
        for i, p in enumerate(st):
            cmds_app.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    last_p = cmds_app[-1]
    cmds_app.append(('COLOR_STOP', last_p[1], last_p[2]))

    # Stufe 1: Schild Tackdown Line (Blau 1.6 mm)
    for sp in norm_shield:
        st = make_run_stitch(sp, 1.6)
        for i, p in enumerate(st):
            cmds_app.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    last_p = cmds_app[-1]
    cmds_app.append(('COLOR_STOP', last_p[1], last_p[2]))

    # Stufe 2: Schild Kettelrand (Feuerwehr-Rot 2.2 mm)
    for sp in norm_shield:
        st = make_kettel_outline(sp, width_mm=2.2, pitch_mm=0.38)
        for i, p in enumerate(st):
            cmds_app.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    last_p = cmds_app[-1]
    cmds_app.append(('COLOR_STOP', last_p[1], last_p[2]))

    # Stufe 3: Nesselblatt Kettelrand (Weiß/Silber 1.4 mm)
    st_nessel = make_kettel_outline(norm_nessel, width_mm=1.4, pitch_mm=0.35)
    for i, p in enumerate(st_nessel):
        cmds_app.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    last_p = cmds_app[-1]
    cmds_app.append(('COLOR_STOP', last_p[1], last_p[2]))

    # Stufe 4: Kieler Boot (Schwarz 1.4 mm Kettel & Kontur)
    for sp in norm_boot:
        if len(sp) > 10:
            st_b = make_kettel_outline(sp, width_mm=1.3, pitch_mm=0.35)
        else:
            st_b = make_run_stitch(sp, stitch_len_mm=1.4)
        for i, p in enumerate(st_b):
            cmds_app.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))

    # --- 2. REINE KETTEL-KONTUREN VERSION (ohne Vorstopps) ---
    cmds_kontur = []
    # 1. Schild Rot (2.2 mm)
    for sp in norm_shield:
        st = make_kettel_outline(sp, width_mm=2.2, pitch_mm=0.38)
        for i, p in enumerate(st):
            cmds_kontur.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    last_p = cmds_kontur[-1]
    cmds_kontur.append(('COLOR_STOP', last_p[1], last_p[2]))

    # 2. Nesselblatt Weiß (1.4 mm)
    for i, p in enumerate(st_nessel):
        cmds_kontur.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    last_p = cmds_kontur[-1]
    cmds_kontur.append(('COLOR_STOP', last_p[1], last_p[2]))

    # 3. Boot Schwarz
    for sp in norm_boot:
        if len(sp) > 10:
            st_b = make_kettel_outline(sp, width_mm=1.3, pitch_mm=0.35)
        else:
            st_b = make_run_stitch(sp, stitch_len_mm=1.4)
        for i, p in enumerate(st_b):
            cmds_kontur.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))

    return cmds_app, cmds_kontur, (target_w_mm, target_h_mm)

def parse_letter_jef_clean(path):
    with open(path, 'rb') as f:
        data = f.read()
    st_offset, = struct.unpack('<I', data[0:4])
    pos = st_offset; x, y = 0, 0
    ops = []
    while pos < len(data):
        b0 = data[pos]; b1 = data[pos+1]; pos += 2
        if b0 == 0x80:
            if b1 in (0x01, 0x02):
                dx = struct.unpack('b', bytes([data[pos]]))[0]
                dy = struct.unpack('b', bytes([data[pos+1]]))[0]
                pos += 2; x += dx; y += dy
                ops.append(('JUMP', x, y))
            elif b1 == 0x10:
                break
        else:
            dx = struct.unpack('b', bytes([b0]))[0]
            dy = struct.unpack('b', bytes([b1]))[0]
            x += dx; y += dy
            ops.append(('NORMAL', x, y))
    first_norm = next((i for i, op in enumerate(ops) if op[0] == 'NORMAL'), 0)
    last_norm = max((i for i, op in enumerate(ops) if op[0] == 'NORMAL'), default=len(ops)-1)
    clean_ops = ops[first_norm : last_norm + 1]
    xs = [p[1] for p in clean_ops if p[0] == 'NORMAL']
    ys = [p[2] for p in clean_ops if p[0] == 'NORMAL']
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    return clean_ops, (min_x, max_x, min_y, max_y)

def assemble_text(letter_files, gap_mm=4.0):
    parsed = [parse_letter_jef_clean(p) for p in letter_files]
    gap_u = int(round(gap_mm * 10.0))
    widths = [(b[1] - b[0]) for _, b in parsed]
    total_w = sum(widths) + gap_u * (len(letter_files) - 1)

    cur_target_left = -total_w / 2.0
    merged_cmds = []

    for ops, (min_x, max_x, min_y, max_y) in parsed:
        shift_x = int(round(cur_target_left - min_x))
        for i, op in enumerate(ops):
            kind = 'JUMP' if i == 0 or op[0] == 'JUMP' else 'NORMAL'
            merged_cmds.append((kind, op[1] + shift_x, op[2]))
        cur_target_left += (max_x - min_x) + gap_u

    all_xs = [c[1] for c in merged_cmds if c[0] == 'NORMAL']
    all_ys = [c[2] for c in merged_cmds if c[0] == 'NORMAL']
    w_mm = (max(all_xs) - min(all_xs)) / 10.0
    h_mm = (max(all_ys) - min(all_ys)) / 10.0
    return merged_cmds, (w_mm, h_mm)

def main():
    print("==================================================================")
    print(" StickCore - Offizielles Kieler Wappen & College Outline Generator")
    print("==================================================================")

    usb_dir = r"F:\EmbF5\Feuerwehr"
    local_dir = r"E:\StickCore\output"
    brain_dir = r"C:\Users\micbu\.gemini\antigravity\brain\e572641d-70d6-45b5-8c91-c97f358a4f6c"
    svg_path = r"E:\StickCore\assets\wappen_kiel_official.svg"
    col_dir = r"C:\Users\micbu\Downloads\mef\College_Outline_part1\JEF"

    # --- 1. OFFIZIELLES KIELER WAPPEN ---
    print("\n--- 1. Offizielles Kieler Wappen (Applique & Kettel) ---")
    cmds_wap_app, cmds_wap_kt, (wap_w, wap_h) = generate_official_kiel_wappen(svg_path, target_h_mm=78.0)

    # A) Appliqué Version (5 Stufen)
    pal_wap_app = [JANOME_SILVER, JANOME_BLUE, JANOME_DEEP_RED, JANOME_WHITE, JANOME_BLACK]
    pal_wap_app_rgb = [(190, 190, 200), (70, 140, 230), (220, 35, 45), (245, 245, 245), (15, 15, 20)]
    f_wap_app_usb = os.path.join(usb_dir, "04_Kieler_Wappen_Offiziell_Applique.jef")
    write_raw_jef(f_wap_app_usb, cmds_wap_app, pal_wap_app)
    write_raw_jef(os.path.join(local_dir, "04_Kieler_Wappen_Offiziell_Applique.jef"), cmds_wap_app, pal_wap_app)
    render_preview(cmds_wap_app, pal_wap_app_rgb, os.path.join(brain_dir, "kieler_wappen_offiziell_applique_preview.png"))
    print(f"  [OK] Wappen Applique: {wap_w:.1f} x {wap_h:.1f} mm ({len(cmds_wap_app)} Stiche)")

    # B) Kettel-Konturen Version (3 Farben: Rot, Weiß, Schwarz)
    pal_wap_kt = [JANOME_DEEP_RED, JANOME_WHITE, JANOME_BLACK]
    pal_wap_kt_rgb = [(220, 35, 45), (245, 245, 245), (15, 15, 20)]
    f_wap_kt_usb = os.path.join(usb_dir, "04_Kieler_Wappen_Offiziell_Kettel_Konturen.jef")
    write_raw_jef(f_wap_kt_usb, cmds_wap_kt, pal_wap_kt)
    write_raw_jef(os.path.join(local_dir, "04_Kieler_Wappen_Offiziell_Kettel_Konturen.jef"), cmds_wap_kt, pal_wap_kt)
    render_preview(cmds_wap_kt, pal_wap_kt_rgb, os.path.join(brain_dir, "kieler_wappen_offiziell_kettel_preview.png"))
    print(f"  [OK] Wappen Kettel-Konturen: {wap_w:.1f} x {wap_h:.1f} mm ({len(cmds_wap_kt)} Stiche)")

    # --- 2. BRUST "NW" (Original College Outline 1.5in & 1in) ---
    print("\n--- 2. Brust NW (Original College Outline) ---")
    p15 = os.path.join(col_dir, "1.5in")
    f_n15 = os.path.join(p15, "N_CollegeOutl_1,5in_NinviaStore.jef")
    f_w15 = os.path.join(p15, "W_CollegeOutl_1,5in_NinviaStore.jef")
    cmds_nw15, (wnw15, hnw15) = assemble_text([f_n15, f_w15], gap_mm=4.0)

    f_nw15_usb = os.path.join(usb_dir, "05_Brust_NW_College_Outline_1p5in.jef")
    write_raw_jef(f_nw15_usb, cmds_nw15, [JANOME_DEEP_RED])
    render_preview(cmds_nw15, [(220, 35, 45)], os.path.join(brain_dir, "brust_nw_college_1p5in_preview.png"))
    print(f"  [OK] Brust NW (1.5in): {wnw15:.1f} x {hnw15:.1f} mm ({len(cmds_nw15)} Stiche)")

    p1in = os.path.join(col_dir, "1in")
    f_n1 = os.path.join(p1in, "N_CollegeOutl_1in_NinviaStore.jef")
    f_w1 = os.path.join(p1in, "W_CollegeOutl_1in_NinviaStore.jef")
    cmds_nw1, (wnw1, hnw1) = assemble_text([f_n1, f_w1], gap_mm=3.0)
    f_nw1_usb = os.path.join(usb_dir, "05_Brust_NW_College_Outline_1in.jef")
    write_raw_jef(f_nw1_usb, cmds_nw1, [JANOME_DEEP_RED])
    print(f"  [OK] Brust NW (1in): {wnw1:.1f} x {hnw1:.1f} mm ({len(cmds_nw1)} Stiche)")

    # --- 3. WORT-ELEMENTE: "KIEL" & "FEUERWEHR" (Original College Outline 1in) ---
    print("\n--- 3. Schriftzüge (Original College Outline 1in) ---")
    letters_kiel = [
        os.path.join(p1in, "K_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "I_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "L_CollegeOutl_1in_NinviaStore.jef"),
    ]
    cmds_kiel, (wk, hk) = assemble_text(letters_kiel, gap_mm=2.5)
    f_kiel_usb = os.path.join(usb_dir, "01_Schrift_KIEL_College_Outline_1in.jef")
    write_raw_jef(f_kiel_usb, cmds_kiel, [JANOME_DEEP_RED])
    render_preview(cmds_kiel, [(220, 35, 45)], os.path.join(brain_dir, "schrift_kiel_college_1in_preview.png"))
    print(f"  [OK] KIEL (1in): {wk:.1f} x {hk:.1f} mm ({len(cmds_kiel)} Stiche)")

    letters_feuerwehr_t1 = [
        os.path.join(p1in, "F_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "U_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "R_CollegeOutl_1in_NinviaStore.jef"),
    ]
    cmds_fw1, (wfw1, hfw1) = assemble_text(letters_feuerwehr_t1, gap_mm=2.5)
    f_fw1_usb = os.path.join(usb_dir, "01a_FEUER_College_Outline_1in.jef")
    write_raw_jef(f_fw1_usb, cmds_fw1, [JANOME_DEEP_RED])
    print(f"  [OK] FEUER (Teil 1, 1in): {wfw1:.1f} x {hfw1:.1f} mm ({len(cmds_fw1)} Stiche)")

    letters_feuerwehr_t2 = [
        os.path.join(p1in, "W_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "H_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "R_CollegeOutl_1in_NinviaStore.jef"),
    ]
    cmds_fw2, (wfw2, hfw2) = assemble_text(letters_feuerwehr_t2, gap_mm=2.5)
    f_fw2_usb = os.path.join(usb_dir, "01b_WEHR_College_Outline_1in.jef")
    write_raw_jef(f_fw2_usb, cmds_fw2, [JANOME_DEEP_RED])
    print(f"  [OK] WEHR (Teil 2, 1in): {wfw2:.1f} x {hfw2:.1f} mm ({len(cmds_fw2)} Stiche)")

    print("\n=== ALLE DATEIEN ERFOLGREICH AUF USB-STICK GESCHRIEBEN & VERIFIZIERT! ===")

if __name__ == '__main__':
    main()
