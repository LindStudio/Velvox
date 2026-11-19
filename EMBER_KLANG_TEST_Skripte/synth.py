"""Technical checks: null, latency, distortion/aliasing, level sweep, hi-hats."""
import sys, os, subprocess, json
S = sys.argv[1]; sys.path.insert(0, S)
import numpy as np, soundfile as sf, ana
E = 'C:/Users/Shadow/dev/velvox-wt/ember-sound-audit/build/EmberTests_artefacts/Release/EmberTests.exe'
T = S + '/synth'; os.makedirs(T, exist_ok=True)
out = {}


def run(inp, outp, *kv, trace=None):
    a = [E, '--process', inp, outp] + list(kv) + (['trace=' + trace] if trace else [])
    p = subprocess.run(a, capture_output=True, text=True)
    return p.stdout


def w(path, x, fs):
    sf.write(path, np.asarray(x, np.float32), fs, subtype='FLOAT')


def r(path):
    x, fs = sf.read(path, dtype='float64', always_2d=True)
    return x, fs

# ---- 1) null tests on a real take (stereo)
take = S + '/data/takes/F22.wav'
dry, fs = r(take)
nul = {}
for name, kv in [('split_thr0', ['thr=0']), ('wide_thr0', ['thr=0', 'mode=1']), ('split_range0', ['range=0', 'thr=-60']),
                 ('split_amount0', ['amount=0', 'thr=-60']), ('wide_amount0', ['amount=0', 'thr=-60', 'mode=1']),
                 ('mix0', ['mix=0', 'thr=-60']), ('mix50_thr0', ['mix=0.5', 'thr=0'])]:
    run(take, T + '/null.wav', *kv)
    y, _ = r(T + '/null.wav')
    d = np.abs(y - dry).max()
    nul[name] = 'bit-exact' if d == 0 else '%.1f dBFS' % (20 * np.log10(d))
out['null'] = nul
print('null', nul, flush=True)

# ---- 2) latency: impulse at 44.1 / 48 / 96 kHz, every lookahead
lat = {}
for sr in (44100, 48000, 96000):
    x = np.zeros(sr); x[sr // 2] = 0.5
    w(T + '/imp.wav', x, sr)
    for la in (0, 2, 5):
        log = run(T + '/imp.wav', T + '/imp_o.wav', 'la=%g' % la, 'thr=0')
        y, _ = r(T + '/imp_o.wav')
        lat['%d_la%g' % (sr, la)] = int(np.argmax(np.abs(y[:, 0]))) - sr // 2
out['latency_offset_samples'] = lat
print('latency', lat, flush=True)

# ---- 3) distortion: steady sines through a held reduction
dist = {}
sr = 48000
t = np.arange(sr * 2) / sr
for f0 in (1000, 7000, 10000, 15000):
    x = 0.3 * np.sin(2 * np.pi * f0 * t)
    w(T + '/sine.wav', x, sr)
    for mode in (0, 1):
        run(T + '/sine.wav', T + '/sine_o.wav', 'thr=-60', 'range=10', 'mode=%d' % mode, 'det=2', 'freq=5000')
        y, _ = r(T + '/sine_o.wav'); y = y[sr // 2: sr // 2 + sr, 0]
        Y = np.abs(np.fft.rfft(y * np.blackman(len(y)))) ** 2
        fr = np.fft.rfftfreq(len(y), 1 / sr)
        main = Y[(fr > f0 - 30) & (fr < f0 + 30)].sum()
        rest = Y[(fr < f0 - 30) | (fr > f0 + 30)].sum()
        gain = 10 * np.log10(main / (np.abs(np.fft.rfft(x[sr // 2: sr // 2 + sr] * np.blackman(sr))) ** 2).sum())
        dist['%dHz_%s' % (f0, 'split' if mode == 0 else 'wide')] = 'gain %.1f dB, THD+N %.0f dBc' % (gain, 10 * np.log10(rest / main + 1e-30))
out['distortion'] = dist
print('dist', dist, flush=True)

# ---- 4) level sweep: same take at different input levels, fixed settings
sweep = {}
for takeName in ('F22', 'M40'):
    x, fs = r(S + '/data/takes/%s.wav' % takeName)
    x = x / np.abs(x).max()
    w(T + '/norm.wav', x, fs)
    C = ana.Classes(x.mean(axis=1), fs)
    for det in (0, 1, 2):
        row = {}
        for g in (0, -10, -20, -30, -40):
            run(T + '/norm.wav', T + '/lv.wav', 'gain=%d' % g, 'det=%d' % det)
            y, _ = r(T + '/lv.wav')
            m = ana.metrics(C, y.mean(axis=1) / 10 ** (g / 20))
            row[g] = (round(m['S_4_12'], 2), round(m['ev_untouched'], 0), round(m['vowel_air'], 2))
        sweep['%s_det%d' % (takeName, det)] = row
        print(takeName, det, row, flush=True)
out['level_sweep'] = sweep

json.dump(out, open(S + '/synth.json', 'w'), indent=1, default=str)
