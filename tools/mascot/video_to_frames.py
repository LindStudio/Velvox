"""Turns Jakob's animation videos of a mascot into the plugin's frames.

Video names (any of .mp4 .mov .webm) in the source folder:
    <state>.mp4        loop of the mood          -> <state>_001.png ...
    <state>_in.mp4     neutral pose -> mood       -> <state>_in_001.png ...
    <state>_out.mp4    mood -> neutral pose       -> <state>_out_001.png ...
States (EMBER): idle singing snapping fire overheated sleeping listening proud

Every frame: dark background removed (same method as cut_ember.py), scaled to --size, reduced to a
256-colour palette with alpha (about 4x smaller; the plugin would otherwise grow by hundreds of MB).
Existing frames of a state/clip that gets a new video are replaced; the others stay.

Usage: python tools/mascot/video_to_frames.py <video folder> <frames folder> [--fps 12] [--size 600] [--full-colour]
       [--transition-seconds 0.5]   (in / out clips are squeezed to this length)
Needs ffmpeg on PATH.
"""
import argparse, os, re, subprocess, sys, tempfile
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cut_ember import cut_image  # noqa: E402

STATES = ['idle', 'singing', 'snapping', 'fire', 'overheated', 'sleeping', 'listening', 'proud']
NAME = re.compile(r'^(%s)(_in|_out)?\.(mp4|mov|webm|mkv)$' % '|'.join(STATES), re.IGNORECASE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--fps', type=float, default=12.0)
    ap.add_argument('--size', type=int, default=600)
    ap.add_argument('--full-colour', action='store_true')
    ap.add_argument('--transition-seconds', type=float, default=0.5)
    a = ap.parse_args()
    os.makedirs(a.dst, exist_ok=True)

    videos = [f for f in sorted(os.listdir(a.src)) if NAME.match(f)]
    if not videos:
        print('no videos named like fire.mp4 / fire_in.mp4 / fire_out.mp4 in', a.src)
        return 1
    total = 0
    for v in videos:
        m = NAME.match(v)
        prefix = m.group(1).lower() + (m.group(2) or '').lower()
        for old in os.listdir(a.dst):
            if re.match(r'^%s_\d{3}\.png$' % re.escape(prefix), old):
                os.remove(os.path.join(a.dst, old))
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(['ffmpeg', '-v', 'error', '-i', os.path.join(a.src, v), '-vf', 'fps=%g' % a.fps,
                            os.path.join(tmp, 'f_%04d.png')], check=True)
            frames = sorted(os.listdir(tmp))
            # In / out clips are transitions: squeezed to --transition-seconds (video tools make 5 s clips at least)
            if m.group(2):
                want = max(2, round(a.transition_seconds * a.fps))
                if len(frames) > want:
                    step = (len(frames) - 1) / (want - 1)
                    frames = [frames[round(i * step)] for i in range(want)]
            for i, f in enumerate(frames, 1):
                img = Image.open(os.path.join(tmp, f)).convert('RGB')
                w, h = img.size
                side = max(w, h)   # pad to a square so every clip keeps the same scale
                square = Image.new('RGB', (side, side), img.getpixel((2, 2)))
                square.paste(img, ((side - w) // 2, (side - h) // 2))
                out = cut_image(square, a.size)
                if not a.full_colour:
                    out = out.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
                path = os.path.join(a.dst, '%s_%03d.png' % (prefix, i))
                out.save(path, optimize=True)
                total += os.path.getsize(path)
        print('%-16s %3d frames' % (prefix, len(frames)))
    print('done, %.1f MB of new frames' % (total / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main())
