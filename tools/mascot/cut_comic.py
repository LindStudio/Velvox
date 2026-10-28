"""Cut Jakob's comic expression sheets (3 x 3 panels on white, made with Higgsfield) into single
transparent portraits of Memphy.

    python tools/mascot/cut_comic.py <sheet.png> <out dir> [--preview <file.png>]

Each panel's white background is removed by a flood fill from the panel border (the thick black outline
keeps the white of his eyes and teeth inside), the edge gets a soft alpha, and the portrait is trimmed to
its content. Names in panel order: see NAMES.
"""

import argparse
import os

import numpy as np
from PIL import Image, ImageFilter

NAMES = ["listening", "talking", "wink", "celebrate", "sad", "shocked", "thinking", "proud", "annoyed"]
WHITE = 236          # brighter than this (all channels) counts as background where it touches the border
PAD = 6


def cut_panel(panel):
    a = np.asarray(panel.convert("RGB"), dtype=np.int16)
    h, w, _ = a.shape
    light = (a >= WHITE).all(axis=2)
    # Flood fill from every border pixel through light pixels
    bg = np.zeros((h, w), dtype=bool)
    stack = [(y, x) for x in range(w) for y in (0, h - 1) if light[y, x]]
    stack += [(y, x) for y in range(h) for x in (0, w - 1) if light[y, x]]
    for y, x in stack:
        bg[y, x] = True
    while stack:
        y, x = stack.pop()
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny < h and 0 <= nx < w and light[ny, nx] and not bg[ny, nx]:
                bg[ny, nx] = True
                stack.append((ny, nx))
    alpha = Image.fromarray(np.where(bg, 0, 255).astype(np.uint8))
    # A one-pixel soft edge (the anti-aliased rim of the outline stays, its white fringe fades)
    alpha = alpha.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.8))
    rgba = panel.convert("RGBA")
    rgba.putalpha(alpha)
    box = alpha.point(lambda v: 255 if v > 16 else 0).getbbox()
    if box:
        box = (max(0, box[0] - PAD), max(0, box[1] - PAD), min(w, box[2] + PAD), min(h, box[3] + PAD))
        rgba = rgba.crop(box)
    return rgba


# Sticker (Jakob 2026-10-28): a light disc with a thick white rim, so the black comic effects stay visible
# on the dark plugin; the figure is cut by the disc at the bottom and may pop out over the rim at the top
STICKER = 512
DISC_FILL = (236, 230, 250)     # pale lilac, close to the plugin's purple accents
RIM = 26                        # white rim, px at 512
OUTLINE = 4                     # dark line around the rim


def sticker(img):
    from PIL import ImageDraw
    s = STICKER
    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    cx, cy, r = s / 2, s * 0.56, s * 0.40          # disc a little low: room for the head to pop out
    # Shadow, outline, white rim, fill
    shadow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).ellipse((cx - r - RIM, cy - r - RIM + 8, cx + r + RIM, cy + r + RIM + 8), fill=(0, 0, 0, 110))
    out.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(10)))
    d = ImageDraw.Draw(out)
    d.ellipse((cx - r - RIM - OUTLINE, cy - r - RIM - OUTLINE, cx + r + RIM + OUTLINE, cy + r + RIM + OUTLINE), fill=(24, 22, 34, 255))
    d.ellipse((cx - r - RIM, cy - r - RIM, cx + r + RIM, cy + r + RIM), fill=(255, 255, 255, 255))
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(*DISC_FILL, 255))

    # The figure: as wide as the disc, its bottom on the disc's bottom; below the disc's centre line
    # only what lies inside the disc stays (cut like a sticker), above it everything may stick out
    fig = img.copy()
    scale = (2 * r * 0.98) / max(fig.width, fig.height * 0.85)
    fig = fig.resize((round(fig.width * scale), round(fig.height * scale)), Image.LANCZOS)
    layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    fx, fy = round(cx - fig.width / 2), round(cy + r - fig.height)
    layer.paste(fig, (fx, fy), fig)
    keep = Image.new("L", (s, s), 0)
    kd = ImageDraw.Draw(keep)
    kd.ellipse((cx - r, cy - r, cx + r, cy + r), fill=255)
    kd.rectangle((0, 0, s, cy), fill=255)
    a = np.minimum(np.asarray(layer.getchannel("A")), np.asarray(keep))
    layer.putalpha(Image.fromarray(a))
    out.alpha_composite(layer)
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("sheet")
    p.add_argument("out")
    p.add_argument("--preview", default="")
    p.add_argument("--stickers", default="", help="also write sticker versions into this folder")
    args = p.parse_args()
    sheet = Image.open(args.sheet).convert("RGB")
    os.makedirs(args.out, exist_ok=True)
    cw, ch = sheet.width // 3, sheet.height // 3
    cuts = []
    for i, name in enumerate(NAMES):
        x, y = (i % 3) * cw, (i // 3) * ch
        img = cut_panel(sheet.crop((x, y, x + cw, y + ch)))
        img.save(os.path.join(args.out, name + ".png"))
        if args.stickers:
            os.makedirs(args.stickers, exist_ok=True)
            img = sticker(img)
            img.save(os.path.join(args.stickers, name + ".png"))
        cuts.append(img)
        print("wrote", name, img.size)

    if args.preview:
        # On the plugin's dark panel colour, small (as in the speech bubble) and large
        tile = 220
        prev = Image.new("RGB", (tile * len(cuts), tile + 90), (22, 24, 32))
        for i, img in enumerate(cuts):
            for size, top in ((tile - 20, 10), (70, tile + 10)):
                t = img.copy()
                t.thumbnail((size, size), Image.LANCZOS)
                prev.paste(t, (i * tile + (tile - t.width) // 2, top), t)
        prev.save(args.preview)


if __name__ == "__main__":
    main()
