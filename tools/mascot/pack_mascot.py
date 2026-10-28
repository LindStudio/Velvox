"""Pack the rendered mascot frames into the plugin's clip files (Resources/Mascot/<clip>.vms).

    python tools/mascot/pack_mascot.py <render dir> Resources/Mascot [--height 320] [--quality 86]

<render dir> is the output of
    render_mascot.py --mode all --rate 30
(<anim>/zu/####.png, plus halb/ and offen/ for the clips he talks in). Every frame is cropped to one
common box (the union of all clips, so the figure never jumps between clips) and scaled to --height.
The closed mouth is the full frame (JPEG colour + PNG alpha: a quarter of a PNG, no colour banding on
the gloss); half and open are stored as small PNG patches where they differ from it. Loops longer than
their limit are cut to the most seamless window (closest last/first frame pair) of 3/5 of it up to it;
the plugin cross-fades at the wrap. Calm clips (CALM) are stored at 15 fps. Format: Source/UI/MascotClips.h.
"""

import argparse
import glob
import io
import os
import struct

import numpy as np
from PIL import Image, ImageFilter

# Clip order and names as in Source/UI/MascotBrain.h; animation per clip from Jakob (2026-10-28)
CLIPS = [
    ("fall", "Fall4", False),
    ("wave", "Big_Wave_Hello", False),
    ("bow", "Gentlemans_Bow", False),
    ("idle", "Listening_Gesture", True),
    ("relax", "Relax_arms_then_strike_battle_pose", False),
    ("bad", "Depressed_Full_Turn_Left", True),
    ("hipHop", "Hip_Hop_Dance", True),
    ("breakdance", "Breakdance_1990", True),
]
FPS = 30
CALM = set()   # clips stored at 15 fps; none (Jakob 2026-10-28: 15 fps idle looked choppy)
# Longest loop in frames at 30 fps
MAX_LOOP = {}
# Frames at the end of a cut loop that are blended into the frames before its start, so the wrap is seamless
LOOP_BLEND = 12
# One-shots that stand still at the end: only their first n frames (30 fps)
CUT = {"wave": 235, "bow": 380}
STANDING = "Listening_Gesture"   # the clip that defines where the standing figure is
PAD = 4


def frames(src, anim, mouth):
    return sorted(glob.glob(os.path.join(src, anim, mouth, "*.png")))


def union_box(src):
    box = None
    for _, anim, _ in CLIPS:
        for path in frames(src, anim, "zu")[::3] + frames(src, anim, "offen")[::3]:
            b = Image.open(path).getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
            if b:
                box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    size = Image.open(frames(src, CLIPS[0][1], "zu")[0]).size
    return (max(0, box[0] - PAD), max(0, box[1] - PAD), min(size[0], box[2] + PAD), min(size[1], box[3] + PAD))


def png(img):
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def bleed(img):
    """Colour under transparent pixels = the nearby figure colour, so JPEG blocks at the edge don't pull in black."""
    a = np.asarray(img, dtype=np.float32) / 255.0
    alpha = a[..., 3:4]
    premul = np.concatenate([a[..., :3] * alpha, alpha], axis=2)
    fill = np.zeros_like(a[..., :3])
    done = np.zeros_like(alpha, dtype=bool)
    for radius in (2, 6, 16, 40):
        blurred = np.asarray(Image.fromarray((premul * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(radius)),
                             dtype=np.float32) / 255.0
        take = ~done & (blurred[..., 3:4] > 1e-3)
        fill = np.where(take, blurred[..., :3] / np.maximum(blurred[..., 3:4], 1e-6), fill)
        done |= take
    rgb = np.where(alpha > 0, a[..., :3], fill)
    return Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8))


def jpeg(img, quality):
    buf = io.BytesIO()
    bleed(img).save(buf, "JPEG", quality=quality, optimize=True)
    return buf.getvalue()


def thumb(path):
    return np.asarray(Image.open(path).convert("RGBA").resize((64, 64)), dtype=np.float32)


def loop_window(paths, max_len):
    """(start, length) of the most seamless loop between 3 s and max_len frames; the whole clip if it is short."""
    n = len(paths)
    if n <= max_len:
        return 0, n
    t = [thumb(p) for p in paths]
    best = (np.inf, 0, n)
    for length in range(max_len * 3 // 5, max_len + 1, 2):
        for start in range(0, n - length, 2):
            d = np.abs(t[start] - t[start + length]).mean()
            if d < best[0]:
                best = (d, start, length)
    return best[1], best[2]


def mix(a, b, w):
    """a -> b by w (0..1), premultiplied so the edges don't darken."""
    x = np.asarray(a, dtype=np.float32) / 255.0
    y = np.asarray(b, dtype=np.float32) / 255.0
    alpha = x[..., 3:4] * (1 - w) + y[..., 3:4] * w
    rgb = (x[..., :3] * x[..., 3:4] * (1 - w) + y[..., :3] * y[..., 3:4] * w) / np.maximum(alpha, 1e-6)
    return Image.fromarray((np.clip(np.concatenate([rgb, alpha], axis=2), 0, 1) * 255).astype(np.uint8), "RGBA")


def patch(base, variant):
    a = np.asarray(base, dtype=np.int16)
    b = np.asarray(variant, dtype=np.int16)
    diff = np.abs(a - b).max(axis=2) > 6
    ys, xs = np.nonzero(diff)
    if len(xs) == 0:
        return None
    x0, y0 = max(0, xs.min() - 2), max(0, ys.min() - 2)
    x1, y1 = min(base.width, xs.max() + 3), min(base.height, ys.max() + 3)
    return x0, y0, variant.crop((x0, y0, x1, y1))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("src")
    p.add_argument("out")
    # 320 px for a figure about 112 units tall: sharp up to ~2.8x (window size x Windows scaling);
    # 240 looked pixelated on Jakob's screen (2026-10-28)
    p.add_argument("--height", type=int, default=320)
    p.add_argument("--quality", type=int, default=86, help="JPEG quality of the colour")
    p.add_argument("--max-loop", type=int, default=240, help="longest loop in frames (8 s), unless MAX_LOOP says otherwise")
    args = p.parse_args()

    box = union_box(args.src)
    scale = args.height / (box[3] - box[1])
    size = (round((box[2] - box[0]) * scale), args.height)
    os.makedirs(args.out, exist_ok=True)
    total = 0

    def load(path):
        return Image.open(path).convert("RGBA").crop(box).resize(size, Image.LANCZOS)

    # Where the standing figure is (idle clip): the plugin scales him so this box fills his place;
    # wider poses (lying, arms out, the spin jump) may reach beyond it
    ref = None
    for path in frames(args.src, STANDING, "zu")[::4]:
        b = load(path).getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
        ref = b if ref is None else (min(ref[0], b[0]), min(ref[1], b[1]), max(ref[2], b[2]), max(ref[3], b[3]))

    for name, anim, loops in CLIPS:
        zu = frames(args.src, anim, "zu")
        # Mouth variants only where he talks; elsewhere every frame keeps the closed face
        halb = frames(args.src, anim, "halb") or [None] * len(zu)
        offen = frames(args.src, anim, "offen") or [None] * len(zu)
        if not zu or len(zu) != len(halb) or len(zu) != len(offen):
            raise SystemExit(f"{anim}: missing frames")
        if name in CUT:
            zu, halb, offen = zu[:CUT[name]], halb[:CUT[name]], offen[:CUT[name]]
        fps = FPS // 2 if name in CALM else FPS
        if fps != FPS:
            zu, halb, offen = zu[::2], halb[::2], offen[::2]
        limit = MAX_LOOP.get(name, args.max_loop) * fps // FPS
        src = list(zip(zu, halb, offen))
        # A cut loop: its last LOOP_BLEND frames dissolve into the frames just before its start, so the
        # last frame leads straight into the first (the plugin no longer fades over a frozen frame)
        blend_from = {}
        if loops and len(src) > limit:
            start, length = loop_window(zu[LOOP_BLEND:], limit)
            start += LOOP_BLEND
            for i in range(LOOP_BLEND):
                blend_from[length - LOOP_BLEND + i] = (start - LOOP_BLEND + i, (i + 1) / (LOOP_BLEND + 1))
        else:
            start, length = 0, len(src)
        window = src[start:start + length]
        data = bytearray(b"VMS3")
        data += struct.pack("<hhhib", fps, size[0], size[1], len(window), 1 if loops else 0)
        data += struct.pack("<hhhh", *ref)
        for idx, paths in enumerate(window):
            base, h, o = (load(p) if p else None for p in paths)
            if idx in blend_from:
                j, w = blend_from[idx]
                other = [load(p) if p else None for p in src[j]]
                base, h, o = (mix(a, b, w) if a is not None else None for a, b in zip((base, h, o), other))
            colour, alpha = jpeg(base, args.quality), png(base.getchannel("A"))
            data += struct.pack("<i", len(colour)) + colour + struct.pack("<i", len(alpha)) + alpha
            for v in (h, o):
                pt = patch(base, v) if v is not None else None
                if pt is None:
                    data += struct.pack("<hhi", 0, 0, 0)
                else:
                    x, y, img = pt
                    blob = png(img)
                    data += struct.pack("<hhi", x, y, len(blob)) + blob
        path = os.path.join(args.out, name + ".vms")
        with open(path, "wb") as f:
            f.write(data)
        total += len(data)
        print(f"{name:8s} {anim:32s} {len(window):4d} frames (from {start}) {len(data) / 1e6:6.2f} MB")
    print(f"frame {size[0]}x{size[1]}, standing figure {ref}, total {total / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
