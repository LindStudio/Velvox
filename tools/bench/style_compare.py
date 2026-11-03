#!/usr/bin/env python3
"""How different the auto-mix styles sound, measured like the professional pairs (pair_metrics.py).

    python tools/bench/style_compare.py <dry dir> <pro wet dir> <bench out dir> [<bench out dir> ...]

Prints the median of every measure per style next to the producers' median, and how far each style is
from every other one (mean absolute difference of the band changes, dynamics and tail levels).
"""
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from pair_metrics import measure  # noqa: E402

KEYS = ["eq_low", "eq_lowmid", "eq_mid", "eq_pres", "eq_top", "eq_air", "lra_proc", "crest_proc",
        "tail_a", "tail_b", "tail_c", "side"]


def medians(dry, proc, suffix):
    rows = []
    for d in sorted(dry.glob("*.wav")):
        for c in (proc / f"{d.stem}{suffix}.wav", proc / "renders" / f"{d.stem}{suffix}.wav"):
            if c.exists():
                rows.append(measure(d, c))
                break
    med = {k: float(np.nanmedian([r[k] if np.isfinite(r[k]) else np.nan for r in rows])) for k in KEYS}
    return med


def main():
    dry, wet = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    styles = {"PROFIS": medians(dry, wet, "")}
    for out in sys.argv[3:]:
        p = pathlib.Path(out)
        styles[p.name.replace("before_", "").replace("after_", "")] = medians(dry, p, "_vocalypse")
    print(f"{'style':16}" + "".join(f"{k:>10}" for k in KEYS))
    for name, m in styles.items():
        print(f"{name:16}" + "".join(f"{m[k]:10.1f}" for k in KEYS))
    names = [n for n in styles if n != "PROFIS"]
    print("\nmean |difference| between styles (dB):")
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            d = np.nanmean([abs(styles[a][k] - styles[b][k]) for k in KEYS if k != "side"])
            print(f"  {a:16} vs {b:16} {d:5.2f}")


if __name__ == "__main__":
    main()
