"""Cut the face decals of the mascot out of Jakob's expression sheet (Character/FaceExpressions.png).

    python tools/mascot/make_faces.py <FaceExpressions.png> art/mascot/faces

The sheet shows the same head five times (neutral, wink, surprised, angry, sad), all in the same place,
4096 px wide. Each decal is the face of one expression - eyes and brows from one head, the mouth from
the same or another one - with a soft alpha around both regions, so the model's own painted face under it
disappears. render_mascot.py projects it onto the face (FACE_* there maps decal pixels to the model).
"""

import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# The five heads in the sheet: left edge of each crop (px at 4096 wide); all crops share top and size
HEAD_X = [205, 980, 1756, 2531, 3306]
HEAD_TOP, CROP_W, CROP_H = 1580, 790, 995
NAMES = ["neutral", "wink", "surprised", "angry", "sad"]

# Regions in crop pixels: eyes + brows, and the mouth (big enough for the open mouth of "surprised")
EYES = (266, 452, 245, 145)    # centre x, y, radius x, y (top stays below the cap)
MOUTH = (266, 642, 115, 80)
FEATHER = 14
# The decal image: this part of the crop (both regions with their feather)
BOX = (0, 270, 540, 740)

# The decals: name -> (eyes from, mouth from); no eyes = the model's own (clearer than the sheet's)
DECALS = {
    "neutral": (None, "neutral"),
    "neutral_smile": (None, "wink"),               # talking: half-open mouth
    "neutral_open": (None, "surprised"),           # talking: open mouth
    "wink": ("wink", "wink"),
    "surprised": ("surprised", "surprised"),
    "sad": ("sad", "sad"),
}


def mask(size, region):
    m = Image.new("L", size, 0)
    cx, cy, rx, ry = region
    ImageDraw.Draw(m).ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=255)
    return m.filter(ImageFilter.GaussianBlur(FEATHER))


def neutral_whites(img):
    """Eye whites, highlights and teeth to grey-white: the sheet's are pinkish, and the render's colour
    grading (saturation x1.35, made for the body texture) turned them red (Jakob, 2026-10-28)."""
    a = np.asarray(img, dtype=np.float32) / 255.0
    hi, lo = a.max(axis=2), a.min(axis=2)
    sat = (hi - lo) / np.maximum(hi, 1e-6)
    w = np.clip((0.55 - sat) / 0.2, 0, 1) * np.clip((hi - 0.45) / 0.15, 0, 1)
    lum = a.mean(axis=2, keepdims=True)
    out = lum + (a - lum) * (1.0 - 0.95 * w[..., None])
    return Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8))


def brown_irises(img, region):
    """The sheet's irises are a dark magenta that read as red eyes on the plugin; the model's own are
    brown with an orange rim: move their hue there (dark, saturated pixels inside `region` only)."""
    hsv = np.asarray(img.convert("HSV"), dtype=np.float32)
    rgbmask = np.asarray(region, dtype=np.float32) / 255.0
    h, s, v = hsv[..., 0], hsv[..., 1] / 255, hsv[..., 2] / 255
    # Red stays (skin, brows): only the magenta / purple of the irises moves, the short way round the hue circle
    to_red = np.minimum(np.abs(h - 0.0), np.abs(h - 255.0))
    not_red = np.clip((to_red - 10.0) / 8.0, 0, 1)
    iris = np.clip((0.75 - v) / 0.15, 0, 1) * np.clip((s - 0.25) / 0.15, 0, 1) * rgbmask * not_red
    delta = (14.0 - h + 128.0) % 256.0 - 128.0               # ~20 degrees: orange-brown
    hsv[..., 0] = (h + delta * iris) % 256.0
    hsv[..., 2] = v * (1.0 - 0.25 * iris) * 255
    return Image.fromarray(np.clip(hsv, 0, 255).astype(np.uint8), "HSV").convert("RGB")


def main():
    sheet = Image.open(sys.argv[1]).convert("RGB")
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    scale = sheet.width / 4096
    heads = {}
    for name, x in zip(NAMES, HEAD_X):
        box = tuple(int(v * scale) for v in (x, HEAD_TOP, x + CROP_W, HEAD_TOP + CROP_H))
        # A light blur against the sheet's compression noise, which the colour grading in the render brings out
        heads[name] = sheet.crop(box).resize((CROP_W, CROP_H), Image.LANCZOS).filter(ImageFilter.GaussianBlur(0.8))

    eyes_m, mouth_m = mask((CROP_W, CROP_H), EYES), mask((CROP_W, CROP_H), MOUTH)
    for name, (eyes_from, mouth_from) in DECALS.items():
        eyes = brown_irises(heads[eyes_from or mouth_from], eyes_m)
        rgb = neutral_whites(Image.composite(heads[mouth_from], eyes, mouth_m))
        alpha = np.maximum(np.asarray(eyes_m) if eyes_from else 0, np.asarray(mouth_m))
        rgba = rgb.convert("RGBA")
        rgba.putalpha(Image.fromarray(alpha))
        rgba = rgba.crop(BOX)
        rgba.save(os.path.join(out, name + ".png"))
        print("wrote", name)


if __name__ == "__main__":
    main()
