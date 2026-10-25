"""Module bench: renders dry vocals and test signals through one Headliner module and its reference plugins at
matched settings (HeadlinerRefHost), level-matches, measures (qmetrics) and reports per metric with a verdict.

    python -I tools/refhost/bench.py <module>|all [--out DIR] [--clips N] [--no-ref] [--jobs N]

Writes <out>/<module>/metrics.json, <out>/<module>/report.md, raw renders in <out>/<module>/raw/ and level-matched
listening files in <out>/<module>/listen/. Default out: C:\\Users\\jakob\\dev\\velvox-bench\\2026-10-25\\modules.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import json
import math
import os
import subprocess
import sys
import time

import numpy as np
from scipy import signal as sps

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modules as M  # noqa: E402
import qmetrics as q  # noqa: E402
import signals as S  # noqa: E402

FS = S.FS
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOST = os.environ.get("REFHOST_EXE", os.path.join(REPO, "build", "HeadlinerRefHost_artefacts", "Release", "HeadlinerRefHost.exe"))
DEFAULT_OUT = r"C:\Users\jakob\dev\velvox-bench\2026-10-25\modules"
HEADLINER = "Headliner"
BLOCK = 512
PREROLL = 0.25   # seconds of silence before every input (parameter smoothing / engine switches settle, as in a DAW)

# metric -> (better, default tolerance, unit, description)
METRICS = {
    "resp_max_dev_db": ("lower", 0.5, "dB", "max |response - analog prototype|, 20 Hz-20 kHz"),
    "resp_rms_dev_db": ("lower", 0.2, "dB", "RMS response deviation from the analog prototype"),
    "resp_hf_max_dev_db": ("lower", 0.5, "dB", "max deviation above 10 kHz (cramping)"),
    "resp_vs_ref_max_db": ("lower", 0.5, "dB", "max |Headliner - reference| response (Headliner row only)"),
    "cramp_dev_db": ("lower", 0.5, "dB", "max |bell at 15 kHz - same bell at 1.5 kHz shifted x10| (cramping)"),
    "curve_dev_db": ("lower", 1.0, "dB", "max deviation of the static curve from the ideal soft-knee curve"),
    "ratio_measured": ("match", 0.5, ":1", "ratio from the top of the static curve"),
    "threshold_measured_db": ("match", 2.0, "dB", "threshold where the curve leaves unity slope"),
    "attack_ms": ("match", 5.0, "ms", "63 % gain-reduction time after a +30 dB step"),
    "release_ms": ("match", 40.0, "ms", "63 % recovery time after the step down"),
    "thd_100hz_db": ("lower", 3.0, "dB", "THD of a 100 Hz tone at -10 dBFS (ballistics distortion)"),
    "crest_reduction_db": ("match", 1.0, "dB", "peak-to-RMS reduction on the vocals"),
    "st_loudness_std_lu": ("lower", 0.5, "LU", "std of 400 ms loudness on voiced parts (dynamics evenness)"),
    "sib_reduction_db": ("higher", 1.0, "dB", "5-10 kHz level reduction in the 10 % most sibilant frames"),
    "nonsib_hf_change_db": ("higher", 0.5, "dB", "5-10 kHz change in non-sibilant voiced frames (0 = no lisp)"),
    "selectivity_db": ("higher", 1.0, "dB", "sibilant reduction minus non-sibilant reduction"),
    "inactive_null_db": ("lower", 0.0, "dB", "residual vs dry with the threshold above the signal"),
    "delta_db": ("match", 1.0, "dB", "energy removed (dry - wet) relative to dry"),
    "pte_reduction_db": ("higher", 0.3, "dB", "drop of STFT peak-to-envelope (1-10 kHz)"),
    "ltas_reduction_db": ("higher", 0.5, "dB", "drop of the largest stationary spectral peak"),
    "flatness_gain_db": ("higher", 0.2, "dB", "spectral flatness gain 1-10 kHz"),
    "transient_change_db": ("match", 0.5, "dB", "onset-to-sustain change (0 = transients kept)"),
    "musical_noise_db": ("lower", 0.2, "dB", "mean spectral gain-mask flux (time + frequency)"),
    "thd_1k_db": ("match", 2.0, "dB", "THD, 1 kHz at -12 dBFS (calibration target for the reference)"),
    "thd_1k_m24_db": ("match", 3.0, "dB", "THD, 1 kHz at -24 dBFS (level dependence)"),
    "h2_h3_db": ("info", 0.0, "dB", "2nd vs 3rd harmonic (even/odd character)"),
    "aliasing_db": ("lower", 3.0, "dB", "non-harmonic energy, 4987 Hz at -6 dBFS"),
    "imd_db": ("lower", 3.0, "dB", "SMPTE IMD, 60 Hz + 7 kHz 4:1"),
    "pitch_err_cents": ("lower", 2.0, "ct", "|median pitch error| of a 220 Hz tone"),
    "pitch_jitter_cents": ("lower", 1.0, "ct", "std of the pitch track of a 220 Hz tone"),
    "spurious_db": ("lower", 3.0, "dB", "non-harmonic energy of the shifted tone"),
    "vocal_pitch_err_cents": ("lower", 3.0, "ct", "median |f0 ratio error| on the vocals"),
    "hnr_drop_db": ("info", 0.0, "dB", "HNR(dry) - HNR(out) on the vocals (pitch changes it; informative only)"),
    "latency_ms": ("lower", 10.0, "ms", "reported latency"),
    "comb_depth_db": ("lower", 1.0, "dB", "peak-to-notch range of the mono-sum transfer (pink noise)"),
    "am_roughness_db": ("lower", 0.5, "dB", "std of the 20 ms envelope of a 440 Hz tone (beating/flanging)"),
    "lr_corr": ("lower", 0.1, "", "L/R correlation of the output (vocals)"),
    "mono_loss_db": ("lower", 1.0, "dB", "loudness lost in the mono sum (vocals)"),
    "voice_delay_err_ms": ("lower", 0.5, "ms", "|median delay of a solo voice - set delay| (noise), mean over voices"),
    "voice_detune_err_cents": ("lower", 1.0, "ct", "|median pitch of a solo voice - set detune| (440 Hz), mean over voices"),
    "voice_pitch_jitter_cents": ("lower", 0.5, "ct", "std of a solo voice's pitch track (no wobble), mean over voices"),
    "voice_splice_db": ("lower", 0.5, "dB", "std of the 20 ms envelope of a solo voice at +30 ct (pitch-shifter artefacts)"),
    "echo_time_err_ms": ("lower", 0.5, "ms", "|first echo time - set time|"),
    "feedback_db_per_repeat": ("match", 1.0, "dB", "level change from echo 1 to echo 2"),
    "echo_smear_ms": ("lower", 1.0, "ms", "time holding 90 % of the first echo's energy"),
    "inter_echo_floor_db": ("lower", 3.0, "dB", "energy between echoes relative to the first echo"),
    "dry_leak_db": ("lower", 0.0, "dB", "dry signal in the output at 100 % wet (burst test)"),
    "t30_s": ("match", 0.3, "s", "reverb time T30"),
    "t30_err_pct": ("lower", 10.0, "%", "|T30 - set decay| / set decay"),
    "edt_s": ("match", 0.3, "s", "early decay time"),
    "curvature_db": ("lower", 0.5, "dB", "RMS deviation of the decay from a straight line"),
    "mean_ned": ("higher", 0.05, "", "normalized echo density 20-300 ms (1 = Gaussian)"),
    "mixing_ms": ("lower", 20.0, "ms", "time until echo density reaches 0.9"),
    "modal_ringing_db": ("lower", 2.0, "dB", "strongest narrow peak in the late tail (metallic ringing)"),
    "tail_corr": ("lower", 0.1, "", "L/R correlation of the tail"),
    "bass_ratio": ("match", 0.2, "x", "T30(250 Hz) / T30(1 kHz)"),
    "treble_ratio": ("match", 0.2, "x", "T30(4 kHz) / T30(1 kHz)"),
    "gain_jitter_db": ("lower", 0.05, "dB", "RMS 2nd difference of the 50 ms gain trace"),
    "edge_splatter_db": ("lower", 0.0, "dB", "energy above 5 kHz with a 1 kHz tone (clicks at gate edges)"),
    "gate_depth_db": ("lower", 0.0, "dB", "closed-gate level relative to open"),
    "null_vs_input_db": ("lower", 0.0, "dB", "residual output - input"),
    "mix0_null_db": ("lower", 0.0, "dB", "residual vs dry at mix 0 (Headliner)"),
    "off_null_db": ("lower", 0.0, "dB", "residual vs dry with the module switched off (Headliner)"),
    "cpu_pct": ("lower", 0.5, "%", "processing time per 512-sample block, share of real time"),
}
ABSOLUTE = {"mix0_null_db": -60.0, "off_null_db": -100.0}


# ---------------------------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------------------------

def verdict(h, r, better, tol, absolute=None):
    if h is None or (isinstance(h, float) and math.isnan(h)):
        return "n/a"
    if better == "info":
        return "info"
    target = absolute if absolute is not None else r
    if target is None or (isinstance(target, float) and math.isnan(target)):
        return "info"
    if better == "lower":
        ok = h <= target + tol
    elif better == "higher":
        ok = h >= target - tol
    else:
        ok = abs(h - target) <= tol
    return "pass" if ok else "FAIL"


def analog_response(f, specs):
    """dB response of analog prototypes: ('bell', f0, gain, Q), ('highshelf'|'lowshelf', f0, gain, Q), ('hpf'|'lpf', f0, order)."""
    f = np.asarray(f, float)
    total = np.zeros_like(f)
    for spec in specs:
        kind = spec[0]
        if kind in ("hpf", "lpf"):
            b, a = sps.butter(spec[2], 2 * np.pi * spec[1], btype="high" if kind == "hpf" else "low", analog=True)
            _, h = sps.freqs(b, a, 2 * np.pi * f)
        else:
            f0, g, Q = spec[1], spec[2], spec[3]
            A = 10 ** (g / 40)
            s = 1j * f / f0
            if kind == "bell":
                h = (s * s + s * (A / Q) + 1) / (s * s + s / (A * Q) + 1)
            elif kind == "highshelf":
                h = A * (A * s * s + (np.sqrt(A) / Q) * s + 1) / (s * s + (np.sqrt(A) / Q) * s + A)
            elif kind == "lowshelf":
                h = A * (s * s + (np.sqrt(A) / Q) * s + A) / (A * s * s + (np.sqrt(A) / Q) * s + 1)
            else:
                raise ValueError(kind)
        total += 20 * np.log10(np.maximum(np.abs(h), 1e-12))
    return total


def stereo(x):
    x = q.channels(x)
    return np.repeat(x, 2, axis=1) if x.shape[1] == 1 else x


def safe(v):
    return None if v is None or (isinstance(v, float) and (math.isnan(v) or math.isinf(v))) else float(v)


class RenderError(RuntimeError):
    pass


# ---------------------------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------------------------

class Side:
    def __init__(self, name, kind, plugin="", sub="", params=None, setn=None):
        self.name, self.kind, self.plugin, self.sub = name, kind, plugin, sub
        self.params = list(params or [])
        self.setn = list(setn or [])

    def args(self, extra=None):
        a = []
        if self.kind == "headliner":
            a += ["--headliner", self.plugin]
            items = dict(self.params)
            items.update(extra or {})
            for k, v in items.items():
                a += ["--set", f"{k}={v}"]
        else:
            a += ["--plugin", self.plugin]
            if self.sub:
                a += ["--sub", self.sub]
            for k, v in self.params + list(extra or []):
                a += ["--set", f"{k}={v}"]
            for k, v in self.setn:
                a += ["--setn", f"{k}={v}"]
        return a


def host_render(side: Side, inp, out, tail=0.0, extra=None, timeout=600):
    cmd = [HOST, "--render", "--in", inp, "--out", out, "--tail", str(tail), "--block", str(BLOCK)] + side.args(extra)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise RenderError(f"{side.name}: timeout (license dialog?)")
    if p.returncode != 0 or not os.path.exists(out):
        raise RenderError(f"{side.name}: exit {p.returncode}: {(p.stderr or p.stdout).strip()[-300:]}")
    with open(out + ".json", encoding="utf-8") as fh:
        info = json.load(fh)
    y, _ = q.read_wav(out)
    return y, info


class Ctx:
    def __init__(self, module: M.Module, out_dir, clips, use_refs=True, jobs=4, log=print):
        self.module, self.out_dir, self.clips, self.jobs, self.log = module, out_dir, clips, jobs, log
        self.raw = os.path.join(out_dir, "raw")
        self.listen = os.path.join(out_dir, "listen")
        self.inputs = os.path.join(out_dir, "_inputs")
        for d in (self.raw, self.listen, self.inputs):
            os.makedirs(d, exist_ok=True)
        self.metrics = []
        self.ref_errors = {}
        self.calibrated = {}
        self.cpu = {}
        self.latency = {}
        self.use_refs = use_refs
        self.case = None

    # sides for the current case
    def sides(self):
        c = self.case
        h = Side(HEADLINER, "headliner", self.module.key, params=list(c.headliner.items()))
        out = [h]
        if self.use_refs:
            only = c.info.get("refs")   # a case may compare against some of the module's references only
            for r in self.module.refs:
                if r.name in self.ref_errors or (only and r.name not in only):
                    continue
                params = list(r.params) + list(c.refs.get(r.name, []))
                if r.name in self.calibrated.get(c.name, {}):
                    k, v = self.calibrated[c.name][r.name]
                    params = [p for p in params if p[0] != k] + [(k, v)]
                out.append(Side(r.name, "ref", r.plugin, r.sub, params, r.setn))
        return out

    def input_file(self, name, x):
        path = os.path.join(self.inputs, name + ".wav")
        q.write_wav(path, x, FS)
        return path

    def render(self, sig, x, tail=0.0, sides=None, headliner_extra=None, ref_extra=None, tag="", raw=False):
        """Renders x through every side; returns {side: y} (failed references are recorded and left out)."""
        pre = int(PREROLL * FS)
        xp = np.concatenate([np.zeros((pre,) + np.shape(x)[1:]), x], axis=0)
        inp = self.input_file(sig, xp)
        sides = sides or self.sides()
        results = {}

        def one(side):
            out = os.path.join(self.raw, f"{self.case.name}__{sig}{tag}__{side.name.replace(' ', '_')}.wav")
            extra = headliner_extra if side.kind == "headliner" else (ref_extra or {}).get(side.name)
            return side, host_render(side, inp, out, tail, extra)

        with cf.ThreadPoolExecutor(max_workers=self.jobs) as ex:
            futs = [ex.submit(one, s) for s in sides]
            for fu in futs:
                try:
                    side, (y, info) = fu.result()
                except RenderError as e:
                    name = str(e).split(":")[0]
                    if name == HEADLINER:
                        raise
                    self.ref_errors[name] = str(e)
                    self.log(f"  reference failed: {e}")
                    continue
                y = q.channels(y)[pre:]
                if self.case.info.get("wet") == "subtract_dry" and side.kind == "headliner" and not headliner_extra and not raw:
                    xx = stereo(x)
                    y = y.copy()
                    y[:len(xx)] -= xx[:len(y)]
                results[side.name] = y
                self.cpu.setdefault(side.name, []).append(q.cpu_percent(info["meanBlockUs"], info["block"], info["sampleRate"]))
                self.latency[side.name] = info["latency"]
        return results

    def metric(self, name, values: dict, note=""):
        better, tol, unit, desc = METRICS[name]
        absolute = ABSOLUTE.get(name)
        over = self.module.tolerances.get(name)
        if over:
            better, tol = over[0], over[1]
            if len(over) > 2:
                absolute = over[2]
        h = safe(values.get(HEADLINER))
        refs = {k: safe(v) for k, v in values.items() if k != HEADLINER}
        primary = next((refs[r.name] for r in self.module.refs if r.name in refs and refs[r.name] is not None), None)
        rec = {"metric": name, "case": self.case.name, "headliner": h, "refs": refs, "better": better, "tol": tol,
               "absolute": absolute, "unit": unit, "desc": desc, "note": note,
               "gap": None if (h is None or primary is None) else h - primary,
               "verdict": verdict(h, primary, better, tol, absolute)}
        self.metrics.append(rec)
        return rec


# ---------------------------------------------------------------------------------------------
# Probes for calibration (matched amount of action)
# ---------------------------------------------------------------------------------------------

def _sib_frames(x):
    """Per-frame 5-10 kHz level and masks of sibilant / voiced non-sibilant frames (dry signal decides)."""
    f, _, Z = q._stft(x, FS, nperseg=1024, hop=256)
    P = np.abs(Z) ** 2
    hf = P[(f >= 5000) & (f <= 10000)].sum(axis=0)
    tot = P[(f >= 80)].sum(axis=0)
    ratio = hf / np.maximum(tot, 1e-20)
    tot_db = 10 * np.log10(tot + 1e-20)
    active = tot_db > tot_db.max() - 40
    sib = active & (ratio >= np.percentile(ratio[active], 90))
    nonsib = active & (ratio <= np.percentile(ratio[active], 50))
    return sib, nonsib


def _hf_level(y, mask):
    f, _, Z = q._stft(y, FS, nperseg=1024, hop=256)
    P = np.abs(Z) ** 2
    hf = P[(f >= 5000) & (f <= 10000)].sum(axis=0)
    n = min(len(hf), len(mask))
    return 10 * np.log10(np.sum(hf[:n][mask[:n]]) + 1e-20)


def probe_value(probe, x, y):
    if probe == "delta_energy":
        return q.delta_energy_db(x, y)
    if probe == "sib_reduction":
        sib, _ = _sib_frames(q.mono(x))
        return _hf_level(q.mono(x), sib) - _hf_level(q.mono(y), sib)
    if probe == "thd_1k":
        return q.thd_db(y, FS, 1000)
    raise ValueError(probe)


def calibrate(ctx: Ctx, case: M.Case):
    if not case.calibrate or not ctx.use_refs:
        return
    for ref_name, cal in case.calibrate.items():
        ref = next(r for r in ctx.module.refs if r.name == ref_name)
        if ref_name in ctx.ref_errors:
            continue
        x = S.sine(FS, 1000, -12, 2.0) if cal.probe == "thd_1k" else S.load_clip(ctx.clips[0])
        hside = ctx.sides()[0]
        h = ctx.render("calib", x, sides=[hside])
        target = probe_value(cal.probe, x, h[HEADLINER])
        base = list(ref.params) + list(case.refs.get(ref_name, []))
        tried = {}

        def measure(v):
            side = Side(ref_name, "ref", ref.plugin, ref.sub, base + [(cal.param, f"{v:g}")], ref.setn)
            r = ctx.render("calib", x, sides=[side], tag=f"_{cal.param.replace(' ', '')}{v:g}")
            tried[v] = probe_value(cal.probe, x, r[ref_name]) if ref_name in r else None
            return tried[v]

        for v in cal.values:
            if measure(v) is None:
                break
        ok = {v: m for v, m in tried.items() if m is not None}
        best = best_v = None
        if ok:
            # refine between the two grid values that bracket the target (bisection, monotonic probe)
            vs = sorted(ok)
            for a, b in zip(vs, vs[1:]):
                if (ok[a] - target) * (ok[b] - target) < 0:
                    for _ in range(5):
                        mid = 0.5 * (a + b)
                        m = measure(mid)
                        if m is None:
                            break
                        if (ok[a] - target) * (m - target) <= 0:
                            b = mid
                        else:
                            a, ok[a] = mid, m
                        ok[mid] = m
                    break
            best = min(tried, key=lambda v: abs(tried[v] - target) if tried[v] is not None else 1e9)
            best_v = tried[best]
            best = f"{best:g}"
        if best is not None:
            ctx.calibrated.setdefault(case.name, {})[ref_name] = (cal.param, str(best))
            ctx.log(f"  calibrated {ref_name}: {cal.param}={best} ({cal.probe}: ref {best_v:.2f} vs Headliner {target:.2f})")


# ---------------------------------------------------------------------------------------------
# Procedures
# ---------------------------------------------------------------------------------------------

def proc_eq_response(ctx, case):
    x = S.white_noise(FS, 8.0, -24.0)
    ys = ctx.render("noise", x)
    f = np.geomspace(20, 20000, 240)
    target = analog_response(f, case.info["analog"])
    resp = {}
    for name, y in ys.items():
        _, h = q.freq_response(x, y, FS, grid=f)
        resp[name] = h
    for key in ("max_db", "rms_db", "hf_max_db"):
        vals = {n: q.response_deviation(f, target, h)[key] for n, h in resp.items()}
        ctx.metric({"max_db": "resp_max_dev_db", "rms_db": "resp_rms_dev_db", "hf_max_db": "resp_hf_max_dev_db"}[key], vals)
    for r in ctx.module.refs:
        if r.name in resp:
            ctx.metric("resp_vs_ref_max_db", {HEADLINER: q.response_deviation(f, resp[r.name], resp[HEADLINER])["max_db"]},
                       note=f"vs {r.name}")


def proc_eq_cramping(ctx, case):
    """Definition-independent cramping test: the same bell an octave-decade lower (1.5 kHz) is the plugin's own
    shape; at 15 kHz it must look the same (shifted by x10 in frequency)."""
    x = S.white_noise(FS, 8.0, -24.0)
    cr = case.info["cramp"]
    lo = ctx.render("noise", x, headliner_extra=cr["lo"]["headliner"], ref_extra={k: v for k, v in cr["lo"].items()}, tag="_lo")
    hi = ctx.render("noise", x, headliner_extra=cr["hi"]["headliner"], ref_extra={k: v for k, v in cr["hi"].items()}, tag="_hi")
    f_hi = np.geomspace(4000, 20000, 120)
    vals = {}
    for n in hi:
        if n not in lo:
            continue
        _, h_hi = q.freq_response(x, hi[n], FS, grid=f_hi)
        _, h_lo = q.freq_response(x, lo[n], FS, grid=f_hi / 10)
        vals[n] = float(np.max(np.abs(h_hi - h_lo)))
    ctx.metric("cramp_dev_db", vals, note="bell +6 dB Q 1.5: 15 kHz vs 1.5 kHz x10")


def _ideal_curve(levels, thr, ratio, knee):
    out = []
    for L in levels:
        d = L - thr
        if 2 * d < -knee:
            out.append(L)
        elif 2 * abs(d) <= knee and knee > 0:
            out.append(L + (1 / ratio - 1) * (d + knee / 2) ** 2 / (2 * knee))
        else:
            out.append(thr + d / ratio)
    return np.array(out)


def proc_dyn_curve(ctx, case):
    levels = np.arange(-60.0, 0.1, 3.0)
    x, step = S.level_steps(FS, levels, 0.4, 1000)
    ys = ctx.render("steps", x)
    curves = {n: q.static_curve(y, FS, step, len(levels)) + 3.01 for n, y in ys.items()}  # peak dBFS
    ratios = {n: q.curve_ratio_threshold(levels, c)[0] for n, c in curves.items()}
    thrs = {n: q.curve_ratio_threshold(levels, c)[1] for n, c in curves.items()}
    ctx.metric("ratio_measured", ratios)
    ctx.metric("threshold_measured_db", thrs)
    if "threshold" in case.info:
        ideal = _ideal_curve(levels, case.info["threshold"], case.info["ratio"], case.info["knee"])
        devs = {}
        for n, c in curves.items():
            c0 = c - (c[0] - levels[0])  # remove makeup / static gain
            devs[n] = float(np.max(np.abs(c0 - ideal)))
        ctx.metric("curve_dev_db", devs)
    ctx.extra_curves = {n: c.tolist() for n, c in curves.items()}


def proc_dyn_timing(ctx, case):
    x, i_up, i_down = S.step_tone(FS, -40, -10, 1000)
    ys = ctx.render("step", x)
    att, rel = {}, {}
    for n, y in ys.items():
        a, r = q.attack_release_ms(x, y, FS, i_up, i_down)
        att[n], rel[n] = a, r
    ctx.metric("attack_ms", att, note=f"set {case.info.get('attack')} ms")
    ctx.metric("release_ms", rel, note=f"set {case.info.get('release')} ms")


def proc_thd_lf(ctx, case):
    x = S.sine(FS, 100, -10, 3.0)
    ys = ctx.render("sine100", x)
    ctx.metric("thd_100hz_db", {n: q.thd_db(y, FS, 100) for n, y in ys.items()})


def _crest_db(y):
    m = q.mono(y)
    rms = np.sqrt(np.mean(m ** 2))
    return 20 * np.log10(np.max(np.abs(m)) / max(rms, 1e-12))


def _st_loudness_std(y):
    m = q.mono(y)
    w, h = int(0.4 * FS), int(0.1 * FS)
    (b1, a1), (b2, a2) = q._k_weighting(FS)
    k = sps.lfilter(b2, a2, sps.lfilter(b1, a1, m))
    c = np.concatenate([[0], np.cumsum(k ** 2)])
    st = np.arange(0, len(k) - w, h)
    L = -0.691 + 10 * np.log10((c[st + w] - c[st]) / w + 1e-20)
    act = L > L.max() - 30
    return float(np.std(L[act]))


def _vocal_renders(ctx):
    """Renders every clip through every side; returns [(clip, x, {side: level-matched y})]."""
    out = []
    for clip in ctx.clips:
        x = S.load_clip(clip)
        ys = ctx.render(clip.name, x, tail=2.0 if ctx.module.key in ("delay", "reverb") else 0.0)
        lm = {}
        q.write_wav(os.path.join(ctx.listen, f"{ctx.case.name}__{clip.name}__dry.wav"), x, FS)
        for n, y in ys.items():
            g = q.match_gain_db(x, y[:len(x)], FS)
            yl = q.apply_gain_db(y, g)
            lm[n] = yl
            q.write_wav(os.path.join(ctx.listen, f"{ctx.case.name}__{clip.name}__{n.replace(' ', '_')}.wav"), yl, FS)
        out.append((clip, x, lm))
    return out


def _mean_over_clips(per_clip):
    names = set().union(*[d.keys() for d in per_clip]) if per_clip else set()
    return {n: float(np.mean([d[n] for d in per_clip if n in d and d[n] is not None and np.isfinite(d[n])]))
            for n in names}


def proc_vocal(ctx, case):
    renders = _vocal_renders(ctx)
    crest, lstd = [], []
    for clip, x, ys in renders:
        crest.append({n: _crest_db(x) - _crest_db(y[:len(x)]) for n, y in ys.items()})
        lstd.append({n: _st_loudness_std(y[:len(x)]) for n, y in ys.items()})
    ctx.metric("crest_reduction_db", _mean_over_clips(crest), note="mean of clips")
    ctx.metric("st_loudness_std_lu", _mean_over_clips(lstd), note="mean of clips")


def proc_vocal_listen(ctx, case):
    _vocal_renders(ctx)


def proc_deess_vocal(ctx, case):
    renders = _vocal_renders(ctx)
    sibr, non, sel, tr = [], [], [], []
    for clip, x, ys in renders:
        sib, nonsib = _sib_frames(q.mono(x))
        a, b, c, d = {}, {}, {}, {}
        for n, y in ys.items():
            yy = q.mono(y[:len(x)])
            # raw (not level matched) output: the de-esser should not change the overall level
            a[n] = _hf_level(q.mono(x), sib) - _hf_level(yy, sib)
            b[n] = _hf_level(yy, nonsib) - _hf_level(q.mono(x), nonsib)
            c[n] = a[n] + b[n]
            d[n] = q.transient_change_db(x, y[:len(x)], FS)
        sibr.append(a), non.append(b), sel.append(c), tr.append(d)
    ctx.metric("sib_reduction_db", _mean_over_clips(sibr))
    ctx.metric("nonsib_hf_change_db", _mean_over_clips(non))
    ctx.metric("selectivity_db", _mean_over_clips(sel))
    ctx.metric("transient_change_db", _mean_over_clips(tr))


def proc_deess_inactive_null(ctx, case):
    x = S.load_clip(ctx.clips[0])
    ys = ctx.render("inactive", x, sides=ctx.sides()[:1], headliner_extra={"deessThreshold": 0})
    ctx.metric("inactive_null_db", {HEADLINER: q.null_db(ys[HEADLINER][:len(x)], stereo(x))})


def proc_resonance_vocal(ctx, case):
    # level matching is not applied to the comparison inputs: the metrics are level independent except delta
    out = []
    for clip in ctx.clips:
        x = S.load_clip(clip)
        ys = ctx.render(clip.name, x)
        q.write_wav(os.path.join(ctx.listen, f"{case.name}__{clip.name}__dry.wav"), x, FS)
        vals = {k: {} for k in ("delta_db", "pte_reduction_db", "ltas_reduction_db", "flatness_gain_db", "transient_change_db",
                                "musical_noise_db")}
        for n, y in ys.items():
            y = y[:len(x)]
            q.write_wav(os.path.join(ctx.listen, f"{case.name}__{clip.name}__{n.replace(' ', '_')}.wav"),
                        q.apply_gain_db(y, q.match_gain_db(x, y, FS)), FS)
            q.write_wav(os.path.join(ctx.listen, f"{case.name}__{clip.name}__{n.replace(' ', '_')}__delta.wav"), stereo(x)[:len(y)] - y, FS)
            vals["delta_db"][n] = q.delta_energy_db(stereo(x), y)
            vals["pte_reduction_db"][n] = q.peak_to_envelope_db(x, FS) - q.peak_to_envelope_db(y, FS)
            vals["ltas_reduction_db"][n] = q.ltas_peakiness_db(x, FS) - q.ltas_peakiness_db(y, FS)
            vals["flatness_gain_db"][n] = q.spectral_flatness_db(y, FS) - q.spectral_flatness_db(x, FS)
            vals["transient_change_db"][n] = q.transient_change_db(x, y, FS)
            vals["musical_noise_db"][n] = q.musical_noise_db(x, y, FS)
        out.append(vals)
    for k in out[0]:
        ctx.metric(k, _mean_over_clips([o[k] for o in out]))


def proc_sat_tones(ctx, case):
    t1 = S.sine(FS, 1000, -12, 2.0)
    t2 = S.sine(FS, 1000, -24, 2.0)
    t3 = S.sine(FS, 4987, -6, 2.0)
    tt = np.arange(int(2.0 * FS)) / FS
    imd = 10 ** (-8 / 20) * (0.8 * np.sin(2 * np.pi * 60 * tt) + 0.2 * np.sin(2 * np.pi * 7000 * tt))
    y1, y2, y3, y4 = (ctx.render(n, s) for n, s in (("sine1k", t1), ("sine1k_m24", t2), ("sine4987", t3), ("imd", imd)))
    ctx.metric("thd_1k_db", {n: q.thd_db(y, FS, 1000) for n, y in y1.items()})
    ctx.metric("thd_1k_m24_db", {n: q.thd_db(y, FS, 1000) for n, y in y2.items()})

    def h23(y):
        f, X = q._tone_spectrum(y, FS)
        return 10 * np.log10(q._band_power(f, X, 2000)[0] / max(q._band_power(f, X, 3000)[0], 1e-30))

    ctx.metric("h2_h3_db", {n: h23(y) for n, y in y1.items()})
    ctx.metric("aliasing_db", {n: q.aliasing_db(y, FS, 4987) for n, y in y3.items()})

    def imd_db(y):
        f, X = q._tone_spectrum(y, FS)
        p7 = q._band_power(f, X, 7000)[0]
        side = sum(q._band_power(f, X, 7000 + k * 60, 4)[0] for k in (-3, -2, -1, 1, 2, 3))
        return 10 * np.log10(side / max(p7, 1e-30))

    ctx.metric("imd_db", {n: imd_db(y) for n, y in y4.items()})


def proc_pitch_tones(ctx, case):
    st = case.info["semitones"]
    x = S.sine(FS, 220, -12, 3.0)
    ys = ctx.render("sine220", x)
    ft = 220 * 2 ** (st / 12)
    err, jit, spur = {}, {}, {}
    for n, y in ys.items():
        y = y[int(0.3 * FS):]
        tr = q.pitch_track_cents(y, FS, ft)
        err[n] = abs(float(np.median(tr)))
        jit[n] = float(np.std(tr))
        spur[n] = q.aliasing_db(y, FS, ft)
    ctx.metric("pitch_err_cents", err)
    ctx.metric("pitch_jitter_cents", jit)
    ctx.metric("spurious_db", spur)
    ctx.metric("latency_ms", {n: ctx.latency.get(n, 0) / FS * 1000 for n in ys})


def proc_pitch_vocal(ctx, case):
    st = case.info["semitones"]
    renders = _vocal_renders(ctx)
    err, hnr = [], []
    for clip, x, ys in renders:
        fx = q.f0_track(x, FS)
        hx = q.hnr_db(x, FS)
        e, h = {}, {}
        for n, y in ys.items():
            fy = q.f0_track(y[:len(x)], FS)
            k = min(len(fx), len(fy))
            v = (fx[:k] > 0) & (fy[:k] > 0)
            cents = 1200 * np.log2(fy[:k][v] / fx[:k][v]) - 100 * st
            e[n] = float(np.median(np.abs(cents))) if np.any(v) else float("nan")
            h[n] = hx - q.hnr_db(y[:len(x)], FS)
        err.append(e), hnr.append(h)
    ctx.metric("vocal_pitch_err_cents", _mean_over_clips(err))
    ctx.metric("hnr_drop_db", _mean_over_clips(hnr))


def _am_roughness(y):
    m = q.mono(y)[int(0.5 * FS):]
    w = int(0.02 * FS)
    c = np.concatenate([[0], np.cumsum(m ** 2)])
    st = np.arange(0, len(m) - w, w // 2)
    e = 10 * np.log10((c[st + w] - c[st]) / w + 1e-20)
    return float(np.std(e))


def proc_mod_widen(ctx, case):
    x = S.pink_noise(FS, 8.0, -20.0)
    ys = ctx.render("pink", x)
    ctx.metric("comb_depth_db", {n: q.comb_depth_db(x, y, FS) for n, y in ys.items()})
    t = S.sine(FS, 440, -12, 4.0)
    yt = ctx.render("sine440", t)
    ctx.metric("am_roughness_db", {n: _am_roughness(y) for n, y in yt.items()})


def voice_tracking_values(xn, yn, xt, yt, f0, max_ms=80.0):
    """Solo voice: (median delay ms of the noise render, median pitch ct and pitch-track std ct of the tone render)."""
    tr = q.delay_track_ms(xn, yn, FS, min_ms=0.5, max_ms=max_ms)
    pt = q.pitch_track_cents(yt, FS, f0)[4:]
    return float(np.median(tr)), float(np.median(pt)), float(np.std(pt))


def proc_voice_tracking(ctx, case):
    xn = S.white_noise(FS, 6.0, -20.0)
    xt = S.sine(FS, 440, -12, 4.0)
    solo = case.info["solo"]
    derr, cerr, jit = [], [], []
    for i, (d_set, c_set) in enumerate(case.info["voices"]):
        h_extra = solo[HEADLINER](i)
        r_extra = {n: f(i) for n, f in solo.items() if n != HEADLINER}
        yn = ctx.render(f"noise_v{i + 1}", xn, headliner_extra=h_extra, ref_extra=r_extra, tag="_solo")
        yt = ctx.render(f"sine440_v{i + 1}", xt, headliner_extra=h_extra, ref_extra=r_extra, tag="_solo")
        dd, cc, jj = {}, {}, {}
        for n in yn:
            if n not in yt:
                continue
            d, c, j = voice_tracking_values(xn, yn[n], xt, yt[n], 440.0)
            dd[n], cc[n], jj[n] = abs(d - d_set), abs(c - c_set), j
            ctx.log(f"    {n} voice {i + 1}: delay {d:.2f} ms (set {d_set}), pitch {c:+.2f} ct (set {c_set:+}), jitter {j:.2f} ct")
        derr.append(dd), cerr.append(cc), jit.append(jj)
    ctx.metric("voice_delay_err_ms", _mean_over_clips(derr))
    ctx.metric("voice_detune_err_cents", _mean_over_clips(cerr))
    ctx.metric("voice_pitch_jitter_cents", _mean_over_clips(jit))


def proc_voice_splice(ctx, case):
    xt = S.sine(FS, 440, -12, 6.0)
    solo, sp = case.info["solo"], case.info["splice"]
    h_extra = dict(solo[HEADLINER](0), **sp[HEADLINER])
    r_extra = {n: f(0) + sp.get(n, []) for n, f in solo.items() if n != HEADLINER}
    ys = ctx.render("sine440_splice", xt, headliner_extra=h_extra, ref_extra=r_extra, tag="_solo")
    ctx.metric("voice_splice_db", {n: _am_roughness(y) for n, y in ys.items()}, note="+30 ct")


def _lr_corr(y):
    y = q.channels(y)
    if y.shape[1] < 2:
        return 1.0
    l, r = y[:, 0], y[:, 1]
    return float(np.sum(l * r) / math.sqrt(max(np.sum(l * l) * np.sum(r * r), 1e-30)))


def proc_vocal_width(ctx, case):
    renders = _vocal_renders(ctx)
    corr, loss = [], []
    for clip, x, ys in renders:
        corr.append({n: _lr_corr(y) for n, y in ys.items()})
        loss.append({n: q.integrated_lufs(y, FS) - q.integrated_lufs(np.stack([q.mono(y)] * 2, 1), FS) for n, y in ys.items()})
    ctx.metric("lr_corr", _mean_over_clips(corr))
    ctx.metric("mono_loss_db", _mean_over_clips(loss))


def proc_delay_ir(ctx, case):
    T = case.info["time_ms"] / 1000
    x = S.impulse(FS, 2.0)
    ys = ctx.render("impulse", x, tail=2.0)
    err, fb, smear, floor = {}, {}, {}, {}
    for n, y in ys.items():
        m = q.mono(y)
        e = m ** 2
        win = int(0.05 * FS)

        def echo(k):
            c = int(k * T * FS)
            a, b = max(0, c - win), min(len(e), c + win)
            seg = e[a:b]
            return a, seg

        a1, s1 = echo(1)
        a2, s2 = echo(2)
        if s1.sum() <= 0:
            continue
        cen1 = a1 + np.sum(np.arange(len(s1)) * s1) / s1.sum()
        err[n] = abs(cen1 / FS - T) * 1000
        fb[n] = 10 * np.log10(max(s2.sum(), 1e-30) / s1.sum())
        cs = np.cumsum(np.sort(s1)[::-1])
        smear[n] = np.searchsorted(cs, 0.9 * cs[-1]) / FS * 1000
        gap = e[int((1.25 * T) * FS):int((1.75 * T) * FS)]
        floor[n] = 10 * np.log10(max(gap.sum(), 1e-30) / s1.sum())
    ctx.metric("echo_time_err_ms", err)
    ctx.metric("feedback_db_per_repeat", fb, note=f"set feedback {case.info['feedback']:.0%}")
    ctx.metric("echo_smear_ms", smear)
    ctx.metric("inter_echo_floor_db", floor)


def proc_dry_leak(ctx, case):
    # short burst; at 100 % wet the output during the burst (before any echo / predelay) must be silent
    x = S.bursts(FS, 1.0, 1.0, 0.010, -12.0)
    ys = ctx.render("burst", x, tail=0.5, raw=True)   # Headliner raw output (no dry subtraction)
    leak = {}
    for n, y in ys.items():
        m = q.channels(y)
        seg = m[:int(0.012 * FS)]
        leak[n] = q.db(np.sum(seg ** 2) / max(np.sum(stereo(x)[:int(0.012 * FS)] ** 2), 1e-30))
    ctx.metric("dry_leak_db", leak, note="burst, 100 % wet")


def proc_reverb_ir(ctx, case):
    x = S.impulse(FS, 0.1)
    ys = ctx.render("impulse", x, tail=case.info["decay_s"] * 2.5 + 1.0)
    vals = {k: {} for k in ("t30_s", "t30_err_pct", "edt_s", "curvature_db", "mean_ned", "mixing_ms", "modal_ringing_db",
                            "tail_corr", "bass_ratio", "treble_ratio")}
    for n, y in ys.items():
        d = q.schroeder_decay(y, FS)
        ed = q.echo_density(y, FS)
        vals["t30_s"][n] = d["t30"]
        vals["t30_err_pct"][n] = abs(d["t30"] - case.info["decay_s"]) / case.info["decay_s"] * 100
        vals["edt_s"][n] = d["edt"]
        vals["curvature_db"][n] = d["curvature_db"]
        vals["mean_ned"][n] = ed["mean_ned"]
        vals["mixing_ms"][n] = ed["mixing_ms"]
        vals["modal_ringing_db"][n] = q.modal_ringing_db(y, FS)
        vals["tail_corr"][n] = q.tail_correlation(y, FS)
        b = d["t30_bands"]
        vals["bass_ratio"][n] = b.get(250, float("nan")) / b.get(1000, float("nan"))
        vals["treble_ratio"][n] = b.get(4000, float("nan")) / b.get(1000, float("nan"))
    for k, v in vals.items():
        ctx.metric(k, v, note=f"set decay {case.info['decay_s']} s" if k.startswith("t30") else "")


def proc_ride_vocal(ctx, case):
    renders = _vocal_renders(ctx)
    lstd, jit, tr = [], [], []
    for clip, x, ys in renders:
        a, b, c = {}, {}, {}
        for n, y in ys.items():
            yy = y[:len(x)]
            a[n] = _st_loudness_std(yy)
            t, g = q.gain_trace_db(x, yy, FS, win_ms=50, hop_ms=10)
            xm = q.mono(x)
            cx = np.concatenate([[0], np.cumsum(xm ** 2)])
            w = int(0.05 * FS)
            st = np.arange(0, len(xm) - w, int(0.01 * FS))
            lev = 10 * np.log10((cx[st + w] - cx[st]) / w + 1e-20)
            act = lev[:len(g)] > lev.max() - 30
            b[n] = q.track_jitter(g[act]) if np.count_nonzero(act) > 10 else float("nan")
            c[n] = q.transient_change_db(x, yy, FS)
        lstd.append(a), jit.append(b), tr.append(c)
    ctx.metric("st_loudness_std_lu", _mean_over_clips(lstd),
               note="dry: %.2f LU" % float(np.mean([_st_loudness_std(x) for _, x, _ in renders])))
    ctx.metric("gain_jitter_db", _mean_over_clips(jit))
    ctx.metric("transient_change_db", _mean_over_clips(tr))


def proc_gate_tone(ctx, case):
    x = S.sine(FS, 1000, -12, 4.0)
    ys = ctx.render("sine1k", x)
    spl, depth = {}, {}
    for n, y in ys.items():
        m = q.mono(y)[int(0.5 * FS):]
        f, p = sps.welch(m, FS, nperseg=8192)
        # clicks are broadband; smooth gate ramps keep their sidebands within a few kHz of the 1 kHz tone
        spl[n] = 10 * np.log10(max(p[f > 5000].sum(), 1e-30) / max(p.sum(), 1e-30))
        w = int(0.002 * FS)
        c = np.concatenate([[0], np.cumsum(m ** 2)])
        st = np.arange(0, len(m) - w, w)
        e = 10 * np.log10((c[st + w] - c[st]) / w + 1e-20)
        depth[n] = float(np.percentile(e, 2) - np.percentile(e, 98))
    ctx.metric("edge_splatter_db", spl)
    ctx.metric("gate_depth_db", depth)


def proc_check_null(ctx, case):
    x = S.load_clip(ctx.clips[0])
    ys = ctx.render(ctx.clips[0].name, x, sides=ctx.sides()[:1])
    ctx.metric("null_vs_input_db", {HEADLINER: q.null_db(ys[HEADLINER][:len(x)], stereo(x))})


def proc_nulls(ctx, case):
    x = S.load_clip(ctx.clips[0])
    h = ctx.sides()[:1]
    from_key = ctx.module.key
    on_param = {"eq": "eqOn", "comp": "compOn", "deess": "deessOn", "sat": "satOn", "doubler": "doublerOn", "pitch": "pitchOn",
                "delay": "delayOn", "reverb": "reverbOn", "chop": "chopOn", "pulse": "pulseOn", "smash": "smashOn",
                "choir": "choirOn", "smooth": "smoothOn", "check": "checkOn", "ride": "rideOn"}[from_key]
    lat = ctx.latency.get(HEADLINER, 0)
    if ctx.module.mix_param:
        y = ctx.render("mix0", x, sides=h, headliner_extra={ctx.module.mix_param: 0}, tag="_mix0")[HEADLINER]
        ctx.metric("mix0_null_db", {HEADLINER: q.null_db(y[:len(x)], stereo(x))}, note=f"latency {lat}")
    y = ctx.render("off", x, sides=h, headliner_extra={on_param: 0}, tag="_off")[HEADLINER]
    ctx.metric("off_null_db", {HEADLINER: q.null_db(y[:len(x)], stereo(x))})


def proc_cpu(ctx, case):
    ctx.metric("cpu_pct", {n: float(np.median(v)) for n, v in ctx.cpu.items()}, note="median over this module's renders")


PROCEDURES = {
    "eq_response": proc_eq_response, "eq_cramping": proc_eq_cramping, "dyn_curve": proc_dyn_curve, "dyn_timing": proc_dyn_timing, "thd_lf": proc_thd_lf,
    "vocal": proc_vocal, "vocal_listen": proc_vocal_listen, "deess_vocal": proc_deess_vocal,
    "deess_inactive_null": proc_deess_inactive_null, "resonance_vocal": proc_resonance_vocal, "sat_tones": proc_sat_tones,
    "pitch_tones": proc_pitch_tones, "pitch_vocal": proc_pitch_vocal, "mod_widen": proc_mod_widen,
    "voice_tracking": proc_voice_tracking, "voice_splice": proc_voice_splice,
    "vocal_width": proc_vocal_width, "delay_ir": proc_delay_ir, "dry_leak": proc_dry_leak, "reverb_ir": proc_reverb_ir,
    "ride_vocal": proc_ride_vocal, "gate_tone": proc_gate_tone, "check_null": proc_check_null, "nulls": proc_nulls,
    "cpu": proc_cpu,
}


# ---------------------------------------------------------------------------------------------
# Running and reporting
# ---------------------------------------------------------------------------------------------

def probe_refs(ctx):
    """Loads every reference once (parameter listing); a plugin that cannot be loaded is recorded and skipped."""
    for r in ctx.module.refs:
        cmd = [HOST, "--list", "--plugin", r.plugin] + (["--sub", r.sub] if r.sub else [])
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
            if p.returncode != 0:
                ctx.ref_errors[r.name] = f"not loadable (exit {p.returncode}): {(p.stderr or p.stdout).strip()[-200:]}"
        except subprocess.TimeoutExpired:
            ctx.ref_errors[r.name] = "not loadable: timeout (authorization dialog?)"
        if r.name in ctx.ref_errors:
            ctx.log(f"  reference {r.name}: {ctx.ref_errors[r.name]}")


def run_module(module: M.Module, out_root, clips=None, use_refs=True, jobs=4, log=print):
    out_dir = os.path.join(out_root, module.key)
    ctx = Ctx(module, out_dir, clips or S.CLIPS, use_refs, jobs, log)
    t0 = time.time()
    if use_refs:
        probe_refs(ctx)
    for case in module.cases:
        ctx.case = case
        log(f"[{module.key}] case {case.name}")
        calibrate(ctx, case)
        for p in case.procedures:
            log(f"  {p}")
            PROCEDURES[p](ctx, case)
    res = {"module": module.key, "title": module.title, "refs": [r.name for r in module.refs], "ref_errors": ctx.ref_errors,
           "calibrated": {c: {r: f"{k}={v}" for r, (k, v) in d.items()} for c, d in ctx.calibrated.items()},
           "latency": ctx.latency, "clips": [c.name for c in ctx.clips], "seconds": round(time.time() - t0, 1),
           "metrics": ctx.metrics}
    with open(os.path.join(out_dir, "metrics.json"), "w", encoding="utf-8") as fh:
        json.dump(res, fh, indent=1)
    with open(os.path.join(out_dir, "report.md"), "w", encoding="utf-8") as fh:
        fh.write(markdown(res))
    return res


def fmt(v, unit=""):
    if v is None:
        return "–"
    a = abs(v)
    s = f"{v:.0f}" if a >= 100 else f"{v:.1f}" if a >= 10 else f"{v:.2f}"
    return s


def markdown(res):
    refs = res["refs"]
    lines = [f"#### {res['title']} (`{res['module']}`)", ""]
    if res["ref_errors"]:
        lines += [f"Reference not loaded: {k} – {v}" for k, v in res["ref_errors"].items()] + [""]
    if res["calibrated"]:
        lines += ["Calibrated: " + "; ".join(f"{c}: " + ", ".join(f"{r} {kv}" for r, kv in d.items())
                                              for c, d in res["calibrated"].items()), ""]
    head = "| case | metric | Headliner | " + " | ".join(refs) + " | gap | tolerance | verdict |"
    lines += [head, "|" + "---|" * (len(refs) + 6)]
    for m in res["metrics"]:
        tol = ("≤" if m["better"] == "lower" else "≥" if m["better"] == "higher" else "±")
        if m["absolute"] is not None:
            tolstr = f"{tol} {fmt(m['absolute'])}" if m["better"] != "match" else f"{fmt(m['absolute'])} ± {fmt(m['tol'])}"
        elif m["better"] == "match":
            tolstr = f"ref ± {fmt(m['tol'])}"
        else:
            tolstr = f"ref {'+' if m['better'] == 'lower' else '−'} {fmt(m['tol'])}"
        name = f"{m['metric']} ({m['unit']})" if m["unit"] else m["metric"]
        if m["note"]:
            name += f" – {m['note']}"
        row = [m["case"], name, fmt(m["headliner"])] + [fmt(m["refs"].get(r)) for r in refs] + \
              [fmt(m["gap"]), tolstr, m["verdict"]]
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines) + "\n"


DOC = os.path.join(REPO, "docs", "QUALITY_BASELINE.md")


def update_doc(key, md, doc=DOC):
    """Replaces the text between '<!-- BEGIN key -->' and '<!-- END key -->' with the module's table."""
    with open(doc, encoding="utf-8") as fh:
        text = fh.read()
    b, e = f"<!-- BEGIN {key} -->", f"<!-- END {key} -->"
    if b not in text or e not in text:
        print(f"no markers for {key} in {doc}")
        return False
    head, rest = text.split(b, 1)
    _, tail = rest.split(e, 1)
    with open(doc, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(head + b + "\n" + md + e + tail)
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("module", help="module key (" + ", ".join(M.MODULES) + ") or 'all'")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--clips", type=int, default=len(S.CLIPS), help="use the first N clips")
    ap.add_argument("--no-ref", action="store_true", help="Headliner only (quick re-measure)")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--update-doc", action="store_true",
                    help="replace this module's table in docs/QUALITY_BASELINE.md (between its BEGIN/END markers)")
    a = ap.parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except AttributeError:
        pass
    if not os.path.exists(HOST):
        sys.exit(f"HeadlinerRefHost not found at {HOST} (build target HeadlinerRefHost or set REFHOST_EXE)")
    keys = list(M.MODULES) if a.module == "all" else [a.module]
    summary = []
    for k in keys:
        res = run_module(M.MODULES[k], a.out, S.CLIPS[:a.clips], not a.no_ref, a.jobs)
        md = markdown(res)
        print(md)
        summary.append(md)
        if a.update_doc:
            update_doc(k, md)
        fails = sum(m["verdict"] == "FAIL" for m in res["metrics"])
        print(f"[{k}] {len(res['metrics'])} metrics, {fails} FAIL, {res['seconds']} s")
    if a.module == "all":
        with open(os.path.join(a.out, "summary.md"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(summary))


if __name__ == "__main__":
    main()
