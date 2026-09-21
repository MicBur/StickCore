#!/usr/bin/env python3
# Generates small looping "stroke video" GIFs that explain each technique.
# Output: src/../media/anim/*.gif  (schematic line animations on fabric-cream)
import math, os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "media", "anim")
os.makedirs(OUT, exist_ok=True)
W, Hh = 300, 190
BG = (239, 233, 222)
GUIDE = (176, 168, 150)
INK = (40, 55, 90)
RED = (200, 60, 70)
GREEN = (70, 150, 90)
GOLD = (220, 165, 60)

def canvas():
    im = Image.new("RGB", (W, Hh), BG)
    return im, ImageDraw.Draw(im)

def save(frames, name, ms=70):
    frames[0].save(os.path.join(OUT, name), save_all=True, append_images=frames[1:],
                   duration=ms, loop=0, disposal=2, optimize=True)
    print("wrote", name, len(frames), "frames")

def lerp(a, b, t): return a + (b - a) * t

# --- geometry helpers ---
def arc_pt(cx, cy, r, a): return (cx + r*math.cos(a), cy + r*math.sin(a))

# rails for the satin demo (two arcs)
def rail(y0, amp):
    pts = []
    for i in range(41):
        x = 30 + i*(W-60)/40
        y = y0 - amp*math.sin(math.pi*i/40)
        pts.append((x, y))
    return pts

# ============================ SATIN ============================
def anim_satin():
    A = rail(120, 55); B = rail(150, 55)
    frames = []
    N = 30
    for f in range(N):
        im, d = canvas()
        d.line(A, fill=GUIDE, width=2); d.line(B, fill=GUIDE, width=2)
        prog = (f+1)/N
        steps = int(prog*40)
        for i in range(steps):
            p = A[i] if i % 2 == 0 else B[i]
            q = B[i] if i % 2 == 0 else A[i+1] if i+1 < 41 else B[i]
            d.line([p, q], fill=RED, width=2)
        frames.append(im)
    for _ in range(6): frames.append(frames[-1])
    save(frames, "satin.gif")

# ============================ FILL ============================
def blob():
    pts = []
    for i in range(48):
        a = 2*math.pi*i/48
        r = 60 + 10*math.sin(3*a)
        pts.append((150 + r*math.cos(a), 95 + r*0.62*math.sin(a)))
    return pts

def anim_fill():
    shp = blob()
    ys = list(range(45, 146, 6))
    frames = []
    N = len(ys)
    for f in range(N+1):
        im, d = canvas()
        d.line(shp+[shp[0]], fill=GUIDE, width=2)
        for j in range(min(f, N)):
            y = ys[j]
            xs = spans(shp, y)
            for k in range(0, len(xs)-1, 2):
                d.line([(xs[k], y), (xs[k+1], y)], fill=GREEN, width=2)
        frames.append(im)
    for _ in range(6): frames.append(frames[-1])
    save(frames, "fill.gif")

def spans(poly, y):
    xs = []
    n = len(poly)
    for i in range(n):
        a = poly[i]; b = poly[(i+1) % n]
        if (a[1] <= y < b[1]) or (b[1] <= y < a[1]):
            t = (y-a[1])/(b[1]-a[1]); xs.append(a[0]+t*(b[0]-a[0]))
    xs.sort(); return xs

# ============================ UNDERLAY ============================
def anim_underlay():
    shp = blob()
    frames = []
    # phase 1: edge run (0..10), phase 2: cross (10..18), phase 3: fill (18..34)
    ys = list(range(45, 146, 6))
    xs_cols = list(range(95, 206, 18))
    total = 34
    for f in range(total+1):
        im, d = canvas()
        d.line(shp+[shp[0]], fill=GUIDE, width=2)
        # phase 1 edge run
        p1 = min(f, 10)/10
        pcount = int(p1*len(shp))
        if pcount > 1:
            d.line(shp[:pcount], fill=INK, width=3)
        if f >= 10:
            # phase 2 cross (vertical sparse)
            p2 = min(f-10, 8)/8
            for j in range(int(p2*len(xs_cols))):
                x = xs_cols[j]
                yy = vspan(shp, x)
                if yy: d.line([(x, yy[0]), (x, yy[-1])], fill=(150,120,170), width=2)
        if f >= 18:
            p3 = min(f-18, 16)/16
            for j in range(int(p3*len(ys))):
                y = ys[j]; sx = spans(shp, y)
                for k in range(0, len(sx)-1, 2):
                    d.line([(sx[k], y), (sx[k+1], y)], fill=GREEN, width=2)
        # labels
        lbl = "1. Kontur" if f < 10 else ("2. Unterlage" if f < 18 else "3. Fuellung")
        d.text((12, 10), lbl, fill=INK)
        frames.append(im)
    for _ in range(8): frames.append(frames[-1])
    save(frames, "underlay.gif", ms=80)

def vspan(poly, x):
    ys = []
    n = len(poly)
    for i in range(n):
        a = poly[i]; b = poly[(i+1) % n]
        if (a[0] <= x < b[0]) or (b[0] <= x < a[0]):
            t = (x-a[0])/(b[0]-a[0]); ys.append(a[1]+t*(b[1]-a[1]))
    ys.sort(); return ys

# ============================ MONOGRAM ============================
def anim_monogram():
    frames = []
    N = 26
    from PIL import ImageFont
    try:
        big = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", 70)
        sm = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", 44)
    except Exception:
        big = sm = ImageFont.load_default()
    for f in range(N):
        im, d = canvas()
        prog = (f+1)/N
        # letters appear
        if prog > 0.15: d.text((78, 92), "A", font=sm, fill=INK, anchor="mm")
        if prog > 0.30: d.text((150, 88), "M", font=big, fill=INK, anchor="mm")
        if prog > 0.45: d.text((222, 92), "B", font=sm, fill=INK, anchor="mm")
        # frame draws
        if prog > 0.55:
            fp = (prog-0.55)/0.45
            a1 = -math.pi/2; a2 = a1 + 2*math.pi*fp
            pts = []
            aa = a1
            while aa <= a2:
                pts.append((150 + 110*math.cos(aa), 95 + 62*math.sin(aa))); aa += 0.12
            if len(pts) > 1: d.line(pts, fill=INK, width=3)
        frames.append(im)
    for _ in range(8): frames.append(frames[-1])
    save(frames, "monogram.gif", ms=80)

# ============================ IMAGE ============================
def anim_image():
    frames = []
    # a simple heart that "becomes" stitches
    def heart_pts(sc):
        pts = []
        for i in range(60):
            t = 2*math.pi*i/60
            x = 16*math.sin(t)**3
            y = 13*math.cos(t)-5*math.cos(2*t)-2*math.cos(3*t)-math.cos(4*t)
            pts.append((150+x*sc, 92-y*sc))
        return pts
    hp = heart_pts(3.4)
    ys = list(range(50, 140, 6))
    N = len(ys)
    for f in range(N+1):
        im, d = canvas()
        if f == 0:
            d.polygon(hp, fill=RED)  # the "photo"
        else:
            d.line(hp+[hp[0]], fill=GUIDE, width=2)
            for j in range(min(f, N)):
                y = ys[j]; sx = spans(hp, y)
                for k in range(0, len(sx)-1, 2):
                    d.line([(sx[k], y), (sx[k+1], y)], fill=RED, width=2)
        d.text((12, 10), "Foto → Stiche", fill=INK)
        frames.append(im)
    for _ in range(6): frames.append(frames[-1])
    save(frames, "image.gif")

anim_satin(); anim_fill(); anim_underlay(); anim_monogram(); anim_image()
print("done ->", OUT)
