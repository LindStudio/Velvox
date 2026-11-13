"""De-esser quality metrics: how much a processed take loses on its S sounds vs. on the rest of the voice.

    python tools/bench/deess_metrics.py <dry.wav> <processed.wav> [<processed2.wav> ...]

Frames (20 ms, hop 10 ms) of the dry take are classed by its spectrum:
  S      high band (4-16 kHz) at least 3 dB above the body (150 Hz - 3 kHz), not silent
  vowel  high band at least 15 dB under the body, loud (within 30 dB of the take's loudest frame)
  near   vowel frames within 40 ms of an S (onset / tail of the S: the vowel next to it must survive)
Reported per processed file (delta = dry - processed):
  S_red      energy the S frames lose, dB (more = de-esses more)
  vowel_red  energy the vowel frames lose in 2-16 kHz, dB (should be ~0)
  near_red   the same for vowel frames next to an S, dB
  purity     share of the delta energy that sits in S frames, % (100 = you only hear S in delta)
  voice_in_delta  delta energy outside S frames relative to the S part of the delta, dB (lower = cleaner)
"""
import sys
import numpy as np
from scipy.io import wavfile


def read(path):
    fs, x = wavfile.read(path)
    if x.dtype.kind == 'i':
        x = x / float(np.iinfo(x.dtype).max)
    return x.astype(np.float64), fs


def mono(x):
    return x.mean(axis=1) if x.ndim > 1 else x


def frames(x, n, hop):
    count = 1 + max(0, (len(x) - n) // hop)
    idx = np.arange(n)[None, :] + hop * np.arange(count)[:, None]
    return x[idx] * np.hanning(n)[None, :]


def band_energy(spec, freqs, lo, hi):
    m = (freqs >= lo) & (freqs < hi)
    return (np.abs(spec[:, m]) ** 2).sum(axis=1) + 1e-20


def classify(dry, fs):
    n, hop = int(0.02 * fs), int(0.01 * fs)
    spec = np.fft.rfft(frames(dry, n, hop), axis=1)
    freqs = np.fft.rfftfreq(n, 1 / fs)
    hi = band_energy(spec, freqs, 4000, 16000)
    body = band_energy(spec, freqs, 150, 3000)
    total = band_energy(spec, freqs, 20, fs / 2)
    lvl = 10 * np.log10(total)
    ratio = 10 * np.log10(hi / body)
    loud = lvl > lvl.max() - 45
    s = loud & (ratio > 3)
    vowel = (lvl > lvl.max() - 30) & (ratio < -15)
    near = np.zeros_like(vowel)
    for k in np.flatnonzero(s):
        near[max(0, k - 4): k + 5] = True
    near &= vowel
    return s, vowel, near, n, hop, freqs


def metrics(dry, wet, fs):
    L = min(len(dry), len(wet))
    dry, wet = dry[:L], wet[:L]
    s, vowel, near, n, hop, freqs = classify(dry, fs)
    D = np.fft.rfft(frames(dry, n, hop), axis=1)
    W = np.fft.rfft(frames(wet, n, hop), axis=1)
    X = np.fft.rfft(frames(dry - wet, n, hop), axis=1)
    full = (20, fs / 2)

    def red(mask, lo, hi):
        if not mask.any():
            return float("nan")
        return 10 * np.log10(band_energy(W[mask], freqs, lo, hi).sum() / band_energy(D[mask], freqs, lo, hi).sum())

    de = band_energy(X, freqs, *full)
    in_s = de[s].sum()
    out_s = de[~s].sum()
    return dict(
        S_red=-red(s, *full),
        vowel_red=-red(vowel, 2000, 16000),
        near_red=-red(near, 2000, 16000),
        purity=100 * in_s / (in_s + out_s),
        voice_in_delta=10 * np.log10(out_s / in_s),
        nS=int(s.sum()), nV=int(vowel.sum()), nN=int(near.sum()),
    )


def main():
    dry, fs = read(sys.argv[1])
    dry = mono(dry)
    for path in sys.argv[2:]:
        wet, fs2 = read(path)
        assert fs2 == fs
        m = metrics(dry, mono(wet), fs)
        print(f"{path.split('/')[-1].split(chr(92))[-1]:40s} S_red {m['S_red']:5.2f}  vowel_red {m['vowel_red']:5.2f}  "
              f"near_red {m['near_red']:5.2f}  purity {m['purity']:5.1f}%  voice_in_delta {m['voice_in_delta']:6.1f} dB"
              f"   (S {m['nS']}, vowel {m['nV']}, near {m['nN']})")


if __name__ == "__main__":
    main()
