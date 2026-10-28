"""Summarises auto-mix reports of a --bench run: which modules each mix used, the EQ moves and the
measured bands before/after. Flags decisions that work against the targets ("dumme Entscheidungen"):
a broad cut of 2-5 kHz or the highs on a take that is darker than finished professional vocals.

    python tools/bench/summarise_reports.py <bench-out-dir> [<bench-out-dir> ...]
"""
import csv, glob, os, re, sys

MODULES = ["Room Noise", "Breath Control", "EQ", "Level Ride", "Compressor", "Vocal Smash", "De-esser", "Smooth",
           "Saturation", "Doubler", "Delay", "Reverb"]


def decisions(report):
    text = open(report, encoding="utf-8", errors="replace").read()
    block = text.split("DECISIONS", 1)[1].split("RESULT", 1)[0] if "DECISIONS" in text else ""
    used, eq = set(), []
    for line in block.splitlines():
        m = re.match(r"\s{2}([A-Za-z\- ]+): (.*?)\s+\((measured|rule)\)\s*$", line)
        if m:
            used.add(m.group(1).strip())
            if m.group(1).strip() == "EQ":
                eq.append(m.group(2))
    return used, eq


def main():
    for out in sys.argv[1:]:
        rows = list(csv.DictReader(open(os.path.join(out, "bench.csv"), encoding="utf-8")))
        counts = {m: 0 for m in MODULES}
        flags = []
        print("\n== %s (%d files)" % (out, len(rows)))
        print("%-32s %7s %7s %7s %7s %7s  %s" % ("file", "pres_in", "pres_out", "top_in", "top_out", "air_out", "EQ"))
        for r in rows:
            name = os.path.splitext(r["file"])[0]
            rep = glob.glob(os.path.join(out, "renders", name + "_report.txt"))
            used, eq = decisions(rep[0]) if rep else (set(), [])
            for m in used:
                if m in counts:
                    counts[m] += 1
            def f(k):
                try:
                    return float(r[k])
                except (KeyError, ValueError):
                    return float("nan")
            print("%-32s %7.1f %7.1f %7.1f %7.1f %7.1f  %s" % (name[:32], f("presence_in"), f("presence_out"), f("top_in"),
                                                              f("top_out"), f("air_out"), "; ".join(eq)[:110]))
            for e in eq:
                cut = re.search(r"(Präsenz|Höhen|Luft|Presence|Highs|Air)\s*-", e)
                if cut and f("presence_in") < -8.0:
                    flags.append("%s: %s on a dark take (2-5 kHz %.1f dB)" % (name, e, f("presence_in")))
        print("modules used: " + ", ".join("%s %d/%d" % (m, c, len(rows)) for m, c in counts.items()))
        for fl in flags:
            print("  DUMB: " + fl)


if __name__ == "__main__":
    main()
