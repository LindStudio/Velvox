"""Pack the rendered mascot frames into the plugin's clip files (Resources/Mascot/<clip>.vms).

    python tools/mascot/pack_mascot.py <render dir> Resources/Mascot [--height 240] [--quality 85]

<render dir> is the output of
    render_mascot.py --mode all --rate 30 --mouths zu,halb,offen
(<anim>/zu|halb|offen/####.png). Every frame is cropped to one common box (the union of all clips,
so the figure never jumps between clips) and scaled to --height. The closed mouth is the full frame
(JPEG colour + PNG alpha: a quarter of a PNG, no colour banding on the gloss); half and open are stored
as small PNG patches where they differ from it. Loops longer than --max-loop frames are cut to the
most seamless window (closest last/first frame pair) of 3 - 5 s; the plugin cross-fades at the wrap.
Calm clips (CALM) are stored at 15 fps. Format: Source/UI/MascotClips.h.
"""

import argparse
import glob
import io
import os
import struct

import numpy as np
from PIL import Image, ImageFilter

# Clip order and names as in Source/UI/MascotBrain.h; animation per clip from the design note
CLIPS = [
    ("wakeUp", "Wake_Up_and_Look_Up", False),
    ("idle", "Short_Breathe_and_Look_Around", True),
    ("bad", "Stand_and_Chat", True),
    ("problem", "Stand_Talking_Angry", True),
    ("okay", "Arm_Circle_Shuffle", True),
    ("good", "All_Night_Dance", True),
    ("super", "Cardio_Dance", True),
    ("spin", "_360_Power_Spin_Jump", False),
    ("working", "Running", True),
    ("walk", "Walking", True),
    ("call", "Call_Gesture", True),
]
FPS = 30
CALM = {"idle", "bad", "problem", "call"}   # little motion: 15 fps is enough
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
    p.add_argument("--height", type=int, default=240)
    p.add_argument("--quality", type=int, default=80, help="JPEG quality of the colour")
    p.add_argument("--max-loop", type=int, default=150, help="longest loop in frames (5 s)")
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
    for path in frames(args.src, "Short_Breathe_and_Look_Around", "zu")[::4]:
        b = load(path).getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
        ref = b if ref is None else (min(ref[0], b[0]), min(ref[1], b[1]), max(ref[2], b[2]), max(ref[3], b[3]))

    for name, anim, loops in CLIPS:
        zu = frames(args.src, anim, "zu")
        halb = frames(args.src, anim, "halb")
        offen = frames(args.src, anim, "offen")
        if not zu or len(zu) != len(halb) or len(zu) != len(offen):
            raise SystemExit(f"{anim}: missing frames")
        fps = FPS // 2 if name in CALM else FPS
        if fps != FPS:
            zu, halb, offen = zu[::2], halb[::2], offen[::2]
        start, length = loop_window(zu, args.max_loop * fps // FPS) if loops else (0, len(zu))
        zu, halb, offen = (f[start:start + length] for f in (zu, halb, offen))
        data = bytearray(b"VMS3")
        data += struct.pack("<hhhib", fps, size[0], size[1], len(zu), 1 if loops else 0)
        data += struct.pack("<hhhh", *ref)
        for z, h, o in zip(zu, halb, offen):
            base = load(z)
            colour, alpha = jpeg(base, args.quality), png(base.getchannel("A"))
            data += struct.pack("<i", len(colour)) + colour + struct.pack("<i", len(alpha)) + alpha
            for v in (h, o):
                pt = patch(base, load(v))
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
        print(f"{name:8s} {anim:32s} {len(zu):4d} frames (from {start}) {len(data) / 1e6:6.2f} MB")
    print(f"frame {size[0]}x{size[1]}, standing figure {ref}, total {total / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
