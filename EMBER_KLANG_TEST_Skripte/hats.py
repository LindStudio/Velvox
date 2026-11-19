"""False triggers: hi-hats (bleed or a bright playback) under the vocal."""
import sys, subprocess, json
S = sys.argv[1]; sys.path.insert(0, S)
import numpy as np, soundfile as sf, ana
from scipy.signal import butter, sosfilt
E = 'C:/Users/Shadow/dev/velvox-wt/ember-sound-audit/build/EmberTests_artefacts/Release/EmberTests.exe'
res = {}
for take in ('F22', 'M40', 'J1_chemicals'):
    x, fs = sf.read(S + '/data/takes/%s.wav' % take, dtype='float64', always_2d=True)
    x = x.mean(axis=1)[: int(60 * fs)]
    x /= np.abs(x).max()
    C = ana.Classes(x, fs)
    rng = np.random.default_rng(1)
    hat = np.zeros_like(x)
    step = int(0.25 * fs); env = np.exp(-np.arange(int(0.08 * fs)) / (0.015 * fs))
    for p in range(0, len(x) - len(env), step):
        hat[p:p + len(env)] += rng.standard_normal(len(env)) * env
    hat = sosfilt(butter(4, 7000, 'hp', fs=fs, output='sos'), hat)
    hat /= np.abs(hat).max()
    # vocal pause frames (for "triggers in the gaps")
    pauses = C.lvl < C.L - 50
    for lvl in (None, -30, -20, -12):
        mix = x + (0 if lvl is None else hat * 10 ** (lvl / 20))
        sf.write(S + '/synth/h_in.wav', mix.astype(np.float32), fs, subtype='FLOAT')
        for cfg, kv in (('def', []), ('auto', ['auto=1']), ('rel', ['det=1']), ('abs', ['det=2'])):
            subprocess.run([E, '--process', S + '/synth/h_in.wav', S + '/synth/h_out.wav', 'trace=' + S + '/synth/h_tr.wav'] + kv, capture_output=True)
            y = sf.read(S + '/synth/h_out.wav', dtype='float64', always_2d=True)[0].mean(axis=1)
            tr = sf.read(S + '/synth/h_tr.wav', dtype='float64')[0]
            Cm = ana.Classes(x, fs)   # classes from the clean vocal
            # reduction measured on the vocal part: compare against the same render of the clean vocal is not linear,
            # so judge the S of the mix by the trace and the mix energy
            m = ana.metrics(Cm, x, tr)   # trace-based numbers only
            hop = Cm.hop; n = int(0.02 * fs)
            tf = np.array([tr[i * hop:i * hop + n].max() for i in range(len(Cm.lvl))])
            k = min(len(tf), len(pauses))
            key = '%s_%s_%s' % (take, 'nohat' if lvl is None else 'hat%d' % lvl, cfg)
            res[key] = dict(S_gr_med=float(m['gr_peak_med']), S_missed=float(m['S_missed_pct']),
                            vowel_gr_pct=float(m['vowel_gr_frames_pct']),
                            pause_gr_pct=float(100 * (tf[:k][pauses[:k]] > 0.5).mean()))
            print(key, {a: round(b, 2) for a, b in res[key].items()}, flush=True)
json.dump(res, open(S + '/hats.json', 'w'), indent=1)
