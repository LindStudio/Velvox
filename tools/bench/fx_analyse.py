#!/usr/bin/env python3
"""Measures the reverb / delay renders of `VelvoxTests --fxrender <dir>`.

Reverb impulse responses (ir_reverb_*.wav):
  - RT60 per octave band (125 Hz .. 8 kHz) from the Schroeder integral (T20 fit, -5..-25 dB)
  - EDT (early decay time, 0..-10 dB)
  - echo density: normalised echo density (Abel & Huang) after 50 / 100 / 200 ms (1.0 = Gaussian, dense)
  - ringing: largest narrow peak of the late-tail spectrum above its 1/3-octave smoothed level (dB);
    > ~12 dB indicates audible metallic resonances
  - inter-channel correlation of the tail (stereo width; ~0 = wide, 1 = mono)
  - tail stability: energy in the last 10 % relative to the expected exponential decay
Delay impulse responses (ir_delay_*.wav): echo times per channel and repeat levels.

    python tools/bench/fx_analyse.py <dir> [--json out.json]
"""

import json
import pathlib
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfiltfilt, find_peaks


def load(path):
    fs, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.dtype.kind == "i":
        x /= np.iinfo(np.int32).max
    if x.ndim == 1:
        x = x[:, None]
    return fs, x


def schroeder_db(x):
    e = np.cumsum((x ** 2)[::-1])[::-1]
    e /= max(e[0], 1e-30)
    return 10.0 * np.log10(np.maximum(e, 1e-30))


def decay_time(edc, fs, hi, lo):
    """Time for the EDC to fall 60 dB, extrapolated from the hi..lo dB fit."""
    idx = np.where((edc <= hi) & (edc >= lo))[0]
    if len(idx) < 10:
        return float("nan")
    t = idx / fs
    slope, _ = np.polyfit(t, edc[idx], 1)
    return float(-60.0 / slope) if slope < 0 else float("nan")


def octave(x, fs, fc):
    lo, hi = fc / np.sqrt(2), min(fc * np.sqrt(2), 0.45 * fs)
    sos = butter(4, [lo, hi], btype="band", fs=fs, output="sos")
    return sosfiltfilt(sos, x)


def echo_density(x, fs, t, win_ms=20.0):
    """Normalised echo density profile value at time t (Abel & Huang 2006)."""
    n = int(win_ms * 1e-3 * fs)
    c = int(t * fs)
    seg = x[max(0, c - n // 2): c + n // 2]
    if len(seg) < n // 2:
        return float("nan")
    w = np.hanning(len(seg))
    w /= w.sum()
    sd = np.sqrt(np.sum(w * seg ** 2))
    if sd <= 0:
        return 0.0
    frac = np.sum(w * (np.abs(seg) > sd))
    return float(frac / 0.3173)   # erfc(1/sqrt(2)) for a Gaussian


def peak_excess(seg, fs):
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / fs)
    band = (f > 200) & (f < 8000)
    db = 10 * np.log10(np.maximum(spec, 1e-30))
    # 1/3-octave smoothing (running mean over a log-spaced window, via cumulative sums)
    c = np.concatenate([[0.0], np.cumsum(spec)])
    lo = np.searchsorted(f, f / 2 ** (1 / 6))
    hi = np.maximum(np.searchsorted(f, f * 2 ** (1 / 6)), lo + 1)
    smooth = 10 * np.log10((c[hi] - c[lo]) / (hi - lo) + 1e-30)
    excess = (db - smooth)[band]
    i = int(np.argmax(excess))
    return float(excess[i]), float(f[band][i])


def ringing_db(x, fs, start_s):
    """Largest narrow spectral peak of the tail, relative to noise with the same envelope.

    A dense, smooth tail behaves like decaying noise (~0 dB); clear metallic modes give
    several dB more."""
    seg = x[int(start_s * fs):]
    seg = seg[: min(len(seg), int(fs * 2.0))]
    if len(seg) < fs // 4:
        return float("nan"), 0.0
    r, rf = peak_excess(seg, fs)
    n = max(1, int(0.01 * fs))
    env = np.sqrt(np.convolve(seg ** 2, np.ones(n) / n, mode="same"))
    rng = np.random.default_rng(1)
    base = np.mean([peak_excess(rng.standard_normal(len(seg)) * env, fs)[0] for _ in range(4)])
    return r - base, rf


def analyse_reverb(path):
    fs, x = load(path)
    mono = x.mean(axis=1)
    out = {"file": path.name}
    edc = schroeder_db(mono)
    out["rt60"] = round(decay_time(edc, fs, -5, -25), 2)
    out["edt"] = round(decay_time(edc, fs, 0, -10), 2)
    bands = {}
    for fc in (125, 250, 500, 1000, 2000, 4000, 8000):
        bands[fc] = round(decay_time(schroeder_db(octave(mono, fs, fc)), fs, -5, -25), 2)
    out["rt60_bands"] = bands
    onset = int(np.argmax(np.abs(mono) > 1e-4 * np.max(np.abs(mono))))
    out["onset_ms"] = round(1000 * onset / fs, 1)
    out["density"] = {f"{int(t*1000)}ms": round(echo_density(mono, fs, onset / fs + t), 2) for t in (0.05, 0.1, 0.2, 0.4)}
    rt = out["rt60"] if np.isfinite(out["rt60"]) else 1.0
    r, rf = ringing_db(mono, fs, onset / fs + min(0.3, 0.3 * rt))
    out["ringing_db"], out["ringing_hz"] = round(r, 1), round(rf)
    if x.shape[1] > 1:
        a, b = x[onset + int(0.08 * fs):, 0], x[onset + int(0.08 * fs):, 1]
        out["tail_corr"] = round(float(np.dot(a, b) / np.sqrt(np.dot(a, a) * np.dot(b, b) + 1e-30)), 2)
    out["peak_dbfs"] = round(20 * np.log10(np.max(np.abs(x)) + 1e-30), 1)
    out["energy_db"] = round(10 * np.log10(np.sum(x ** 2) + 1e-30), 1)
    out["finite"] = bool(np.all(np.isfinite(x)))
    return out


def analyse_delay(path):
    fs, x = load(path)
    res = {"file": path.name, "echoes": []}
    for ch in range(x.shape[1]):
        env = np.abs(x[:, ch])
        peaks, _ = find_peaks(env, height=1e-3 * env.max(), distance=int(0.05 * fs))
        res["echoes"].append([(round(1000 * p / fs, 2), round(20 * np.log10(env[p] + 1e-30), 1)) for p in peaks[:6]])
    res["finite"] = bool(np.all(np.isfinite(x)))
    return res


def main():
    d = pathlib.Path(sys.argv[1])
    report = {"reverb": [analyse_reverb(p) for p in sorted(d.glob("ir_reverb_*.wav")) + sorted(d.glob("ir_v03reverb_*.wav"))],
              "delay": [analyse_delay(p) for p in sorted(d.glob("ir_delay_*.wav"))]}
    for r in report["reverb"]:
        b = r["rt60_bands"]
        print(f"{r['file']:34s} RT60 {r['rt60']:5.2f}s EDT {r['edt']:5.2f}s  bands " +
              " ".join(f"{k}:{v:.1f}" for k, v in b.items()) +
              f"  dens {list(r['density'].values())}  ring {r['ringing_db']} dB@{r['ringing_hz']}  corr {r.get('tail_corr')}  E {r['energy_db']} dB")
    for r in report["delay"]:
        print(f"{r['file']:24s} " + " | ".join(str(e) for e in r["echoes"]))
    if "--json" in sys.argv:
        with open(sys.argv[sys.argv.index("--json") + 1], "w") as f:
            json.dump(report, f, indent=1)


if __name__ == "__main__":
    main()
