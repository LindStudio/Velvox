"""Listening set: per preset 1 fitting take, 20 s excerpt with the most S, processed + delta (from the bench renders)."""
import os, pickle, numpy as np, soundfile as sf
from presets import P, key
OUT = 'C:/Users/Shadow/dev/velvox-bench/2026-11-24/presets'
os.makedirs(OUT, exist_ok=True)
CAT = {'none': 'Default', 'basics': 'VocalBasics', 'male': 'MaleVocals', 'female': 'FemaleVocals', 'genre': 'Genre', 'special': 'SpecialPurpose'}
TAKE = {'Default': 'J1_chemicals', 'Gentle Vocal': 'F08', 'Natural De-Essing': 'J2_kissed', 'Transparent': 'F01', 'Subtle Reduction': 'F22',
        'Strong Sibilance': 'M_notme', 'Male Rap': 'M40', 'Deep Male Vocal': 'M36', 'Bright Male Vocal': 'M_notme', 'Aggressive Rap': 'M_allittakes',
        'Modern Pop Male': 'M_allittakes', 'Female Pop': 'F22', 'Bright Female Vocal': 'F_badboy', 'Airy Vocal': 'F01', 'Soft Female Vocal': 'F08',
        'Powerful Female Vocal': 'F_runwild', 'Hypertechno': 'G_F01_club', 'EDM Lead Vocal': 'G_F_runwild_club', 'Dance Pop': 'G_F22_club',
        'Hard Dance': 'G_M_notme_club', 'Melodic Techno': 'G_J1_chemicals_club', 'Trap Vocals': 'G_M40_trap', 'Modern Hip-Hop': 'G_M_emerald_trap',
        'Acoustic Vocals': 'F_wings', 'Harsh S Sounds': 'J3_lonely', 'Extreme Sibilance': 'F_badboy', 'Sharp T Sounds': 'J2_kissed',
        'Vocal Recording Rescue': 'M_notme', 'Mix / Bus': 'J1_chemicals'}
for p in P:
    name, cat = p[0], p[1]
    take = TAKE[name]
    src = ('data/genre/' if take.startswith('G_') else 'data/takes/') + take + '.wav'
    if name == 'Mix / Bus':
        src, wet = 'data/mix/%s.wav' % take, 'out/%s__mix_%s.wav' % (take, key(p))
    else:
        wet = 'out/%s__%s.wav' % (take, key(p))
    d, fs = sf.read(src, always_2d=True); w, _ = sf.read(wet, always_2d=True)
    C = pickle.load(open('out/%s.cls' % take, 'rb'))
    hop = C.hop; n = int(20 * fs)
    starts = np.array([a * hop for a, b in C.events])
    best = max(range(0, max(1, len(d) - n), int(fs)), key=lambda s: ((starts >= s) & (starts < s + n)).sum())
    k = min(len(d), len(w)); d, w = d[best:best + n], w[best:best + n]
    if d.shape[1] != w.shape[1]:
        w = np.repeat(w, d.shape[1], axis=1) if w.shape[1] == 1 else w[:, :d.shape[1]]
    base = '%s_%s__%s' % (CAT[cat], name.replace(' / ', '-').replace(' ', ''), take)
    sf.write('%s/%s.wav' % (OUT, base), w.astype(np.float32), fs, subtype='FLOAT')
    sf.write('%s/%s_DELTA.wav' % (OUT, base), (d - w).astype(np.float32), fs, subtype='FLOAT')
    print(base, 'from %.0f s' % (best / fs))
