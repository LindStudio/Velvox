"""Genre stand-ins for the preset bench (r2/ember-presets, 2026-11-23).
There is no real hypertechno / EDM / trap vocal material, so real dry takes are processed the way such vocals are mixed
before a de-esser sees them:
  club  (hypertechno, hard dance, EDM, dance pop): HP 120 Hz, compressor 8:1 (RMS 1 ms frames, attack 1 ms,
        release 40 ms, ~10-12 dB GR on loud phrases), +3 dB bell at 3.5 kHz, +6 dB high shelf at 8 kHz, soft clip, -1 dBFS
  trap  (trap, modern hip-hop): HP 80 Hz, compressor 5:1 (attack 3 ms, release 80 ms, ~8 dB GR), +4 dB high shelf
        at 10 kHz ("air"), -1 dBFS
The first 90 s of each take. Output: data/genre/G_<take>_<style>.wav
"""
import os, sys
import numpy as np, soundfile as sf
from scipy.signal import lfilter

S = os.path.dirname(os.path.abspath(__file__))
OUT = S + '/data/genre'
os.makedirs(OUT, exist_ok=True)


def biquad(kind, f0, fs, gain_db=0.0, q=0.707):
    A = 10 ** (gain_db / 40)
    w = 2 * np.pi * f0 / fs
    c, s = np.cos(w), np.sin(w)
    al = s / (2 * q)
    if kind == 'hp':
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]; a = [1 + al, -2 * c, 1 - al]
    elif kind == 'peak':
        b = [1 + al * A, -2 * c, 1 - al * A]; a = [1 + al / A, -2 * c, 1 - al / A]
    elif kind == 'hs':
        sq = 2 * np.sqrt(A) * al
        b = [A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq)]
        a = [(A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq]
    b, a = np.array(b) / a[0], np.array(a) / a[0]
    return b, a


def comp(x, fs, thr_db, ratio, att_ms, rel_ms):
    hop = int(fs / 1000)
    n = len(x) // hop
    mono = x[:n * hop].mean(axis=1) if x.ndim > 1 else x[:n * hop]
    rms = np.sqrt((mono.reshape(n, hop) ** 2).mean(axis=1) + 1e-12)
    lvl = 20 * np.log10(rms)
    want = np.maximum(0.0, (lvl - thr_db) * (1 - 1 / ratio))
    ga, gr = np.exp(-1 / att_ms), np.exp(-1 / rel_ms)
    g = np.zeros(n)
    s = 0.0
    for i in range(n):
        k = ga if want[i] > s else gr
        s = k * s + (1 - k) * want[i]
        g[i] = s
    gs = np.interp(np.arange(len(x)) / hop, np.arange(n) + 0.5, g)
    gain = 10 ** (-gs / 20)
    return x * (gain[:, None] if x.ndim > 1 else gain), float(np.percentile(g[lvl > lvl.max() - 30], 90))


def make(take, style):
    x, fs = sf.read(S + '/data/takes/' + take + '.wav', dtype='float64', always_2d=True)
    x = x[:int(90 * fs)]
    peak_db = 20 * np.log10(np.percentile(np.abs(x), 99.9) + 1e-12)
    if style == 'club':
        chain = [('hp', 120, 0, 0.707)]
        thr, ratio, att, rel = peak_db - 22, 8.0, 1.0, 40.0
        post = [('peak', 3500, 3.0, 0.8), ('hs', 8000, 6.0, 0.707)]
    else:
        chain = [('hp', 80, 0, 0.707)]
        thr, ratio, att, rel = peak_db - 18, 5.0, 3.0, 80.0
        post = [('hs', 10000, 4.0, 0.707)]
    for k, f0, g, q in chain:
        b, a = biquad(k, f0, fs, g, q); x = lfilter(b, a, x, axis=0)
    x, gr = comp(x, fs, thr, ratio, att, rel)
    for k, f0, g, q in post:
        b, a = biquad(k, f0, fs, g, q); x = lfilter(b, a, x, axis=0)
    x = x / (np.abs(x).max() + 1e-12) * 1.2
    if style == 'club':
        x = np.tanh(x)            # soft clip the few loudest peaks (limiter stand-in)
    x = x / np.abs(x).max() * 10 ** (-1 / 20)
    name = 'G_%s_%s' % (take, style)
    sf.write(OUT + '/' + name + '.wav', x.astype(np.float32), fs, subtype='FLOAT')
    print(name, 'GR p90 %.1f dB' % gr)


if __name__ == '__main__':
    for t in ['F01', 'F22', 'F_runwild', 'M_notme', 'J1_chemicals']:
        make(t, 'club')
    for t in ['M36', 'M40', 'M_emerald', 'M_allittakes']:
        make(t, 'trap')
