"""Tonal / dynamic profile of vocal files, the way the auto mix measures them (bands relative to the
300 Hz - 2 kHz core, voiced frames only). Used to calibrate the auto-mix targets against finished
professional vocals (Jakob 2026-10-28: "Profis boosten die Höhen eher").

    python tools/bench/vocal_profile.py <file-or-folder> [...]  [--csv out.csv]

Needs ffmpeg on the PATH and numpy. Prints one row per file and the median per argument.
"""
import os, subprocess, sys
import numpy as np

FS = 48000
BANDS = [("rumble", 40, 90), ("body", 100, 300), ("lowmid", 250, 500), ("core", 300, 2000),
         ("presence", 2000, 5000), ("top", 2000, 16000), ("sib", 5000, 10000), ("air", 10000, 16000)]


def decode(path):
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(FS), "-f", "f32le", "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(out, dtype=np.float32).astype(np.float64)


def profile(x):
    n = 4096
    hop = n // 2
    win = np.hanning(n)
    frames = [x[i:i + n] for i in range(0, len(x) - n, hop)]
    if not frames:
        return None
    rms = np.array([np.sqrt(np.mean(f * f)) + 1e-12 for f in frames])
    db = 20 * np.log10(rms)
    loud = np.percentile(db, 95)
    voiced = [f for f, d in zip(frames, db) if d > loud - 20]   # the singing, not the pauses
    freqs = np.fft.rfftfreq(n, 1 / FS)
    acc = np.zeros(len(freqs))
    for f in voiced:
        acc += np.abs(np.fft.rfft(f * win)) ** 2
    acc /= max(1, len(voiced))
    def band(lo, hi):
        return 10 * np.log10(np.mean(acc[(freqs >= lo) & (freqs < hi)]) + 1e-20)
    core = band(300, 2000)
    res = {name: band(lo, hi) - core for name, lo, hi in BANDS if name != "core"}
    # Dynamics: spread of 400 ms voice levels, crest factor of the voiced part
    blk = int(0.4 * FS)
    lv = np.array([20 * np.log10(np.sqrt(np.mean(x[i:i + blk] ** 2)) + 1e-12) for i in range(0, len(x) - blk, blk // 4)])
    lv = lv[lv > np.max(lv) - 25]
    res["spread"] = np.percentile(lv, 95) - np.percentile(lv, 10)
    v = np.concatenate(voiced)
    res["crest"] = 20 * np.log10(np.max(np.abs(v)) / (np.sqrt(np.mean(v * v)) + 1e-12))
    # Tilt: dB per octave between 250 Hz and 8 kHz (least squares on octave bands)
    cs = [250 * 2 ** k for k in range(6)]
    lvl = [band(c / 1.41, c * 1.41) for c in cs]
    res["tilt"] = np.polyfit(np.log2(cs), lvl, 1)[0]
    return res


def files(arg):
    if os.path.isdir(arg):
        for root, _, names in os.walk(arg):
            for nme in sorted(names):
                if nme.lower().endswith((".wav", ".mp3", ".flac", ".aif", ".aiff")):
                    yield os.path.join(root, nme)
    else:
        yield arg


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    csv = sys.argv[sys.argv.index("--csv") + 1] if "--csv" in sys.argv else None
    if csv in args:
        args.remove(csv)
    keys = ["rumble", "body", "lowmid", "presence", "top", "sib", "air", "tilt", "spread", "crest"]
    rows = []
    print("%-44s" % "file" + "".join("%9s" % k for k in keys))
    for a in args:
        group = []
        for f in files(a):
            try:
                p = profile(decode(f))
            except Exception as e:  # unreadable file: skip, say so
                print("  skip %s (%s)" % (f, e))
                continue
            if p is None:
                continue
            group.append(p)
            rows.append((f, p))
            print("%-44s" % os.path.basename(f)[:44] + "".join("%9.1f" % p[k] for k in keys))
        if len(group) > 1:
            print("%-44s" % ("MEDIAN " + os.path.basename(a.rstrip("/\\")))[:44]
                  + "".join("%9.1f" % np.median([g[k] for g in group]) for k in keys))
    if csv:
        with open(csv, "w", encoding="utf-8") as fh:
            fh.write("file," + ",".join(keys) + "\n")
            for f, p in rows:
                fh.write('"%s",' % f + ",".join("%.2f" % p[k] for k in keys) + "\n")


if __name__ == "__main__":
    main()
