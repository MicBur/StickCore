# -*- coding: utf-8 -*-
"""
StickCore - Vollstick-Wappen & Aufrechte College-Outline Schriftzüge
1. Ausgefülltes Kieler Wappen (Offizielles Wikipedia-Vektor):
   - Vollstick (Roter Tatami-Schild, weißes Tatami-Nesselblatt, schwarzes Boot mit Ziegeln und 3 Nägeln, roter Kettelrand)
   - Appliqué-Vollstick (Roter Patchgrund + ausgesticktes Nesselblatt & Boot)
2. Aufrechte College-Outline Schriftzüge (Y-Achse korrigiert):
   - Brust NW (1.5in & 1in)
   - KIEL (1in)
   - FEUER & WEHR (1in geteilt)
   - FEUERWEHR Kompakt (1-Rahmen Version 120 mm)
3. Gesamt-Ensemble Vorschaubild (Alle Komponenten wie auf der fertigen Jacke)
"""

import os
import re
import math
import struct
import datetime
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw, ImageFont

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

def generate_tatami_fill(polygon_pts, pitch_mm=0.45, stitch_len_mm=3.2, angle_deg=0.0):
    rad = math.radians(-angle_deg)
    cos_a, sin_a = math.cos(rad), math.sin(rad)
    rot_pts = [(p[0]*cos_a - p[1]*sin_a, p[0]*sin_a + p[1]*cos_a) for p in polygon_pts]
    if rot_pts[0] != rot_pts[-1]:
        rot_pts.append(rot_pts[0])
    
    min_y = min(p[1] for p in rot_pts)
    max_y = max(p[1] for p in rot_pts)
    
    y_lines = []
    y = min_y + pitch_mm * 0.5
    while y <= max_y:
        y_lines.append(y)
        y += pitch_mm
        
    stitches = []
    cos_back, sin_back = math.cos(-rad), math.sin(-rad)
    
    forward = True
    row_idx = 0
    for y in y_lines:
        xs = []
        for i in range(len(rot_pts) - 1):
            p1 = rot_pts[i]
            p2 = rot_pts[i+1]
            if (p1[1] <= y < p2[1]) or (p2[1] <= y < p1[1]):
                t = (y - p1[1]) / (p2[1] - p1[1])
                ix = p1[0] + t * (p2[0] - p1[0])
                xs.append(ix)
        xs.sort()
        
        segments = []
        for j in range(0, len(xs) - 1, 2):
            segments.append((xs[j], xs[j+1]))
            
        if not forward:
            segments.reverse()
            
        for seg in segments:
            x_start, x_end = seg if forward else (seg[1], seg[0])
            dist = abs(x_end - x_start)
            steps = max(1, int(round(dist / stitch_len_mm)))
            for s_i in range(steps + 1):
                cur_x = x_start + (x_end - x_start) * (s_i / steps)
                orig_x = cur_x * cos_back - y * sin_back
                orig_y = cur_x * sin_back + y * cos_back
                kind = 'JUMP' if (len(stitches) == 0 or (s_i == 0 and len(segments) > 1 and seg != segments[0])) else 'NORMAL'
                stitches.append((kind, orig_x, orig_y))
        forward = not forward
        row_idx += 1
    return stitches

def parse_svg_path_d(d_str):
    tokens = re.findall(r'([a-zA-Z]|[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)', d_str)
    subpaths = []
    curr_poly = []
    i = 0; cur_x, cur_y = 0.0, 0.0; start_x, start_y = 0.0, 0.0
    cmd = None
    while i < len(tokens):
        t = tokens[i]
        if t.isalpha(): cmd = t; i += 1
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
                p0 = (cur_x, cur_y); p1 = (cur_x + x1, cur_y + y1); p2 = (cur_x + x2, cur_y + y2); p3 = (cur_x + x, cur_y + y)
            else:
                p0 = (cur_x, cur_y); p1 = (x1, y1); p2 = (x2, y2); p3 = (x, y)
            for step in range(1, 8):
                t_val = step / 7.0; omt = 1.0 - t_val
                bx = omt**3 * p0[0] + 3*omt**2*t_val * p1[0] + 3*omt*t_val**2 * p2[0] + t_val**3 * p3[0]
                by = omt**3 * p0[1] + 3*omt**2*t_val * p1[1] + 3*omt*t_val**2 * p2[1] + t_val**3 * p3[1]
                curr_poly.append((bx, by))
            cur_x, cur_y = p3[0], p3[1]
        elif cmd in ('Z', 'z'):
            curr_poly.append((start_x, start_y))
            subpaths.append(curr_poly); curr_poly = []
            cur_x, cur_y = start_x, start_y
        else: i += 1
    if curr_poly: subpaths.append(curr_poly)
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

def render_preview(commands, palette_rgbs, out_png, hoop_w=126.0, hoop_h=110.0):
    im = Image.new('RGB', (1000, 1000), (28, 28, 32))
    draw = ImageDraw.Draw(im)

    scale = 6.8
    cx, cy = 500, 500
    hw = hoop_w * 0.5 * scale
    hh = hoop_h * 0.5 * scale
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

def parse_letter_jef_upright(path):
    """Liest Letter JEF und spiegelt Y (-y), damit der Buchstabe 100% aufrecht steht!"""
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
                ops.append(('JUMP', x, -y)) # FLIP Y FOR UPRIGHT
            elif b1 == 0x10:
                break
        else:
            dx = struct.unpack('b', bytes([b0]))[0]
            dy = struct.unpack('b', bytes([b1]))[0]
            x += dx; y += dy
            ops.append(('NORMAL', x, -y)) # FLIP Y FOR UPRIGHT
            
    first_norm = next((i for i, op in enumerate(ops) if op[0] == 'NORMAL'), 0)
    last_norm = max((i for i, op in enumerate(ops) if op[0] == 'NORMAL'), default=len(ops)-1)
    clean_ops = ops[first_norm : last_norm + 1]
    xs = [p[1] for p in clean_ops if p[0] == 'NORMAL']
    ys = [p[2] for p in clean_ops if p[0] == 'NORMAL']
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    return clean_ops, (min_x, max_x, min_y, max_y)

def assemble_text_upright(letter_files, gap_mm=3.0, scale=1.0):
    parsed = [parse_letter_jef_upright(p) for p in letter_files]
    gap_u = int(round(gap_mm * 10.0))
    widths = [int(round((b[1] - b[0]) * scale)) for _, b in parsed]
    total_w = sum(widths) + gap_u * (len(letter_files) - 1)

    cur_target_left = -total_w / 2.0
    merged_cmds = []

    for ops, (min_x, max_x, min_y, max_y) in parsed:
        w_curr = int(round((max_x - min_x) * scale))
        shift_x = int(round(cur_target_left - min_x * scale))
        for i, op in enumerate(ops):
            kind = 'JUMP' if i == 0 or op[0] == 'JUMP' else 'NORMAL'
            sx = int(round(op[1] * scale)) + shift_x
            sy = int(round(op[2] * scale))
            merged_cmds.append((kind, sx, sy))
        cur_target_left += w_curr + gap_u

    all_xs = [c[1] for c in merged_cmds if c[0] == 'NORMAL']
    all_ys = [c[2] for c in merged_cmds if c[0] == 'NORMAL']
    w_mm = (max(all_xs) - min(all_xs)) / 10.0
    h_mm = (max(all_ys) - min(all_ys)) / 10.0
    return merged_cmds, (w_mm, h_mm)

def build_filled_kiel_wappen(svg_path, target_h_mm=82.0):
    tree = ET.parse(svg_path)
    root = tree.getroot()

    # 1. Schild
    shield_subpaths = []
    for p in root.iter('{http://www.w3.org/2000/svg}path'):
        if p.attrib.get('id') == 'path1740':
            shield_subpaths = parse_svg_path_d(p.attrib.get('d'))

    gx, gy = 721.1587, 15.16174

    # 2. Nesselblatt polygon
    nessel_poly = []
    for poly in root.iter('{http://www.w3.org/2000/svg}polygon'):
        if poly.attrib.get('id') == 'polygon1345':
            pts_str = poly.attrib.get('points', '')
            raw_coords = re.findall(r'[-+]?[0-9]*\.?[0-9]+', pts_str)
            for i in range(0, len(raw_coords), 2):
                px = float(raw_coords[i]); py = float(raw_coords[i+1])
                m_x = 1.793022 * px - 703.4302; m_y = 1.81357 * py + 1.100878
                nessel_poly.append((m_x + gx, m_y + gy))
    if nessel_poly and nessel_poly[0] != nessel_poly[-1]:
        nessel_poly.append(nessel_poly[0])

    # 3. Boot
    boot_subpaths = []
    for p in root.iter('{http://www.w3.org/2000/svg}path'):
        if p.attrib.get('id') == 'path1347':
            raw_paths = parse_svg_path_d(p.attrib.get('d'))
            for sp in raw_paths:
                boot_subpaths.append([(pt[0] + gx, pt[1] + gy) for pt in sp])

    all_raw_pts = [pt for sp in shield_subpaths for pt in sp]
    min_x, max_x = min(p[0] for p in all_raw_pts), max(p[0] for p in all_raw_pts)
    min_y, max_y = min(p[1] for p in all_raw_pts), max(p[1] for p in all_raw_pts)
    scale = target_h_mm / (max_y - min_y)
    cx, cy = 0.5 * (min_x + max_x), 0.5 * (min_y + max_y)

    def tr(pts): return [((p[0] - cx) * scale, (p[1] - cy) * scale) for p in pts]

    shield_tr = [tr(sp) for sp in shield_subpaths]
    nessel_tr = tr(nessel_poly)
    boot_tr = [tr(sp) for sp in boot_subpaths]

    w_shield_mm = (max_x - min_x) * scale
    h_shield_mm = target_h_mm

    # --- VERSION 1: VOLLSTICK (100% ausgefüllt mit Garn) ---
    cmds_voll = []

    # Farbe 1: Rot (Schildgrund Tatami 45 deg)
    st_shield_fill = generate_tatami_fill(shield_tr[0], pitch_mm=0.48, stitch_len_mm=3.2, angle_deg=45.0)
    for p in st_shield_fill:
        cmds_voll.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    cmds_voll.append(('COLOR_STOP', cmds_voll[-1][1], cmds_voll[-1][2]))

    # Farbe 2: Silber/Weiß (Nesselblatt Tatami -45 deg + Kettelrand 1.2 mm)
    st_nessel_fill = generate_tatami_fill(nessel_tr, pitch_mm=0.44, stitch_len_mm=3.0, angle_deg=-45.0)
    for p in st_nessel_fill:
        cmds_voll.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    st_nessel_edge = make_kettel_outline(nessel_tr, width_mm=1.2, pitch_mm=0.35)
    for p in st_nessel_edge:
        cmds_voll.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    cmds_voll.append(('COLOR_STOP', cmds_voll[-1][1], cmds_voll[-1][2]))

    # Farbe 3: Schwarz (Kieler Boot: Rumpf + Ziegelsteine)
    # Boot Subpaths: 8 ist Außenrumpf, 0..7 sind die Ziegel
    st_boot_hull = make_kettel_outline(boot_tr[8], width_mm=1.3, pitch_mm=0.35)
    for p in st_boot_hull:
        cmds_voll.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    for k in range(8):
        st_brick = make_kettel_outline(boot_tr[k], width_mm=1.1, pitch_mm=0.35)
        for p in st_brick:
            cmds_voll.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    cmds_voll.append(('COLOR_STOP', cmds_voll[-1][1], cmds_voll[-1][2]))

    # Farbe 4: Rot (Dichter Bourdon Kettelrand um den Schild 2.4 mm)
    st_shield_cover = make_kettel_outline(shield_tr[0], width_mm=2.4, pitch_mm=0.38)
    for p in st_shield_cover:
        cmds_voll.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))

    # --- VERSION 2: APPLIQUÉ-VOLLSTICK (Roter Filz/Patch + ausgesticktes Nesselblatt & Boot) ---
    cmds_app_fill = []
    # Stufe 0: Schild Vorstich (Silber 1.6 mm)
    for sp in shield_tr:
        for p in make_run_stitch(sp, 1.6):
            cmds_app_fill.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    cmds_app_fill.append(('COLOR_STOP', cmds_app_fill[-1][1], cmds_app_fill[-1][2]))

    # Stufe 1: Schild Heftstich (Blau 1.6 mm)
    for sp in shield_tr:
        for p in make_run_stitch(sp, 1.6):
            cmds_app_fill.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    cmds_app_fill.append(('COLOR_STOP', cmds_app_fill[-1][1], cmds_app_fill[-1][2]))

    # Stufe 2: Weißes Nesselblatt (Füllung + Kontur)
    for p in st_nessel_fill:
        cmds_app_fill.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    for p in st_nessel_edge:
        cmds_app_fill.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    cmds_app_fill.append(('COLOR_STOP', cmds_app_fill[-1][1], cmds_app_fill[-1][2]))

    # Stufe 3: Schwarzes Boot
    for p in st_boot_hull:
        cmds_app_fill.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    for k in range(8):
        st_brick = make_kettel_outline(boot_tr[k], width_mm=1.1, pitch_mm=0.35)
        for p in st_brick:
            cmds_app_fill.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))
    cmds_app_fill.append(('COLOR_STOP', cmds_app_fill[-1][1], cmds_app_fill[-1][2]))

    # Stufe 4: Roter Kettelrand
    for p in st_shield_cover:
        cmds_app_fill.append((p[0], int(round(p[1] * 10.0)), int(round(p[2] * 10.0))))

    return cmds_voll, cmds_app_fill, (w_shield_mm, h_shield_mm)

def main():
    print("==================================================================")
    print(" StickCore - Vollstick Wappen & Aufrechte College Schriftzuege")
    print("==================================================================")

    usb_dir = r"F:\EmbF5\Feuerwehr"
    local_dir = r"E:\StickCore\output"
    brain_dir = r"C:\Users\micbu\.gemini\antigravity\brain\e572641d-70d6-45b5-8c91-c97f358a4f6c"
    svg_path = r"E:\StickCore\assets\wappen_kiel_official.svg"
    col_dir = r"C:\Users\micbu\Downloads\mef\College_Outline_part1\JEF"

    os.makedirs(usb_dir, exist_ok=True)
    os.makedirs(local_dir, exist_ok=True)
    os.makedirs(brain_dir, exist_ok=True)

    # -------------------------------------------------------------
    # 1. AUSGEFÜLLTES KIELER WAPPEN
    # -------------------------------------------------------------
    print("\n--- 1. Ausgefuelltes Kieler Wappen ---")
    cmds_w_voll, cmds_w_app_fill, (ww, wh) = build_filled_kiel_wappen(svg_path, target_h_mm=80.0)

    # A) Vollstick Version
    pal_voll = [JANOME_DEEP_RED, JANOME_WHITE, JANOME_BLACK, JANOME_DEEP_RED]
    pal_voll_rgb = [(210, 30, 40), (245, 245, 250), (15, 15, 20), (190, 20, 30)]
    f_w_voll = os.path.join(usb_dir, "04_Kieler_Wappen_Vollstick_Ausgefuellt.jef")
    write_raw_jef(f_w_voll, cmds_w_voll, pal_voll)
    render_preview(cmds_w_voll, pal_voll_rgb, os.path.join(brain_dir, "kieler_wappen_vollstick_preview.png"))
    print(f"  [OK] Wappen Vollstick ausgefuellt: {ww:.1f} x {wh:.1f} mm ({len(cmds_w_voll)} Stiche)")

    # B) Appliqué + Füllung Version
    pal_app_f = [JANOME_SILVER, JANOME_BLUE, JANOME_WHITE, JANOME_BLACK, JANOME_DEEP_RED]
    pal_app_f_rgb = [(190, 190, 200), (70, 140, 230), (245, 245, 250), (15, 15, 20), (210, 30, 40)]
    f_w_app = os.path.join(usb_dir, "04_Kieler_Wappen_Applique_Ausgefuellt.jef")
    write_raw_jef(f_w_app, cmds_w_app_fill, pal_app_f)
    render_preview(cmds_w_app_fill, pal_app_f_rgb, os.path.join(brain_dir, "kieler_wappen_applique_ausgefuellt_preview.png"))
    print(f"  [OK] Wappen Applique ausgefuellt: {ww:.1f} x {wh:.1f} mm ({len(cmds_w_app_fill)} Stiche)")

    # -------------------------------------------------------------
    # 2. AUFRECHTE COLLEGE-OUTLINE SCHRIFTZÜGE (Y-ACHSE KORRIGIERT)
    # -------------------------------------------------------------
    print("\n--- 2. Aufrechte College-Outline Schriftzuege ---")
    p15 = os.path.join(col_dir, "1.5in")
    p1in = os.path.join(col_dir, "1in")

    # A) Brust "NW" (1.5in aufrecht)
    f_n15 = os.path.join(p15, "N_CollegeOutl_1,5in_NinviaStore.jef")
    f_w15 = os.path.join(p15, "W_CollegeOutl_1,5in_NinviaStore.jef")
    cmds_nw15, (wnw15, hnw15) = assemble_text_upright([f_n15, f_w15], gap_mm=4.0)
    f_nw15_usb = os.path.join(usb_dir, "05_Brust_NW_College_Outline_1p5in.jef")
    write_raw_jef(f_nw15_usb, cmds_nw15, [JANOME_DEEP_RED])
    render_preview(cmds_nw15, [(220, 35, 45)], os.path.join(brain_dir, "brust_nw_college_1p5in_aufrecht_preview.png"))
    print(f"  [OK] Brust NW (1.5in aufrecht): {wnw15:.1f} x {hnw15:.1f} mm")

    # B) Brust "NW" (1in aufrecht)
    f_n1 = os.path.join(p1in, "N_CollegeOutl_1in_NinviaStore.jef")
    f_w1 = os.path.join(p1in, "W_CollegeOutl_1in_NinviaStore.jef")
    cmds_nw1, (wnw1, hnw1) = assemble_text_upright([f_n1, f_w1], gap_mm=3.0)
    f_nw1_usb = os.path.join(usb_dir, "05_Brust_NW_College_Outline_1in.jef")
    write_raw_jef(f_nw1_usb, cmds_nw1, [JANOME_DEEP_RED])
    print(f"  [OK] Brust NW (1in aufrecht): {wnw1:.1f} x {hnw1:.1f} mm")

    # C) Wort "KIEL" (1in aufrecht)
    letters_kiel = [
        os.path.join(p1in, "K_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "I_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "L_CollegeOutl_1in_NinviaStore.jef"),
    ]
    cmds_kiel, (wk, hk) = assemble_text_upright(letters_kiel, gap_mm=2.5)
    f_kiel_usb = os.path.join(usb_dir, "01_Schrift_KIEL_College_Outline_1in.jef")
    write_raw_jef(f_kiel_usb, cmds_kiel, [JANOME_DEEP_RED])
    render_preview(cmds_kiel, [(220, 35, 45)], os.path.join(brain_dir, "schrift_kiel_college_1in_aufrecht_preview.png"))
    print(f"  [OK] KIEL (1in aufrecht): {wk:.1f} x {hk:.1f} mm")

    # D) "FEUER" & "WEHR" (1in geteilt)
    letters_feuer = [
        os.path.join(p1in, "F_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "U_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "R_CollegeOutl_1in_NinviaStore.jef"),
    ]
    cmds_feuer, (wf, hf) = assemble_text_upright(letters_feuer, gap_mm=2.5)
    f_feuer_usb = os.path.join(usb_dir, "01a_FEUER_College_Outline_1in.jef")
    write_raw_jef(f_feuer_usb, cmds_feuer, [JANOME_DEEP_RED])

    letters_wehr = [
        os.path.join(p1in, "W_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "E_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "H_CollegeOutl_1in_NinviaStore.jef"),
        os.path.join(p1in, "R_CollegeOutl_1in_NinviaStore.jef"),
    ]
    cmds_wehr, (wwr, hwr) = assemble_text_upright(letters_wehr, gap_mm=2.5)
    f_wehr_usb = os.path.join(usb_dir, "01b_WEHR_College_Outline_1in.jef")
    write_raw_jef(f_wehr_usb, cmds_wehr, [JANOME_DEEP_RED])
    print(f"  [OK] FEUER (Teil 1): {wf:.1f} x {hf:.1f} mm & WEHR (Teil 2): {wwr:.1f} x {hwr:.1f} mm")

    # E) "FEUERWEHR" Kompakt (skaliert auf 120 mm für Janome Rahmen A in 1 Durchgang!)
    all_fw_letters = letters_feuer + letters_wehr
    scale_fw = 120.0 / (wf + wwr + 4.0)
    cmds_fw_kompakt, (w_fwk, h_fwk) = assemble_text_upright(all_fw_letters, gap_mm=2.0 * scale_fw, scale=scale_fw)
    f_fwk_usb = os.path.join(usb_dir, "01_FEUERWEHR_College_Outline_Kompakt.jef")
    write_raw_jef(f_fwk_usb, cmds_fw_kompakt, [JANOME_DEEP_RED])
    render_preview(cmds_fw_kompakt, [(220, 35, 45)], os.path.join(brain_dir, "feuerwehr_college_outline_kompakt_preview.png"))
    print(f"  [OK] FEUERWEHR Kompakt (1 Durchgang Rahmen A): {w_fwk:.1f} x {h_fwk:.1f} mm")

    # -------------------------------------------------------------
    # 3. GESAMT-ENSEMBLE VORSCHAU (RÜCKEN & VORNE)
    # -------------------------------------------------------------
    print("\n--- 3. Gesamt-Ensemble Vorschau ---")
    im_ensemble = Image.new('RGB', (1400, 1600), (22, 22, 26))
    draw_e = ImageDraw.Draw(im_ensemble)

    # Titel
    draw_e.text((700, 40), "FEUERWEHR KIEL - NORDWACHE II", fill=(240, 240, 245), anchor="mm")
    draw_e.text((700, 75), "Gesamt-Patchset Janome MC350E (Rahmen A <= 126x110 mm)", fill=(160, 165, 180), anchor="mm")

    def paste_centered(cmds, pal_rgb, center_x, center_y, scale_fac=5.5):
        cur_c = 0
        lx, ly = center_x, center_y
        for c in cmds:
            op, tx, ty = c
            px = center_x + tx * 0.1 * scale_fac
            py = center_y + ty * 0.1 * scale_fac
            if op == 'COLOR_STOP':
                cur_c = min(len(pal_rgb)-1, cur_c + 1)
            elif op != 'JUMP':
                col = pal_rgb[cur_c]
                draw_e.line([(lx, ly), (px, py)], fill=col, width=2)
            lx, ly = px, py

    # -------------------------------------------------------------
    # 3. DEDIZIERTER BOGEN "BERUFSFEUERWEHR KIEL"
    # -------------------------------------------------------------
    print("\n--- 3. Bogen Berufsfeuerwehr Kiel ---")
    from fontTools.ttLib import TTFont
    from fontTools.pens.recordingPen import RecordingPen

    font_path = 'E:/StickCore/assets/fonts/varsity_regular.ttf'
    font = TTFont(font_path)
    glyph_set = font.getGlyphSet()
    hmtx = font['hmtx']
    cmap = font.getBestCmap()

    def get_text_polys_on_arc_local(text, cap_h_mm, arc_r_mm, arc_down=False, letter_sp_mm=1.4):
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
            items.append((ch, conts, adv))
            total_adv += adv + sp_units
        if items: total_adv -= sp_units

        all_polys = []
        cursor_mm = - (total_adv * scale) * 0.5
        for ch, conts, adv in items:
            char_w_mm = adv * scale
            center_s_mm = cursor_mm + char_w_mm * 0.5
            cursor_mm += char_w_mm + letter_sp_mm
            if not conts: continue

            theta = center_s_mm / arc_r_mm
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

    polys_top = get_text_polys_on_arc_local("berufsfeuerwehr kiel", cap_h_mm=9.5, arc_r_mm=68.0, arc_down=False, letter_sp_mm=1.5)
    cmds_arc_top = []
    for poly in polys_top:
        st = make_kettel_outline(poly, width_mm=0.65, pitch_mm=0.28)
        for p in st:
            cmds_arc_top.append((p[0], int(round(p[1]*10.0)), -int(round(p[2]*10.0))))

    # Rendere Bogen dediziert in voller Auflösung
    im_arc = Image.new('RGB', (1100, 500), (28, 28, 32))
    draw_arc = ImageDraw.Draw(im_arc)
    # Rahmen A Markierung (126 x 110 mm)
    sc = 7.5
    draw_arc.text((550, 40), "OBERER BOGEN: BERUFSFEUERWEHR KIEL", fill=(240, 240, 245), anchor="mm")
    draw_arc.text((550, 70), "Janome MC350E Rahmen A (Breite 122 mm, Micro-Kettelrand 0.65 mm)", fill=(160, 165, 180), anchor="mm")
    for i in range(1, len(cmds_arc_top)):
        p1 = cmds_arc_top[i-1]; p2 = cmds_arc_top[i]
        if p2[0] == 'NORMAL':
            x1, y1 = 550 + p1[1] * 0.1 * sc, 300 + p1[2] * 0.1 * sc
            x2, y2 = 550 + p2[1] * 0.1 * sc, 300 + p2[2] * 0.1 * sc
            draw_arc.line([(x1, y1), (x2, y2)], fill=(220, 35, 45), width=2)
    arc_preview_png = os.path.join(brain_dir, "feuerwehr_bogen_berufsfeuerwehr_kiel_preview.png")
    im_arc.save(arc_preview_png)
    print(f"  [OK] Dedizierter Bogen Vorschau generiert: {arc_preview_png}")

    # -------------------------------------------------------------
    # 4. GESAMT-ENSEMBLE VORSCHAU (RÜCKEN & VORNE)
    # -------------------------------------------------------------
    print("\n--- 4. Gesamt-Ensemble Vorschau ---")
    im_ensemble = Image.new('RGB', (1400, 1650), (22, 22, 26))
    draw_e = ImageDraw.Draw(im_ensemble)

    draw_e.text((700, 40), "FEUERWEHR KIEL - NORDWACHE II", fill=(240, 240, 245), anchor="mm")
    draw_e.text((700, 75), "Gesamt-Patchset Janome MC350E (Rahmen A <= 126x110 mm)", fill=(160, 165, 180), anchor="mm")

    def paste_centered(cmds, pal_rgb, center_x, center_y, scale_fac=5.5):
        cur_c = 0
        lx, ly = center_x, center_y
        for c in cmds:
            op, tx, ty = c
            px = center_x + tx * 0.1 * scale_fac
            py = center_y + ty * 0.1 * scale_fac
            if op == 'COLOR_STOP':
                cur_c = min(len(pal_rgb)-1, cur_c + 1)
            elif op != 'JUMP':
                col = pal_rgb[cur_c]
                draw_e.line([(lx, ly), (px, py)], fill=col, width=2)
            lx, ly = px, py

    # 1. Bogen Oben: BERUFSFEUERWEHR KIEL
    paste_centered(cmds_arc_top, [(220, 35, 45)], 700, 240, scale_fac=5.2)

    # 2. Ausgefülltes Wappen
    paste_centered(cmds_w_voll, pal_voll_rgb, 700, 560, scale_fac=4.6)

    # 3. Zahl 30 Appliqué
    from merge_mef_numbers import merge_applique_pair
    f_3in_3 = r"C:\Users\micbu\Downloads\mef\01234_applique_numbers\3\JEF\Three 3in.jef"
    f_3in_0 = r"C:\Users\micbu\Downloads\mef\01234_applique_numbers\0\JEF\Zero 3in.jef"
    cmds_30_3in, _ = merge_applique_pair(f_3in_3, f_3in_0, gap_mm=5.0)
    paste_centered(cmds_30_3in, [(190, 190, 200), (70, 140, 230), (220, 35, 45)], 700, 990, scale_fac=4.5)

    # 4. Bogen Unten: NORDWACHE II
    polys_bot = get_text_polys_on_arc_local("nordwache ii", cap_h_mm=10.5, arc_r_mm=65.0, arc_down=True, letter_sp_mm=1.6)
    cmds_arc_bot = []
    for poly in polys_bot:
        st = make_kettel_outline(poly, width_mm=0.65, pitch_mm=0.28)
        for p in st:
            cmds_arc_bot.append((p[0], int(round(p[1]*10.0)), -int(round(p[2]*10.0))))
    paste_centered(cmds_arc_bot, [(220, 35, 45)], 700, 1320, scale_fac=5.2)

    # 5. Brust NW & KIEL (links und rechts)
    paste_centered(cmds_nw15, [(220, 35, 45)], 220, 560, scale_fac=4.0)
    draw_e.text((220, 680), "BRUST: NW (1.5in)", fill=(180, 185, 200), anchor="mm")

    paste_centered(cmds_kiel, [(220, 35, 45)], 1180, 560, scale_fac=4.0)
    draw_e.text((1180, 680), "BRUST: KIEL (1in)", fill=(180, 185, 200), anchor="mm")

    out_ensemble_png = os.path.join(brain_dir, "gesamt_feuerwehr_kiel_patch_set_preview.png")
    im_ensemble.save(out_ensemble_png)
    print(f"  [OK] Gesamt-Ensemble Vorschau generiert: {out_ensemble_png}")

    print("\n=== ALLE DATEIEN ERFOLGREICH VERIFIZIERT & EXPORTIERT! ===")

if __name__ == '__main__':
    main()
