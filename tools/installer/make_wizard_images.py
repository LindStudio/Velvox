"""Wizard pictures for the Inno Setup installer of a single plugin, from its icon PNG.

Usage: python tools/installer/make_wizard_images.py <icon.png> <out folder> [accent hex, e.g. ff3b1f]
Writes wizard_small.bmp (110 x 116) and wizard_large.bmp (328 x 628).
"""
import os, sys
from PIL import Image, ImageDraw, ImageFilter

icon = Image.open(sys.argv[1]).convert('RGBA')
out = sys.argv[2]
accent = sys.argv[3] if len(sys.argv) > 3 else 'ff3b1f'
rgb = tuple(int(accent[i:i + 2], 16) for i in (0, 2, 4))

small = Image.new('RGBA', (110, 116), (5, 6, 10, 255))
small.alpha_composite(icon.resize((100, 100), Image.LANCZOS), (5, 8))
small.convert('RGB').save(os.path.join(out, 'wizard_small.bmp'))

large = Image.new('RGBA', (328, 628), (5, 6, 10, 255))
glow = Image.new('RGBA', large.size, (0, 0, 0, 0))
ImageDraw.Draw(glow).ellipse((24, 120, 304, 400), fill=rgb + (90,))
large.alpha_composite(glow.filter(ImageFilter.GaussianBlur(40)))
large.alpha_composite(icon.resize((240, 240), Image.LANCZOS), (44, 140))
large.convert('RGB').save(os.path.join(out, 'wizard_large.bmp'))
print('ok')
