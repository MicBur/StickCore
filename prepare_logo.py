import os
from PIL import Image, ImageDraw

src_path = r"C:\Users\micbu\.gemini\antigravity\brain\e572641d-70d6-45b5-8c91-c97f358a4f6c\stickcore_logo_1792130604648.jpg"
media_dir = r"e:\StickCore\media"
os.makedirs(media_dir, exist_ok=True)

img = Image.open(src_path).convert("RGBA")
w, h = img.size

# 1. Save full-size logo PNG
out_full = os.path.join(media_dir, "stickcore_logo.png")
img.save(out_full, "PNG", optimize=True)
print(f"Saved: {out_full} ({w}x{h})")

# 2. Create circular masked badge for clean app icons
mask = Image.new('L', (w, h), 0)
draw = ImageDraw.Draw(mask)
# Leave a small margin or full circle
draw.ellipse((0, 0, w - 1, h - 1), fill=255)

circ_img = img.copy()
circ_img.putalpha(mask)

out_circ = os.path.join(media_dir, "stickcore_badge.png")
circ_img.save(out_circ, "PNG", optimize=True)
print(f"Saved: {out_circ}")

# 3. Create Windows .ico containing multiple sizes: 256, 128, 64, 48, 32, 16
ico_sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)]
out_ico = os.path.join(media_dir, "stickcore.ico")
out_root_ico = r"e:\StickCore\stickcore.ico"

# For icon, circular or slightly rounded rectangle works wonderfully.
# We'll use the circular badge resized with high-quality Lanczos.
circ_img.save(out_ico, format='ICO', sizes=ico_sizes)
circ_img.save(out_root_ico, format='ICO', sizes=ico_sizes)
print(f"Saved: {out_ico} and {out_root_ico} with sizes {ico_sizes}")

# 4. Also save installer banner / wizard image for Inno Setup (164x314 and 55x58)
banner_small = img.resize((150, 150), Image.Resampling.LANCZOS)
banner_small.save(os.path.join(media_dir, "installer_logo_small.png"), "PNG")
print("All logo assets prepared successfully.")
