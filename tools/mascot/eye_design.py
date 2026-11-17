"""Vector eyes and brows for a figure drawn without them (2026-11-17, Ember).

    python tools/mascot/eye_design.py <figure_without_eyes.png> <out eyes.json> [--ref <figure_with_eyes.png>] [--preview <png>]

The eyes are designed here by hand as cubic Bezier curves in the 1024 px space of rig_ember.py, traced over the
figure's original drawing (with --ref the preview shows the original next to the result). The renderer
(ember_live_prototype_template.html) reads eyes.json and draws them live: the opening between an upper and a lower
curve, a slit pupil and glint cut to the opening, a thick upper lid stroke and a thin lower one that follow the lids,
and a brow above. Blinking moves the upper curve towards the lower one; above it there is the figure's own skin
(the figure has no eyes drawn), so the animation has no seams.

To restyle the eyes edit DESIGN below and run the script again; --preview renders the rest pose and a few
expressions so they can be checked before anything goes into the renderer.
"""
import json
import sys

import numpy as np
from PIL import Image, ImageDraw

W = 1024
SS = 4            # supersampling of the preview

# ---------------------------------------------------------------- the design (1024 px space)
# top / bot: Bezier curves from the outer to the inner corner (y as a function of x along the eye).
# lid: thickness of the upper lid stroke along the eye (outer, middle, inner) and how far it runs on past the outer
# corner (wing). low: thickness of the lower lid line (outer, middle, inner). brow: a centre line (Bezier) and its
# thickness (start, peak, end). pupil: centre, half width, half height, slant (dx per dy). glint: centre and radii.
DESIGN = {
    'eyes': [
        {   # Ember's right eye = on the viewer's left; the inner corner (towards the nose) is on the right
            'top': [[400, 393], [394, 345], [436, 334], [483, 393]],
            'bot': [[400, 393], [404, 414], [463, 422], [483, 391]],
            'lid': [5.5, 11, 21],         # outer, middle, inner: the angry wedge towards the nose
            'tail': [[401, 395], [392, 398], [384, 397]],   # the lower lid runs on past the outer corner
            'low': [3.2, 3.6, 1.6],
            'pupil': [447, 385, 8.5, 25, 0.16],
            'glint': [436, 366, 6, 5],
            'brow': {'line': [[397, 347], [414, 333], [455, 333], [485, 353]], 'hook': [[485, 353], [493, 358], [495, 347]],
                     'thick': [3, 8, 1.8]},
        },
        {   # Ember's left eye = on the viewer's right; the inner corner is on the left
            'top': [[626, 391], [633, 343], [592, 331], [545, 395]],
            'bot': [[626, 391], [621, 414], [563, 422], [545, 393]],
            'lid': [5.5, 11, 21],
            'tail': [[625, 393], [634, 396], [642, 394]],
            'low': [3.2, 3.6, 1.6],
            'pupil': [581, 384, 9, 26, -0.14],
            'glint': [567, 374, 5.5, 6.2],
            'brow': {'line': [[631, 346], [614, 331], [572, 331], [541, 355]], 'hook': [[541, 355], [533, 360], [530, 349]],
                     'thick': [3, 8, 1.8]},
        },
    ],
    'colors': {
        'line': [0.20, 0.02, 0.08],          # lid strokes and brows: the drawing's outline maroon
        'pupil': [0.17, 0.01, 0.07], 'pupil_rim': [0.30, 0.04, 0.10],
        'yellow_hi': [1.00, 0.88, 0.40], 'yellow': [0.99, 0.78, 0.25], 'yellow_lo': [0.93, 0.56, 0.14],
        'shade': [0.80, 0.36, 0.12],         # under the upper lid
        'glint': [1.00, 0.99, 0.97],
    },
}



# 2026-11-17 Jakob, round 2: the big round eyes of the first design were right ("die Glubschaugen haben gepasst"), only
# the lid strokes were far too thick; the narrow mean eyes of round 1 were wrong. So every variant keeps the big eyes
# traced from Embernew.png (DESIGN above) and changes only lids, pupils and brows. Three stay close to the original,
# two are our own takes. All are switchable in the preview.
COLORS = DESIGN['colors']
VARIANTS = {
    # round 3 (Jakob: "Teuflisch am besten, nur die Pupillen sehr dünn"): five takes on Teuflisch, thicker pupils,
    # different brows. pupil_w = width of the slit relative to the traced pupil (Teuflisch round 2 was 0.62).
    't1': {'label': 'T1 Klassisch', 'no_tail': True, 'lid': [3.0, 5.0, 7.5], 'pupil_w': 0.85, 'glint': 1.15,
           'brow_dy': 4, 'brow_tilt': 11},
    't2': {'label': 'T2 Keil', 'no_tail': True, 'lid': [3.0, 5.0, 7.5], 'pupil_w': 0.95, 'glint': 1.15,
           'brow_dy': 3, 'brow_tilt': 13, 'brow_shape': 'straight', 'brow_thick': [2.5, 7, 8]},
    't3': {'label': 'T3 Flamme', 'no_tail': True, 'lid': [3.0, 5.0, 7.5], 'pupil_w': 0.8, 'pupil': 1.08, 'glint': 1.2,
           'brow_dy': 2, 'brow_tilt': 9, 'brow_shape': 'flame', 'brow_thick': [2, 8, 9]},
    't4': {'label': 'T4 Hoch', 'no_tail': True, 'lid': [3.0, 5.0, 7.5], 'pupil_w': 1.05, 'glint': 1.15,
           'brow_dy': -3, 'brow_tilt': 15, 'brow_shape': 'straight', 'brow_thick': [2, 6, 5]},
    't5': {'label': 'T5 Dick', 'no_tail': True, 'lid': [3.2, 5.4, 8.0], 'pupil_w': 0.9, 'glint': 1.1,
           'brow_dy': 5, 'brow_tilt': 10, 'brow_thick': [3.5, 10, 6]},
}


def _rot(pts, c, deg):
    a = np.radians(deg)
    R = np.array([[np.cos(a), -np.sin(a)], [np.sin(a), np.cos(a)]])
    return ((np.asarray(pts, float) - c) @ R.T + c).tolist()


def variant_design(name):
    v = VARIANTS[name]
    eyes = []
    for idx, e0 in enumerate(DESIGN['eyes']):
        e = json.loads(json.dumps(e0))
        inner_right = e['top'][0][0] < e['top'][-1][0]          # viewer-left eye: inner corner on the right
        e['lid'] = v['lid']
        if v.get('no_tail'):
            e['tail'] = None
        c = np.mean(np.array(e['top'] + e['bot'], float), axis=0)
        k = v.get('scale', 1.0)
        if k != 1.0:                                              # bigger eyes, about their own centre
            for key in ('top', 'bot'):
                e[key] = (c + (np.array(e[key], float) - c) * k).tolist()
            if e.get('tail'):
                e['tail'] = (c + (np.array(e['tail'], float) - c) * k).tolist()
            e['pupil'][0:2] = (c + (np.array(e['pupil'][0:2]) - c) * k).tolist()
            e['glint'][0:2] = (c + (np.array(e['glint'][0:2]) - c) * k).tolist()
        if v.get('inner_drop'):                                   # the lid falls more steeply towards the nose
            e['top'][2][1] += v['inner_drop']
        pk = v.get('pupil', 1.0)
        e['pupil'][2] *= pk * v.get('pupil_w', 1.0); e['pupil'][3] *= pk
        gk = v.get('glint', 1.0)
        e['glint'][2] *= gk; e['glint'][3] *= gk
        # brows: up / down and tilt (positive = the inner end goes down); "other" = the viewer-right brow only
        dy = v.get('brow_dy', 0) + (v.get('brow_other_dy', 0) if idx == 1 else 0)
        tilt = v.get('brow_tilt', 0) + (v.get('brow_other_tilt', 0) if idx == 1 else 0)
        b = e['brow']
        L = np.array(b['line'], float)
        if v.get('brow_shape') == 'straight':          # a straight wedge from end to end
            L[1] = L[0] + (L[3] - L[0]) / 3; L[2] = L[0] + 2 * (L[3] - L[0]) / 3
        elif v.get('brow_shape') == 'flame':           # rises at the outer end like a flame tip, sharp hook inside
            L[0] = L[0] + [0, -9]; L[1] = L[1] + [0, -6]
        b['line'] = L.tolist()
        if v.get('brow_thick'):
            b['thick'] = v['brow_thick']
        bc = np.mean(np.array(b['line'], float), axis=0)
        sgn = 1 if inner_right else -1
        for key in ('line', 'hook'):
            b[key] = (np.array(_rot(b[key], bc, tilt * sgn)) + [0, dy]).tolist()
        eyes.append(e)
    return {'eyes': eyes, 'colors': COLORS}


def bez(p, n=64):
    p = np.asarray(p, float)
    t = np.linspace(0, 1, n)[:, None]
    return (1 - t) ** 3 * p[0] + 3 * (1 - t) ** 2 * t * p[1] + 3 * (1 - t) * t ** 2 * p[2] + t ** 3 * p[3]


def y_of_x(curve, xs):
    """a Bezier that runs left-right (or right-left) as y(x) on the given xs"""
    o = np.argsort(curve[:, 0])
    return np.interp(xs, curve[o, 0], curve[o, 1])


def sample_eye(e, step=0.5):
    top, bot = bez(e['top'], 200), bez(e['bot'], 200)
    xa, xb = sorted([top[0, 0], top[-1, 0]])
    xs = np.arange(xa, xb + step / 2, step)
    yt, yb = y_of_x(top, xs), y_of_x(bot, xs)
    outer_left = e['top'][0][0] < e['top'][-1][0]
    u = (xs - xa) / (xb - xa)
    u = u if outer_left else 1 - u                        # 0 at the outer corner, 1 at the inner one
    lo, mi, hi = e['lid']
    th_up = np.where(u < 0.45, lo + (mi - lo) * np.sin(np.pi * u / 0.9), mi + (hi - mi) * np.sin(np.clip((u - 0.45) / 0.45, 0, 1) * np.pi / 2))
    th_up = th_up * np.clip((1 - u) * 9, 0, 1) ** 0.6                     # closes into the inner corner
    a, b, c = e['low']
    th_lo = np.where(u < 0.5, a + (b - a) * 2 * u, b + (c - b) * (2 * u - 1)) * np.clip((1 - u) * 8, 0, 1)
    return dict(x=xs.tolist(), top=yt.tolist(), bot=yb.tolist(), up=th_up.tolist(), low=th_lo.tolist())


def brow_poly(b, n=80):
    main = bez(b['line'], n)
    h = np.asarray(b['hook'], float)
    t = np.linspace(0, 1, 24)[1:, None]
    hook = (1 - t) ** 2 * h[0] + 2 * (1 - t) * t * h[1] + t ** 2 * h[2]
    line = np.vstack([main, hook])
    d = np.gradient(line, axis=0)
    nrm = np.stack([-d[:, 1], d[:, 0]], 1) / np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-6)
    s = np.linspace(0, 1, len(line))
    t0, tp, t1 = b['thick']
    th = np.where(s < 0.35, t0 + (tp - t0) * np.sin(s / 0.35 * np.pi / 2), t1 + (tp - t1) * np.cos((s - 0.35) / 0.65 * np.pi / 2))
    return line, line - nrm * th[:, None] / 2, line + nrm * th[:, None] / 2


def export(design):
    out = {'eyes': [], 'colors': design['colors']}
    for e in design['eyes']:
        s = sample_eye(e)
        line, up, lo = brow_poly(e['brow'])
        s.update(pupil=e['pupil'], glint=e['glint'], tail=e.get('tail'),
                 brow=dict(line=np.round(line, 2).tolist(), up=np.round(up, 2).tolist(), low=np.round(lo, 2).tolist(),
                           ),
                 cx=float(np.mean([p[0] for p in e['top']])))
        out['eyes'].append(s)
    return out


# ---------------------------------------------------------------- preview (PIL, the same geometry as the renderer)
def col(c, a=1.0):
    return tuple(int(round(v * 255)) for v in c) + (int(round(a * 255)),)


def render(base, ex, close=0.0, low=0.0, look=(0, 0), brow_dy=0.0, brow_tilt=0.0):
    img = base.convert('RGBA').resize((W * SS, W * SS), Image.LANCZOS)
    C = ex['colors']
    for e in ex['eyes']:
        xs, yt, yb = np.array(e['x']), np.array(e['top']), np.array(e['bot'])
        meet = yt + 0.72 * (yb - yt)
        ytop = yt + close * (meet - yt) + low * 0.1 * (yb - yt)
        ybot = np.maximum(ytop, yb - max(close, low) * (yb - meet))
        P = lambda pts: [(float(x) * SS, float(y) * SS) for x, y in pts]
        layer = Image.new('RGBA', img.size, (0, 0, 0, 0)); d = ImageDraw.Draw(layer)
        opening = list(zip(xs, ytop)) + list(zip(xs[::-1], ybot[::-1]))
        if close < 0.97:
            # yellow: three nested shapes for a soft radial shade, then the shade under the lid
            d.polygon(P(opening), fill=col(C['yellow_lo']))
            cx, cy = xs.mean(), (yt + yb).mean() / 2 + 3
            for k, c_ in ((0.82, C['yellow']), (0.55, C['yellow_hi'])):
                pts = [(cx + (x - cx) * k, cy + (y - cy) * k) for x, y in opening]
                d.polygon(P(pts), fill=col(c_))
            sh = list(zip(xs, ytop)) + list(zip(xs[::-1], (ytop + 0.22 * (ybot - ytop))[::-1]))
            d.polygon(P(sh), fill=col(C['shade'], 0.45))
            m = Image.new('L', img.size, 0); ImageDraw.Draw(m).polygon(P(opening), fill=255)
            px_, py_, hw, hh, sl = e['pupil']
            px_ += look[0] * 12; py_ += look[1] * 8
            v = np.linspace(-1, 1, 80)
            w = hw * (1 - v ** 2) ** 0.55
            pup = [(px_ + sl * vv * hh + ww, py_ + vv * hh) for vv, ww in zip(v, w)] + [(px_ + sl * vv * hh - ww, py_ + vv * hh) for vv, ww in zip(v[::-1], w[::-1])]
            pl = Image.new('RGBA', img.size, (0, 0, 0, 0)); pd = ImageDraw.Draw(pl)
            pd.polygon(P(pup), fill=col(C['pupil']))
            gx, gy, rx, ry = e['glint']
            pd.ellipse([(gx + look[0] * 4 - rx) * SS, (gy + look[1] * 2.5 - ry) * SS, (gx + look[0] * 4 + rx) * SS, (gy + look[1] * 2.5 + ry) * SS], fill=col(C['glint']))
            layer.alpha_composite(Image.composite(pl, Image.new('RGBA', img.size, (0, 0, 0, 0)), m))
        up, lo_ = np.array(e['up']), np.array(e['low'])
        d = ImageDraw.Draw(layer)
        upc = up * (1 - 0.5 * close)
        d.polygon(P(list(zip(xs, ytop + 1.2)) + list(zip(xs[::-1], (ytop - upc)[::-1]))), fill=col(C['line']))
        if e.get('tail'):
            tp = np.asarray(e['tail'], float); tt = np.linspace(0, 1, 20)[:, None]
            q = (1 - tt) ** 2 * tp[0] + 2 * (1 - tt) * tt * tp[1] + tt ** 2 * tp[2]
            w = 3.0 * (1 - tt[:, 0]) + 0.6
            d.polygon(P(list(zip(q[:, 0], q[:, 1] - w / 2)) + list(zip(q[::-1, 0], (q[:, 1] + w / 2)[::-1]))), fill=col(C['line']))
        d.polygon(P(list(zip(xs, ybot - 0.6)) + list(zip(xs[::-1], (ybot + lo_)[::-1]))), fill=col(C['line']))
        b = e['brow']
        bc = np.mean(b['line'], axis=0)

        def tf(pts):
            pts = np.array(pts, float) - bc
            a = np.radians(brow_tilt) * (1 if e['cx'] < 512 else -1)
            R = np.array([[np.cos(a), -np.sin(a)], [np.sin(a), np.cos(a)]])
            return pts @ R.T + bc + [0, brow_dy]
        d.polygon(P(list(map(tuple, tf(b['up']))) + list(map(tuple, tf(b['low'])[::-1]))), fill=col(C['line']))
        img.alpha_composite(layer)
    return img.resize((W, W), Image.LANCZOS)


def main(src, out, ref=None, preview=None, default='t4'):
    allv = {name: export(variant_design(name)) for name in VARIANTS}
    for name in allv:
        allv[name]['label'] = VARIANTS[name]['label']
    json.dump({'default': default, 'variants': allv}, open(out, 'w'), separators=(',', ':'))
    print('eyes written:', out, list(allv))
    if preview:
        base = Image.open(src).convert('RGB').resize((W, W), Image.LANCZOS)
        box = (370, 300, 660, 440)
        tiles = []
        if ref:
            tiles.append(Image.open(ref).convert('RGB').resize((W, W), Image.LANCZOS).crop(box))
        for name, ex in allv.items():
            for kw in ({}, {'close': 1.0}):
                tiles.append(render(base, ex, **kw).convert('RGB').crop(box))
        w_, h_ = box[2] - box[0], box[3] - box[1]
        sheet = Image.new('RGB', (w_ * 2 * 2, h_ * 2 * ((len(tiles) + 1) // 2)), 'white')
        for i, t in enumerate(tiles):
            sheet.paste(t.resize((w_ * 2, h_ * 2), Image.LANCZOS), ((i % 2) * w_ * 2, (i // 2) * h_ * 2))
        sheet.save(preview)
        print('preview:', preview)


if __name__ == '__main__':
    a = sys.argv[1:]
    opt = lambda k: a[a.index(k) + 1] if k in a else None
    main(a[0], a[1], opt('--ref'), opt('--preview'))
