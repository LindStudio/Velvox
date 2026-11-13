"""Ember as a 2D puppet: animates Jakob's single still (Embernew.png, 2026-11-13) without drawing new frames.

The picture is cut out once; every frame is the same picture, warped with soft regional transforms
(head tilt, tail swing, ear flicks, arm swings, breathing) plus a blink painted over the eyes. Ember therefore stays
exactly on model - what AI video could not do.

    python tools/mascot/puppet_ember.py <Embernew.png> <out dir> [--states idle,...] [--preview]

Writes <state>_###.png (600 x 600, the plugin's frame format, see Products/Ember/Mascot/README.md) and, with
--preview, <state>_preview.gif at 2x speed-true timing for Jakob.
Landmarks are in the 1024 px working picture (the 2048 px original / 2).
"""
import argparse
import math
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, os.path.dirname(__file__))
from cut_ember import smoothstep  # noqa: E402

W = 1024          # working size
OUT = 600         # frame size of the plugin
FPS = 12

# ---- landmarks of Embernew.png at 1024 px
FEET_Y = 855
NECK = (512, 520)            # head pivot
TAIL_BASE = (640, 770)
EAR_L = (360, 375)           # ear bases (pivots)
EAR_R = (668, 375)
SHOULDER_L = (385, 545)
SHOULDER_R = (640, 545)


def rot_inverse(px, py, c, deg):
    """Where a destination pixel came from when the region turns by deg around c."""
    a = math.radians(-deg)
    ca, sa = math.cos(a), math.sin(a)
    dx, dy = px - c[0], py - c[1]
    return c[0] + ca * dx - sa * dy, c[1] + sa * dx + ca * dy


def cut_white(im):
    """Embernew.png is on white: every white area that is background - the outside and the closed gaps between
    arm and body - becomes transparent; small white spots (eye highlights) stay. The soft edge is pulled in
    1.5 px so no light seam stays around the dark outline."""
    rgb = np.asarray(im.resize((W, W), Image.LANCZOS)).astype(np.float32) / 255.0
    whiteness = rgb.min(axis=2)
    white = whiteness > 0.86
    lab, n = ndimage.label(white)
    sizes = ndimage.sum(white, lab, range(1, n + 1))
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    bg = np.isin(lab, [k + 1 for k in range(n) if sizes[k] > 150 or (k + 1) in border])
    # soft alpha: 1 inside, fading over the anti-aliased edge by how white the pixel is
    edge = ndimage.binary_dilation(bg, iterations=2) & ~bg
    alpha = np.where(bg, 0.0, 1.0)
    alpha = np.where(edge, np.clip((1.0 - whiteness) / 0.35, 0, 1), alpha)
    alpha = np.minimum(alpha, ndimage.gaussian_filter(ndimage.grey_erosion(alpha, size=(3, 3)), 0.6))
    # un-mix the white out of the edge colour
    a = alpha[..., None]
    rgb = np.where(a > 0.02, (rgb - (1 - a)) / np.maximum(a, 0.02), rgb)
    return np.concatenate([np.clip(rgb, 0, 1), alpha[..., None]], axis=2).astype(np.float32)


class Puppet:
    def __init__(self, path):
        im = Image.open(path).convert('RGB')
        self.src = cut_white(im)
        self.py, self.px = np.mgrid[0:W, 0:W].astype(np.float32)
        self._regions()
        self._eyes()

    # ------------------------------------------------------------ weights
    def _regions(self):
        px, py = self.px, self.py
        self.w_head = 1.0 - smoothstep(py, NECK[1] - 30, NECK[1] + 25)
        # tail: right of the leg, below the arm; weight grows along the tail so it bends, not just turns
        d = np.hypot(px - TAIL_BASE[0], py - TAIL_BASE[1])
        # the tail by its colour (dark purple, like the horns) inside the box right of the leg, with its outline
        rgb, al = self.src[..., :3], self.src[..., 3]
        purple = (rgb[..., 2] > rgb[..., 1] + 0.04) & (rgb[..., 0] < 0.6) & (al > 0.5) & (px > TAIL_BASE[0] - 30) & (py > 620) & ((px > 714) | (py > 750)) & (py < 815)
        lab, n = ndimage.label(purple)
        big = np.argmax(ndimage.sum(purple, lab, range(1, n + 1))) + 1
        tail = ndimage.binary_dilation(ndimage.binary_fill_holes(lab == big), iterations=7)
        self.w_tail = ndimage.gaussian_filter(tail.astype(np.float32), 2.0) * smoothstep(d, 0, 140)
        self.tail_len = 1.0
        # ears: outside the face
        self.w_ear_l = (1 - smoothstep(px, EAR_L[0] - 5, EAR_L[0] + 20)) * (1 - smoothstep(py, 410, 440)) * smoothstep(py, 260, 280)
        self.w_ear_r = smoothstep(px, EAR_R[0] - 20, EAR_R[0] + 5) * (1 - smoothstep(py, 410, 440)) * smoothstep(py, 260, 280)
        # arms: outside the torso, from the shoulder down (the right hand stays clear of the tail box)
        self.w_arm_l = (1 - smoothstep(px, SHOULDER_L[0] - 5, SHOULDER_L[0] + 15)) * smoothstep(py, 560, 600) * (1 - smoothstep(py, 760, 780))
        self.w_arm_r = smoothstep(px, SHOULDER_R[0] - 15, SHOULDER_R[0] + 5) * smoothstep(py, 560, 600) * (1 - smoothstep(py, 760, 780)) * (1 - self.w_tail)
        self.w_belly = np.exp(-(((px - 512) / 150) ** 2 + ((py - 640) / 120) ** 2))

    def _eyes(self):
        rgb = self.src[..., :3]
        yellow = (rgb[..., 0] > 0.75) & (rgb[..., 1] > 0.6) & (rgb[..., 2] < 0.5) & (self.py < 450) & (self.py > 300)
        lab, n = ndimage.label(yellow)
        sizes = ndimage.sum(yellow, lab, range(1, n + 1))
        keep = np.argsort(sizes)[::-1][:2] + 1
        self.eyes = []
        for k in keep:
            m = ndimage.binary_fill_holes(ndimage.binary_dilation(lab == k, iterations=3))
            ys, xs = np.nonzero(m)
            self.eyes.append((m, xs.min(), xs.max(), ys.min(), ys.max()))
        # skin above the eyes (lid colour) and the outline colour
        above = (self.py > min(e[3] for e in self.eyes) - 40) & (self.py < min(e[3] for e in self.eyes) - 25) \
            & (self.px > min(e[1] for e in self.eyes)) & (self.px < max(e[2] for e in self.eyes))
        self.skin = np.median(rgb[above], axis=0)
        self.line = np.array([0.30, 0.10, 0.12], np.float32)

    # ------------------------------------------------------------ frame
    def blinked(self, close):
        """Source picture with both eyes closed by `close` (0 open .. 1 shut): a lid of skin with a dark edge."""
        if close <= 0.01:
            return self.src
        img = self.src.copy()
        for m, x0, x1, y0, y1 in self.eyes:
            sub = m[y0:y1 + 1, x0:x1 + 1]
            cols = np.arange(x0, x1 + 1)
            top = np.array([np.argmax(sub[:, i]) if sub[:, i].any() else 0 for i in range(len(cols))]) + y0
            bot = np.array([len(sub) - 1 - np.argmax(sub[::-1, i]) if sub[:, i].any() else 0 for i in range(len(cols))]) + y0
            edge = top + close * (bot - top)
            yy = self.py[y0:y1 + 1, x0:x1 + 1]
            lid = sub & (yy <= edge[None, :])
            region = img[y0:y1 + 1, x0:x1 + 1]
            region[lid, :3] = self.skin
            dist = np.abs(yy - edge[None, :])
            line = sub & (dist < 2.6) & (close > 0.15)
            a = np.clip(2.6 - dist, 0, 1)[..., None]
            region[..., :3] = np.where(line[..., None], region[..., :3] * (1 - a) + self.line * a, region[..., :3])
        return img

    def frame(self, pose, close=0.0):
        """pose: head, tail, ear_l, ear_r, arm_l, arm_r (degrees), breath (0..1), bob (px up)."""
        px, py = self.px, self.py
        sx, sy = px.copy(), py.copy()

        def add(w, c, deg):
            nonlocal sx, sy
            if abs(deg) < 1e-3:
                return
            rx, ry = rot_inverse(px, py, c, deg)
            sx += w * (rx - px)
            sy += w * (ry - py)

        add(self.w_head, NECK, pose.get('head', 0))
        # the tail bends: the further out, the more it turns (two joints)
        add(self.w_tail, TAIL_BASE, pose.get('tail', 0) * 0.6)
        add(self.w_tail ** 2, TAIL_BASE, pose.get('tail', 0) * 0.4)
        add(self.w_ear_l, EAR_L, pose.get('ear_l', 0))
        add(self.w_ear_r, EAR_R, pose.get('ear_r', 0))
        add(self.w_arm_l, SHOULDER_L, pose.get('arm_l', 0))
        add(self.w_arm_r, SHOULDER_R, pose.get('arm_r', 0))
        # breathing: the body stretches up from the feet, the belly widens a little
        b = pose.get('breath', 0.0)
        s = 1.0 + 0.018 * b
        sy = FEET_Y + (sy - FEET_Y) / s
        sx = sx - self.w_belly * (sx - 512) * 0.025 * b
        sy = sy + pose.get('bob', 0.0)

        img = self.blinked(close)
        out = np.empty_like(img)
        # premultiplied, so transparent pixels never bleed colour into the edge
        pm = img.copy()
        pm[..., :3] *= pm[..., 3:4]
        for ch in range(4):
            out[..., ch] = ndimage.map_coordinates(pm[..., ch], [sy, sx], order=1, mode='constant', cval=0.0)
        a = out[..., 3:4]
        out[..., :3] = np.where(a > 1e-4, out[..., :3] / np.maximum(a, 1e-4), 0)
        return out


def to_frame(img):
    """1024 working picture -> 600 x 600 plugin frame: Ember ~480 px tall, a little left of centre (fire goes right)."""
    pil = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), 'RGBA')
    crop = pil.crop((170, 130, 870, 900))           # 700 x 770 around Ember (tail and bob included)
    scale = 520 / 770
    crop = crop.resize((round(700 * scale), 520), Image.LANCZOS)
    canvas = Image.new('RGBA', (OUT, OUT), (0, 0, 0, 0))
    canvas.alpha_composite(crop, (60, 50))
    return canvas


# ---------------------------------------------------------------- states
def ease(x):
    return 0.5 - 0.5 * math.cos(math.pi * np.clip(x, 0, 1))


def pulse(t, at, length):
    """0 -> 1 -> 0 around time `at` (seconds), `length` seconds long."""
    x = (t - at) / length
    return float(math.sin(math.pi * x)) if 0 <= x <= 1 else 0.0


def idle(t, T):
    w = 2 * math.pi * t / T
    pose = dict(
        breath=0.5 - 0.5 * math.cos(2 * w),                        # 2 breaths per loop
        head=2.0 * math.sin(w),
        tail=10.0 * math.sin(2 * w + 0.6) + 4.0 * math.sin(4 * w),
        ear_r=-9.0 * pulse(t, 2.6, 0.35),                          # a flick of the right ear
        ear_l=1.5 * math.sin(w + 1.0),
        arm_l=1.5 * math.sin(2 * w), arm_r=-1.5 * math.sin(2 * w),
    )
    close = max(pulse(t, 1.1, 0.3), 0.0)
    close = min(1.0, close * 1.6)
    return pose, close


STATES = {'idle': (idle, 4.0)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('out')
    ap.add_argument('--states', default='idle')
    ap.add_argument('--preview', action='store_true')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    pup = Puppet(a.src)
    for name in a.states.split(','):
        fn, T = STATES[name]
        frames = []
        n = int(round(T * FPS))
        for i in range(n):
            pose, close = fn(i / FPS, T)
            f = to_frame(pup.frame(pose, close))
            f.save(os.path.join(a.out, f'{name}_{i + 1:03d}.png'), optimize=True)
            frames.append(f)
        if a.preview:
            bg = (24, 14, 16, 255)
            pv = []
            for f in frames:
                c = Image.new('RGBA', f.size, bg)
                c.alpha_composite(f)
                pv.append(c.convert('RGB').convert('P', palette=Image.ADAPTIVE))
            pv[0].save(os.path.join(a.out, f'{name}_preview.gif'), save_all=True, append_images=pv[1:],
                       duration=int(1000 / FPS), loop=0, disposal=2)
        print(name, n, 'frames')


if __name__ == '__main__':
    main()
