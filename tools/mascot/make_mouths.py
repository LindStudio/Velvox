"""Draw the mouth decals for the devil mascot (style from art/mascot/concepts/RedDevilGesichter.png).

    python tools/mascot/make_mouths.py art/mascot/mouths

Writes zu.png / halb.png / offen.png (512x512 RGBA). The decal is mapped onto a plane that is
shrink-wrapped to the face, so the mouth sits in the middle of the image with transparent margin.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 512
SS = 4  # supersampling for smooth edges

LIP = (110, 12, 18, 255)
INSIDE = (55, 6, 12, 255)
TONGUE = (205, 70, 85, 255)
TOOTH = (250, 246, 240, 255)


def canvas():
    return Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))


def s(*v):
    return [x * SS for x in v]


def finish(im):
    im = im.filter(ImageFilter.GaussianBlur(SS * 0.6))
    return im.resize((SIZE, SIZE), Image.LANCZOS)


def closed():
    im = canvas()
    d = ImageDraw.Draw(im)
    # Small, slightly smiling line like the neutral face.
    d.arc(s(196, 180, 316, 290), 35, 145, fill=LIP, width=11 * SS)
    return finish(im)


def half():
    im = canvas()
    d = ImageDraw.Draw(im)
    d.ellipse(s(216, 214, 296, 300), fill=INSIDE, outline=LIP, width=7 * SS)
    d.chord(s(230, 262, 282, 312), 200, 340, fill=TONGUE)
    return finish(im)


def open_():
    im = canvas()
    d = ImageDraw.Draw(im)
    box = s(176, 196, 336, 326)
    d.ellipse(box, fill=INSIDE, outline=LIP, width=8 * SS)
    # Tongue at the bottom, clipped to the mouth.
    tongue = canvas()
    ImageDraw.Draw(tongue).ellipse(s(200, 268, 312, 360), fill=TONGUE)
    mask = canvas()
    ImageDraw.Draw(mask).ellipse(s(184, 204, 328, 318), fill=(255, 255, 255, 255))
    im.alpha_composite(Image.composite(tongue, Image.new("RGBA", tongue.size), mask.split()[3]))
    # Two little fangs hanging from the upper lip.
    for x in (214, 278):
        d.polygon(s(x, 206, x + 20, 206, x + 10, 238), fill=TOOTH)
    return finish(im)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "art/mascot/mouths"
    os.makedirs(out, exist_ok=True)
    for name, make in (("zu", closed), ("halb", half), ("offen", open_)):
        make().save(os.path.join(out, name + ".png"))


if __name__ == "__main__":
    main()
