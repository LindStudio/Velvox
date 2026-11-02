"""0.8: how far mixed vocals are from the party target curve (third octaves, like the auto mix's objective).

    VelvoxTests --thirds <renders-dir> <thirds.csv>      (once per run to compare)
    python tools/bench/target_distance.py <thirds.csv> [<thirds.csv> ...] [--only _velvox]

Per file: the RMS of the residual (deviation outside the target's spread) over the thirds the EQ works on
(160 Hz - and 1.3 x the lowest sung note - to 12.9 kHz). Prints the median per csv.
"""
import csv, os, re, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "..", "Source", "AutoMix", "PartyTargetCurve.h")


def target():
    text = open(HEADER, encoding="utf-8").read()
    def arr(name):
        m = re.search(name + r"\[24\] \{([^}]*)\}", text)
        return [float(x.strip().rstrip("f")) for x in m.group(1).split(",")]
    curve, spread = arr("partyCurve"), arr("partySpread")
    # the generator's floor of the spread (MixTarget: at least 1.5 dB)
    return curve, [max(1.5, s) for s in spread]


def centres():
    return [80.0 * 2.0 ** (k / 3.0) for k in range(24)]


def distance(row, curve, spread):
    f0 = float(row["f0_low"] or 0.0)
    lowest = max(160.0, 1.3 * f0 if f0 > 0 else 160.0)
    acc, n = 0.0, 0
    for k, c in enumerate(centres()):
        if c < lowest * 0.99 or c > 13000.0:
            continue
        v = float(row["t%d" % round(c)])
        if v <= -190:
            continue
        d = v - curve[k]
        r = d - spread[k] if d > spread[k] else (d + spread[k] if d < -spread[k] else 0.0)
        acc += r * r
        n += 1
    return (acc / n) ** 0.5 if n else float("nan")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    if only:
        args = [a for a in args if a != only]
    curve, spread = target()
    for path in args:
        rows = list(csv.DictReader(open(path, encoding="utf-8")))
        if only:
            rows = [r for r in rows if only in r["file"]]
        ds = [distance(r, curve, spread) for r in rows]
        ds = [d for d in ds if d == d]
        print("%-70s files %2d  median %.2f dB  mean %.2f dB" % (path[-70:], len(ds), statistics.median(ds), statistics.mean(ds)))


if __name__ == "__main__":
    main()
