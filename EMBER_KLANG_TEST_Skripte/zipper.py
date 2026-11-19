"""Zipper check: EMBER's split shelf is recomputed every 16 samples (step-wise gain). Compare a re-implementation
with the same 16-sample steps (must match the engine) against one with per-sample coefficients."""
import sys, subprocess, re
S = sys.argv[1]; sys.path.insert(0, S)
import numpy as np, soundfile as sf
E = 'C:/Users/Shadow/dev/velvox-wt/ember-sound-audit/build/EmberTests_artefacts/Release/EmberTests.exe'
take, secs, extra = sys.argv[2], float(sys.argv[3]), sys.argv[4:]
x, fs = sf.read(S + '/data/takes/%s.wav' % take, dtype='float64', always_2d=True)
x = x.mean(axis=1)
# excerpt with many S: start at 20 s
x = x[int(20 * fs): int((20 + secs) * fs)]
sf.write(S + '/synth/z_in.wav', x.astype(np.float32), fs, subtype='FLOAT')
log = subprocess.run([E, '--process', S + '/synth/z_in.wav', S + '/synth/z_out.wav', 'trace=' + S + '/synth/z_tr.wav'] + extra,
                     capture_output=True, text=True).stdout
freq = float(re.search(r'SETTINGS freq (\d+)', log).group(1))
L = int(re.search(r'LATENCY (\d+)', log).group(1))
y, _ = sf.read(S + '/synth/z_out.wav', dtype='float64')
tr, _ = sf.read(S + '/synth/z_tr.wav', dtype='float64')
xin = sf.read(S + '/synth/z_in.wav', dtype='float32')[0].astype(np.float64)


def shelf(g_db):
    A = 10 ** (-g_db / 40); w0 = 2 * np.pi * min(freq, fs * 0.45) / fs; cw = np.cos(w0); al = np.sin(w0) / (2 * 0.70710678); sq = 2 * np.sqrt(A) * al
    b0 = A * ((A + 1) + (A - 1) * cw + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sq)
    a0 = (A + 1) - (A - 1) * cw + sq; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sq
    return b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0


def render(gains):
    out = np.empty_like(xin); s1 = s2 = 0.0; cache = {}
    for t in range(len(xin)):
        g = gains[t]
        if g < 0.001:
            out[t] = xin[t]; s1 = s2 = 0.0; continue
        c = cache.get(g)
        if c is None:
            c = cache[g] = shelf(g)
        b0, b1, b2, a1, a2 = c
        v = b0 * xin[t] + s1
        s1 = b1 * xin[t] - a1 * v + s2
        s2 = b2 * xin[t] - a2 * v
        out[t] = v
    return out

held = tr.copy()
for t in range(len(tr)):
    if (t + L) % 16 != 0:
        held[t] = held[t - 1] if t > 0 else 0.0
smooth = tr
a = render(held); b = render(smooth)
sig = (xin ** 2).sum()
delta = ((xin - b) ** 2).sum()
print('freq %.0f  engine vs. stepped re-implementation: %.1f dB (validates the model)' % (freq, 10 * np.log10(((y - a) ** 2).sum() / sig + 1e-30)))
print('zipper (stepped - per-sample): %.1f dB re signal, %.1f dB re the removed S (delta)' % (
    10 * np.log10(((a - b) ** 2).sum() / sig), 10 * np.log10(((a - b) ** 2).sum() / delta)))
# where does the zipper energy sit? fraction below 4 kHz
Z = np.abs(np.fft.rfft(a - b)) ** 2; f = np.fft.rfftfreq(len(a), 1 / fs)
print('zipper energy below 4 kHz: %.0f %%; max step per 16 samples: %.2f dB' % (100 * Z[f < 4000].sum() / Z.sum(), np.abs(np.diff(held[held > 0])).max()))
sf.write(S + '/synth/z_zipper_x20.wav', ((a - b) * 10).astype(np.float32), fs, subtype='FLOAT')
