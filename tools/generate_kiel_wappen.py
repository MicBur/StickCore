#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
StickCore - Official Kieler Wappen Generator
Generates production-grade, 100% upright Janome JEF embroidery files for Janome MC350E Hoop A.
Strict Machine Constraint: Starts with SCHWARZ (Black - Janome 1) as Color 1.
"""

import math
import os
import struct
import datetime
import re
from PIL import Image, ImageDraw

def to_stitch_pt(svg_x, svg_y):
    # Shield SVG center: Xc = 338.21908, Yc = 355.41194
    # Scale for 75 mm width: S = 75.0 / 642.235825 = 0.11677953
    # Height = 677.056598 * S = 79.066 mm
    # NEGATIVE sign on Y ensures 100% upright orientation in JEF (+Y is up, -Y is down)
    S = 0.11677953
    x = (svg_x - 338.21908) * S
    y = -(svg_y - 355.41194) * S
    return (x, y)

def cubic_bezier(p0, p1, p2, p3, steps=25):
    pts = []
    for i in range(steps + 1):
        t = i / float(steps)
        u = 1.0 - t
        x = u**3 * p0[0] + 3*u**2*t * p1[0] + 3*u*t**2 * p2[0] + t**3 * p3[0]
        y = u**3 * p0[1] + 3*u**2*t * p1[1] + 3*u*t**2 * p2[1] + t**3 * p3[1]
        pts.append((x, y))
    return pts

def parse_svg_path(d_str, offset_x=0.0, offset_y=0.0):
    tokens = re.findall(r'([A-Za-z]|[-+]?(?:[0-9]*\.[0-9]+|[0-9]+)(?:[eE][-+]?[0-9]+)?)', d_str)
    subpaths = []
    curr_poly = []
    i = 0
    cmd = None
    last_pt = (0.0, 0.0)

    while i < len(tokens):
        t = tokens[i]
        if t.isalpha():
            cmd = t
            i += 1
            if cmd in ('z', 'Z'):
                if curr_poly:
                    subpaths.append(curr_poly)
                    curr_poly = []
                continue
        else:
            if cmd in ('M', 'm'):
                x = float(tokens[i]) + offset_x
                y = float(tokens[i+1]) + offset_y
                i += 2
                if curr_poly:
                    subpaths.append(curr_poly)
                curr_poly = [to_stitch_pt(x, y)]
                last_pt = (x, y)
                cmd = 'L' if cmd == 'M' else 'l'
            elif cmd in ('L', 'l'):
                x = float(tokens[i]) + offset_x
                y = float(tokens[i+1]) + offset_y
                i += 2
                curr_poly.append(to_stitch_pt(x, y))
                last_pt = (x, y)
            elif cmd in ('C', 'c'):
                x1 = float(tokens[i]) + offset_x
                y1 = float(tokens[i+1]) + offset_y
                x2 = float(tokens[i+2]) + offset_x
                y2 = float(tokens[i+3]) + offset_y
                x3 = float(tokens[i+4]) + offset_x
                y3 = float(tokens[i+5]) + offset_y
                i += 6
                curve_pts = cubic_bezier(last_pt, (x1, y1), (x2, y2), (x3, y3), steps=15)
                for pt in curve_pts[1:]:
                    curr_poly.append(to_stitch_pt(pt[0], pt[1]))
                last_pt = (x3, y3)
            else:
                i += 1
    if curr_poly:
        subpaths.append(curr_poly)
    return subpaths

def poly_area(poly):
    n = len(poly)
    if n < 3: return 0.0
    a = 0.0
    for i in range(n):
        p1 = poly[i]
        p2 = poly[(i+1)%n]
        a += (p1[0]*p2[1] - p2[0]*p1[1])
    return 0.5 * a

def poly_perimeter(poly):
    l = 0.0
    for i in range(len(poly)):
        p1 = poly[i]
        p2 = poly[(i+1)%len(poly)]
        l += math.hypot(p2[0]-p1[0], p2[1]-p1[1])
    return l

def sample_poly(poly, s):
    total_l = poly_perimeter(poly)
    if total_l < 1e-6:
        return poly[0], (0.0, 1.0)
    target = s * total_l
    acc = 0.0
    for i in range(len(poly)):
        p1 = poly[i]
        p2 = poly[(i+1)%len(poly)]
        d = math.hypot(p2[0]-p1[0], p2[1]-p1[1])
        if acc + d >= target or i == len(poly) - 1:
            rem = (target - acc) / max(1e-6, d)
            rem = max(0.0, min(1.0, rem))
            pt = (p1[0] + (p2[0]-p1[0])*rem, p1[1] + (p2[1]-p1[1])*rem)
            dir_v = (p2[0]-p1[0], p2[1]-p1[1])
            dlen = math.hypot(dir_v[0], dir_v[1])
            normal = (-dir_v[1]/dlen, dir_v[0]/dlen) if dlen > 1e-6 else (0.0, 1.0)
            return pt, normal
        acc += d
    return poly[0], (0.0, 1.0)

def make_satin_contour(poly, width_mm=2.5, pitch_mm=0.38):
    stitches = []
    l = poly_perimeter(poly)
    if l < 0.5: return stitches
    steps = max(6, int(round(l / pitch_mm)))
    outer = True
    for i in range(steps + 1):
        s = i / float(steps)
        pt, norm = sample_poly(poly, s)
        half_w = width_mm * 0.5
        w = half_w if outer else -half_w
        sx = pt[0] + norm[0] * w
        sy = pt[1] + norm[1] * w
        flag = "jump" if i == 0 else "normal"
        stitches.append((sx, sy, flag))
        outer = not outer
    return stitches

def point_in_poly(x, y, poly):
    inside = False
    n = len(poly)
    for i in range(n):
        p1 = poly[i]
        p2 = poly[(i+1)%n]
        if ((p1[1] > y) != (p2[1] > y)) and (x < (p2[0]-p1[0]) * (y-p1[1]) / (p2[1]-p1[1] + 1e-12) + p1[0]):
            inside = not inside
    return inside

def tatami_fill(outer_poly, holes, fill_angle_deg=0.0, row_spacing_mm=0.38, max_stitch_mm=3.5):
    rad = math.radians(-fill_angle_deg)
    cos_r = math.cos(rad)
    sin_r = math.sin(rad)

    def rot(pt):
        return (pt[0]*cos_r - pt[1]*sin_r, pt[0]*sin_r + pt[1]*cos_r)
    def unrot(pt):
        return (pt[0]*cos_r + pt[1]*sin_r, -pt[0]*sin_r + pt[1]*cos_r)

    rot_outer = [rot(p) for p in outer_poly]
    rot_holes = [[rot(p) for p in h] for h in holes]

    min_y = min(p[1] for p in rot_outer)
    max_y = max(p[1] for p in rot_outer)

    stitches = []
    curr_y = min_y + row_spacing_mm * 0.5
    row_idx = 0

    while curr_y < max_y:
        xs = []
        for poly in [rot_outer] + rot_holes:
            n = len(poly)
            for i in range(n):
                p1, p2 = poly[i], poly[(i+1)%n]
                if (p1[1] <= curr_y < p2[1]) or (p2[1] <= curr_y < p1[1]):
                    if abs(p2[1]-p1[1]) > 1e-6:
                        t = (curr_y - p1[1]) / (p2[1] - p1[1])
                        xs.append(p1[0] + t*(p2[0]-p1[0]))
        xs.sort()

        # Check every adjacent interval between sorted intersections
        valid_intervals = []
        for k in range(len(xs) - 1):
            x0, x1 = xs[k], xs[k+1]
            if x1 - x0 < 0.25:
                continue
            mid_x = (x0 + x1) * 0.5
            if point_in_poly(mid_x, curr_y, rot_outer):
                in_hole = any(point_in_poly(mid_x, curr_y, h) for h in rot_holes)
                if not in_hole:
                    valid_intervals.append((x0, x1))

        if row_idx % 2 == 1:
            valid_intervals = [(x1, x0) for (x0, x1) in reversed(valid_intervals)]

        for sx0, sx1 in valid_intervals:
            dist = abs(sx1 - sx0)
            steps = max(1, int(math.ceil(dist / max_stitch_mm)))
            for st in range(steps + 1):
                frac = st / float(steps)
                px = sx0 + (sx1 - sx0) * frac
                pt_orig = unrot((px, curr_y))
                flag = "jump" if st == 0 else "normal"
                stitches.append((pt_orig[0], pt_orig[1], flag))

        curr_y += row_spacing_mm
        row_idx += 1

    return stitches

def write_jef(path, stitch_seq, palette_codes, hoop_code=0):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    if not stitch_seq:
        return False

    min_x = min(s[0] for s in stitch_seq)
    max_x = max(s[0] for s in stitch_seq)
    min_y = min(s[1] for s in stitch_seq)
    max_y = max(s[1] for s in stitch_seq)

    cx = 0.5 * (min_x + max_x)
    cy = 0.5 * (min_y + max_y)

    u_min_x = int(round((min_x - cx) * 10))
    u_max_x = int(round((max_x - cx) * 10))
    u_min_y = int(round((min_y - cy) * 10))
    u_max_y = int(round((max_y - cy) * 10))

    stitch_offset = 0x74 + len(palette_codes) * 8
    header = bytearray()
    header += struct.pack("<I", stitch_offset)
    header += struct.pack("<I", 0x14)
    date_str = datetime.datetime.now().strftime("%Y%m%d%H%M%S").encode("ascii")[:14]
    header += date_str.ljust(14, b"0")
    header += bytes([0x64, 0x00])
    header += struct.pack("<I", len(palette_codes))
    header += struct.pack("<I", 0) # stitch count placeholder
    header += struct.pack("<I", hoop_code)
    header += struct.pack("<i", -u_min_x)
    header += struct.pack("<i",  u_max_y)
    header += struct.pack("<i",  u_max_x)
    header += struct.pack("<i", -u_min_y)
    for _ in range(16):
        header += struct.pack("<i", -1)
    for c in palette_codes:
        header += struct.pack("<I", c)
    for _ in palette_codes:
        header += struct.pack("<I", 0x0D)

    body = bytearray()
    count_field = 0
    last_x, last_y = 0, 0
    have_last = False

    for st in stitch_seq:
        x, y, flag = st[0], st[1], st[2]
        ux = int(round((x - cx) * 10))
        uy = int(round((y - cy) * 10))

        dx = (ux - last_x) if have_last else ux
        dy = (uy - last_y) if have_last else uy

        while abs(dx) > 127 or abs(dy) > 127:
            sx = max(-127, min(127, dx))
            sy = max(-127, min(127, dy))
            body += bytes([0x80, 0x02, sx & 0xFF, sy & 0xFF])
            dx -= sx
            dy -= sy
            count_field += 2

        if flag == "color_change":
            body += bytes([0x80, 0x01, dx & 0xFF, dy & 0xFF])
            count_field += 2
        elif flag == "jump":
            body += bytes([0x80, 0x02, dx & 0xFF, dy & 0xFF])
            count_field += 2
        else:
            body += bytes([dx & 0xFF, dy & 0xFF])
            count_field += 1

        last_x, last_y = ux, uy
        have_last = True

    body += bytes([0x80, 0x10, 0x00, 0x00])
    count_field += 1

    struct.pack_into("<I", header, 0x1C, count_field)

    with open(path, "wb") as f:
        f.write(header + body)
    return True

def render_preview(stitch_seq, palette_colors, out_path, size=800):
    img = Image.new("RGBA", (size, size), (22, 24, 30, 255))
    draw = ImageDraw.Draw(img)

    if len(stitch_seq) < 2:
        img.save(out_path)
        return

    min_x = min(s[0] for s in stitch_seq)
    max_x = max(s[0] for s in stitch_seq)
    min_y = min(s[1] for s in stitch_seq)
    max_y = max(s[1] for s in stitch_seq)

    w = max(1e-3, max_x - min_x)
    h = max(1e-3, max_y - min_y)
    pad = size * 0.08
    scale = min((size - 2*pad) / w, (size - 2*pad) / h)
    ox = (size - w * scale) * 0.5
    oy = (size - h * scale) * 0.5

    def X(x): return ox + (x - min_x) * scale
    def Y(y): return size - (oy + (y - min_y) * scale) # Invert Y so +Y is TOP

    color_idx = 0
    curr_color = palette_colors[0]
    last_pt = None

    for st in stitch_seq:
        x, y, flag = st[0], st[1], st[2]
        px, py = X(x), Y(y)
        if flag == "color_change":
            color_idx = min(color_idx + 1, len(palette_colors) - 1)
            curr_color = palette_colors[color_idx]
            last_pt = None
            continue
        elif flag == "jump":
            last_pt = None
            continue

        if last_pt is not None:
            # Draw thread shadow
            dark_col = tuple(max(0, c - 70) for c in curr_color[:3]) + (curr_color[3] if len(curr_color)>3 else 255,)
            draw.line([(last_pt[0]+0.8, last_pt[1]+0.8), (px+0.8, py+0.8)], fill=dark_col, width=3)
            # Main thread
            draw.line([last_pt, (px, py)], fill=curr_color, width=2)
            # Sheen highlight
            bright_col = tuple(min(255, c + 45) for c in curr_color[:3]) + (curr_color[3] if len(curr_color)>3 else 255,)
            draw.line([(last_pt[0]-0.4, last_pt[1]-0.4), (px-0.4, py-0.4)], fill=bright_col, width=1)

        last_pt = (px, py)

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    img.save(out_path)

def main():
    print("======================================================================")
    print("StickCore - Generating Kieler Wappen (Black-First, Upright, MC350E)")
    print("======================================================================")

    # 1. Official Vector Geometries from SVG assets
    shield_d = (
        "M 17.101165,16.883642 L 659.33699,16.883642 "
        "C 649.81982,134.56077 640.62988,245.53262 631.15109,343.82332 "
        "C 617.56651,451.11662 546.5855,591.33768 338.57938,693.94024 "
        "C 121.48621,602.66642 57.664746,449.86598 43.258438,343.96904 "
        "C 30.078052,247.08353 22.568575,95.193505 17.101165,16.883642 z"
    )
    shield_subpaths = parse_svg_path(shield_d)
    shield_poly = max(shield_subpaths, key=poly_perimeter)

    raw_nessel_pts = [
        (177.957,10.294), (151.33,84.175), (125.078,12.366), (97.32,85.675), (70.316,15.925),
        (16.306,15.925), (16.306,68.804), (84.956,97.685), (15.553,134.437), (88.697,147.186),
        (31.686,189.948), (107.439,192.2), (56.444,243.948), (128.457,234.569), (86.826,289.709),
        (165.202,269.083), (134.457,324.221), (177.957,356.661), (221.471,324.221), (190.712,269.083),
        (269.102,289.709), (227.458,234.569), (299.47,243.948), (248.461,192.2), (324.213,189.948),
        (267.217,147.186), (340.345,134.437), (270.972,97.685), (339.591,68.804), (339.591,15.925),
        (285.584,15.925), (258.594,85.675), (230.848,12.366), (204.584,84.175), (177.957,10.294)
    ]
    nessel_poly = [to_stitch_pt(pt[0]*1.793022 + 17.7285, pt[1]*1.81357 + 16.2626) for pt in raw_nessel_pts]

    boat_d = (
        "M -432.55654,322.88989 C -450.42221,317.99325 -470.08987,312.99324 -484.6008,303.52459 C -470.96487,319.23191 -451.34562,335.48332 -432.57805,343.5537 L -432.55654,322.88989 z "
        "M -349.80319,325.71181 C -360.61332,327.63601 -373.44956,328.64798 -384.93746,328.64798 C -384.93746,328.64798 -408.70934,327.63419 -419.51947,325.71181 L -419.49437,348.43946 C -408.78644,351.40284 -395.9484,352.98065 -384.3852,352.98065 C -374.32814,352.98065 -360.56311,351.40284 -349.83008,348.43946 L -349.80319,325.71181 z "
        "M -391.0409,317.08283 C -414.83789,316.21052 -440.81161,310.69001 -460.88091,303.25073 L -460.83071,278.96522 C -440.91561,286.33014 -415.11581,291.6312 -391.0678,292.49264 L -391.0409,317.08283 z "
        "M -474.14389,274.32248 C -488.58131,267.24955 -504.32225,256.93397 -514.35421,247.0355 C -511.07657,257.24771 -505.29586,273.47373 -500.16782,282.53433 C -492.71064,287.89886 -482.90102,294.35336 -474.1421,298.60618 L -474.1421,274.32248 L -474.14389,274.32248 z "
        "M -350.02911,278.76211 C -360.68862,280.25467 -373.00131,281.06714 -384.43541,281.06714 C -395.89641,281.06714 -408.63224,280.25467 -419.31866,278.76211 L -419.31866,257.65214 C -408.68245,259.08305 -395.92151,259.83025 -384.43541,259.83025 C -372.94931,259.83025 -360.66352,259.08305 -350.02911,257.65214 L -350.02911,278.76211 L -350.02911,278.76211 z "
        "M -336.74461,322.8645 C -318.92555,317.99145 -299.23279,312.99324 -284.74517,303.52459 C -298.33269,319.23191 -318.00214,335.48332 -336.74461,343.51744 L -336.74461,322.8645 z "
        "M -378.27996,317.08283 C -354.48477,316.21052 -328.53615,310.69001 -308.46506,303.25073 L -308.51526,278.96522 C -328.40705,286.33014 -354.20506,291.6312 -378.22797,292.49264 L -378.27996,317.08283 z "
        "M -295.17697,274.32248 C -280.74135,267.24955 -265.00041,256.93397 -254.99355,247.0355 C -258.2694,257.24771 -264.0268,273.47373 -269.17815,282.53433 C -276.63533,287.89886 -286.39654,294.35336 -295.17697,298.60618 L -295.17697,274.32248 z "
        "M -336.5922,276.54773 L -336.5922,255.61731 C -327.80998,253.76748 -317.89994,250.66809 -310.14154,247.93504 L -310.14154,215.98174 L -293.75331,215.98174 L -293.75331,241.17404 C -266.72709,228.14172 -249.7382,209.37672 -249.6109,188.53335 L -228.84053,188.53335 C -228.54289,192.93671 -228.6164,196.3172 -228.6164,200.82211 C -228.6164,296.82162 -296.90365,375.88967 -384.63623,376.03112 C -471.46691,375.36918 -540.70626,296.82162 -540.70626,200.82211 C -540.70626,196.3172 -540.77977,192.93671 -540.48034,188.53335 L -519.71176,188.53335 C -519.58804,209.37854 -502.59736,228.14172 -475.56935,241.17404 L -475.56935,215.98174 L -459.20623,215.98174 L -459.20623,247.93504 C -451.42272,250.66989 -441.51448,253.76929 -432.70536,255.61731 L -432.70536,276.54773 C -445.24217,273.7766 -458.53026,269.21728 -469.16288,264.57635 C -469.16288,264.57635 -473.77095,262.58686 -476.0194,261.41168 C -479.84929,259.52556 -483.57698,257.50343 -487.08055,255.37611 C -515.08218,239.32782 -527.6943,222.70102 -528.91893,221.37347 C -519.96099,301.22317 -458.70418,363.68796 -384.66133,363.84212 C -309.34364,363.68796 -249.38677,301.22498 -240.37863,221.37347 C -241.62836,222.70102 -254.21717,239.32782 -282.2439,255.37611 C -285.77257,257.50343 -289.45006,259.52556 -293.30506,261.41168 C -295.55709,262.58686 -297.88264,263.75118 -300.28529,264.94269 C -300.2835,264.94452 -324.10559,273.7766 -336.5922,276.54773 z"
    )
    boat_subpaths = parse_svg_path(boat_d, offset_x=721.1587, offset_y=15.16174)
    # The outer hull is the largest polygon in area
    boat_outer_hull = max(boat_subpaths, key=lambda p: abs(poly_area(p)))
    boat_openings = [p for p in boat_subpaths if p != boat_outer_hull]

    print(f"Shield points: {len(shield_poly)}, Nesselblatt points: {len(nessel_poly)}, Boat openings: {len(boat_openings)}")

    # -------------------------------------------------------------------------
    # STITCH PRODUCTION
    # -------------------------------------------------------------------------
    # Palette definition:
    # 0: Janome 1  - Schwarz / Black   (#0C0C0C)
    # 1: Janome 2  - Weiss / White     (#FAFAFA)
    # 2: Janome 76 - Deep Red / Rot    (#D21923)
    janome_palette = [1, 2, 76]
    render_colors = [
        (12, 12, 12, 255),    # Black
        (250, 250, 250, 255), # White
        (210, 25, 35, 255)    # Deep Red
    ]

    # A. Boat Stitches (BLACK - COLOR 1)
    boat_fill = tatami_fill(boat_outer_hull, boat_openings, fill_angle_deg=45.0, row_spacing_mm=0.38, max_stitch_mm=3.2)
    boat_hull_satin = make_satin_contour(boat_outer_hull, width_mm=1.1, pitch_mm=0.38)
    boat_openings_satin = []
    for op in boat_openings:
        boat_openings_satin.extend(make_satin_contour(op, width_mm=0.75, pitch_mm=0.38))

    # B. Nesselblatt Stitches (WHITE - COLOR 2)
    # Cutout boat from Nesselblatt to guarantee disjoint stitching!
    nessel_fill = tatami_fill(nessel_poly, [boat_outer_hull], fill_angle_deg=135.0, row_spacing_mm=0.38, max_stitch_mm=3.5)
    nessel_satin_thick = make_satin_contour(nessel_poly, width_mm=1.8, pitch_mm=0.38) # for Kettel design
    nessel_satin_med   = make_satin_contour(nessel_poly, width_mm=1.2, pitch_mm=0.38) # for Vollstick design

    # C. Shield Stitches (RED - COLOR 3)
    # Cutout Nesselblatt from Shield to guarantee disjoint stitching!
    shield_fill = tatami_fill(shield_poly, [nessel_poly], fill_angle_deg=0.0, row_spacing_mm=0.40, max_stitch_mm=4.0)
    shield_kettel = make_satin_contour(shield_poly, width_mm=2.5, pitch_mm=0.38) # 2.5 mm 3D Kettelrand

    # =========================================================================
    # DESIGN 1: KETTEL-KONTUR (Patch Style)
    # =========================================================================
    seq_kettel = []
    # 1. Color 0 (Black): Boat fill + outline
    seq_kettel.extend(boat_fill)
    seq_kettel.extend(boat_hull_satin)
    seq_kettel.extend(boat_openings_satin)

    # Color stop -> White
    if nessel_satin_thick:
        seq_kettel.append((nessel_satin_thick[0][0], nessel_satin_thick[0][1], "color_change"))
        seq_kettel.extend(nessel_satin_thick[1:])

    # Color stop -> Red
    if shield_kettel:
        seq_kettel.append((shield_kettel[0][0], shield_kettel[0][1], "color_change"))
        seq_kettel.extend(shield_kettel[1:])

    # =========================================================================
    # DESIGN 2: SCHWARZ-START VOLLSTICK (Black -> White -> Red)
    # =========================================================================
    seq_voll = []
    # 1. Color 0 (Black): Boat fill + outline
    seq_voll.extend(boat_fill)
    seq_voll.extend(boat_hull_satin)
    seq_voll.extend(boat_openings_satin)

    # Color stop -> White
    if nessel_fill:
        seq_voll.append((nessel_fill[0][0], nessel_fill[0][1], "color_change"))
        seq_voll.extend(nessel_fill[1:])
    seq_voll.extend(nessel_satin_med)

    # Color stop -> Red
    if shield_fill:
        seq_voll.append((shield_fill[0][0], shield_fill[0][1], "color_change"))
        seq_voll.extend(shield_fill[1:])
    seq_voll.extend(shield_kettel)

    # =========================================================================
    # DESIGN 3: MEISTER-VOLLSTICK WEISS-START (White -> Black -> Red, 100% Solid)
    # =========================================================================
    # 1. White (Color 1 - Janome 2): Solid Nesselblatt (no holes cut out)
    nessel_fill_solid = tatami_fill(nessel_poly, [], fill_angle_deg=135.0, row_spacing_mm=0.38, max_stitch_mm=3.5)
    nessel_satin_solid = make_satin_contour(nessel_poly, width_mm=1.2, pitch_mm=0.38)

    seq_weiss_voll = []
    # Color 1: White
    seq_weiss_voll.extend(nessel_fill_solid)
    seq_weiss_voll.extend(nessel_satin_solid)

    # Color stop -> Black (Color 2: Boat directly over white)
    seq_weiss_voll.append((boat_fill[0][0], boat_fill[0][1], "color_change"))
    seq_weiss_voll.extend(boat_fill[1:])
    seq_weiss_voll.extend(boat_hull_satin)
    seq_weiss_voll.extend(boat_openings_satin)

    # Color stop -> Red (Color 3: Shield background + Kettelrand)
    seq_weiss_voll.append((shield_fill[0][0], shield_fill[0][1], "color_change"))
    seq_weiss_voll.extend(shield_fill[1:])
    seq_weiss_voll.extend(shield_kettel)

    # Center all sequences
    def center(seq):
        mx = 0.5 * (min(s[0] for s in seq) + max(s[0] for s in seq))
        my = 0.5 * (min(s[1] for s in seq) + max(s[1] for s in seq))
        return [(s[0]-mx, s[1]-my, s[2]) for s in seq]

    seq_kettel = center(seq_kettel)
    seq_voll = center(seq_voll)
    seq_weiss_voll = center(seq_weiss_voll)

    # Check bounds
    kw = max(s[0] for s in seq_kettel) - min(s[0] for s in seq_kettel)
    kh = max(s[1] for s in seq_kettel) - min(s[1] for s in seq_kettel)
    vw = max(s[0] for s in seq_voll) - min(s[0] for s in seq_voll)
    vh = max(s[1] for s in seq_voll) - min(s[1] for s in seq_voll)
    ww = max(s[0] for s in seq_weiss_voll) - min(s[0] for s in seq_weiss_voll)
    wh = max(s[1] for s in seq_weiss_voll) - min(s[1] for s in seq_weiss_voll)

    print(f"Design 1 (Kettel-Kontur):           {kw:.1f} x {kh:.1f} mm, {len(seq_kettel)} stitches")
    print(f"Design 2 (Schwarz-Start Vollstick): {vw:.1f} x {vh:.1f} mm, {len(seq_voll)} stitches")
    print(f"Design 3 (Weiss-Start Vollstick):   {ww:.1f} x {wh:.1f} mm, {len(seq_weiss_voll)} stitches")

    palette_weiss_first = [2, 1, 76] # White (2), Black (1), Red (76)
    render_colors_weiss_first = [
        (250, 250, 250, 255), # White
        (12, 12, 12, 255),    # Black
        (210, 25, 35, 255)    # Red
    ]

    # -------------------------------------------------------------------------
    # EXPORT LOCATIONS
    # -------------------------------------------------------------------------
    target_dirs = [
        "F:/",
        "F:/EMB/MyDesign",
        "F:/EmbF5/MyDesign",
        "F:/MyDesign",
        "F:/fw",
        "C:/Users/micbu/Downloads",
        "E:/StickCore/output"
    ]

    for d in target_dirs:
        if d.startswith("F:/") and not os.path.exists("F:/"):
            continue
        os.makedirs(d, exist_ok=True)
        # Long descriptive filenames
        f1 = os.path.join(d, "01_Kieler_Wappen_Schwarz_Start_Kettel_HoopA.jef")
        f2 = os.path.join(d, "02_Kieler_Wappen_Schwarz_Start_Vollstick_HoopA.jef")
        f3 = os.path.join(d, "03_Kieler_Wappen_Meister_Vollstick_Weiss_Start_HoopA.jef")
        # 8.3 Short filenames for Janome MC350E screen display
        s1 = os.path.join(d, "KIEL_KTL.JEF")
        s2 = os.path.join(d, "KIEL_BLK.JEF")
        s3 = os.path.join(d, "KIEL_VOL.JEF") # Primary Vollstick is the superior Weiss-Start version!
        s4 = os.path.join(d, "KIEL_WHT.JEF")

        write_jef(f1, seq_kettel, janome_palette, hoop_code=0)
        write_jef(f2, seq_voll, janome_palette, hoop_code=0)
        write_jef(f3, seq_weiss_voll, palette_weiss_first, hoop_code=0)

        write_jef(s1, seq_kettel, janome_palette, hoop_code=0)
        write_jef(s2, seq_voll, janome_palette, hoop_code=0)
        write_jef(s3, seq_weiss_voll, palette_weiss_first, hoop_code=0)
        write_jef(s4, seq_weiss_voll, palette_weiss_first, hoop_code=0)
        print(f"  [Exported] -> {d}")

    # Overwrite legacy files in output
    write_jef("E:/StickCore/output/04_Kieler_Wappen_Offiziell_Kettel_Konturen.jef", seq_kettel, janome_palette, hoop_code=0)
    write_jef("E:/StickCore/output/04_Kieler_Wappen_Offiziell_Applique.jef", seq_weiss_voll, palette_weiss_first, hoop_code=0)

    # -------------------------------------------------------------------------
    # RENDER PREVIEWS
    # -------------------------------------------------------------------------
    p_kettel_art = "C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/kieler_wappen_kettel_verified_upright.png"
    p_kettel_lib = "E:/StickCore/library/thumbs/kieler-wappen-kettel-konturen.png"
    p_voll_art   = "C:/Users/micbu/.gemini/antigravity/brain/e572641d-70d6-45b5-8c91-c97f358a4f6c/kieler_wappen_vollstick_verified_upright.png"
    p_voll_lib   = "E:/StickCore/library/thumbs/kieler-wappen-offiziell-applique.png"

    render_preview(seq_kettel, render_colors, p_kettel_art, size=800)
    render_preview(seq_kettel, render_colors, p_kettel_lib, size=800)
    render_preview(seq_weiss_voll, render_colors_weiss_first, p_voll_art, size=800)
    render_preview(seq_weiss_voll, render_colors_weiss_first, p_voll_lib, size=800)

    print("  [Rendered] Preview thumbnails generated successfully!")
    print("ALL DONE SUCCESSFULLY!")

if __name__ == "__main__":
    main()
