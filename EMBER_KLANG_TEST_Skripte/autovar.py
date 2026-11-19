"""AUTO: does it follow the voice? Pitch/formant-shifted (resampled) and EQ'd variants, and different 12 s windows."""
import sys, subprocess, re, json
S = sys.argv[1]; sys.path.insert(0, S)
import numpy as np, soundfile as sf, ana
from scipy.signal import resample_poly, sosfilt
E = 'C:/Users/Shadow/dev/velvox-wt/ember-sound-audit/build/EmberTests_artefacts/Release/EmberTests.exe'


def shelf_sos(fs, f0, gdb):
    A = 10 ** (gdb / 40); w0 = 2 * np.pi * f0 / fs; cw = np.cos(w0); al = np.sin(w0) / (2 * 0.7071); sq = 2 * np.sqrt(A) * al
    b = [A * ((A + 1) + (A - 1) * cw + sq), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sq)]
    a = [(A + 1) - (A - 1) * cw + sq, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sq]
    return np.array([b + a]) / a[0]


def auto(path, *kv):
    o = subprocess.run([E, '--process', path, S + '/synth/a_out.wav', 'auto=1', 'trace=' + S + '/synth/a_tr.wav'] + list(kv), capture_output=True, text=True).stdout
    m = re.search(r'AUTO status (\d) freq (\d+) thr (\S+) range (\S+) peakHz (\d+) target (\S+) expected (\S+) vowel (\S+) mixed (\S+) sVsV (\S+)', o)
    return dict(status=int(m.group(1)), freq=int(m.group(2)), thr=float(m.group(3)), range=float(m.group(4)), peakHz=int(m.group(5)),
                target=float(m.group(6)), expected=float(m.group(7)), sVsV=float(m.group(10)))


res = {}
for take in ('F01', 'F22', 'F_wings', 'M40', 'M42', 'J1_chemicals', 'J3_lonely', 'M_notme'):
    x, fs = sf.read(S + '/data/takes/%s.wav' % take, dtype='float64', always_2d=True)
    x = x.mean(axis=1)[: int(90 * fs)]
    variants = {
        'orig': x,
        'tiefer_x0.84': resample_poly(x, 25, 21),
        'hoeher_x1.19': resample_poly(x, 21, 25),
        'hell_+6dB': sosfilt(shelf_sos(fs, 6000, 6), x),
        'dumpf_-6dB': sosfilt(shelf_sos(fs, 6000, -6), x),
    }
    for vn, v in variants.items():
        p = S + '/synth/a_in.wav'
        sf.write(p, (v / max(1e-9, np.abs(v).max()) * 0.5).astype(np.float32), fs, subtype='FLOAT')
        a = auto(p)
        # what it does on the whole excerpt
        C = ana.Classes(sf.read(p)[0], fs)
        m = ana.metrics(C, sf.read(S + '/synth/a_out.wav')[0], sf.read(S + '/synth/a_tr.wav')[0])
        a.update(S_4_12=round(m['S_4_12'], 2), S_air=round(m['S_air'], 2), S_low=round(m['S_low'], 2), ev_untouched=round(m['ev_untouched'], 1),
                 vowel_air=round(m['vowel_air'], 2), near_air=round(m['near_air'], 2), burst=round(m['burst_4_12'], 2))
        res['%s_%s' % (take, vn)] = a
        print(take, vn, a, flush=True)
    # stability: three different 12 s windows of the original
    p = S + '/synth/a_in.wav'
    sf.write(p, (x / np.abs(x).max() * 0.5).astype(np.float32), fs, subtype='FLOAT')
    for st in (0, 25, 50):
        a = auto(p, 'autostart=%d' % st)
        res['%s_win%d' % (take, st)] = a
        print(take, 'window', st, a, flush=True)
json.dump(res, open(S + '/autovar.json', 'w'), indent=1)
