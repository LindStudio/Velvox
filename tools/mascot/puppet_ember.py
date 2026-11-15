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
from PIL import Image, ImageDraw, ImageFont
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
# arms (2026-11-15): own layers below ARM_CUT, where the outline separates them from the torso; they bend from
# the cut (weight 0) to ARM_CUT + 70 (weight 1) around the shoulder points, hands at HAND_*
ARM_CUT = 585
SHOULDER_L = (363, 585)
SHOULDER_R = (660, 585)
HAND_L = (345, 722)
HAND_R = (680, 722)


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
        # arms: their own layers (Jakob 2026-11-15: red blocks at the waist when the arms swung out - the old
        # weight box turned torso pixels with the arm). Below ARM_CUT the dark outline separates arm and torso:
        # each arm is the fill inside its outline plus its own side of the line; the torso keeps the shared line,
        # so it still has an edge where the arm swings away.
        lum = rgb @ np.array([0.3, 0.59, 0.11], np.float32)
        below = py >= ARM_CUT
        fill = (al > 0.5) & (lum >= 0.30) & below & (py < 800)
        lab, n = ndimage.label(fill)
        sizes = ndimage.sum(fill, lab, range(1, n + 1))
        order = np.argsort(-sizes)
        torso = lab == order[0] + 1
        arms = sorted((lab == i + 1 for i in order[1:3]), key=lambda m: np.nonzero(m)[1].mean())   # left, right
        d_t = ndimage.distance_transform_edt(~torso)
        self.arm_layer, self.arm_rm = [], []
        for m in arms:
            d_a = ndimage.distance_transform_edt(~m)
            near = (d_a <= 9) & ~tail
            # at the cut the two overlap (layer from 8 px above, removal from 4 px below; the arm does not move
            # there yet), so no soft seam lets the background through
            layer = near & (d_a <= d_t + 1) & (py >= ARM_CUT - 8)
            rm = near & (d_a + 1 < d_t) & (py >= ARM_CUT + 4)
            self.arm_layer.append(ndimage.gaussian_filter(layer.astype(np.float32), 0.7) * (py >= ARM_CUT - 8))
            self.arm_rm.append(ndimage.gaussian_filter(rm.astype(np.float32), 0.7) * (py >= ARM_CUT + 4))
        self.w_arm = smoothstep(py, ARM_CUT, ARM_CUT + 70)
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
            # inside of the eye = the yellow with the pupil gap closed; pupil and highlight as a layer that can slide
            disk = np.hypot(*np.mgrid[-8:9, -8:9]) <= 8
            interior = ndimage.binary_closing(yellow & (lab == k), structure=disk) | (yellow & (lab == k))
            interior = ndimage.binary_fill_holes(interior)
            lum = rgb.mean(axis=2)
            core = ndimage.binary_erosion(interior, iterations=4)          # the outline never slides along
            feat_a = np.where(core, np.clip((0.45 - lum) / 0.2, 0, 1) + np.clip((lum - 0.85) / 0.08, 0, 1), 0)
            yel = interior & (feat_a < 0.05)
            num = np.stack([ndimage.gaussian_filter(rgb[..., q] * yel, 6) for q in range(3)], axis=2)
            den = ndimage.gaussian_filter(yel.astype(np.float32), 6)[..., None]
            ybg = (num / np.maximum(den, 1e-6)).astype(np.float32)
            self.eyes.append(dict(c=c, major=major, A=A + margin, B=B + margin, Ai=A, Bi=B, u=u, v=v, region=region,
                                  skin=skin, box=box, interior=ndimage.gaussian_filter(interior.astype(np.float32), 0.7),
                                  ybg=ybg, feat_a=feat_a.astype(np.float32)))
        self.line = np.array([0.10, 0.04, 0.06], np.float32)   # closed eye: a line as dark as the outline
        self.yellow = np.median(rgb[yellow], axis=0)
        self._mouth()

    def _mouth(self):
        """The smile line (dark pixels under the nose), smoothed: the upper edge of the open mouth."""
        lum = self.src[..., :3].mean(axis=2)
        dark = (lum < 0.3) & (self.px > 440) & (self.px < 600) & (self.py > 440) & (self.py < 475)
        ys, xs = np.nonzero(dark)
        x0, x1 = xs.min(), xs.max()
        cols = np.arange(x0, x1 + 1)
        top = np.array([ys[xs == x].mean() if (xs == x).any() else np.nan for x in cols])
        ok = ~np.isnan(top)
        top = np.interp(np.arange(len(cols)), np.flatnonzero(ok), top[ok])
        self.smile = (x0, x1, ndimage.gaussian_filter1d(top, 2.0))

    # ------------------------------------------------------------ frame
    def face(self, close=0.0, look=0.0, mouth=0.0):
        """Source picture with the eyes looking sideways (`look` in px, + = to his left / our right), blinking and
        the mouth open (`mouth` 0..1)."""
        img = self.src
        if abs(look) > 0.05:
            img = img.copy()
            for e in self.eyes:
                y0, y1, x0, x1 = e['box']
                ys = self.py[y0:y1, x0:x1]
                xs = self.px[y0:y1, x0:x1] - look
                inner = e['interior'][y0:y1, x0:x1][..., None]
                fa = ndimage.map_coordinates(e['feat_a'], [ys, xs], order=1)[..., None]
                frgb = np.stack([ndimage.map_coordinates(self.src[..., q], [ys, xs], order=1) for q in range(3)], axis=2)
                eye = e['ybg'][y0:y1, x0:x1] * (1 - fa) + frgb * fa
                reg = img[y0:y1, x0:x1]
                reg[..., :3] = reg[..., :3] * (1 - inner) + eye * inner
        img = self.blinked(close, img)
        if mouth > 0.02:
            img = self.open_mouth(img, mouth)
        return img

    def open_mouth(self, img, amount):
        """An open grin under the smile line: dark inside, a tongue at the bottom, outlined."""
        img = img.copy()
        x0, x1, top = self.smile
        H = 30.0 * amount
        cols = np.arange(x0, x1 + 1)
        uu = (cols - x0) / max(1, x1 - x0)
        depth = H * np.sin(np.pi * np.clip(uu, 0, 1)) ** 0.8
        bot = top + 1.5 + depth
        y0, y1 = int(top.min()) - 4, int(bot.max()) + 6
        yy = self.py[y0:y1, x0:x1 + 1]
        t, b = top[None, :], bot[None, :]
        inside = np.clip(np.minimum(yy - t, b - yy) + 0.5, 0, 1)
        f = np.clip((yy - t) / np.maximum(b - t, 1e-3), 0, 1)
        inner = np.array([0.22, 0.03, 0.05]) * (1 - f[..., None]) + np.array([0.42, 0.07, 0.09]) * f[..., None]
        tongue = np.clip((f - 0.62) / 0.12, 0, 1)[..., None] * np.clip(1 - np.abs(uu[None, :] - 0.55) / 0.3, 0, 1)[..., None]
        inner = inner * (1 - tongue) + np.array([0.86, 0.33, 0.33]) * tongue
        reg = img[y0:y1, x0:x1 + 1]
        a = inside[..., None]
        reg[..., :3] = reg[..., :3] * (1 - a) + inner * a
        # the lower lip line
        d = np.abs(yy - b)
        la = (np.clip(3.0 - d, 0, 1) * (depth[None, :] > 1.0))[..., None]
        reg[..., :3] = reg[..., :3] * (1 - la) + self.line * la
        return img

    def blinked(self, close, base=None):
        """Source picture with both eyes closed by `close` (0 open .. 1 shut), like a cartoon blink: the eye is
        squashed onto its middle, skin closes over it, and shut it is a dark, slightly bowed line."""
        base = self.src if base is None else base
        if close <= 0.01:
            return base
        img = base.copy()
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
            eye = np.stack([ndimage.map_coordinates(base[..., k], [syp, sxp], order=1) for k in range(3)], axis=2)
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
        """pose: head, tail, ear_l, ear_r, arm_l, arm_r, lean (degrees), breath (0..), squash (-..+, + = flatter),
        bob (px up), look (px), mouth (0..1)."""
        # 1) the whole body: where each output pixel sits on the body at rest
        gx, gy = global_inverse(self.px, self.py, pose, self.w_belly)

        def wt(arr):
            return ndimage.map_coordinates(arr, [gy, gx], order=1, mode='nearest')

        # 2) the parts, around their joints, with their weights read at that rest position
        def parts(spec):
            sx, sy = gx.copy(), gy.copy()
            for w, c, deg in spec:
                if abs(deg) < 1e-3:
                    continue
                rx, ry = rot_inverse(gx, gy, c, deg)
                sx += w * (rx - gx)
                sy += w * (ry - gy)
            return sx, sy

        body = parts([(wt(self.w_head), NECK, pose.get('head', 0)),
                      (wt(self.w_ear_l), EAR_L, pose.get('ear_l', 0)),
                      (wt(self.w_ear_r), EAR_R, pose.get('ear_r', 0))])
        wa = wt(self.w_arm)
        arm_maps = [parts([(wa, SHOULDER_L, pose.get('arm_l', 0))]), parts([(wa, SHOULDER_R, pose.get('arm_r', 0))])]
        # the tail layer bends: the further out, the more it turns (two joints)
        wtl = wt(self.w_tail)
        tail = parts([(wtl, TAIL_BASE, pose.get('tail', 0) * 0.6), (wtl ** 2, TAIL_BASE, pose.get('tail', 0) * 0.4)])

        img = self.face(close, pose.get('look', 0.0), pose.get('mouth', 0.0))
        # premultiplied, so transparent pixels never bleed colour into the edge
        pm = img.copy()
        pm[..., :3] *= pm[..., 3:4]

        def warp(layer, m):
            mx, my = m
            return np.stack([ndimage.map_coordinates(layer[..., c], [my, mx], order=1, mode='constant', cval=0.0)
                             for c in range(4)], axis=2)

        t = self.tail_mask[..., None]
        rm = np.clip(self.arm_rm[0] + self.arm_rm[1], 0, 1)[..., None]
        out_body = warp(pm * (1 - t) * (1 - rm), body)
        out_tail = warp(pm * t, tail)
        out = out_body + out_tail * (1 - out_body[..., 3:4])     # tail under the body
        for mask, m in zip(self.arm_layer, arm_maps):              # arms over both
            arm = warp(pm * mask[..., None], m)
            out = arm + out * (1 - arm[..., 3:4])
        a = out[..., 3:4]
        out[..., :3] = np.where(a > 1e-4, out[..., :3] / np.maximum(a, 1e-4), 0)
        return out


FOOT = (512.0, FEET_Y)


def body_scale(pose):
    """Vertical / horizontal scale of the body from the feet: breathing stretches, squash flattens and widens."""
    sy = 1.0 + 0.018 * pose.get('breath', 0.0) - pose.get('squash', 0.0)
    sx = 1.0 + 0.6 * pose.get('squash', 0.0)
    return sx, sy


def global_inverse(x, y, pose, w_belly=None):
    """Output pixel -> the point of the resting body (undo bob, lean, squash / breath)."""
    y = y + pose.get('bob', 0.0)
    a = math.radians(-pose.get('lean', 0.0))
    dx, dy = x - FOOT[0], y - FOOT[1]
    x = FOOT[0] + math.cos(a) * dx - math.sin(a) * dy
    y = FOOT[1] + math.sin(a) * dx + math.cos(a) * dy
    sx, sy = body_scale(pose)
    y = FOOT[1] + (y - FOOT[1]) / sy
    x = FOOT[0] + (x - FOOT[0]) / sx
    if w_belly is not None:
        x = x - w_belly * (x - 512) * 0.025 * pose.get('breath', 0.0)
    return x, y


def to_frame(img):
    """1024 working picture -> 600 x 600 plugin frame: Ember ~480 px tall, a little left of centre (fire goes right)."""
    pil = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), 'RGBA')
    crop = pil.crop((170, 60, 870, 900))            # 700 x 840 around Ember (tail, jumps and effects included)
    scale = 520 / 770
    crop = crop.resize((round(700 * scale), round(840 * scale)), Image.LANCZOS)
    canvas = Image.new('RGBA', (OUT, OUT), (0, 0, 0, 0))
    canvas.alpha_composite(crop, (60, round(50 - 70 * scale)))   # Ember stays where he was, more room above
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


# ---------------------------------------------------------------- effects (drawn over the frame, 1024 px space)
FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Resources', 'Fonts', 'Montserrat-Bold.ttf')
MOUTH = (518, 470)


def head_point(p, pose):
    """Where a point on the head ends up for this pose - effects follow the head."""
    return body_point(p, NECK, pose.get('head', 0.0), pose)


def hand_point(side, pose):
    """Where a hand ends up for this pose (side 0 = left, 1 = right) - the plugin's S sparks fly from there."""
    return body_point((HAND_L, HAND_R)[side], (SHOULDER_L, SHOULDER_R)[side], pose.get(('arm_l', 'arm_r')[side], 0.0), pose)


def body_point(p, pivot, deg, pose):
    a = math.radians(deg)
    dx, dy = p[0] - pivot[0], p[1] - pivot[1]
    x = pivot[0] + math.cos(a) * dx - math.sin(a) * dy
    y = pivot[1] + math.sin(a) * dx + math.cos(a) * dy
    sx, sy = body_scale(pose)
    x = FOOT[0] + (x - FOOT[0]) * sx
    y = FOOT[1] + (y - FOOT[1]) * sy
    b = math.radians(pose.get('lean', 0.0))
    dx, dy = x - FOOT[0], y - FOOT[1]
    x = FOOT[0] + math.cos(b) * dx - math.sin(b) * dy
    y = FOOT[1] + math.sin(b) * dx + math.cos(b) * dy
    return x, y - pose.get('bob', 0.0)


class Fx:
    """A transparent 1024 layer for effects, drawn 2x supersampled for smooth edges."""
    S = 2

    def __init__(self):
        self.im = Image.new('RGBA', (W * self.S, W * self.S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im, 'RGBA')

    @staticmethod
    def _c(col, a):
        return tuple(int(v) for v in col[:3]) + (int(255 * float(np.clip(a, 0, 1))),)

    def circle(self, x, y, r, col, a):
        k = self.S
        self.d.ellipse([(x - r) * k, (y - r) * k, (x + r) * k, (y + r) * k], fill=self._c(col, a))

    def arc(self, x, y, r, a0, a1, col, a, width):
        k = self.S
        self.d.arc([(x - r) * k, (y - r) * k, (x + r) * k, (y + r) * k], a0, a1, fill=self._c(col, a), width=int(width * k))

    def poly(self, pts, col, a):
        self.d.polygon([(x * self.S, y * self.S) for x, y in pts], fill=self._c(col, a))

    def line(self, pts, col, a, width):
        self.d.line([(x * self.S, y * self.S) for x, y in pts], fill=self._c(col, a), width=int(width * self.S), joint='curve')

    def text(self, x, y, txt, size, col, a, angle=0.0):
        f = ImageFont.truetype(FONT, int(size * self.S))
        side = int(size * self.S * 2)
        tmp = Image.new('RGBA', (side, side), (0, 0, 0, 0))
        ImageDraw.Draw(tmp).text((side / 2, side / 2), txt, font=f, fill=self._c(col, a), anchor='mm',
                                 stroke_width=max(1, int(size * self.S * 0.07)), stroke_fill=self._c((40, 12, 20), a))
        tmp = tmp.rotate(angle, resample=Image.BICUBIC)
        self.im.alpha_composite(tmp, (int(x * self.S - side / 2), int(y * self.S - side / 2)))

    def star(self, x, y, r, col, a):
        pts = []
        for i in range(8):
            rr = r if i % 2 == 0 else r * 0.28
            ang = math.pi / 4 * i
            pts.append((x + rr * math.sin(ang), y - rr * math.cos(ang)))
        self.poly(pts, col, a)

    def note(self, x, y, size, col, a):
        """An eighth note: head, stem, flag."""
        k = self.S
        self.d.ellipse([(x - size * 0.55) * k, (y - size * 0.4) * k, (x + size * 0.55) * k, (y + size * 0.4) * k], fill=self._c(col, a))
        sx = x + size * 0.45
        self.line([(sx, y), (sx, y - size * 2.2)], col, a, size * 0.24)
        self.line([(sx, y - size * 2.2), (sx + size * 0.8, y - size * 1.4)], col, a, size * 0.24)

    def drop(self, x, y, r, a):
        """A sweat drop, pointed at the top."""
        pts = [(x, y - r * 2.1)] + [(x + r * math.cos(math.radians(d)), y + r * math.sin(math.radians(d))) for d in range(-20, 201, 15)]
        self.poly(pts, (150, 215, 255), a)
        self.circle(x - r * 0.35, y - r * 0.1, r * 0.25, (255, 255, 255), a * 0.9)

    def layer(self):
        return np.asarray(self.im.resize((W, W), Image.LANCZOS)).astype(np.float32) / 255.0


def over(img, fx):
    """Effects layer over the picture (both straight alpha)."""
    a = fx[..., 3:4]
    oa = img[..., 3:4]
    na = a + oa * (1 - a)
    out = np.empty_like(img)
    out[..., :3] = np.where(na > 1e-4, (fx[..., :3] * a + img[..., :3] * oa * (1 - a)) / np.maximum(na, 1e-4), 0)
    out[..., 3:4] = na
    return out


# ---------------------------------------------------------------- states
# Each returns (pose, eyes closed 0..1, effects function or None). Loops: time t runs 0 .. T.
def ease(x):
    return 0.5 - 0.5 * math.cos(math.pi * np.clip(x, 0, 1))


def pulse(t, at, length):
    """0 -> 1 -> 0 around time `at` (seconds), `length` seconds long."""
    x = (t - at) / length
    return float(math.sin(math.pi * x)) if 0 <= x <= 1 else 0.0


def cyc(t, T, phase=0.0):
    return math.sin(2 * math.pi * (t / T + phase))


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
    return pose, close, None


def dancing(t, T):
    """Signal (Jakob 2026-11-15: four moods only, he dances while the vocal plays): bounces on the beat
    (120 BPM) with squash and stretch, steps from side to side, swings one arm out after the other and leans
    with it, head bops, ears and tail swing. The S he takes away is drawn live by the plugin: a snap and a
    spark at the hands (mascot.json "hands", per frame)."""
    beat = (t * 2.0) % 1.0
    up = math.sin(math.pi * beat)                                  # 0 on the beat, 1 between
    land = max(0.0, 1.0 - beat / 0.2) if beat < 0.2 else 0.0       # squash just after the beat
    s = math.sin(2 * math.pi * t / 1.0)                            # one side per beat
    a_l, a_r = ease(max(0.0, s)), ease(max(0.0, -s))
    pose = dict(bob=22.0 * up, squash=0.06 * land - 0.025 * up, lean=-6.0 * s, head=5.0 * s + 4.0 * cyc(t, 0.5, 0.15),
                look=10.0 * s, tail=24.0 * cyc(t, 1.0, 0.2), ear_l=7.0 * cyc(t, 0.5), ear_r=-7.0 * cyc(t, 0.5),
                arm_l=(5.0 + 30.0 * a_l) - 6.0 * a_r, arm_r=-(5.0 + 30.0 * a_r) + 6.0 * a_l,   # + = left hand out
                mouth=0.2 + 0.15 * up, breath=0.5)
    return pose, min(1.0, 1.3 * pulse(t, 1.55, 0.36)), None


def sleeping(t, T):
    """Bypass: eyes shut, slumped, deep slow breaths, the head drops and jerks back up once, Zzz."""
    w = 2 * math.pi * t / T
    drop = ease(t / (T * 0.75)) if t < T * 0.75 else 1.0 - ease((t - T * 0.75) / (T * 0.08))
    pose = dict(breath=1.4 * (0.5 - 0.5 * math.cos(2 * w)), squash=0.03, head=6.0 + 9.0 * drop, lean=3.0 * drop + 1.5 * math.sin(w),
                bob=-6.0 * drop, tail=-22.0 + 4.0 * math.sin(w), ear_l=-12.0, ear_r=12.0, arm_l=2.0 * math.sin(w),
                arm_r=-2.0 * math.sin(w), mouth=0.15 + 0.1 * (0.5 - 0.5 * math.cos(2 * w)))

    def fx(f, pose):
        x0, y0 = head_point((660, 330), pose)
        for i in range(3):
            ph = (t / T + i / 3.0) % 1.0
            f.text(x0 + 120 * ph + 12 * math.sin(6 * ph), y0 - 150 * ph, 'Z', 44 + 40 * ph, (240, 230, 250),
                   math.sin(math.pi * ph) ** 0.7, angle=14)
    return pose, 1.0, fx


def listening(t, T):
    """AUTO listens: head to the side, right ear up, eyes towards it, sound waves at the ear."""
    w = 2 * math.pi * t / T
    pose = dict(head=-7.0 + 1.0 * math.sin(w), ear_r=-14.0 + 3.0 * math.sin(2 * w), ear_l=2.0, look=12.0,
                breath=0.5 - 0.5 * math.cos(w), tail=6.0 * math.sin(w))

    def fx(f, pose):
        ex, ey = head_point((715, 330), pose)
        for i in range(3):
            ph = (t + i / 3.0) % 1.0
            f.arc(ex + 5, ey, 28 + 70 * ph, -50, 50, (255, 214, 140), 0.95 * math.sin(math.pi * ph), 8)
    return pose, min(1.0, 1.3 * pulse(t, 1.4, 0.42)), fx


STATES = {
    'idle': (idle, 4.0),
    'dancing': (dancing, 2.0),
    'sleeping': (sleeping, 4.0),
    'listening': (listening, 3.0),
}


def frame_point(p):
    """1024 working point -> pixel in the 600 x 600 frame (the mapping of to_frame)."""
    return (p[0] - 170) * 520 / 770 + 60, (p[1] - 130) * 520 / 770 + 50


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('out')
    ap.add_argument('--states', default=','.join(STATES))
    ap.add_argument('--preview', action='store_true')
    ap.add_argument('--plugin', action='store_true',
                    help='frames for Products/Ember/Mascot/frames: old PNGs there are removed, 256-colour PNGs, '
                         'mascot.json (fps, mouth) written next to the folder')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    if a.plugin:
        for f in os.listdir(a.out):
            if f.endswith('.png'):
                os.remove(os.path.join(a.out, f))
    pup = Puppet(a.src)
    for name in a.states.split(','):
        fn, T = STATES[name]
        frames = []
        n = int(round(T * FPS))
        for i in range(n):
            pose, close, fx = fn(i / FPS, T)
            img = pup.frame(pose, close)
            if fx is not None:
                layer = Fx()
                fx(layer, pose)
                img = over(img, layer.layer())
            f = to_frame(img)
            out = f.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE) if a.plugin else f
            out.save(os.path.join(a.out, f'{name}_{i + 1:03d}.png'), optimize=True)
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
    if a.plugin:
        # where his hands are in every dancing frame: the plugin's S sparks fly from there
        fn, T = STATES['dancing']
        hands = []
        for i in range(int(round(T * FPS))):
            pose = fn(i / FPS, T)[0]
            hands.append([[round(c, 1) for c in frame_point(hand_point(s, pose))] for s in (0, 1)])
        import json
        with open(os.path.join(os.path.dirname(os.path.abspath(a.out)), 'mascot.json'), 'w', encoding='utf-8') as fh:
            json.dump({'fps': FPS, 'size': OUT, 'hands': hands,
                       '_source': 'tools/mascot/puppet_ember.py <Drive: Plugins/01 EMBER De-Esser/Embernew.png> '
                                  'Products/Ember/Mascot/frames --plugin'}, fh, indent=1)
        print('mascot.json: hands for', len(hands), 'dancing frames')


if __name__ == '__main__':
    main()
