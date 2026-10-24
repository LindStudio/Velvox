"""Quality metrics for Headliner modules vs. reference plugins (Headliner 0.6, spec section 6).

All functions take float arrays shaped (n,) or (n, channels) and the sample rate. Stereo signals are
reduced to mono where a metric is defined on one signal (mean of the channels) unless noted.
Tested in tools/refhost/tests/test_qmetrics.py.
"""
from __future__ import annotations

import math
import warnings

import numpy as np
from scipy import signal
from scipy.io import wavfile

EPS = 1e-30


# ---------------------------------------------------------------------------------------------
# IO and helpers
# ---------------------------------------------------------------------------------------------

def read_wav(path):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", wavfile.WavFileWarning)
        fs, x = wavfile.read(path)
    if x.dtype == np.int16:
        x = x / 32768.0
    elif x.dtype == np.int32:
        x = x / 2147483648.0
    elif x.dtype == np.uint8:
        x = (x - 128.0) / 128.0
    return np.asarray(x, dtype=np.float64), int(fs)


def write_wav(path, x, fs):
    wavfile.write(path, int(fs), np.asarray(x, dtype=np.float32))


def mono(x):
    x = np.asarray(x, dtype=np.float64)
    return x.mean(axis=1) if x.ndim == 2 else x


def channels(x):
    x = np.asarray(x, dtype=np.float64)
    return x[:, None] if x.ndim == 1 else x


def trim_pair(a, b):
    n = min(len(a), len(b))
    return a[:n], b[:n]


def db(p, floor=-300.0):
    return max(floor, 10.0 * math.log10(max(float(p), EPS)))


def _logsmooth_db(f, mag_db, fraction, along=-1):
    """Average of dB values over +-fraction/2 octave around each frequency (cumulative-sum trick)."""
    lo = np.searchsorted(f, f * 2 ** (-fraction / 2))
    hi = np.searchsorted(f, f * 2 ** (fraction / 2), side="right")
    hi = np.maximum(hi, lo + 1)
    c = np.cumsum(mag_db, axis=along)
    c = np.concatenate([np.zeros_like(np.take(c, [0], axis=along)), c], axis=along)
    width = (hi - lo).astype(float)
    if mag_db.ndim == 2 and along in (0, -2):
        width = width[:, None]
    return (np.take(c, hi, axis=along) - np.take(c, lo, axis=along)) / width


# ---------------------------------------------------------------------------------------------
# Loudness (ITU-R BS.1770-4, gated integrated loudness)
# ---------------------------------------------------------------------------------------------

def _k_weighting(fs):
    """BS.1770 K-weighting for any rate (bilinear form that reproduces the standard 48 kHz coefficients)."""
    G, Q, fc = 3.999843853973347, 0.7071752369554196, 1681.974450955533
    K = np.tan(np.pi * fc / fs)
    Vh = 10 ** (G / 20)
    Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b1 = np.array([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0])
    a1 = np.array([1.0, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0])
    Q2, fc2 = 0.5003270373238773, 38.13547087602444
    K = np.tan(np.pi * fc2 / fs)
    a0 = 1 + K / Q2 + K * K
    b2 = np.array([1.0, -2.0, 1.0])
    a2 = np.array([1.0, 2 * (K * K - 1) / a0, (1 - K / Q2 + K * K) / a0])
    return (b1, a1), (b2, a2)


def integrated_lufs(x, fs):
    x = channels(x)
    if not np.any(x):
        return -np.inf
    (b1, a1), (b2, a2) = _k_weighting(fs)
    y = signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)
    block, hop = int(0.4 * fs), int(0.1 * fs)
    if len(y) < block:
        z = np.mean(y ** 2, axis=0)[None, :]
    else:
        starts = np.arange(0, len(y) - block + 1, hop)
        c = np.concatenate([np.zeros((1, y.shape[1])), np.cumsum(y ** 2, axis=0)])
        z = (c[starts + block] - c[starts]) / block
    power = z.sum(axis=1)
    loud = -0.691 + 10 * np.log10(np.maximum(power, EPS))
    keep = loud > -70.0
    if not np.any(keep):
        return -np.inf
    rel = -0.691 + 10 * np.log10(np.mean(power[keep])) - 10.0
    keep &= loud > rel
    if not np.any(keep):
        return -np.inf
    return float(-0.691 + 10 * np.log10(np.mean(power[keep])))


def match_gain_db(ref, x, fs):
    """Gain in dB that makes x as loud as ref (0 when either is silent)."""
    lr, lx = integrated_lufs(ref, fs), integrated_lufs(x, fs)
    if not (np.isfinite(lr) and np.isfinite(lx)):
        return 0.0
    return float(lr - lx)


def apply_gain_db(x, g):
    return np.asarray(x) * 10 ** (g / 20)


# ---------------------------------------------------------------------------------------------
# Distortion
# ---------------------------------------------------------------------------------------------

def _tone_spectrum(y, fs):
    y = mono(y)
    n = len(y)
    seg = y[n // 4: n // 4 + 2 ** int(math.floor(math.log2(max(n // 2, 1024))))]
    w = signal.windows.blackmanharris(len(seg))
    X = np.abs(np.fft.rfft(seg * w)) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / fs)
    return f, X


def _band_power(f, X, fc, half_bins=8):
    k = int(round(fc / (f[1] - f[0])))
    return float(X[max(0, k - half_bins): k + half_bins + 1].sum()), (max(0, k - half_bins), k + half_bins + 1)


def thd_db(y, fs, f0, max_harmonic=40):
    """Total harmonic distortion (harmonics 2.. below Nyquist) relative to the fundamental, dB."""
    f, X = _tone_spectrum(y, fs)
    p1, _ = _band_power(f, X, f0)
    ph = sum(_band_power(f, X, k * f0)[0] for k in range(2, max_harmonic + 1) if k * f0 < fs / 2 - 100)
    return db(ph / max(p1, EPS))


def aliasing_db(y, fs, f0, max_harmonic=200):
    """Energy outside DC, the fundamental and its (non-aliased) harmonics, relative to the fundamental, dB."""
    f, X = _tone_spectrum(y, fs)
    p1, _ = _band_power(f, X, f0)
    mask = np.ones(len(X), bool)
    mask[f < 20] = False
    for k in range(1, max_harmonic + 1):
        if k * f0 >= fs / 2:
            break
        _, (a, b) = _band_power(f, X, k * f0)
        mask[a:b] = False
    return db(X[mask].sum() / max(p1, EPS))


# ---------------------------------------------------------------------------------------------
# Dynamics
# ---------------------------------------------------------------------------------------------

def static_curve(y, fs, step, nsteps, settle=0.5):
    """RMS output level (dBFS) in the settled part of each level step."""
    y = channels(y)
    out = []
    for k in range(nsteps):
        a = k * step + int(step * settle)
        b = (k + 1) * step - int(0.05 * step)
        out.append(db(np.mean(y[a:b] ** 2)))
    return np.array(out)


def curve_ratio_threshold(levels, out, top=3):
    """Ratio (from the slope of the top points) and threshold (intersection with the unity-slope line)."""
    levels, out = np.asarray(levels, float), np.asarray(out, float)
    g0 = out[0] - levels[0]
    a, b = np.polyfit(levels[-top:], out[-top:], 1)
    if a >= 0.98:
        return 1.0, float("nan")
    return float(1.0 / max(a, 1e-6)), float((b - g0) / (1 - a))


def gain_trace_db(x, y, fs, win_ms=2.0, hop_ms=0.5):
    x, y = trim_pair(mono(x), mono(y))
    w, h = max(1, int(win_ms * fs / 1000)), max(1, int(hop_ms * fs / 1000))
    cx = np.concatenate([[0], np.cumsum(x ** 2)])
    cy = np.concatenate([[0], np.cumsum(y ** 2)])
    starts = np.arange(0, len(x) - w, h)
    ex = (cx[starts + w] - cx[starts]) / w
    ey = (cy[starts + w] - cy[starts]) / w
    t = (starts + w / 2) / fs
    return t, 10 * np.log10(np.maximum(ey, EPS) / np.maximum(ex, EPS))


def attack_release_ms(x, y, fs, i_up, i_down):
    """63 % time constants of the gain change after a level step up (sample i_up) and back down (i_down)."""
    t, g = gain_trace_db(x, y, fs)
    tu, td = i_up / fs, i_down / fs
    before = np.median(g[(t > tu - 0.2) & (t < tu - 0.005)])
    held = np.median(g[(t > td - 0.1) & (t < td - 0.005)])
    end = np.median(g[t > t[-1] - 0.15])

    def crossing(t0, g_from, g_to, t_end):
        if abs(g_to - g_from) < 0.5:
            return float("nan")
        target = g_from + 0.632 * (g_to - g_from)
        sel = (t > t0) & (t < t_end)
        tt, gg = t[sel], g[sel]
        hit = np.nonzero((gg - target) * np.sign(g_to - g_from) >= 0)[0]
        return float((tt[hit[0]] - t0) * 1000) if len(hit) else float("nan")

    return crossing(tu, before, held, td), crossing(td, held, end, t[-1])


# ---------------------------------------------------------------------------------------------
# Spectra, EQ response, nulls
# ---------------------------------------------------------------------------------------------

def freq_response(x, y, fs, nperseg=8192, grid=None):
    """H1 transfer function x->y (dB) on a 1/24-octave grid 20 Hz..20 kHz."""
    x, y = trim_pair(mono(x), mono(y))
    f, pxy = signal.csd(x, y, fs, nperseg=nperseg)
    _, pxx = signal.welch(x, fs, nperseg=nperseg)
    H = np.abs(pxy) / np.maximum(pxx, EPS)
    if grid is None:
        grid = np.geomspace(20, min(20000, fs / 2 * 0.95), 24 * 10)
    out = np.interp(grid, f, 20 * np.log10(np.maximum(H, 1e-12)))
    return grid, out


def response_deviation(f, a_db, b_db):
    d = np.asarray(b_db) - np.asarray(a_db)
    sel = (f >= 20) & (f <= 20000)
    hf = f > 10000
    return {"max_db": float(np.max(np.abs(d[sel]))), "rms_db": float(np.sqrt(np.mean(d[sel] ** 2))),
            "hf_max_db": float(np.max(np.abs(d[hf]))) if np.any(hf) else 0.0}


def null_db(a, b):
    """Residual a-b relative to b, dB."""
    a, b = trim_pair(channels(a), channels(b))
    ch = min(a.shape[1], b.shape[1])
    a, b = a[:, :ch], b[:, :ch]
    return db(np.sum((a - b) ** 2) / max(np.sum(b ** 2), EPS))


def _stft(x, fs, nperseg=2048, hop=512):
    return signal.stft(mono(x), fs, window="hann", nperseg=nperseg, noverlap=nperseg - hop, boundary=None, padded=True)


def _istft(Z, fs, n, nperseg=2048, hop=512):
    _, y = signal.istft(Z, fs, window="hann", nperseg=nperseg, noverlap=nperseg - hop, boundary=False)
    y = np.asarray(y, float)
    return np.pad(y, (0, max(0, n - len(y))))[:n]


def ltas_peakiness_db(x, fs, fmin=1000.0, fmax=10000.0):
    """Largest stationary spectral peak: long-term spectrum (Welch, 5.9 Hz bins) above its 1/2-oct envelope (dB)."""
    f, p = signal.welch(mono(x), fs, nperseg=8192)
    sel = f > 10
    f, p = f[sel], p[sel]
    fine = 10 * np.log10(np.maximum(p, EPS))
    env = _logsmooth_db(f, fine, 0.5)
    band = (f >= fmin) & (f <= fmax)
    return float(np.max((fine - env)[band]))


def peak_to_envelope_db(x, fs, fmin=1000.0, fmax=10000.0, top=10):
    """Mean over active frames of the mean of the `top` largest STFT peaks above a 1/3-oct envelope (dB)."""
    f, _, Z = _stft(x, fs)
    mag = 20 * np.log10(np.abs(Z) + 1e-12)
    sel = f > 20
    f, mag = f[sel], mag[sel]
    env = _logsmooth_db(f, mag, 1 / 3, along=0)
    band = (f >= fmin) & (f <= fmax)
    excess = (mag - env)[band]
    energy = 10 * np.log10(np.sum(np.abs(Z) ** 2, axis=0) + EPS)
    active = energy > energy.max() - 40
    if not np.any(active):
        return 0.0
    ex = np.sort(excess[:, active], axis=0)[-top:]
    return float(np.mean(ex))


def spectral_flatness_db(x, fs, fmin=1000.0, fmax=10000.0):
    """Mean over active frames of geometric/arithmetic mean of the power spectrum in [fmin, fmax] (dB, <= 0)."""
    f, _, Z = _stft(x, fs)
    P = np.abs(Z) ** 2 + 1e-20
    band = (f >= fmin) & (f <= fmax)
    energy = P.sum(axis=0)
    active = 10 * np.log10(energy + EPS) > 10 * np.log10(energy.max() + EPS) - 40
    Pb = P[band][:, active]
    fl = np.exp(np.mean(np.log(Pb), axis=0)) / np.mean(Pb, axis=0)
    return float(np.mean(10 * np.log10(fl)))


def delta_energy_db(dry, wet):
    """Energy of what the process removed/added (dry - wet) relative to dry, dB."""
    a, b = trim_pair(channels(dry), channels(wet))
    ch = min(a.shape[1], b.shape[1])
    return db(np.sum((a[:, :ch] - b[:, :ch]) ** 2) / max(np.sum(a[:, :ch] ** 2), EPS))


def musical_noise_db(dry, wet, fs, fmin=200.0, fmax=16000.0, range_db=40.0):
    """Mean frame-to-frame and bin-to-bin change of the spectral gain mask wet/dry (dB). 0 = static gain."""
    d, w = trim_pair(mono(dry), mono(wet))
    f, _, D = _stft(d, fs)
    _, _, W = _stft(w, fs)
    G = 20 * np.log10(np.abs(W) + 1e-9) - 20 * np.log10(np.abs(D) + 1e-9)
    Ddb = 20 * np.log10(np.abs(D) + 1e-12)
    valid = (Ddb > Ddb.max(axis=0, keepdims=True) - range_db) & (Ddb > Ddb.max() - 80)
    band = (f >= fmin) & (f <= fmax)
    valid &= band[:, None]
    vt = valid[:, 1:] & valid[:, :-1]
    vf = valid[1:, :] & valid[:-1, :]
    dt = np.abs(np.diff(G, axis=1))[vt]
    dfq = np.abs(np.diff(G, axis=0))[vf]
    if dt.size == 0 or dfq.size == 0:
        return 0.0
    return float(0.5 * (dt.mean() + dfq.mean()))


def _onsets(e, hop_s, rise_db=6.0, lag=2, refractory_s=0.05):
    edb = 10 * np.log10(e + EPS)
    floor = edb.max() - 40
    idx, last = [], -10 ** 9
    for i in range(lag, len(edb)):
        if edb[i] > floor and edb[i] - edb[i - lag] >= rise_db and (i - last) * hop_s >= refractory_s:
            idx.append(i)
            last = i
    return idx


def transient_change_db(dry, wet, fs):
    """Mean change (wet - dry) of the onset-to-sustain energy ratio: first 10 ms vs 30-60 ms after each onset.
    0 = transients preserved, negative = flattened, positive = emphasised."""
    d, w = trim_pair(mono(dry), mono(wet))
    hop = int(0.0025 * fs)
    win = int(0.005 * fs)
    starts = np.arange(0, len(d) - win, hop)
    cd = np.concatenate([[0], np.cumsum(d ** 2)])
    e = (cd[starts + win] - cd[starts]) / win
    ons = _onsets(e, hop / fs)
    cw = np.concatenate([[0], np.cumsum(w ** 2)])

    def ratio(c, i0):
        a = c[min(len(c) - 1, i0 + int(0.010 * fs))] - c[i0]
        b = c[min(len(c) - 1, i0 + int(0.060 * fs))] - c[min(len(c) - 1, i0 + int(0.030 * fs))]
        return 10 * np.log10((a / 0.010 + EPS) / (b / 0.030 + EPS))

    diffs = []
    for i in ons:
        i0 = max(0, starts[i] - hop)
        if i0 + int(0.06 * fs) >= len(d):
            continue
        diffs.append(ratio(cw, i0) - ratio(cd, i0))
    return float(np.mean(diffs)) if diffs else 0.0


# ---------------------------------------------------------------------------------------------
# Reverb
# ---------------------------------------------------------------------------------------------

def _onset_index(h, rel=0.01):
    a = np.abs(h)
    return int(np.argmax(a > rel * a.max())) if a.max() > 0 else 0


def _edc_db(h):
    e = np.cumsum((h ** 2)[::-1])[::-1]
    return 10 * np.log10(np.maximum(e, EPS) / max(e[0], EPS))


def _fit_decay(t, edc, hi, lo):
    sel = (edc <= hi) & (edc >= lo)
    if np.count_nonzero(sel) < 10:
        return float("nan"), float("nan")
    a, b = np.polyfit(t[sel], edc[sel], 1)
    resid = edc[sel] - (a * t[sel] + b)
    return float(-60.0 / a) if a < 0 else float("nan"), float(np.sqrt(np.mean(resid ** 2)))


def schroeder_decay(ir, fs, bands=(250, 500, 1000, 2000, 4000, 8000)):
    """EDT, T20, T30 (s) broadband, T30 per octave band, and the curvature (dB RMS off a line, -5..-35 dB)."""
    x = channels(ir)
    h = np.sqrt(np.sum(x ** 2, axis=1))  # energy sum of the channels
    h = h[_onset_index(h):]
    edc = _edc_db(h)
    t = np.arange(len(h)) / fs
    edt, _ = _fit_decay(t, edc, 0.0, -10.0)
    t20, _ = _fit_decay(t, edc, -5.0, -25.0)
    t30, curv = _fit_decay(t, edc, -5.0, -35.0)
    per_band = {}
    m = mono(ir)
    m = m[_onset_index(m):]
    for fc in bands:
        if fc * 1.414 >= fs / 2:
            continue
        sos = signal.butter(3, [fc / 1.414, fc * 1.414], btype="band", fs=fs, output="sos")
        hb = signal.sosfilt(sos, m)
        tb = np.arange(len(hb)) / fs
        per_band[fc] = _fit_decay(tb, _edc_db(hb), -5.0, -35.0)[0]
    return {"edt": edt, "t20": t20, "t30": t30, "curvature_db": curv, "t30_bands": per_band}


def echo_density(ir, fs, win_ms=20.0, hop_ms=1.0, span_ms=(20.0, 300.0)):
    """Abel-Huang normalized echo density (1 = Gaussian-like): mean over span after the onset, and the
    mixing time (first time NED >= 0.9, ms after the onset)."""
    h = channels(ir)[:, 0]
    h = h[_onset_index(h):]
    w, hop = int(win_ms * fs / 1000), int(hop_ms * fs / 1000)
    starts = np.arange(0, max(1, len(h) - w), hop)
    ned = np.empty(len(starts))
    norm = math.erfc(1 / math.sqrt(2))
    for k, s in enumerate(starts):
        seg = h[s:s + w]
        sd = seg.std()
        ned[k] = np.mean(np.abs(seg - seg.mean()) > sd) / norm if sd > 0 else 0.0
    t_ms = (starts + w / 2) / fs * 1000
    sel = (t_ms >= span_ms[0]) & (t_ms <= span_ms[1])
    hit = np.nonzero(ned >= 0.9)[0]
    return {"mean_ned": float(ned[sel].mean()) if np.any(sel) else float(ned.mean()),
            "mixing_ms": float(t_ms[hit[0]]) if len(hit) else float("nan")}


def modal_ringing_db(ir, fs, start_ms=100.0, length_s=1.0, fmin=100.0, fmax=8000.0):
    """Strongest narrow spectral peak of the decay-compensated late tail above its 1/3-oct median (dB).
    High values = metallic / modal ringing."""
    h = mono(ir)
    h = h[_onset_index(h):]
    a = int(start_ms * fs / 1000)
    seg = h[a:a + int(length_s * fs)]
    if len(seg) < 8192:
        return float("nan")
    # flatten the decay with a smoothed envelope
    env = np.sqrt(np.convolve(seg ** 2, np.ones(2400) / 2400, mode="same")) + 1e-12
    seg = seg / env
    f, p = signal.welch(seg, fs, nperseg=8192)
    sel = f > 10
    f, pdb = f[sel], 10 * np.log10(np.maximum(p[sel], EPS))
    med = _logsmooth_db(f, pdb, 1 / 3)
    band = (f >= fmin) & (f <= fmax)
    return float(np.max((pdb - med)[band]))


def tail_correlation(ir, fs, start_ms=50.0):
    """L/R correlation of the reverb tail (stereo IR), 1 = mono."""
    x = channels(ir)
    if x.shape[1] < 2:
        return 1.0
    a = int(start_ms * fs / 1000) + _onset_index(mono(x))
    l, r = x[a:, 0], x[a:, 1]
    den = math.sqrt(float(np.sum(l ** 2) * np.sum(r ** 2)))
    return float(np.sum(l * r) / den) if den > 0 else 1.0


# ---------------------------------------------------------------------------------------------
# Modulation, combing, pitch
# ---------------------------------------------------------------------------------------------

def delay_track_ms(x, y, fs, frame=4096, hop=1024, min_ms=0.0, max_ms=60.0):
    """Per-frame lag (ms) of the cross-correlation peak x->y, with parabolic interpolation."""
    x, y = trim_pair(mono(x), mono(y))
    lo, hi = int(min_ms * fs / 1000), int(max_ms * fs / 1000)
    win = np.hanning(frame)
    out = []
    nfft = 2 * frame
    for s in range(0, len(x) - frame, hop):
        a, b = x[s:s + frame] * win, y[s:s + frame] * win
        if np.sum(a ** 2) < 1e-10:
            continue
        r = np.fft.irfft(np.conj(np.fft.rfft(a, nfft)) * np.fft.rfft(b, nfft), nfft)
        seg = r[lo:hi + 1]
        k = int(np.argmax(seg))
        frac = 0.0
        if 0 < k < len(seg) - 1:
            y0, y1, y2 = seg[k - 1], seg[k], seg[k + 1]
            den = y0 - 2 * y1 + y2
            frac = 0.5 * (y0 - y2) / den if den != 0 else 0.0
        out.append((lo + k + frac) / fs * 1000)
    return np.array(out)


def track_jitter(track):
    """RMS of the second difference of a track (unit per frame^2): 0 = perfectly smooth."""
    track = np.asarray(track, float)
    return float(np.sqrt(np.mean(np.diff(track, 2) ** 2))) if len(track) > 3 else 0.0


def pitch_track_cents(y, fs, ref_hz, frame=8192, hop=1024, search_cents=300):
    """Frequency of the strongest peak near ref_hz per frame, in cents relative to ref_hz."""
    y = mono(y)
    win = np.hanning(frame)
    nfft = frame * 4
    f = np.fft.rfftfreq(nfft, 1 / fs)
    lo, hi = ref_hz * 2 ** (-search_cents / 1200), ref_hz * 2 ** (search_cents / 1200)
    sel = np.nonzero((f >= lo) & (f <= hi))[0]
    out = []
    for s in range(0, len(y) - frame, hop):
        X = np.log(np.abs(np.fft.rfft(y[s:s + frame] * win, nfft)) + 1e-12)
        k = sel[np.argmax(X[sel])]
        y0, y1, y2 = X[k - 1], X[k], X[k + 1]
        den = y0 - 2 * y1 + y2
        frac = 0.5 * (y0 - y2) / den if den != 0 else 0.0
        out.append(1200 * math.log2((k + frac) * (f[1] - f[0]) / ref_hz))
    return np.array(out)


def comb_depth_db(dry, wet, fs, fmin=200.0, fmax=8000.0):
    """Peak-to-notch range (95th - 5th percentile, dB) of the mono-sum transfer function dry->wet after
    removing its 1/3-oct trend. 0 = no combing / phasing."""
    d, w = trim_pair(mono(dry), mono(wet))
    f, pxy = signal.csd(d, w, fs, nperseg=4096)
    _, pxx = signal.welch(d, fs, nperseg=4096)
    sel = f > 20
    f = f[sel]
    H = 20 * np.log10(np.maximum(np.abs(pxy[sel]) / np.maximum(pxx[sel], EPS), 1e-6))
    resid = H - _logsmooth_db(f, H, 1 / 3)
    band = (f >= fmin) & (f <= fmax)
    return float(np.percentile(resid[band], 95) - np.percentile(resid[band], 5))


def f0_track(y, fs, frame=2048, hop=256, fmin=60.0, fmax=1000.0, threshold=0.15):
    """YIN f0 per frame (Hz, 0 = unvoiced)."""
    y = mono(y)
    tmax, tmin = int(fs / fmin), int(fs / fmax)
    W = frame - tmax
    out = []
    for s in range(0, len(y) - frame, hop):
        x = y[s:s + frame]
        if np.sum(x ** 2) < 1e-8:
            out.append(0.0)
            continue
        c = np.concatenate([[0], np.cumsum(x ** 2)])
        nfft = 1 << (2 * frame - 1).bit_length()
        r = np.fft.irfft(np.conj(np.fft.rfft(x[:W], nfft)) * np.fft.rfft(x, nfft), nfft)[:tmax + 1]
        taus = np.arange(tmax + 1)
        d = c[W] + (c[taus + W] - c[taus]) - 2 * r
        d[0] = 0
        cm = np.cumsum(d[1:]) / np.arange(1, tmax + 1)
        dn = np.ones(tmax + 1)
        dn[1:] = d[1:] / np.maximum(cm, EPS)
        cand = np.nonzero(dn[tmin:tmax] < threshold)[0]
        if len(cand) == 0:
            out.append(0.0)
            continue
        k = cand[0] + tmin
        while k + 1 < tmax and dn[k + 1] < dn[k]:
            k += 1
        y0, y1, y2 = dn[k - 1], dn[k], dn[k + 1]
        den = y0 - 2 * y1 + y2
        frac = 0.5 * (y0 - y2) / den if den != 0 else 0.0
        out.append(fs / (k + frac))
    return np.array(out)


def hnr_db(y, fs, frame=2048, hop=512):
    """Median harmonics-to-noise ratio of voiced frames from the normalized autocorrelation at the f0 lag."""
    y = mono(y)
    f0 = f0_track(y, fs, frame=frame, hop=hop, threshold=0.6)
    vals = []
    for i, f in enumerate(f0):
        if f <= 0:
            continue
        s = i * hop
        x = y[s:s + frame]
        lag = int(round(fs / f))
        a, b = x[:-lag], x[lag:]
        den = math.sqrt(float(np.sum(a ** 2) * np.sum(b ** 2)))
        if den <= 0:
            continue
        r = min(0.999999, max(1e-6, float(np.sum(a * b) / den)))
        vals.append(10 * math.log10(r / (1 - r)))
    return float(np.median(vals)) if vals else float("nan")


def cpu_percent(mean_block_us, block, fs):
    """Share of real time one block takes (percent)."""
    return 100.0 * mean_block_us * 1e-6 / (block / fs)
