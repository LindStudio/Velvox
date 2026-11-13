"""EMBER vs. FabFilter Pro-DS on real takes (de-esser bench).

    python tools/bench/deess_bench.py <takes dir> <out dir> [--ember <EmberTests.exe>] [--tag name] [--only F01,M40]

Renders every *.wav of <takes dir> through EMBER (AUTO, `EmberTests --render`) into <out dir>/<tag>/ and, once
(cached), through Pro-DS at its default setting (Single Vocal, -36 dB, 6 dB range, wide band) via the RefHost,
then prints deess_metrics for both and the mean over all takes. Pro-DS must be installed (it is never shipped).
"""
import argparse
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from deess_metrics import read, mono, metrics  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PRODS = r"C:\Program Files\Common Files\VST3\FabFilter\FabFilter Pro-DS.vst3"
REFHOST = os.path.join(ROOT, "build", "VocalypseRefHost_artefacts", "Release", "VocalypseRefHost.exe")
KEYS = ["S_red", "vowel_red", "near_red", "purity", "voice_in_delta"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("takes")
    ap.add_argument("out")
    ap.add_argument("--ember", default=os.path.join(ROOT, "build", "EmberTests_artefacts", "Release", "EmberTests.exe"))
    ap.add_argument("--tag", default="ember")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    a.ember = os.path.abspath(a.ember)

    names = sorted(f[:-4] for f in os.listdir(a.takes) if f.lower().endswith(".wav"))
    if a.only:
        names = [n for n in names if n in a.only.split(",")]
    edir, pdir = os.path.join(a.out, a.tag), os.path.join(a.out, "prods")
    os.makedirs(edir, exist_ok=True)
    os.makedirs(pdir, exist_ok=True)

    rows = {"ember": [], "prods": []}
    for n in names:
        src = os.path.join(a.takes, n + ".wav")
        r = subprocess.run([a.ember, "--render", src, edir], capture_output=True, text=True)
        auto = r.stdout.splitlines()[0] if r.stdout else r.stderr
        pfile = os.path.join(pdir, n + "_prods.wav")
        if not os.path.exists(pfile):
            subprocess.run([REFHOST, "--render", "--plugin", PRODS, "--in", src, "--out", pfile], capture_output=True)
        dry, fs = read(src)
        dry = mono(dry)
        print(f"{n}: {auto.split(': ', 1)[-1]}")
        for kind, path in (("ember", os.path.join(edir, n + "_ember.wav")), ("prods", pfile)):
            m = metrics(dry, mono(read(path)[0]), fs)
            rows[kind].append(m)
            print(f"   {kind:6s} " + "  ".join(f"{k} {m[k]:6.2f}" for k in KEYS))
    print("\nmean over", len(names), "takes")
    for kind in rows:
        print(f"   {kind:6s} " + "  ".join(f"{k} {np.mean([m[k] for m in rows[kind]]):6.2f}" for k in KEYS))


if __name__ == "__main__":
    main()
