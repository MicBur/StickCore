# -*- coding: utf-8 -*-
"""
StickCore - MEF Varsity Appliqué Merger
Merges authentic collegiate appliqué numbers (0 and 3) from:
  C:/Users/micbu/Downloads/mef/01234_applique_numbers/
into ready-to-sew Janome MC350E (.jef) files for Hoop A (<= 126 x 110 mm).

Workflow per Appliqué design:
  Stage 0 (Color 1 - Silver/White): Placement Line (Vorstich 1.6 mm) for both numbers
  Color Stop (0x80 0x01): Machine pauses -> User lays down fabric/felt
  Stage 1 (Color 2 - Blue): Tackdown Line (Heftstich 1.6 mm) for both numbers
  Color Stop (0x80 0x01): Machine pauses -> User trims fabric along edges
  Stage 2 (Color 3 - Deep Red): Dense Satin Cover (Bourdon 2.4 mm) enclosing all cut edges
  End (0x80 0x10 0x00 0x00)
"""

import os
import struct
import datetime
from PIL import Image, ImageDraw

# Janome Thread Palette
JANOME_SILVER = 10
JANOME_BLUE = 7
JANOME_DEEP_RED = 13
JANOME_WHITE = 15
JANOME_BLACK = 1

def read_jef_raw(path):
    """Liest JEF und gibt Liste von Stages [(type, x, y), ...] zurück."""
    with open(path, 'rb') as f:
        data = f.read()
    st_offset, = struct.unpack('<I', data[0:4])
    pos = st_offset
    stages = []
    cur_ops = []
    x, y = 0, 0
    while pos < len(data):
        b0 = data[pos]
        b1 = data[pos+1]
        pos += 2
        if b0 == 0x80:
            if b1 == 0x01: # color stop
                dx = struct.unpack('b', bytes([data[pos]]))[0]
                dy = struct.unpack('b', bytes([data[pos+1]]))[0]
                pos += 2
                x += dx
                y += dy
                stages.append(cur_ops)
                cur_ops = []
            elif b1 == 0x02: # jump
                dx = struct.unpack('b', bytes([data[pos]]))[0]
                dy = struct.unpack('b', bytes([data[pos+1]]))[0]
                pos += 2
                x += dx
                y += dy
                cur_ops.append(('JUMP', x, y))
            elif b1 == 0x10: # end
                break
        else:
            dx = struct.unpack('b', bytes([b0]))[0]
            dy = struct.unpack('b', bytes([b1]))[0]
            x += dx
            y += dy
            cur_ops.append(('NORMAL', x, y))
    if cur_ops:
        stages.append(cur_ops)
    return stages

def clean_stage_ops(ops):
    """Entfernt künstliche Rücksprünge nach (0,0) am Anfang/Ende, behält innere Sprünge."""
    first_norm = next((i for i, op in enumerate(ops) if op[0] == 'NORMAL'), 0)
    last_norm = max((i for i, op in enumerate(ops) if op[0] == 'NORMAL'), default=len(ops)-1)
    return ops[first_norm : last_norm + 1]

def write_raw_jef(path, raw_commands, palette_codes, hoop_code=0):
    """
    Schreibt raw JEF Stitch stream byte-exakt für Janome MC350E.
    raw_commands: Liste von:
      ('NORMAL', x, y)
      ('JUMP', x, y)
      ('COLOR_STOP', x, y)
    Koordinaten in 0.1 mm Einheiten.
    """
    k_fixed_header = 0x74
    k_version = 0x14
    k_max_delta = 127
    k_byte16 = 0x64
    k_color_attrib = 0x0D
    color_count = len(palette_codes)

    # Berechne Bounding Box aller NORMAL Stiche
    norm_xs = [c[1] for c in raw_commands if c[0] == 'NORMAL']
    norm_ys = [c[2] for c in raw_commands if c[0] == 'NORMAL']
    if not norm_xs:
        raise ValueError("Keine druckbaren Stiche!")

    min_x, max_x = min(norm_xs), max(norm_xs)
    min_y, max_y = min(norm_ys), max(norm_ys)

    # Encode moves
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
        else: # NORMAL
            encode_delta(dx, dy, 0)
        cur_x, cur_y = tx, ty

    # Return jump to center (0, 0)
    if cur_x != 0 or cur_y != 0:
        encode_delta(-cur_x, -cur_y, 0x02)
        cur_x, cur_y = 0, 0

    # End marker
    body.extend(bytes([0x80, 0x10, 0x00, 0x00]))
    count_field += 1

    # Header zusammenbauen
    header = bytearray()
    header.extend(struct.pack('<i', k_fixed_header + color_count * 8)) # 0x00
    header.extend(struct.pack('<i', k_version))                         # 0x04
    date_str = datetime.datetime.now().strftime("%Y%m%d%H%M%S").encode('ascii').ljust(14, b'0')[:14]
    header.extend(date_str)                                            # 0x08
    header.extend(bytes([k_byte16, 0x00]))                             # 0x16
    header.extend(struct.pack('<i', color_count))                      # 0x18
    header.extend(struct.pack('<i', count_field))                      # 0x1C
    header.extend(struct.pack('<i', hoop_code))                        # 0x20
    header.extend(struct.pack('<i', -min_x))                           # 0x24 distLeft
    header.extend(struct.pack('<i',  max_y))                           # 0x28 distTop
    header.extend(struct.pack('<i',  max_x))                           # 0x2C distRight
    header.extend(struct.pack('<i', -min_y))                           # 0x30 distBottom
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

def merge_applique_pair(p3, p0, gap_mm=5.0):
    """
    Führt die Stages von 3 und 0 zu einem nahtlosen 3-Stufen Appliqué-Muster zusammen.
    Gibt (commands, (w_mm, h_mm)) zurück.
    """
    st3 = read_jef_raw(p3)
    st0 = read_jef_raw(p0)

    c3 = [clean_stage_ops(st3[i]) for i in range(3)]
    c0 = [clean_stage_ops(st0[i]) for i in range(3)]

    # Berechne Breite der Satin-Stufe (Stage 2) für perfekte Ausrichtung
    xs3 = [p[1] for p in c3[2] if p[0] == 'NORMAL']
    xs0 = [p[1] for p in c0[2] if p[0] == 'NORMAL']
    w3_u = max(xs3) - min(xs3)
    w0_u = max(xs0) - min(xs0)
    gap_u = int(round(gap_mm * 10.0))
    total_w_u = w3_u + gap_u + w0_u

    # Symmetrische Zentrierung um x = 0
    left_3_target = -total_w_u / 2.0
    shift3_u = int(round(left_3_target - min(xs3)))
    right_0_target = total_w_u / 2.0
    shift0_u = int(round(right_0_target - max(xs0)))

    merged_commands = []

    for stage_idx in range(3):
        # 1. Ziffer 3 Stiche dieser Stufe
        ops_3 = c3[stage_idx]
        for i, op in enumerate(ops_3):
            sx = op[1] + shift3_u
            sy = op[2]
            kind = 'JUMP' if (i == 0 or op[0] == 'JUMP') else 'NORMAL'
            merged_commands.append((kind, sx, sy))

        # 2. Sprung zur Ziffer 0
        ops_0 = c0[stage_idx]
        first_0_x = ops_0[0][1] + shift0_u
        first_0_y = ops_0[0][2]
        merged_commands.append(('JUMP', first_0_x, first_0_y))

        # 3. Ziffer 0 Stiche dieser Stufe
        for i, op in enumerate(ops_0):
            sx = op[1] + shift0_u
            sy = op[2]
            kind = 'JUMP' if (i == 0 or op[0] == 'JUMP') else 'NORMAL'
            merged_commands.append((kind, sx, sy))

        # Farbstopp am Ende von Stufe 0 und Stufe 1
        if stage_idx < 2:
            last_x = ops_0[-1][1] + shift0_u
            last_y = ops_0[-1][2]
            merged_commands.append(('COLOR_STOP', last_x, last_y))

    # Bounding Box
    all_xs = [c[1] for c in merged_commands if c[0] == 'NORMAL']
    all_ys = [c[2] for c in merged_commands if c[0] == 'NORMAL']
    w_mm = (max(all_xs) - min(all_xs)) / 10.0
    h_mm = (max(all_ys) - min(all_ys)) / 10.0

    return merged_commands, (w_mm, h_mm)

def extract_kettel_only(merged_commands):
    """Filtert nur die finale Satinstufe (Stage 2) heraus für reine Kettel-Stickung."""
    stops = [i for i, c in enumerate(merged_commands) if c[0] == 'COLOR_STOP']
    if len(stops) >= 2:
        kettel_slice = merged_commands[stops[1] + 1 :]
    else:
        kettel_slice = merged_commands
    # Setze ersten Stich als JUMP
    kettel_cmds = []
    for i, c in enumerate(kettel_slice):
        kind = 'JUMP' if i == 0 else c[0]
        kettel_cmds.append((kind, c[1], c[2]))
    return kettel_cmds

def center_single_number(path):
    """Zentriert eine einzelne Zahl (z.B. 4in) exakt bei (0,0) und behält alle 3 Stufen."""
    stages = read_jef_raw(path)
    clean_stages = [clean_stage_ops(stages[i]) for i in range(3)]
    # Berechne Schwerpunkt / Mitte
    all_xs = [p[1] for s in clean_stages for p in s if p[0] == 'NORMAL']
    all_ys = [p[2] for s in clean_stages for p in s if p[0] == 'NORMAL']
    cx = int(round(0.5 * (min(all_xs) + max(all_xs))))
    cy = int(round(0.5 * (min(all_ys) + max(all_ys))))

    cmds = []
    for stage_idx in range(3):
        ops = clean_stages[stage_idx]
        for i, op in enumerate(ops):
            kind = 'JUMP' if (i == 0 or op[0] == 'JUMP') else 'NORMAL'
            cmds.append((kind, op[1] - cx, op[2] - cy))
        if stage_idx < 2:
            last_op = ops[-1]
            cmds.append(('COLOR_STOP', last_op[1] - cx, last_op[2] - cy))

    w_mm = (max(all_xs) - min(all_xs)) / 10.0
    h_mm = (max(all_ys) - min(all_ys)) / 10.0
    return cmds, (w_mm, h_mm)

def render_preview(commands, palette_rgbs, out_png, title="Janome MC350E - Rahmen A"):
    """Rendert eine hochauflösende 1000x1000 Pixel Stickvorschau mit Rahmen A Markierung."""
    im = Image.new('RGB', (1000, 1000), (28, 28, 32))
    draw = ImageDraw.Draw(im)

    # Zeichne Rahmen A (126 x 110 mm)
    # Skalierung: 1 mm = 6.8 Pixel -> 126 mm = 856.8 px, 110 mm = 748 px
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
            # Gepunktete Linie für Reise-/Sprungstiche
            draw.line([(cur_x, cur_y), (px, py)], fill=(70, 75, 90), width=1)
        else: # NORMAL
            c = palette_rgbs[cur_color_idx]
            width = 3 if cur_color_idx == 2 else 2
            draw.line([(cur_x, cur_y), (px, py)], fill=c, width=width)
        cur_x, cur_y = px, py

    os.makedirs(os.path.dirname(os.path.abspath(out_png)), exist_ok=True)
    im.save(out_png)
    print(f"  [PREVIEW] Bild generiert: {out_png}")

def run_all():
    print("==================================================================")
    print(" StickCore - MEF Varsity Applique Merger & Janome MC350E Export")
    print("==================================================================")

    p0_dir = r"C:\Users\micbu\Downloads\mef\01234_applique_numbers\0\JEF"
    p3_dir = r"C:\Users\micbu\Downloads\mef\01234_applique_numbers\3\JEF"
    usb_dir = r"F:\EmbF5\Feuerwehr"
    local_dir = r"E:\StickCore\output\mef_jef"
    brain_dir = r"C:\Users\micbu\.gemini\antigravity\brain\e572641d-70d6-45b5-8c91-c97f358a4f6c"

    os.makedirs(usb_dir, exist_ok=True)
    os.makedirs(local_dir, exist_ok=True)
    os.makedirs(brain_dir, exist_ok=True)

    pal_app = [JANOME_SILVER, JANOME_BLUE, JANOME_DEEP_RED]
    pal_app_rgbs = [(190, 190, 200), (70, 140, 230), (220, 35, 45)]

    pal_kettel = [JANOME_DEEP_RED]
    pal_kettel_rgbs = [(220, 35, 45)]

    # 1. HAUPTDATEI: 3in Appliqué Zahl 30 (106.6 x 75.0 mm -> Rahmen A)
    print("\n--- 1. Zahl 30 Applique (3in - 106.6 x 75.0 mm) ---")
    file_3in_3 = os.path.join(p3_dir, "Three 3in.jef")
    file_3in_0 = os.path.join(p0_dir, "Zero 3in.jef")
    cmds_30_3in, (w30_3, h30_3) = merge_applique_pair(file_3in_3, file_3in_0, gap_mm=5.0)

    out_app_3in_usb = os.path.join(usb_dir, "02_MITTE_Zahl_30_Applique_Original_Mef.jef")
    out_app_3in_loc = os.path.join(local_dir, "02_MITTE_Zahl_30_Applique_Original_Mef.jef")
    write_raw_jef(out_app_3in_usb, cmds_30_3in, pal_app)
    write_raw_jef(out_app_3in_loc, cmds_30_3in, pal_app)
    print(f"  [OK] Applique 3in geschrieben: {w30_3:.1f} x {h30_3:.1f} mm ({len(cmds_30_3in)} ops)")

    render_preview(cmds_30_3in, pal_app_rgbs, os.path.join(brain_dir, "feuerwehr_30_mef_applique_preview.png"))

    # Reine Kettel-Version (nur dichter roter Satinstich ohne Vorstopps)
    cmds_kettel_3in = extract_kettel_only(cmds_30_3in)
    out_ket_3in_usb = os.path.join(usb_dir, "02_MITTE_Zahl_30_Kettel_Original_Mef.jef")
    write_raw_jef(out_ket_3in_usb, cmds_kettel_3in, pal_kettel)
    print(f"  [OK] Kettel-only 3in geschrieben: ({len(cmds_kettel_3in)} ops)")

    # 2. MAXIMALGRÖSSE RAHMEN A: 3.5in Appliqué Zahl 30 (122.2 x 88.0 mm)
    print("\n--- 2. Zahl 30 Applique Maximal (3.5in - 122.2 x 88.0 mm) ---")
    file_3p5_3 = os.path.join(p3_dir, "Three 3p5in.jef")
    file_3p5_0 = os.path.join(p0_dir, "Zero 3p5in.jef")
    cmds_30_3p5, (w30_3p5, h30_3p5) = merge_applique_pair(file_3p5_3, file_3p5_0, gap_mm=3.5)

    out_app_3p5_usb = os.path.join(usb_dir, "02_MITTE_Zahl_30_Applique_Maximal_3p5in.jef")
    write_raw_jef(out_app_3p5_usb, cmds_30_3p5, pal_app)
    print(f"  [OK] Applique 3.5in geschrieben: {w30_3p5:.1f} x {h30_3p5:.1f} mm ({len(cmds_30_3p5)} ops)")

    # 3. KOMPAKT: 2.5in Appliqué Zahl 30 (89.4 x 63.0 mm)
    print("\n--- 3. Zahl 30 Applique Kompakt (2.5in - 89.4 x 63.0 mm) ---")
    file_2p5_3 = os.path.join(p3_dir, "Three 2p5in.jef")
    file_2p5_0 = os.path.join(p0_dir, "Zero 2p5in.jef")
    cmds_30_2p5, (w30_2p5, h30_2p5) = merge_applique_pair(file_2p5_3, file_2p5_0, gap_mm=4.0)

    out_app_2p5_usb = os.path.join(usb_dir, "02_MITTE_Zahl_30_Applique_Kompakt_2p5in.jef")
    write_raw_jef(out_app_2p5_usb, cmds_30_2p5, pal_app)
    print(f"  [OK] Applique 2.5in geschrieben: {w30_2p5:.1f} x {h30_2p5:.1f} mm ({len(cmds_30_2p5)} ops)")

    # 4. ÄRMEL & BRUST: 2in Appliqué Zahl 30 (72.2 x 50.0 mm)
    print("\n--- 4. Ärmel / Brust Zahl 30 Applique (2in - 72.2 x 50.0 mm) ---")
    file_2in_3 = os.path.join(p3_dir, "Three 2in.jef")
    file_2in_0 = os.path.join(p0_dir, "Zero 2in.jef")
    cmds_30_2in, (w30_2, h30_2) = merge_applique_pair(file_2in_3, file_2in_0, gap_mm=4.0)

    out_app_2in_usb = os.path.join(usb_dir, "06_Aermel_30_Applique_Original_Mef_2in.jef")
    write_raw_jef(out_app_2in_usb, cmds_30_2in, pal_app)
    render_preview(cmds_30_2in, pal_app_rgbs, os.path.join(brain_dir, "feuerwehr_aermel_30_mef_preview.png"))

    cmds_ket_2in = extract_kettel_only(cmds_30_2in)
    out_ket_2in_usb = os.path.join(usb_dir, "06_Aermel_30_Kettel_Original_Mef_2in.jef")
    write_raw_jef(out_ket_2in_usb, cmds_ket_2in, pal_kettel)
    print(f"  [OK] Ärmel Applique & Kettel 2in geschrieben: {w30_2:.1f} x {h30_2:.1f} mm")

    # 5. GROSSE EINZELZIFFERN: 4in Solo (100.0 mm Höhe für solo Hoop A)
    print("\n--- 5. Große Einzelziffern (4in - 100.0 mm Höhe) ---")
    file_4in_3 = os.path.join(p3_dir, "Three 4in.jef")
    file_4in_0 = os.path.join(p0_dir, "Zero 4in.jef")

    cmds_3_4in, (w3_4, h3_4) = center_single_number(file_4in_3)
    out_3_4in_usb = os.path.join(usb_dir, "02a_MITTE_Gross_Ziffer_3_Original_Mef_4in.jef")
    write_raw_jef(out_3_4in_usb, cmds_3_4in, pal_app)

    cmds_0_4in, (w0_4, h0_4) = center_single_number(file_4in_0)
    out_0_4in_usb = os.path.join(usb_dir, "02b_MITTE_Gross_Ziffer_0_Original_Mef_4in.jef")
    write_raw_jef(out_0_4in_usb, cmds_0_4in, pal_app)
    print(f"  [OK] Solo 3 (4in) geschrieben: {w3_4:.1f} x {h3_4:.1f} mm")
    print(f"  [OK] Solo 0 (4in) geschrieben: {w0_4:.1f} x {h0_4:.1f} mm")

    print("\n=== ALLE ORIGINAL MEF APPLIQUE-DATEIEN ERFOLGREICH GENERIERT! ===")

if __name__ == '__main__':
    run_all()
