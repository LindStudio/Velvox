"""Turns Jakob's animation videos of a mascot into the plugin's frames.

Two ways to say which video becomes which clip:

1. File names in the video folder (any of .mp4 .mov .webm .mkv):
       <state>.mp4        loop of the mood          -> <state>_001.png ...
       <state>_in.mp4     neutral pose -> mood       -> <state>_in_001.png ...
       <state>_out.mp4    mood -> neutral pose       -> <state>_out_001.png ...

2. A recipe (--recipe animation.json) that cuts clips out of any video:
       { "fire_in": { "src": "FireEmber.mp4", "end": 1.2 },
         "fire":    { "src": "FireEmber.mp4", "start": 1.2, "loop": "crossfade" },
         "singing": { "src": "SingingEmber.mp4", "start": 3.6, "loop": "pingpong" } }
   start / end in seconds (optional); loop: "crossfade" (the end blends into the start, seamless),
   "pingpong" (forwards then backwards, for short pieces) or "plain".

States (EMBER): idle singing snapping fire overheated sleeping listening proud

Every frame: dark background removed (same method as cut_ember.py), scaled to --size, reduced to a
256-colour palette with alpha (about 4x smaller). In / out clips are squeezed to --transition-seconds
(video tools make long clips). Existing frames of a clip that is rebuilt are replaced; the others stay.

Usage: python tools/mascot/video_to_frames.py <video folder> <frames folder> [--recipe file.json]
       [--fps 12] [--size 600] [--full-colour] [--transition-seconds 0.6] [--crossfade-seconds 0.5]
Needs ffmpeg on PATH.
"""
import argparse, json, os, re, subprocess, sys, tempfile
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cut_ember import cut_image  # noqa: E402

STATES = ['idle', 'singing', 'snapping', 'fire', 'overheated', 'sleeping', 'listening', 'proud']
CLIP = re.compile(r'^(%s)(_in|_out)?$' % '|'.join(STATES))
VIDEO = re.compile(r'^(.+)\.(mp4|mov|webm|mkv)$', re.IGNORECASE)


def extract(path, fps, start, end, tmp):
    cmd = ['ffmpeg', '-v', 'error']
    if start:
        cmd += ['-ss', str(start)]
    cmd += ['-i', path]
    if end is not None:
        cmd += ['-t', str(end - (start or 0.0))]
    cmd += ['-vf', 'fps=%g' % fps, os.path.join(tmp, 'f_%04d.png')]
    subprocess.run(cmd, check=True)
    return [Image.open(os.path.join(tmp, f)).convert('RGB') for f in sorted(os.listdir(tmp))]


def make_loop(frames, mode, blend):
    if mode == 'pingpong' and len(frames) > 2:
        return frames + frames[-2:0:-1]
    if mode == 'crossfade' and len(frames) > 2 * blend > 0:
        # the last `blend` frames fade into the first ones; the loop then ends where it starts
        body = frames[:len(frames) - blend]
        tail = frames[len(frames) - blend:]
        out = list(body)
        for i in range(blend):
            w = (i + 1) / (blend + 1)
            a = np.asarray(tail[i], dtype=np.float32)
            b = np.asarray(body[i], dtype=np.float32)
            out[i] = Image.fromarray((a * (1 - w) + b * w).astype(np.uint8))
        return out
    return frames


def write(frames, prefix, dst, size, full_colour):
    for old in os.listdir(dst):
        if re.match(r'^%s_\d{3}\.png$' % re.escape(prefix), old):
            os.remove(os.path.join(dst, old))
    total = 0
    for i, img in enumerate(frames, 1):
        w, h = img.size
        side = max(w, h)   # pad to a square so every clip keeps the same scale
        square = Image.new('RGB', (side, side), img.getpixel((2, 2)))
        square.paste(img, ((side - w) // 2, (side - h) // 2))
        out = cut_image(square, size)
        if not full_colour:
            out = out.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
        path = os.path.join(dst, '%s_%03d.png' % (prefix, i))
        out.save(path, optimize=True)
        total += os.path.getsize(path)
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--recipe')
    ap.add_argument('--fps', type=float, default=12.0)
    ap.add_argument('--size', type=int, default=600)
    ap.add_argument('--full-colour', action='store_true')
    ap.add_argument('--transition-seconds', type=float, default=0.6)
    ap.add_argument('--crossfade-seconds', type=float, default=0.5)
    a = ap.parse_args()
    os.makedirs(a.dst, exist_ok=True)

    jobs = []   # (clip name, video file, start, end, loop mode)
    if a.recipe:
        with open(a.recipe, encoding='utf-8') as f:
            recipe = json.load(f)
        for clip, spec in recipe.items():
            if clip.startswith('_'):
                continue   # comments
            if not CLIP.match(clip):
                print('unknown clip name', clip)
                return 1
            jobs.append((clip, spec['src'], spec.get('start'), spec.get('end'), spec.get('loop', 'crossfade')))
    else:
        for f in sorted(os.listdir(a.src)):
            m = VIDEO.match(f)
            if m and CLIP.match(m.group(1).lower()):
                jobs.append((m.group(1).lower(), f, None, None, 'crossfade'))
    if not jobs:
        print('nothing to do: name the videos fire.mp4 / fire_in.mp4 ... or pass --recipe')
        return 1

    blend = max(0, round(a.crossfade_seconds * a.fps))
    total = 0
    for clip, video, start, end, loop in jobs:
        with tempfile.TemporaryDirectory() as tmp:
            frames = extract(os.path.join(a.src, video), a.fps, start, end, tmp)
        transition = clip.endswith('_in') or clip.endswith('_out')
        if transition:
            want = max(2, round(a.transition_seconds * a.fps))
            if len(frames) > want:
                step = (len(frames) - 1) / (want - 1)
                frames = [frames[round(i * step)] for i in range(want)]
        else:
            frames = make_loop(frames, loop, blend)
        total += write(frames, clip, a.dst, a.size, a.full_colour)
        print('%-16s %3d frames  (%s)' % (clip, len(frames), 'transition' if transition else 'loop: ' + loop))
    print('done, %.1f MB of new frames' % (total / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main())
