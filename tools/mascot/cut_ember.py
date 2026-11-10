"""Prepares Jakob's Ember images for the plugin (EMBER De-Esser).

The generated images have a dark solid background instead of transparency. This removes it:
  - distance of every pixel from the background colour (median of the border)
  - the character core (clearly not background) is dilated and its holes filled -> fully opaque
    (outlines, pupils and the mouth stay solid even though they are dark)
  - everything else fades with the distance (soft glow and embers keep a soft edge)
Then every image is scaled to the same size (same scale for all states, so Ember never jumps in size).

Usage: python tools/mascot/cut_ember.py <source dir> <frames dir> [size]
"""
import os, sys
import numpy as np
from PIL import Image
from scipy import ndimage

MAPPING = {
    'Sitting Ember.png': 'idle',
    'Singing Ember.png': 'singing',
    'Snapping Ember.png': 'snapping',
    'Fire Ember.png': 'fire',
    'Overheated Ember.png': 'overheated',
    'Sleeping Ember.png': 'sleeping',
    'Listening Ember.png': 'listening',
    'Proud Ember.png': 'proud',
}

def smoothstep(x, a, b):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)

def cut(path, size):
    im = Image.open(path).convert('RGB')
    work = im.resize((1024, 1024), Image.LANCZOS)   # mask on a smaller copy, applied at full size below
    a = np.asarray(work).astype(np.float32)
    border = np.concatenate([a[:8].reshape(-1, 3), a[-8:].reshape(-1, 3), a[:, :8].reshape(-1, 3), a[:, -8:].reshape(-1, 3)])
    bg = np.median(border, axis=0)
    dist = np.sqrt(((a - bg) ** 2).sum(axis=2))
    core = dist > 70
    core = ndimage.binary_opening(core, iterations=1)
    solid = ndimage.binary_fill_holes(ndimage.binary_dilation(core, iterations=3))
    solid = ndimage.binary_erosion(solid, iterations=1)
    alpha = np.maximum(smoothstep(dist, 18, 70), solid.astype(np.float32))
    alpha = ndimage.gaussian_filter(alpha, 0.6)
    mask = Image.fromarray((np.clip(alpha, 0, 1) * 255).astype(np.uint8))
    out = im.resize((size, size), Image.LANCZOS).convert('RGBA')
    out.putalpha(mask.resize((size, size), Image.LANCZOS))
    # un-premultiply the dark background out of the soft edge: lift colour where alpha is partial
    arr = np.asarray(out).astype(np.float32)
    al = arr[..., 3:4] / 255.0
    bgc = np.asarray(Image.new('RGB', (1, 1), tuple(int(v) for v in bg))).astype(np.float32).reshape(1, 1, 3)
    rgb = arr[..., :3]
    fixed = np.where(al > 0.02, (rgb - bgc * (1 - al)) / np.maximum(al, 0.02), rgb)
    arr[..., :3] = np.clip(fixed, 0, 255)
    # soft fade towards the picture edge (the fire runs into the right edge of its picture);
    # applied after the colour fix so the faded edge keeps its real colour
    ramp = smoothstep(np.minimum(np.arange(size), np.arange(size)[::-1]).astype(np.float32), 0, size * 0.14)
    arr[..., 3] *= ramp[None, :] * ramp[:, None]
    return Image.fromarray(arr.astype(np.uint8), 'RGBA')

def main():
    src, dst = sys.argv[1], sys.argv[2]
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 900
    os.makedirs(dst, exist_ok=True)
    for f in os.listdir(dst):
        if f.endswith('.png'):
            os.remove(os.path.join(dst, f))
    for name, state in MAPPING.items():
        p = os.path.join(src, name)
        if not os.path.exists(p):
            print('missing', name)
            continue
        img = cut(p, size)
        out = os.path.join(dst, f'{state}_001.png')
        img.save(out, optimize=True)
        print(state, os.path.getsize(out) // 1024, 'KB')

if __name__ == '__main__':
    main()
