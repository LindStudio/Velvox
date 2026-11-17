"""Ember as a live rig (2026-11-17, v2): cuts Jakob's still (Embernew.png) into parts that a renderer moves at run
time - head (with horns), ears, arms, body, tail, pupils - instead of baking warped frames.

    python tools/mascot/rig_ember.py <Embernew.png> <out dir>

v2 (Jakob: "Texturfehler bei Schultern, Ohren, Hörnern, Schwanz und Füßen"):
- Every pixel has exactly one owner: the fill areas (skin, horn, tail, ear, arm) are labelled, and each outline pixel
  belongs to the nearest fill. No part carries pieces of another (the tail no longer takes the leg's outline).
- The soft edge takes the colour of the outline next to it. Un-mixing it from the white background made light
  fringes that glowed on the dark stage.
- Ears, arms and tail are meshes with weights: their base stays where it is (weight 0) and only the free end turns
  (weight 1), so the joint bends instead of opening a gap. Arms reach up into the shoulder, which they draw over the
  body (the body keeps its own copy, identical where the weight is 0).

Writes <part>.png (cropped, straight alpha) and rig.json: offset of every part in the 1024 px picture, joints, meshes
(grid step, vertex weights, which quads hold pixels), eye ellipses, skin colours, the smile line.
A check picture _rest_check.png puts the parts back together at rest next to the original.
"""
import json
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, os.path.dirname(__file__))
import puppet_ember as pe  # noqa: E402

W = pe.W
STEP = 14          # mesh grid step at 1024 px


def smooth(x, a, b):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def inpaint(rgb, known, sigma=3.0):
    idx = ndimage.distance_transform_edt(~known, return_distances=False, return_indices=True)
    filled = rgb[idx[0], idx[1]]
    soft = np.stack([ndimage.gaussian_filter(filled[..., k], sigma) for k in range(3)], axis=2)
    return np.where(known[..., None], rgb, soft)


def comp(mask, point):
    lab, _ = ndimage.label(mask)
    k = lab[point[1], point[0]]
    assert k, f"no component at {point}"
    return lab == k


STRIP_OUTLINE = True


def main(src, out, no_eyes=False):
    os.makedirs(out, exist_ok=True)
    if no_eyes:
        # the figure is drawn without eyes and brows (emberNoEyes.png): the renderer draws them (eye_design.py)
        P = pe.Puppet.__new__(pe.Puppet)
        P.src = pe.cut_white(Image.open(src).convert('RGB'))
        P.py, P.px = np.mgrid[0:W, 0:W].astype(np.float32)
        P._regions()
        P.eyes = []
        P.line = np.array([0.10, 0.04, 0.06], np.float32)
        P._mouth()
    else:
        P = pe.Puppet(src)
    img = P.src.copy()
    rgb, al = img[..., :3].copy(), img[..., 3].copy()
    px, py = P.px, P.py
    seen = al > 0.003

    # ---- 1) the soft edge takes the colour of the solid picture next to it (no light fringe on dark backgrounds)
    core = al > 0.92
    idx = ndimage.distance_transform_edt(~core, return_distances=False, return_indices=True)
    rgb = np.where(core[..., None], rgb, rgb[idx[0], idx[1]])

    lum = rgb @ np.array([0.3, 0.59, 0.11], np.float32)
    solid = al > 0.5
    fill = solid & (lum > 0.26)
    fill_dark = solid & (lum > 0.17)

    # ---- 2) fill areas, labelled by part
    band = (py > 250) & (py < 455)
    labels = np.zeros((W, W), np.int16)
    HEAD, EAR_L, EAR_R, TAIL, ARM_L, ARM_R, BODY = 1, 2, 3, 4, 5, 6, 7
    # the chin has no outline in the drawing, only a shadow: the head ends on a curve through the neck notches
    # (412, 510) and (612, 510), lowest under the chin (512, 548)
    dxn = np.abs(px - 512) / 100.0
    chin = np.where(dxn <= 1, 548 - 38 * dxn ** 2, 512.0)   # outside the notches the drawn outline parts head and shoulder
    head_fill = ndimage.binary_fill_holes(comp(fill & (py < chin), (512, 300)))
    horns = ndimage.binary_fill_holes(comp(fill_dark & (py < 300), (400, 200))) | \
        ndimage.binary_fill_holes(comp(fill_dark & (py < 300), (630, 220)))
    ear_l = fill & band & (px < 357)
    ear_r = fill & band & (px > 667)
    head_fill &= ~(band & ((px < 357) | (px > 667)))   # incl. the ears' inner lines (holes the fill closed)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    purple = (b > g + 0.04) & (r < 0.6) & solid & (px > 600) & (py > 620) & ((px > 714) | (py > 750)) & (py < 830)
    lab, n = ndimage.label(purple)
    tail_fill = ndimage.binary_fill_holes(lab == np.argmax(ndimage.sum(purple, lab, range(1, n + 1))) + 1)
    below = py >= pe.ARM_CUT
    f2 = solid & (lum >= 0.30) & below & (py < 800) & ~tail_fill
    lab, n = ndimage.label(f2)
    order = np.argsort(-ndimage.sum(f2, lab, range(1, n + 1)))
    arms = sorted((lab == i + 1 for i in order[1:3]), key=lambda m: np.nonzero(m)[1].mean())
    for m, k in ((head_fill | horns, HEAD), (ear_l, EAR_L), (ear_r, EAR_R), (tail_fill, TAIL), (arms[0], ARM_L), (arms[1], ARM_R)):
        labels[m & (labels == 0)] = k
    # skin at the arms' edge that is a little darker than the arm's fill still belongs to the arm
    for m, k in ((arms[0], ARM_L), (arms[1], ARM_R)):
        labels[fill & (labels == 0) & below & ndimage.binary_dilation(m, iterations=6)] = k
    labels[fill & (labels == 0) & band & (px < 368)] = EAR_L      # the ears' inner lines next to the face
    labels[fill & (labels == 0) & band & (px > 656)] = EAR_R
    labels[fill & (labels == 0) & (py < 470)] = HEAD
    labels[fill & (labels == 0)] = BODY
    # every other visible pixel (outline, features, soft edge) belongs to the nearest fill
    idx = ndimage.distance_transform_edt(labels == 0, return_distances=False, return_indices=True)
    owner = np.where(seen, labels[idx[0], idx[1]], 0)
    owner[(owner == HEAD) & (py >= chin)] = BODY                  # below the chin curve: neck and shoulders
    owner[(owner == TAIL) & (px < 652)] = BODY                    # the leg keeps its outline where the tail starts
    # (the ears keep their own inner lines; they reach 4 px under the head, so their base never shows a gap)

    def soft(m, s=0.6):
        return np.clip(ndimage.gaussian_filter(m.astype(np.float32), s), 0, 1)

    # The outer outline (Jakob: "die Outlines sind überall unterschiedlich dick und nicht gerundet"): where parts meet,
    # their outlines doubled up or ended abruptly. So the outer outline is taken off every part - the dark band
    # reached from the background, at most OUTLINE_MAX px deep - and the renderer draws ONE outline of even width
    # with round ends around the whole assembled figure, every frame.
    OUTLINE_MAX = 9
    dt_bg = ndimage.distance_transform_edt(seen)
    darkband = seen & (lum < 0.22) & (dt_bg <= OUTLINE_MAX)
    outer = ndimage.binary_propagation(darkband & (dt_bg <= 2), mask=darkband)
    keep_a = 1.0 - soft(outer, 0.7)

    # ---- 3) the face without pupils (pupils are own parts that slide inside the eye)
    face = rgb.copy()
    # (the neck bends as a mesh in the renderer now, so the chin's shading strokes stay as drawn)
    pupils = []
    # The eye from the drawing's colours (2026-11-17): its inside = the yellow, closed over the pupil slit; the pupil =
    # the dark slit inside it (cut off where it meets the lid stroke); the glint = the white spot. The old detection
    # (puppet_ember) only caught the glint and some crumbs, so the pupil never moved and a thin line slid instead.
    disk = lambda r: np.hypot(*np.mgrid[-r:r + 1, -r:r + 1]) <= r
    yellow_all = (rgb[..., 0] > 0.8) & (rgb[..., 1] > 0.66) & (rgb[..., 2] < 0.5) & solid   # not the orange skin highlights
    for e in P.eyes:
        y0, y1, x0, x1 = e['box']
        y0, y1, x0, x1 = y0 - 25, y1 + 25, x0 - 25, x1 + 25
        box = np.zeros_like(solid); box[y0:y1, x0:x1] = True
        Y = yellow_all & box
        inside0 = ndimage.binary_fill_holes(ndimage.binary_closing(Y, structure=disk(14))) & box
        lab_, n_ = ndimage.label(inside0)
        inside0 = lab_ == (np.argmax(ndimage.sum(inside0, lab_, range(1, n_ + 1))) + 1)
        glint = (rgb.min(axis=2) > 0.72) & inside0                # white, not bright yellow
        pdark = (lum < 0.4) & inside0 & ~glint
        lab_, n_ = ndimage.label(pdark)
        core_ = lab_ == (np.argmax(ndimage.sum(pdark, lab_, range(1, n_ + 1))) + 1)
        # Pupil, lid stroke and the shade under the lid are the same maroon in the drawing and run into each other
        # (Jakob: "ein Teil hängt bei den Augenbrauen fest"), so they are told apart by shape: from the eye's middle
        # row each column is followed up and down to the first dark pixel (the lids). Columns the pupil crosses give
        # no answer; the lids there come from a smooth curve through the other columns. Dark inside = pupil.
        dk = lum < 0.2
        cyi = int(round(e['c'][1]))
        cols = np.nonzero(inside0[cyi - 16:cyi + 17].any(axis=0))[0]    # columns of the eye, corners included
        xs = np.arange(cols.min(), cols.max() + 1)
        lend, lbot = [], []
        for x in xs:
            y = cyi
            while y > cyi - 80 and not dk[y, x]:            # up through the eye to the lid's lower edge
                y -= 1
            lend.append(y + 1 if y < cyi else np.nan)       # dark right at the middle row: a pupil column
            yb = cyi
            while yb < cyi + 80 and not dk[yb, x]:
                yb += 1
            lbot.append(yb - 1 if yb > cyi else np.nan)
        lend, lbot = np.array(lend, float), np.array(lbot, float)
        # a lid must sit next to the eye's yellow in that column, or the walk ran past the eye into the cheek
        ymin = np.array([np.nonzero(Y[:, x])[0].min() if Y[:, x].any() else np.nan for x in xs], float)
        ymax = np.array([np.nonzero(Y[:, x])[0].max() if Y[:, x].any() else np.nan for x in xs], float)
        lbot[~(lbot <= ymax + 8)] = np.nan
        lend[~(lend >= ymin - 30)] = np.nan
        lend[np.isnan(ymin)] = np.nan

        def smooth_lid(v):
            """a smooth lid line through the reliable columns; pupil columns and stray hits are left out"""
            ok = ~np.isnan(v)
            ok &= ~ndimage.binary_dilation(~ok, iterations=3)          # the pupil's soft rim columns too
            m = ok.copy()
            for _ in range(4):
                c = np.polyfit(xs[m], v[m], 3)
                r = np.abs(np.polyval(c, xs) - v)
                m = ok & (r <= max(1.5, np.nanquantile(r[m], 0.8)))
            return np.polyval(np.polyfit(xs[m], v[m], 3), xs)

        Ltop = np.clip(smooth_lid(lend), np.nanmin(lend) - 2, np.nanmax(lend))     # the cubic never overshoots
        Lbot = np.clip(smooth_lid(lbot), np.nanmin(lbot), np.nanmax(lbot))         # past what was measured
        inside_ = np.zeros_like(solid)
        for k_, x in enumerate(xs):
            a_, b_ = int(round(Ltop[k_])), int(round(Lbot[k_]))
            if b_ > a_:
                inside_[a_:b_ + 1, x] = True
        inside_ = ndimage.binary_opening(inside_, structure=disk(2))
        glint = (rgb.min(axis=2) > 0.72) & inside_
        pdark = dk & inside_
        lab_, n_ = ndimage.label(pdark)
        core_ = lab_ == (np.argmax(ndimage.sum(pdark, lab_, range(1, n_ + 1))) + 1)
        glint &= inside_
        pup = ndimage.binary_fill_holes(core_ | (glint & ndimage.binary_dilation(core_, iterations=2))) & inside_
        # the pupil's hidden top: the slit is an almond, so its upper half (cut by the lid in the drawing) is the
        # mirror of its lower half; it lies under the lid at rest and shows round when he looks down
        rows = np.nonzero(pup.any(axis=1))[0]
        widths = np.array([np.ptp(np.nonzero(pup[y])[0]) + 1 if pup[y].any() else 0 for y in rows])
        centers = np.array([np.nonzero(pup[y])[0].mean() for y in rows])
        wi = int(np.argmax(widths)); yw = rows[wi]
        cfit = np.polyfit(rows[wi:], centers[wi:], 1)             # the slit's slant from its lower half
        tip = np.zeros_like(solid)
        for d in range(1, len(rows) - wi):
            y = yw - d
            if y < 0:
                break
            hw = widths[wi + d] / 2.0
            cx_ = np.polyval(cfit, y)
            tip[y, int(np.floor(cx_ - hw)):int(np.ceil(cx_ + hw)) + 1] = True
        tip &= ~pup
        pcol = np.median(rgb[pup & ~glint & (lum < 0.3)], axis=0)
        # the face: under pupil and glint the yellow, painted from the yellow around (only below the lid line)
        rim = ndimage.binary_dilation(pup | glint, iterations=3) & inside_
        ybg_ = inpaint(rgb, Y & ~rim, 3.0)
        face = np.where(rim[..., None], ybg_, face)
        dark_a = np.clip((0.62 - lum) / 0.4, 0, 1)
        pa = np.maximum(np.where(ndimage.binary_dilation(pup, iterations=2) & inside_, dark_a, 0), soft(tip, 0.8))
        pa = np.where(glint, 1.0, pa)
        ga = soft(glint, 0.5)
        e['interior'] = soft(inside_, 0.6)          # the eye mask the renderer cuts pupil and glint to
        e['eye2'] = dict(xs=xs, Ltop=Ltop, Lbot=Lbot, inside=inside_)
        crop = (slice(y0 - 30, y1), slice(x0, x1))
        gidx = ndimage.distance_transform_edt(glint, return_distances=False, return_indices=True)
        prgb = rgb[gidx[0], gidx[1]]                  # under the glint the pupil takes its own colour
        prgb = np.where((tip & ~pup)[..., None], pcol, prgb)
        pupils.append(dict(rgba=np.concatenate([prgb[crop], pa[crop][..., None]], axis=2),
                           glint=np.concatenate([np.ones((y1 - y0 + 30, x1 - x0, 3)) * np.array([1.0, 0.98, 0.95]), ga[crop][..., None]], axis=2),
                           x=int(x0), y=int(y0 - 30), cx=float(e['c'][0])))

    # ---- 4) arms reach up into the shoulder: the body's own pixels on the arm's side, drawn over the body
    shoulders, arm_parts, arm_w = [], [], []
    for k, (m, side) in enumerate(((arms[0], -1), (arms[1], 1))):
        row = np.nonzero(m[pe.ARM_CUT + 6])[0]
        inner = float(row.max() if side < 0 else row.min())          # the arm's edge towards the torso
        c = (float(row.min() + row.max()) / 2, float(pe.ARM_CUT + 4))
        shoulders.append(c)
        top = pe.ARM_CUT - 80
        up = seen & (owner == BODY) & (py >= top) & (py < pe.ARM_CUT + 2) & ((px < inner + 2) if side < 0 else (px > inner - 2))
        own = owner == (ARM_L if side < 0 else ARM_R)
        arm_parts.append(own | up)
        # weight: 0 at the top of the shoulder and along the torso side above the cut, 1 down the arm
        d_in = (inner - px) if side < 0 else (px - inner)
        w = smooth(py, top, pe.ARM_CUT + 45) * np.where(py > pe.ARM_CUT + 45, 1.0, np.clip(d_in / 26.0, 0, 1) ** 0.7)
        arm_w.append(w)

    # ---- 5) body: its own pixels; under the chin a neck of skin so a tilted head shows no hole
    body_m = ((owner == BODY) & (py > 470)).astype(np.float32)   # nothing of the body sits higher than the neck
    neck = (((px - 512) / 125.0) ** 2 + ((py - 528) / 46.0) ** 2 <= 1.0) & (owner == HEAD) & (py > 470)
    skin_known = (owner == BODY) & fill & (py > 525) & (py < 640) & (np.abs(px - 512) < 150)
    # plain skin, no lines: the head covers it at rest; a turned head must not uncover a copy of the chin's
    # strokes (Jakob: "Linien beim Schlüsselbein")
    neck_rgb = inpaint(rgb, skin_known, 9.0)
    body_rgb = np.where(neck[..., None], neck_rgb, rgb)
    # no seams: a part that lies under another reaches 4 px under it with full cover (only the outer silhouette
    # keeps the drawing's soft edge)
    grow4 = lambda m: ndimage.binary_dilation(m, iterations=4) & seen
    body_a = al * np.maximum(grow4(body_m > 0), neck)
    # the torso's flank under each arm (Jakob: "bei Armen und Bauch ein paar dunkle Linien zu viel"): what lies under
    # an arm in the drawing is pieces of the arm's outline, which stayed on the body as dark spikes when the arm
    # swung out. Under the arm the flank is rebuilt: one clean outline along the torso's skin edge, nothing outside.
    line_col = np.median(rgb[(lum < 0.2) & seen & (py > 560) & (py < 760)], axis=0)
    torso_fill = (owner == BODY) & fill
    for m, side in ((arms[0], -1), (arms[1], 1)):
        ys_a, xs_a = np.nonzero(m)
        ya, yb = pe.ARM_CUT - 4, int(ys_a.max()) + 8
        xa, xb = int(xs_a.min()) - 18, int(xs_a.max()) + 18
        rows = np.arange(ya, yb + 1)
        edge = np.array([(np.nonzero(torso_fill[y, xa:xb])[0].min() + xa) if side < 0 else (np.nonzero(torso_fill[y, xa:xb])[0].max() + xa)
                         if torso_fill[y, xa:xb].any() else np.nan for y in rows], float)
        ok = ~np.isnan(edge)
        edge = np.interp(rows, rows[ok], edge[ok])
        edge = ndimage.median_filter(edge, 9)
        edge = ndimage.gaussian_filter1d(edge, 2.0)
        W_LINE = 4.5
        for y, e_ in zip(rows, edge):
            xx = px[y, xa:xb]
            d = (e_ - xx) if side < 0 else (xx - e_)             # > 0 outside the skin
            cover = np.clip(0.5 - d, 0, 1)                        # skin up to its edge
            linew = np.clip(d + 0.5, 0, 1) * (d < W_LINE + 1) * 0.0   # (outline comes from the renderer now)
            body_a[y, xa:xb] = np.where(d > 0, cover, body_a[y, xa:xb])
            near = (d > -14) & (d <= 0) & (lum[y, xa:xb] < 0.3)     # leftover arm strokes just inside the flank
            if near.any():
                okx = (d > -24) & (d <= -2) & (lum[y, xa:xb] >= 0.3)
                if okx.any():
                    body_rgb[y, xa:xb][near] = np.median(rgb[y, xa:xb][okx], axis=0)
            body_rgb[y, xa:xb] = np.where((linew > 0)[:, None], body_rgb[y, xa:xb] * (1 - linew[:, None]) + line_col * linew[:, None], body_rgb[y, xa:xb])

    parts, meshes = {}, {}

    def put(name, prgb, pa, z):
        pa = np.clip(pa, 0, 1) * (keep_a if STRIP_OUTLINE else 1.0)
        if STRIP_OUTLINE:   # crisp edge: faint fringe pixels made a pale halo once the outline was stamped round them
            pa = np.clip((pa - 0.3) / 0.4, 0, 1)
        ys, xs = np.nonzero(pa > 0.004)
        y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
        rgba = np.concatenate([prgb[y0:y1, x0:x1], pa[y0:y1, x0:x1, None]], axis=2)
        Image.fromarray((np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA').save(os.path.join(out, name + '.png'))
        parts[name] = dict(x=int(x0), y=int(y0), w=int(x1 - x0), h=int(y1 - y0), z=z)
        return pa

    def mesh(name, pa, weight, pivot):
        """Grid over the part: vertex weights (how much of the bone's turn a point takes) and the quads with pixels."""
        p = parts[name]
        x0, y0 = p['x'] - STEP, p['y'] - STEP
        cols = int(np.ceil((p['w'] + 2 * STEP) / STEP)) + 1
        rows = int(np.ceil((p['h'] + 2 * STEP) / STEP)) + 1
        ws, quads = [], []
        for j in range(rows):
            for i in range(cols):
                x, y = int(np.clip(x0 + i * STEP, 0, W - 1)), int(np.clip(y0 + j * STEP, 0, W - 1))
                ws.append(round(float(weight[y, x]), 3))
        for j in range(rows - 1):
            for i in range(cols - 1):
                ya, yb = max(0, y0 + j * STEP), min(W, y0 + (j + 1) * STEP + 1)
                xa, xb = max(0, x0 + i * STEP), min(W, x0 + (i + 1) * STEP + 1)
                quads.append(1 if (yb > ya and xb > xa and pa[ya:yb, xa:xb].max() > 0.004) else 0)
        meshes[name] = dict(x=x0, y=y0, step=STEP, cols=cols, rows=rows, w=ws, q=quads, pivot=[float(pivot[0]), float(pivot[1])])

    tail_a = put('tail', rgb, al * grow4(owner == TAIL), 0)
    d_tail = np.hypot(px - pe.TAIL_BASE[0], py - pe.TAIL_BASE[1])
    mesh('tail', tail_a, smooth(d_tail, 20, 170), pe.TAIL_BASE)
    for name, lab_, base_x, sgn, piv in (('ear_l', EAR_L, 362, -1, pe.EAR_L), ('ear_r', EAR_R, 662, 1, pe.EAR_R)):
        a = put(name, rgb, al * grow4(owner == lab_), 1)
        mesh(name, a, smooth(sgn * (px - base_x), 0, 55), piv)
    put('body', body_rgb, body_a, 2)
    for k, name in enumerate(('arm_l', 'arm_r')):
        a = put(name, rgb, al * soft(arm_parts[k]), 3)
        mesh(name, a, arm_w[k], shoulders[k])
    put('head', face, al * soft(owner == HEAD), 4)
    for p in pupils:
        name = 'pupil_l' if p['cx'] < 512 else 'pupil_r'
        for nm_, arr in ((name, p['rgba']), (name.replace('pupil', 'glint'), p['glint'])):
            Image.fromarray((np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA').save(os.path.join(out, nm_ + '.png'))
            parts[nm_] = dict(x=p['x'], y=p['y'], w=int(arr.shape[1]), h=int(arr.shape[0]), z=5 if nm_ == name else 6)

    # ---- 6) the eyes as an assembly of their own (Jakob 2026-11-17: "lösch die Augen und leg eigene drüber"): the
    # drawn eyes are painted out of the head with the skin around them; the renderer puts each eye back together from
    # the drawing's own pieces - yellow (pupil removed), pupil, glint, upper and lower lid stroke. Blinking moves the
    # real lid strokes column by column; above the moving lid there is skin, so nothing is squashed or guessed.
    eyes = sorted(P.eyes, key=lambda e: e['c'][0])
    eyes2, erase_all = [], np.zeros_like(solid)
    dk = lum < 0.2

    def save_piece(name, prgb, pa):
        ys_, xs_ = np.nonzero(pa > 0.004)
        a0, a1, b0, b1 = ys_.min(), ys_.max() + 1, xs_.min(), xs_.max() + 1
        arr = np.concatenate([prgb[a0:a1, b0:b1], pa[a0:a1, b0:b1, None]], axis=2)
        Image.fromarray((np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA').save(os.path.join(out, name + '.png'))
        return dict(x=int(b0), y=int(a0), w=int(b1 - b0), h=int(a1 - a0))

    for k, e in enumerate(eyes):
        g = e['eye2']
        xs, Ltop, Lbot, inside_e = g['xs'], g['Ltop'], g['Lbot'], g['inside']
        # only columns where the eye is open; at the very corners the stroke stays in the head as drawn
        xs_all, top_all, bot_all = xs, Ltop, Lbot
        open_ = (Lbot - Ltop) > 3
        xs, Ltop, Lbot = xs[open_], Ltop[open_], Lbot[open_]
        lupr, llowr = [], []
        for x, t_, b_ in zip(xs, Ltop, Lbot):
            y = int(round(t_)) - 1
            while y > t_ - 32 and lum[y, x] < 0.25:
                y -= 1
            lupr.append(y + 1)
            y = int(round(b_)) + 1
            while y < b_ + 14 and lum[y, x] < 0.25:
                y += 1
            llowr.append(y - 1)

        def robust(v, deg=3):
            v = np.asarray(v, float); m = np.ones(len(v), bool)
            for _ in range(4):
                c = np.polyfit(xs[m], v[m], deg); r = np.abs(np.polyval(c, xs) - v)
                m = r <= max(1.5, np.quantile(r[m], 0.8))
            return np.polyval(np.polyfit(xs[m], v[m], deg), xs)

        Lup = np.minimum(robust(lupr), Ltop - 2)                       # the lid stroke's top edge, smooth
        Llow = np.clip(robust(llowr), Lbot + 1, Lbot + 12)             # the lower stroke's bottom edge
        up = np.zeros_like(solid)
        low = np.zeros_like(solid)
        for x, a_, t_, b_, c_ in zip(xs, Lup, Ltop, Lbot, Llow):
            up[int(np.floor(a_ - 1.5)):int(np.ceil(t_ + 1)), x] = True
            low[int(np.floor(b_ - 1)):int(np.ceil(c_ + 1.5)), x] = True
        up &= seen
        low &= seen & ~up
        nm = 'lr'[k]
        yel = save_piece(f'eyeyel_{nm}', face, soft(inside_e, 0.5) * al)
        upb = save_piece(f'lidup_{nm}', rgb, soft(up, 0.5) * al)
        lowb = save_piece(f'lidlow_{nm}', rgb, soft(low, 0.5) * al)
        erase_all |= inside_e | up | low
        eyes2.append(dict(x0=int(xs_all[0]), top=[round(float(v), 2) for v in top_all], bot=[round(float(v), 2) for v in bot_all],
                          sx0=int(xs[0]), sn=int(len(xs)),
                          yel=yel, up=upb, low=lowb, cx=float((xs[0] + xs[-1]) / 2)))
    # paint the drawn eyes out of the head: a smooth weighted mean of the skin around, feathered
    er = erase_all
    known = (seen & ~ndimage.binary_dilation(erase_all, iterations=5) & (lum > 0.35)).astype(np.float32)
    # several scales: near the edge the skin right beside it, in the middle of the eye a wider mean (one scale alone
    # ran out of skin there and went black)
    skin_ = None
    for sig in (40, 20, 8):
        num = np.stack([ndimage.gaussian_filter(rgb[..., q] * known, sig) for q in range(3)], axis=2)
        den = ndimage.gaussian_filter(known, sig)[..., None]
        est = num / np.maximum(den, 1e-6)
        skin_ = est if skin_ is None else np.where(den > 0.08, est, skin_)
    skin_ = skin_.astype(np.float32)
    ea = soft(er, 0.5)[..., None]
    face_out = face * (1 - ea) + skin_ * ea
    put('head', face_out, al * soft(owner == HEAD), 4)
    x0, x1, top = P.smile
    rig = dict(
        size=W, parts=parts, meshes=meshes,
        joints=dict(neck=pe.NECK, tail=pe.TAIL_BASE, ear_l=pe.EAR_L, ear_r=pe.EAR_R,
                    shoulder_l=shoulders[0], shoulder_r=shoulders[1], feet=(512, pe.FEET_Y)),
        eyes=[dict(cx=float(e['c'][0]), cy=float(e['c'][1]), a=float(e['Ai']), b=float(e['Bi']),
                   skin=[float(v) for v in np.median(e['skin'][e['region']], axis=0)]) for e in eyes],
        eyes2=eyes2, line=[float(v) for v in P.line],
        smile=dict(x0=int(x0), pts=[round(float(v), 1) for v in top[::3]], step=3),
    )
    with open(os.path.join(out, 'rig.json'), 'w') as f:
        json.dump(rig, f, separators=(',', ':'))

    # ---- check: the parts back together at rest, on dark like the plugin, next to the original on dark
    dark = np.full((W, W, 3), 0.08, np.float32)
    canvas = dark.copy()
    for name, p in sorted(parts.items(), key=lambda kv: kv[1]['z']):
        part = np.asarray(Image.open(os.path.join(out, name + '.png'))).astype(np.float32) / 255
        reg = canvas[p['y']:p['y'] + p['h'], p['x']:p['x'] + p['w']]
        a = part[..., 3:4]
        reg[...] = part[..., :3] * a + reg * (1 - a)
    orig = rgb * al[..., None] + dark * (1 - al[..., None])
    diff = np.abs(canvas - orig).max(axis=2)
    print('rest vs original: max diff %.3f, pixels > 0.1: %d' % (diff.max(), int((diff > 0.1).sum())))
    both = np.concatenate([orig, canvas, np.repeat(np.clip(diff * 4, 0, 1)[..., None], 3, axis=2)], axis=1)
    Image.fromarray((both * 255).astype(np.uint8)).resize((1536, 512)).save(os.path.join(out, '_rest_check.png'))
    print('parts:', {k: (v['w'], v['h']) for k, v in parts.items()}, 'quads:', {k: sum(m['q']) for k, m in meshes.items()})


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], '--no-eyes' in sys.argv)
