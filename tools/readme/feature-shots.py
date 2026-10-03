# tools/readme/feature-shots.py [name...], run from the repo root (needs Pillow):
# makes docs/feature-<name>.png from docs/studio-<name>.png.
#
# Crops the README screenshots to the part each feature is about, then gives
# them rounded corners and a soft shadow on a transparent background.
import sys
from PIL import Image, ImageDraw, ImageFilter

CROPS = {  # name: (left, top, right, bottom) in the screenshot's pixels
    "diagram":   (400, 150, 1450, 825),
    "data":      (321, 117, 1323, 600),
    "editing":   (288, 10, 1385, 953),
    "profile":   (321, 107, 1313, 650),
    "query":     (311, 117, 1323, 700),
    "plan":      (292, 0, 1400, 525),
    "builder":   (321, 117, 1323, 895),
    "structure": (321, 117, 1333, 650),
    "compare":   (282, 0, 1400, 650),
    "migration": (311, 117, 1333, 914),
    "cpp":       (321, 117, 1333, 584),
    "ide":       (311, 117, 1333, 778),
    "selection": (288, 270, 1040, 952),
    "running":   (290, 0, 1004, 330),
    "recent":    (330, 370, 1080, 800),
    "connect":   (468, 318, 932, 1012),
    "redis":          (0, 36, 820, 520),
    "redis-console":  (284, 64, 880, 640),
    "redis-server":   (284, 64, 1010, 560),
}
# Crops that end partway through rows fade out at the bottom instead.
FADE = {"data", "profile", "structure", "compare", "query", "migration", "redis-server"}
RADIUS, PAD, BLUR, DROP, FADE_PX = 18, 48, 22, 12, 90

def shot(name):
    im = Image.open(f"docs/studio-{name}.png").convert("RGBA").crop(CROPS[name])
    w, h = im.size
    if name in FADE:
        bg = im.getpixel((w - 4, h - 4))
        veil = Image.new("RGBA", (w, h), bg)
        ramp = Image.linear_gradient("L").resize((w, FADE_PX))
        alpha = Image.new("L", (w, h), 0)
        alpha.paste(ramp, (0, h - FADE_PX))
        im = Image.composite(veil, im, alpha)
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, w - 1, h - 1), RADIUS, fill=255)
    im.putalpha(mask)
    out = Image.new("RGBA", (w + 2 * PAD, h + 2 * PAD), (0, 0, 0, 0))
    shadow = Image.new("RGBA", out.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((PAD, PAD + DROP, PAD + w, PAD + h + DROP), RADIUS, fill=(0, 0, 0, 90))
    out = Image.alpha_composite(out, shadow.filter(ImageFilter.GaussianBlur(BLUR)))
    out.alpha_composite(im, (PAD, PAD))
    # A hairline edge so a light shot doesn't melt into a white page.
    ImageDraw.Draw(out).rounded_rectangle((PAD, PAD, PAD + w - 1, PAD + h - 1), RADIUS, outline=(0, 0, 0, 40), width=1)
    out.save(f"docs/feature-{name}.png", optimize=True)

for n in sys.argv[1:] or CROPS:
    shot(n)
