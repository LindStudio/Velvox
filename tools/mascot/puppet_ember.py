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
        tail = ndimage.binary_dilation(ndimage.binary_fill_holes(lab == big), iterations=7) & (px > 610)
        # the tail is its own layer (under the body): it can swing anywhere without being cut off
        self.tail_mask = ndimage.gaussian_filter(tail.astype(np.float32), 1.2)
        self.w_tail = smoothstep(d, 0, 140)
        # ears: outside the face
        self.w_ear_l = (1 - smoothstep(px, EAR_L[0] - 5, EAR_L[0] + 20)) * (1 - smoothstep(py, 410, 440)) * smoothstep(py, 260, 280)
        self.w_ear_r = smoothstep(px, EAR_R[0] - 20, EAR_R[0] + 5) * (1 - smoothstep(py, 410, 440)) * smoothstep(py, 260, 280)
        # arms: outside the torso, from the shoulder down (the right hand stays clear of the tail box)
        self.w_arm_l = (1 - smoothstep(px, SHOULDER_L[0] - 5, SHOULDER_L[0] + 15)) * smoothstep(py, 560, 600) * (1 - smoothstep(py, 760, 780))
        self.w_arm_r = smoothstep(px, SHOULDER_R[0] - 15, SHOULDER_R[0] + 5) * smoothstep(py, 560, 600) * (1 - smoothstep(py, 760, 780)) * (1 - self.tail_mask)
        self.w_belly = np.exp(-(((px - 512) / 150) ** 2 + ((py - 640) / 120) ** 2))

    def _eyes(self):
        """Each eye as an ellipse fitted to its yellow (both halves around the pupil), plus the skin around it."""
        rgb = self.src[..., :3]
        yellow = (rgb[..., 0] > 0.75) & (rgb[..., 1] > 0.6) & (rgb[..., 2] < 0.5) & (self.py < 450) & (self.py > 300)
        r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
        skinlike = (r > 0.7) & (g > 0.2) & (g < 0.6) & (b < 0.4) & (self.src[..., 3] > 0.9)
        self.eyes = []
        # the two biggest yellow areas once the halves around each pupil are joined = the eyes
        grown = ndimage.binary_fill_holes(ndimage.binary_dilation(yellow, iterations=6))
        lab, n = ndimage.label(grown)
        sizes = ndimage.sum(grown, lab, range(1, n + 1))
        for k in np.argsort(sizes)[::-1][:2] + 1:
            ys, xs = np.nonzero(yellow & (lab == k))
            # level ellipse from the extent of the yellow (a fitted tilt made the shut line run to the nose)
            c = np.array([(xs.min() + xs.max()) * 0.5, (ys.min() + ys.max()) * 0.5])
            A, B = (xs.max() - xs.min()) * 0.5, (ys.max() - ys.min()) * 0.5
            major = np.array([1.0, 0.0])
            u = self.px - c[0]
            v = self.py - c[1]
            margin = 7.0                                                     # the dark outline around the eye
            region = (u / (A + margin)) ** 2 + (v / (B + margin)) ** 2 <= 1.0
            ring = ndimage.binary_dilation(region, iterations=18) & ~ndimage.binary_dilation(region, iterations=3) & skinlike
            sig = 14
            num = np.stack([ndimage.gaussian_filter(rgb[..., k] * ring, sig) for k in range(3)], axis=2)
            den = ndimage.gaussian_filter(ring.astype(np.float32), sig)[..., None]
            flat = np.median(rgb[ring], axis=0)
            wgt = np.clip(den / 0.05, 0, 1)
            skin = ((num / np.maximum(den, 1e-6)) * wgt + flat * (1 - wgt)).astype(np.float32)
            yy, xx = np.nonzero(region)
            box = (yy.min() - 2, yy.max() + 3, xx.min() - 2, xx.max() + 3)
            self.eyes.append(dict(c=c, major=major, A=A + margin, B=B + margin, u=u, v=v, region=region, skin=skin, box=box))
        self.line = np.array([0.10, 0.04, 0.06], np.float32)   # closed eye: a line as dark as the outline

    # ------------------------------------------------------------ frame
    def blinked(self, close):
        """Source picture with both eyes closed by `close` (0 open .. 1 shut), like a cartoon blink: the eye is
        squashed onto its middle, skin closes over it, and shut it is a dark, slightly bowed line."""
        if close <= 0.01:
            return self.src
        img = self.src.copy()
        sq = max(0.04, 1.0 - close)
        for e in self.eyes:
            y0, y1, x0, x1 = e['box']
            u = e['u'][y0:y1, x0:x1]
            v = e['v'][y0:y1, x0:x1]
            region = e['region'][y0:y1, x0:x1]
            A, B, c, mj = e['A'], e['B'], e['c'], e['major']
            # the eye closes onto a line a little below its middle (the upper lid travels further)
            v0 = 0.15 * B
            vs = v0 + (v - v0) / sq                                       # where the squashed pixel comes from
            inside = region & ((u / A) ** 2 + (vs / B) ** 2 <= 1.0) & (close < 0.9)   # shut: only skin and the line
            sxp = c[0] + u
            syp = c[1] + vs
            eye = np.stack([ndimage.map_coordinates(self.src[..., k], [syp, sxp], order=1) for k in range(3)], axis=2)
            reg = img[y0:y1, x0:x1]
            sk = e['skin'][y0:y1, x0:x1]
            soft = np.clip(((1.0 - np.sqrt((u / A) ** 2 + (v / B) ** 2)) * B) / 1.5, 0, 1)[..., None]   # 1.5 px soft rim
            new = np.where(inside[..., None], eye, sk)
            reg[..., :3] = reg[..., :3] * (1 - soft) + new * soft
            # the lid line: follows the squashed middle, bowed down a little, thinning to both ends
            t = np.clip(u / A, -1, 1)
            bow = v0 + 0.18 * B * (1 - t * t) * close
            half = (2.0 + 2.2 * close) * np.sqrt(np.clip(1 - t * t, 0, 1))
            dist = np.abs(v - bow)
            a = (np.clip(half + 0.8 - dist, 0, 1) * smoothstep(close, 0.35, 0.8) * (np.abs(u) < A * 0.92))[..., None]
            reg[..., :3] = reg[..., :3] * (1 - a) + self.line * a
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
        add(self.w_ear_l, EAR_L, pose.get('ear_l', 0))
        add(self.w_ear_r, EAR_R, pose.get('ear_r', 0))
        add(self.w_arm_l, SHOULDER_L, pose.get('arm_l', 0))
        add(self.w_arm_r, SHOULDER_R, pose.get('arm_r', 0))
        body = (sx, sy)
        # the tail layer bends: the further out, the more it turns (two joints)
        sx, sy = px.copy(), py.copy()
        add(self.w_tail, TAIL_BASE, pose.get('tail', 0) * 0.6)
        add(self.w_tail ** 2, TAIL_BASE, pose.get('tail', 0) * 0.4)
        tail = (sx, sy)

        b = pose.get('breath', 0.0)
        bob = pose.get('bob', 0.0)

        def global_(m):
            # breathing: the body stretches up from the feet, the belly widens a little
            mx, my = m
            my = FEET_Y + (my - FEET_Y) / (1.0 + 0.018 * b)
            mx = mx - self.w_belly * (mx - 512) * 0.025 * b
            return mx, my + bob

        img = self.blinked(close)
        # premultiplied, so transparent pixels never bleed colour into the edge
        pm = img.copy()
        pm[..., :3] *= pm[..., 3:4]

        def warp(layer, m):
            mx, my = global_(m)
            return np.stack([ndimage.map_coordinates(layer[..., c], [my, mx], order=1, mode='constant', cval=0.0)
                             for c in range(4)], axis=2)

        t = self.tail_mask[..., None]
        out_body = warp(pm * (1 - t), body)
        out_tail = warp(pm * t, tail)
        out = out_body + out_tail * (1 - out_body[..., 3:4])     # tail under the body
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


def plugin_background(size):
    """What the plugin draws behind Ember in SIMPLE: the panel colour and a soft red glow (EmberMascot.cpp)."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    r = np.hypot(x - size * 0.45, y - size * 0.5) / (size * 0.5)
    glow = 0.18 * np.clip(1 - r, 0, 1) ** 1.5
    panel = np.array([7, 8, 13], np.float32)
    red = np.array([255, 59, 31], np.float32)
    rgb = panel * (1 - glow[..., None]) + red * glow[..., None]
    return Image.fromarray(np.concatenate([rgb, np.full((size, size, 1), 255, np.float32)], axis=2).astype(np.uint8), 'RGBA')


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
    close = min(1.0, 1.3 * pulse(t, 1.08, 0.42))                   # half - shut - shut - half
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
            bg = plugin_background(OUT)
            pv = []
            for f in frames:
                c = bg.copy()
                c.alpha_composite(f)
                pv.append(c.convert('RGB').convert('P', palette=Image.ADAPTIVE))
            pv[0].save(os.path.join(a.out, f'{name}_preview.gif'), save_all=True, append_images=pv[1:],
                       duration=int(1000 / FPS), loop=0, disposal=2)
        print(name, n, 'frames')


if __name__ == '__main__':
    main()
