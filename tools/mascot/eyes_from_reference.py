"""Measures a figure's drawn eyes and brows on a reference picture and writes them as smooth vector geometry.

    python tools/mascot/eyes_from_reference.py <reference_with_eyes.png> <out eyes.json> [--preview <png>]

Why (2026-11-17, Ember): cutting animated eyes out of a flat drawing never got clean - pupil, lid stroke and the shade
under the lid are the same maroon and run into each other. So the figure is drawn once WITHOUT eyes and brows
(emberNoEyes.png) and the renderer draws them itself as vector shapes. This script takes their look from the version
WITH eyes (Embernew.png, same pose and size): per eye the opening (upper and lower curve), the thickness of the upper
lid stroke and the lower lid stroke, the slit pupil, the glint, the brow, and the colours.

Everything is in the 1024 px space of rig_ember.py. Curves are cubic polynomials y(x) over [x0, x1]; the renderer
samples them. The JSON is small and can be edited by hand to restyle the eyes.
"""
import json
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

W = 1024


def robust_fit(x, y, deg=3, keep=0.8):
    x, y = np.asarray(x, float), np.asarray(y, float)
    ok = ~np.isnan(y)
    m = ok.copy()
    for _ in range(5):
        c = np.polyfit(x[m], y[m], deg)
        r = np.abs(np.polyval(c, x) - y)
        m = ok & (r <= max(1.2, np.nanquantile(r[m], keep)))
    return np.polyfit(x[m], y[m], deg)


def main(ref, out, preview=None):
    im = Image.open(ref).convert('RGB').resize((W, W), Image.LANCZOS)
    rgb = np.asarray(im).astype(np.float32) / 255
    lum = rgb @ np.array([0.3, 0.59, 0.11], np.float32)
    dk = lum < 0.2
    yellow = (rgb[..., 0] > 0.8) & (rgb[..., 1] > 0.66) & (rgb[..., 2] < 0.5)
    lab, n = ndimage.label(ndimage.binary_closing(yellow, structure=np.ones((15, 15))))
    sizes = ndimage.sum(yellow, lab, range(1, n + 1))
    eye_labels = [k + 1 for k in np.argsort(sizes)[::-1][:2]]
    eyes = []
    for k in eye_labels:
        region = ndimage.binary_fill_holes(lab == k)
        ys, xs_ = np.nonzero(region)
        cx, cy = (xs_.min() + xs_.max()) / 2, (ys.min() + ys.max()) / 2
        # the opening = the yellow with the pupil's gap closed; its top and bottom per column, fitted smooth. Columns
        # where the pupil touches a lid are left out of the fit (the pupil would pull the lid line down).
        opening0 = ndimage.binary_fill_holes(ndimage.binary_closing(yellow & ndimage.binary_dilation(region, iterations=4),
                                                                    structure=np.hypot(*np.mgrid[-14:15, -14:15]) <= 14))
        cols = np.nonzero(opening0.any(axis=0))[0]
        xs = np.arange(cols.min(), cols.max() + 1)
        top = np.array([np.nonzero(opening0[:, x])[0].min() for x in xs], float)
        bot = np.array([np.nonzero(opening0[:, x])[0].max() for x in xs], float)
        darkin = dk & opening0
        touch_t = np.array([darkin[int(t):int(t) + 5, x].any() for x, t in zip(xs, top)])
        touch_b = np.array([darkin[int(b) - 4:int(b) + 1, x].any() for x, b in zip(xs, bot)])
        c_top = robust_fit(xs[~touch_t], top[~touch_t])
        c_bot = robust_fit(xs[~touch_b], bot[~touch_b])
        sup, slo = [], []
        for x in xs:
            t = int(round(np.polyval(c_top, x))); y = t - 1
            while y > t - 32 and dk[y, x]:
                y -= 1
            sup.append(t - (y + 1))
            b = int(round(np.polyval(c_bot, x))); y = b + 1
            while y < b + 16 and dk[y, x]:
                y += 1
            slo.append((y - 1) - b)
        sup, slo = np.array(sup, float), np.array(slo, float)
        sel_u = ~touch_t & (sup > 0)
        c_thu = robust_fit(xs[sel_u], sup[sel_u], 2)
        sel_l = ~touch_b & (slo > 0)
        c_thl = robust_fit(xs[sel_l], slo[sel_l], 2) if sel_l.sum() > 5 else np.array([0.0, 0.0, 3.0])
        xx = np.arange(xs[0] - 10, xs[-1] + 11, 0.5)
        gap = np.polyval(c_bot, xx) - np.polyval(c_top, xx)
        inside_cols = xx[gap > 0]
        x0, x1 = float(max(inside_cols.min(), xs[0] - 2)), float(min(inside_cols.max(), xs[-1] + 2))
        # pupil: the dark shape inside the opening
        Yy, Xx = np.mgrid[0:W, 0:W]
        opening = (Xx >= x0) & (Xx <= x1) & (Yy > np.polyval(c_top, Xx) + 1) & (Yy < np.polyval(c_bot, Xx) - 1)
        pd = dk & opening
        plab, pn = ndimage.label(pd)
        core = plab == (np.argmax(ndimage.sum(pd, plab, range(1, pn + 1))) + 1)
        py_, px_ = np.nonzero(core)
        rows = np.unique(py_)
        widths = np.array([np.ptp(px_[py_ == r]) + 1 for r in rows])
        centers = np.array([px_[py_ == r].mean() for r in rows])
        wi = int(np.argmax(widths))
        slant = float(np.polyfit(rows, centers, 1)[0])            # dx per dy
        pupil = dict(cx=float(centers[wi]), cy=float(rows[wi]), hw=float(widths[wi] / 2), hh=float(max(rows[-1] - rows[wi], rows[wi] - rows[0])), slant=slant)
        # glint: the white spot
        gl = (rgb.min(axis=2) > 0.75) & ndimage.binary_dilation(opening, iterations=2)
        gy, gx = np.nonzero(gl)
        glint = dict(cx=float(gx.mean()), cy=float(gy.mean()), rx=float(np.ptp(gx) / 2 + 0.5), ry=float(np.ptp(gy) / 2 + 0.5))
        # brow: dark strokes above the lid stroke, within the eye's width and a bit beyond
        lid_top = np.polyval(c_top, Xx) - np.polyval(c_thu, Xx)
        band = (Xx > x0 - 20) & (Xx < x1 + 20) & (Yy < lid_top - 2) & (Yy > lid_top - 40)
        bd = dk & band
        blab, bn = ndimage.label(bd)
        brow = None
        if bn:
            bcore = blab == (np.argmax(ndimage.sum(bd, blab, range(1, bn + 1))) + 1)
            by_, bx_ = np.nonzero(bcore)
            bxs = np.arange(bx_.min(), bx_.max() + 1)
            bt = np.array([by_[bx_ == x].min() if (bx_ == x).any() else np.nan for x in bxs], float)
            bb = np.array([by_[bx_ == x].max() if (bx_ == x).any() else np.nan for x in bxs], float)
            brow = dict(x0=float(bxs[0]), x1=float(bxs[-1]), top=robust_fit(bxs, bt).tolist(), bot=robust_fit(bxs, bb).tolist())
        # colours of the drawing
        yel = rgb[yellow & opening]
        lum_y = yel @ np.array([0.3, 0.59, 0.11])
        colors = dict(yellow_hi=np.quantile(yel, 0.9, axis=0).tolist(), yellow=np.median(yel, axis=0).tolist(),
                      yellow_lo=yel[lum_y < np.quantile(lum_y, 0.15)].mean(axis=0).tolist(),
                      line=rgb[dk & band | (dk & opening)].mean(axis=0).tolist())
        eyes.append(dict(x0=x0, x1=x1, top=c_top.tolist(), bot=c_bot.tolist(), thick_up=c_thu.tolist(),
                         thick_low=c_thl.tolist(), pupil=pupil, glint=glint, brow=brow, colors=colors))
    eyes.sort(key=lambda e: e['x0'])
    json.dump(dict(size=W, eyes=eyes), open(out, 'w'), indent=1)
    print('eyes:', [(round(e['x0']), round(e['x1']), e['brow'] is not None) for e in eyes])
    if preview:
        from PIL import ImageDraw
        pv = im.copy(); d = ImageDraw.Draw(pv)
        for e in eyes:
            xx = np.arange(e['x0'], e['x1'] + 0.5, 1)
            for c, col in ((e['top'], (0, 255, 0)), (e['bot'], (0, 160, 255))):
                d.line(list(zip(xx, np.polyval(c, xx))), fill=col, width=1)
            d.line(list(zip(xx, np.polyval(e['top'], xx) - np.polyval(e['thick_up'], xx))), fill=(255, 0, 255), width=1)
            d.line(list(zip(xx, np.polyval(e['bot'], xx) + np.polyval(e['thick_low'], xx))), fill=(255, 255, 0), width=1)
            p = e['pupil']; d.ellipse([p['cx'] - p['hw'], p['cy'] - p['hh'], p['cx'] + p['hw'], p['cy'] + p['hh']], outline=(255, 0, 0))
            if e['brow']:
                bx = np.arange(e['brow']['x0'], e['brow']['x1'] + 1)
                d.line(list(zip(bx, np.polyval(e['brow']['top'], bx))), fill=(255, 128, 0))
                d.line(list(zip(bx, np.polyval(e['brow']['bot'], bx))), fill=(255, 128, 0))
        pv.crop((370, 280, 660, 440)).resize((870, 480), Image.NEAREST).save(preview)


if __name__ == '__main__':
    a = sys.argv[1:]
    main(a[0], a[1], a[a.index('--preview') + 1] if '--preview' in a else None)
