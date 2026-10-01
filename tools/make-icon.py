#!/usr/bin/env python3
"""Draw the Qivot Studio app icon and write every size the platforms want.

    python3 tools/make-icon.py            # writes app/icons/

The mark is "Qs" in Inter Display Black (tools/fonts, SIL Open Font License),
white on an indigo-to-blue squircle, on Apple's icon grid (an 824 px body on a
1024 px canvas, with the standard soft shadow).

Outputs: QivotStudio.png (1024), QivotStudio.icns (macOS, via iconutil),
QivotStudio.ico (Windows), and qivot-studio-256.png (the window icon).
"""
import math
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

SS = 4                      # supersampling: draw at 4x, scale down for smooth edges
N = 1024 * SS
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "icons")


def squircle_mask(size, inset, radius):
    """Apple's rounded square, as a mask (a superellipse-ish via a big radius)."""
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle((inset, inset, size - inset, size - inset), radius, fill=255)
    return m


def gradient(size, stops):
    """A diagonal gradient (top-left -> bottom-right) through the colour stops."""
    small = 256                          # smooth enough to draw small and scale up
    g = Image.new("RGB", (small, small))
    px = g.load()
    for y in range(small):
        for x in range(small):
            t = (x + y) / (2 * (small - 1))
            for (a, ca), (b, cb) in zip(stops, stops[1:]):
                if a <= t <= b:
                    k = (t - a) / (b - a)
                    px[x, y] = tuple(int(ca[i] + (cb[i] - ca[i]) * k) for i in range(3))
                    break
    return g.resize((size, size), Image.BICUBIC)


FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts", "InterDisplay-Black.otf")


def draw_mark(size):
    """The white "Qs" on a transparent layer: Inter Display Black, set tight
    and centred on the letters' own ink (not the font's metrics)."""
    u = size / 1024
    font = ImageFont.truetype(FONT, int(500 * u))
    q_w = font.getlength("Q")
    tight = -8 * u                                    # tracking between Q and s
    layer = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(layer)
    x, base = 200 * u, 700 * u
    d.text((x, base), "Q", font=font, fill=255, anchor="ls")
    d.text((x + q_w + tight, base), "s", font=font, fill=255, anchor="ls")
    # Centre the ink in the body.
    left, top, right, bottom = layer.getbbox()
    dx = (size - (right - left)) / 2 - left
    dy = (size - (bottom - top)) / 2 - top - 4 * u          # a hair above centre reads as centred
    return ImageChops.offset(layer, int(dx), int(dy))


def render():
    body_inset, body_radius = 100 * SS, 186 * SS          # Apple grid: 824 body, ~22.5% radius
    mask = squircle_mask(N, body_inset, body_radius)

    # Background: indigo -> blue -> cyan, with a soft glow at the top.
    bg = gradient(N, [(0.0, (94, 92, 230)), (0.55, (10, 132, 255)), (1.0, (34, 195, 238))]).convert("RGBA")
    glow = Image.new("L", (N, N), 0)
    ImageDraw.Draw(glow).ellipse((-0.3 * N, -0.75 * N, 1.3 * N, 0.42 * N), fill=46)
    glow = glow.filter(ImageFilter.GaussianBlur(140 * SS))
    bg = Image.composite(Image.new("RGBA", (N, N), (255, 255, 255, 255)), bg, glow)

    # The mark, with a faint shadow under it for depth.
    mark = draw_mark(N)
    shadow = mark.filter(ImageFilter.GaussianBlur(14 * SS)).point(lambda v: v * 0.35)
    shadow = ImageChops.offset(shadow, 0, 10 * SS)
    bg = Image.composite(Image.new("RGBA", (N, N), (20, 20, 70, 255)), bg, shadow)
    bg = Image.composite(Image.new("RGBA", (N, N), (255, 255, 255, 255)), bg, mark)

    # Cut to the squircle; a hairline of light on the edge.
    icon = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    icon.paste(bg, (0, 0), mask)
    edge = ImageChops.subtract(mask, squircle_mask(N, body_inset + 3 * SS, body_radius - 3 * SS)).point(lambda v: v * 0.25)
    icon = Image.composite(Image.new("RGBA", (N, N), (255, 255, 255, 255)), icon, edge)

    # Apple's drop shadow under the body.
    out = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    drop = mask.filter(ImageFilter.GaussianBlur(28 * SS)).point(lambda v: v * 0.30)
    drop = ImageChops.offset(drop, 0, 12 * SS)
    out.paste((0, 0, 0, 255), (0, 0), drop)
    out.alpha_composite(icon)
    return out.resize((1024, 1024), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    icon = render()
    icon.save(os.path.join(OUT, "QivotStudio.png"))
    icon.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, "qivot-studio-256.png"))

    # Windows: an .ico with the usual sizes.
    icon.save(os.path.join(OUT, "QivotStudio.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

    # macOS: an .icns from an iconset (needs iconutil, so only on a Mac).
    if shutil.which("iconutil"):
        with tempfile.TemporaryDirectory() as tmp:
            iconset = os.path.join(tmp, "QivotStudio.iconset")
            os.makedirs(iconset)
            for pts in (16, 32, 128, 256, 512):
                for scale in (1, 2):
                    px = pts * scale
                    name = f"icon_{pts}x{pts}{'@2x' if scale == 2 else ''}.png"
                    icon.resize((px, px), Image.LANCZOS).save(os.path.join(iconset, name))
            subprocess.run(["iconutil", "-c", "icns", iconset, "-o", os.path.join(OUT, "QivotStudio.icns")], check=True)
    else:
        print("iconutil not found: skipped the .icns", file=sys.stderr)
    print("wrote", os.path.abspath(OUT))


if __name__ == "__main__":
    main()
