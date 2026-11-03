#!/usr/bin/env python3
"""Collects the SONOVOX lead-vocal pairs (same take, dry and producer-processed "wet") into a bench folder.

    python tools/bench/sonovox_pairs.py <sonovox root> <out dir>

<sonovox root> is the extracted dataset (%USERPROFILE%/dev/velvox-data/sonovox, one folder per pack).
Writes <out>/dry/NN.wav, <out>/wet/NN.wav (wet + the pack's separate reverb print, if any) and pairs.csv.
"""
import csv
import pathlib
import sys

import numpy as np
from scipy.io import wavfile

# pack, dry lead, wet lead, extra wet prints summed onto the wet (separate reverb / FX returns)
PAIRS = [
    ("F01", "DRY/main vox hookHOW ARE YOU STEMS DRY.wav", "WET/main vox hookHOW ARE YOU STEMS WET.wav", []),
    ("F02", "LEAD_VOCAL_DRY.wav", "LEAD_VOCAL_WET.wav", []),
    ("F08", "Dry/Lead 1.wav", "Wet/Lead 1.wav", []),
    ("F09", "talk about vocals.wav", "wet vox.wav", []),
    ("F11", "DRY/LEAD_1.wav", "WET/LEAD_1.wav", []),
    ("F17", "DRY/LEAD_1.wav", "WET/LEAD_1.wav", []),
    ("F22", "raw lead.wav", "wet lead.wav", []),
    ("M34", "DRY/ STILL IN LOVE - VOKAAL DRY MAIN.wav", "WET/ STILL IN LOVE - VOKAAL WET MAIN.wav", []),
    ("M36", "DRY VOCAL.wav", "WET VOCAL.wav", []),
    ("M38", "ALL I GOT - DRY.wav", "ALL I GOT - WET.wav", []),
    ("M40", "DRY/LEAD VOCAL DRY.wav", "WET/LEAD VOCAL WET.wav", []),
    ("M42", "Lead/Lead Dry 1.wav", "Lead/Lead Wet.wav", []),
]


def find(pack_dir: pathlib.Path, suffix: str) -> pathlib.Path:
    hits = [p for p in pack_dir.rglob("*.wav") if "MACOSX" not in str(p) and not p.name.startswith("._")
            and str(p).replace("\\", "/").endswith(suffix)]
    if len(hits) != 1:
        raise SystemExit(f"{pack_dir.name}: {len(hits)} matches for {suffix!r}")
    return hits[0]


def load(path):
    fs, x = wavfile.read(path)
    if x.dtype.kind == "i":
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    elif x.dtype.kind == "u":
        x = (x.astype(np.float64) - 128.0) / 128.0
    else:
        x = x.astype(np.float64)
    if x.ndim == 1:
        x = x[:, None]
    return fs, x


def main():
    root, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    (out / "dry").mkdir(parents=True, exist_ok=True)
    (out / "wet").mkdir(parents=True, exist_ok=True)
    rows = []
    for pack, dry, wet, extras in PAIRS:
        d = root / pack
        fs_d, xd = load(find(d, dry))
        fs_w, xw = load(find(d, wet))
        for e in extras:
            fs_e, xe = load(find(d, e))
            n = min(len(xw), len(xe))
            xw = xw[:n] + (xe[:n] if xe.shape[1] == xw.shape[1] else xe[:n].mean(axis=1, keepdims=True))
        assert fs_d == fs_w, pack
        wavfile.write(out / "dry" / f"{pack}.wav", fs_d, xd.astype(np.float32))
        wavfile.write(out / "wet" / f"{pack}.wav", fs_w, xw.astype(np.float32))
        rows.append([pack, fs_d, xd.shape[1], xw.shape[1], round(len(xd) / fs_d, 1), round(len(xw) / fs_w, 1), dry, wet])
        print(pack, fs_d, xd.shape, xw.shape)
    with open(out / "pairs.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["pack", "fs", "dry_ch", "wet_ch", "dry_s", "wet_s", "dry", "wet"])
        w.writerows(rows)


if __name__ == "__main__":
    main()
