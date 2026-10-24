"""Blind A/B listening kit from two folders of renders (e.g. Headliner 0.3 vs 0.4).

Usage:
    python make_ab_kit.py <dirA> <dirB> <outDir> [--suffix _velvox.wav] [--seed 20261024]

Pairs files with the same name in both folders, matches both to the same integrated
loudness (EBU R128 via ffmpeg, the quieter of the two, peaks kept below -1 dBTP by
turning both down further if needed), and writes them in random order as
NN_A.wav / NN_B.wav. The answer goes to ab_key.csv - open it only after listening.
Needs ffmpeg on PATH.
"""
import argparse, csv, random, re, subprocess
from pathlib import Path


def measure(path):
    out = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", str(path), "-af", "ebur128=peak=true", "-f", "null", "-"],
                         capture_output=True, text=True, encoding="utf-8", errors="replace").stderr
    summary = out[out.rfind("Summary:"):]
    lufs = float(re.search(r"I:\s+(-?[\d.]+) LUFS", summary).group(1))
    peak = float(re.search(r"Peak:\s+(-?[\d.]+|-inf) dBFS", summary).group(1))
    return lufs, peak


def render(src, dst, gain_db):
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(src), "-af", f"volume={gain_db:.2f}dB",
                    "-c:a", "pcm_s24le", str(dst)], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir_a"); ap.add_argument("dir_b"); ap.add_argument("out")
    ap.add_argument("--suffix", default="_velvox.wav")
    ap.add_argument("--seed", type=int, default=20261024)
    args = ap.parse_args()

    dir_a, dir_b, out = Path(args.dir_a), Path(args.dir_b), Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    names = sorted(p.name for p in dir_a.glob("*" + args.suffix) if (dir_b / p.name).exists())
    if not names:
        raise SystemExit("no matching files")

    with open(out / "ab_key.csv", "w", newline="", encoding="utf-8") as f:
        key = csv.writer(f)
        key.writerow(["pair", "file", "A", "B", "lufs_a", "lufs_b", "gain_a_db", "gain_b_db"])
        for i, name in enumerate(names, 1):
            (la, pa), (lb, pb) = measure(dir_a / name), measure(dir_b / name)
            target = min(la, lb)
            ga, gb = target - la, target - lb
            over = max(pa + ga, pb + gb) + 1.0          # keep -1 dB of headroom on both
            if over > 0.0:
                ga -= over; gb -= over
            swap = rng.random() < 0.5
            first, second = ((dir_b / name, gb), (dir_a / name, ga)) if swap else ((dir_a / name, ga), (dir_b / name, gb))
            pair = f"{i:02d}"
            render(first[0], out / f"{pair}_A.wav", first[1])
            render(second[0], out / f"{pair}_B.wav", second[1])
            labels = (dir_b.name, dir_a.name) if swap else (dir_a.name, dir_b.name)
            key.writerow([pair, name, labels[0], labels[1], f"{la:.1f}", f"{lb:.1f}", f"{ga:.1f}", f"{gb:.1f}"])
            print(f"{pair} {name}: {la:.1f} / {lb:.1f} LUFS -> {target - max(0.0, over):.1f}")
    print(f"Kit written to {out} ({len(names)} pairs). Key: ab_key.csv")


if __name__ == "__main__":
    main()
