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

# The decals: name -> (eyes from, mouth from)
DECALS = {
    "neutral": ("neutral", "neutral"),
    "neutral_smile": ("neutral", "wink"),          # talking: half-open mouth
    "neutral_open": ("neutral", "surprised"),      # talking: open mouth
    "wink": ("wink", "wink"),
    "surprised": ("surprised", "surprised"),
    "sad": ("sad", "sad"),
}


def mask(size, region):
    m = Image.new("L", size, 0)
    cx, cy, rx, ry = region
    ImageDraw.Draw(m).ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=255)
    return m.filter(ImageFilter.GaussianBlur(FEATHER))


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
        rgb = Image.composite(heads[mouth_from], heads[eyes_from], mouth_m)
        alpha = np.maximum(np.asarray(eyes_m), np.asarray(mouth_m))
        rgba = rgb.convert("RGBA")
        rgba.putalpha(Image.fromarray(alpha))
        rgba = rgba.crop(BOX)
        rgba.save(os.path.join(out, name + ".png"))
        print("wrote", name)


if __name__ == "__main__":
    main()
