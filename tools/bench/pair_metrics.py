#!/usr/bin/env python3
"""What a mix did to a dry vocal: spectrum, dynamics, audible space (reverb / delay tails), width.

    python tools/bench/pair_metrics.py <dry dir> <processed dir> [--suffix _vocalypse] [--csv out.csv]

Pairs <dry>/<name>.wav with <processed>/<name><suffix>.wav (or renders/<name><suffix>.wav). Built for the
SONOVOX dry / producer-wet pairs (2026-11-03) and for VOCALYPSE bench renders, so both are measured alike:
  - eq_*      third-octave change (processed minus dry, both relative to their 300 Hz - 2 kHz core), dB
  - lra_*     spread of the 400 ms loudness over the sung parts (p95 - p10), dB  (dynamics)
  - crest_*   median peak-to-RMS in 400 ms windows over the sung parts, dB
  - tail_a/b/c  level in the dry gaps 0.15-0.4 / 0.4-0.8 / 0.8-1.6 s after a phrase, relative to the
              processed phrases, dB: how loud the reverb / delay is where you hear it
  - bed       level of the processed signal in the gaps 0.15-1.6 s minus the dry's own gap level, dB
  - side      side / mid energy of the processed signal over the sung parts, dB (width; -inf = mono)
"""
import argparse
import csv
import pathlib

import numpy as np
from scipy.io import wavfile
from scipy.signal import welch

THIRDS = [80.0 * 2 ** (k / 3) for k in range(24)]   # 80 Hz .. 16 kHz, as MixTarget
GROUPS = {"low": (80, 200), "lowmid": (200, 500), "mid": (500, 2000), "pres": (2000, 5000),
          "top": (5000, 10000), "air": (10000, 16500)}


def load(path):
    fs, x = wavfile.read(path)
    if x.dtype.kind == "i":
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    else:
        x = x.astype(np.float64)
    if x.ndim == 1:
        x = x[:, None]
    return fs, x


def frames_db(m, fs, win, hop):
    n = max(1, (len(m) - win) // hop + 1)
    idx = np.arange(win)[None, :] + hop * np.arange(n)[:, None]
    seg = m[idx]
    rms = np.sqrt(np.mean(seg ** 2, axis=1) + 1e-20)
    pk = np.max(np.abs(seg), axis=1) + 1e-20
    return 20 * np.log10(rms), 20 * np.log10(pk)


def align(dry, proc, fs):
    """Lag (samples) of proc against dry from 10 ms envelopes, +-2 s."""
    hop = int(fs * 0.01)
    a, _ = frames_db(dry, fs, hop, hop)
    b, _ = frames_db(proc, fs, hop, hop)
    n = min(len(a), len(b))
    a = np.maximum(a[:n], a.max() - 60); b = np.maximum(b[:n], b.max() - 60)
    a -= a.mean(); b -= b.mean()
    best, lag = -1e30, 0
    for L in range(-200, 201):
        if L >= 0:
            c = np.dot(a[:n - L], b[L:])
        else:
            c = np.dot(a[-L:], b[:n + L])
        if c > best:
            best, lag = c, L
    return lag * hop


def thirds(m, fs, mask_samples):
    x = m[mask_samples]
    f, p = welch(x, fs, nperseg=8192)
    out = []
    for c in THIRDS:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        sel = (f >= lo) & (f < hi)
        out.append(10 * np.log10(np.mean(p[sel]) + 1e-30))
    core = (f >= 300) & (f < 2000)
    ref = 10 * np.log10(np.mean(p[core]) + 1e-30)
    return np.array(out) - ref


def measure(dry_path, proc_path):
    fs, xd = load(dry_path)
    fs2, xp = load(proc_path)
    assert fs == fs2
    md, mp = xd.mean(axis=1), xp.mean(axis=1)
    lag = align(md, mp, fs)
    if lag > 0:
        mp = mp[lag:]; xp = xp[lag:]
    elif lag < 0:
        md = md[-lag:]; xd = xd[-lag:]
    n = min(len(md), len(mp))
    md, mp, xp = md[:n], mp[:n], xp[:n]

    hop = int(fs * 0.05)
    dd, _ = frames_db(md, fs, hop, hop)
    pd, _ = frames_db(mp, fs, hop, hop)
    ref = np.percentile(dd, 95)
    active = dd > ref - 30
    silent = dd < ref - 45
    # seconds since the last active frame
    since = np.full(len(dd), 1e9)
    last = -1e9
    for i, a in enumerate(active):
        if a:
            last = i
        since[i] = (i - last) * 0.05
    phrase = np.median(pd[active])
    res = {"lag_ms": round(lag / fs * 1000, 1)}

    def gap(lo, hi):
        sel = silent & (since >= lo) & (since < hi)
        return (np.median(pd[sel]) - phrase) if sel.sum() >= 5 else np.nan, sel

    for key, (lo, hi) in {"tail_a": (0.15, 0.4), "tail_b": (0.4, 0.8), "tail_c": (0.8, 1.6)}.items():
        res[key] = gap(lo, hi)[0]
    sel = silent & (since >= 0.15) & (since < 1.6)
    res["bed"] = (np.median(pd[sel]) - phrase) - (np.median(dd[sel]) - np.median(dd[active])) if sel.sum() >= 5 else np.nan
    res["gap_frames"] = int(sel.sum())

    # dynamics on 400 ms windows over the sung parts
    w = int(fs * 0.4)
    for name, m in (("dry", md), ("proc", mp)):
        r, p = frames_db(m, fs, w, w // 4)
        rd, _ = frames_db(md, fs, w, w // 4)
        act = rd > np.percentile(rd, 95) - 20
        res[f"lra_{name}"] = np.percentile(r[act], 95) - np.percentile(r[act], 10)
        res[f"crest_{name}"] = np.median(p[act] - r[act])

    mask = np.repeat(active, hop)[:n]
    if len(mask) < n:
        mask = np.concatenate([mask, np.zeros(n - len(mask), bool)])
    td, tp = thirds(md, fs, mask), thirds(mp, fs, mask)
    diff = tp - td
    for g, (lo, hi) in GROUPS.items():
        sel = [i for i, c in enumerate(THIRDS) if lo <= c < hi]
        res[f"eq_{g}"] = float(np.mean(diff[sel]))
    res["eq_thirds"] = diff
    if xp.shape[1] == 2:
        mid = (xp[:, 0] + xp[:, 1]) / 2; side = (xp[:, 0] - xp[:, 1]) / 2
        res["side"] = 10 * np.log10((np.sum(side[mask] ** 2) + 1e-20) / (np.sum(mid[mask] ** 2) + 1e-20))
    else:
        res["side"] = -np.inf
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dry"); ap.add_argument("proc")
    ap.add_argument("--suffix", default="")
    ap.add_argument("--csv")
    a = ap.parse_args()
    dry, proc = pathlib.Path(a.dry), pathlib.Path(a.proc)
    keys = ["lag_ms", "eq_low", "eq_lowmid", "eq_mid", "eq_pres", "eq_top", "eq_air", "lra_dry", "lra_proc",
            "crest_dry", "crest_proc", "tail_a", "tail_b", "tail_c", "bed", "side"]
    rows = []
    for d in sorted(dry.glob("*.wav")):
        cands = [proc / f"{d.stem}{a.suffix}.wav", proc / "renders" / f"{d.stem}{a.suffix}.wav"]
        p = next((c for c in cands if c.exists()), None)
        if p is None:
            continue
        r = measure(d, p)
        rows.append((d.stem, r))
    print("name  " + " ".join(f"{k:>9}" for k in keys))
    for name, r in rows:
        print(f"{name:5} " + " ".join(f"{r[k]:9.1f}" for k in keys))
    med = {k: np.nanmedian([r[k] for _, r in rows]) for k in keys}
    print("MED   " + " ".join(f"{med[k]:9.1f}" for k in keys))
    thirds_med = np.median(np.array([r["eq_thirds"] for _, r in rows]), axis=0)
    print("thirds (median change, dB): " + " ".join(f"{int(c)}:{v:+.1f}" for c, v in zip(THIRDS, thirds_med)))
    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["name"] + keys + [f"t{int(c)}" for c in THIRDS])
            for name, r in rows:
                w.writerow([name] + [r[k] for k in keys] + list(r["eq_thirds"]))


if __name__ == "__main__":
    main()
