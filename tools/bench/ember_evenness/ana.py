"""De-esser audit metrics (EMBER sound audit 2026-11-19)."""
import numpy as np, soundfile as sf

N_MS, HOP_MS = 20, 10


def load(path):
    x, fs = sf.read(path, dtype='float64', always_2d=True)
    return x.mean(axis=1), fs


def stft(x, fs):
    n, hop = int(N_MS * fs / 1000), int(HOP_MS * fs / 1000)
    cnt = 1 + max(0, (len(x) - n) // hop)
    idx = np.arange(n)[None, :] + hop * np.arange(cnt)[:, None]
    X = np.fft.rfft(x[idx] * np.hanning(n)[None, :], axis=1)
    return np.abs(X) ** 2, np.fft.rfftfreq(n, 1 / fs), hop


def band(P, f, lo, hi):
    m = (f >= lo) & (f < hi)
    return P[:, m].sum(axis=1) + 1e-20


def runs(mask):
    out, i, n = [], 0, len(mask)
    while i < n:
        if mask[i]:
            j = i
            while j < n and mask[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def dilate(mask, k):
    m = mask.copy()
    for s in range(1, k + 1):
        m[s:] |= mask[:-s]
        m[:-s] |= mask[s:]
    return m


class Classes:
    def __init__(self, dry, fs):
        P, f, hop = stft(dry, fs)
        self.P, self.f, self.hop, self.fs = P, f, hop, fs
        tot = band(P, f, 20, fs / 2)
        hi = band(P, f, 4000, 16000)
        body = band(P, f, 150, 3000)
        lvl = 10 * np.log10(tot)
        L = np.percentile(lvl, 99)
        ratio = 10 * np.log10(hi / body)
        hil = 10 * np.log10(hi) - L
        self.lvl, self.L, self.ratio = lvl, L, ratio
        loud = lvl > L - 45
        fric = loud & (ratio > 0)
        n = len(lvl)
        S = np.zeros(n, bool); weak = np.zeros(n, bool); burst = np.zeros(n, bool)
        self.events = []
        for a, b in runs(fric):
            if b - a <= 3:
                burst[a:b] = True
            elif hil[a:b].max() > -30:
                S[a:b] = True
                self.events.append((a, b))
            else:
                weak[a:b] = True
        vowel = (lvl > L - 30) & (ratio < -15)
        nearfric = dilate(fric, 3)
        bright = (lvl > L - 30) & (ratio >= -15) & (ratio < -5) & ~nearfric
        breath = (lvl > L - 55) & (lvl < L - 30) & (ratio > -12) & (ratio < 0) & ~dilate(vowel | fric, 5)
        near = dilate(S, 4) & vowel
        self.m = dict(S=S, weak=weak, burst=burst, vowel=vowel, bright=bright, breath=breath, near=near)
        # context of each S event: silence before (pause) or voice before
        self.ctx = []
        for a, b in self.events:
            pre = lvl[max(0, a - 15):max(0, a - 3)]
            self.ctx.append('pause' if len(pre) == 0 or pre.max() < L - 40 else 'voice')
        self.Slevel = np.array([hil[a:b].max() for a, b in self.events])
        spec = P[S].mean(axis=0) if S.any() else P.mean(axis=0)
        sm = np.array([spec[(f >= fc / 2 ** (1 / 6)) & (f < fc * 2 ** (1 / 6))].mean() for fc in f[1:]])
        sel = (f[1:] >= 3500) & (f[1:] <= 12000)
        self.peak = float(f[1:][sel][np.argmax(sm[sel])])


def red(Pd, Pw, f, mask, lo, hi):
    if not mask.any():
        return float('nan')
    return 10 * np.log10(band(Pd[mask], f, lo, hi).sum() / band(Pw[mask], f, lo, hi).sum())


def centroid(P, f, mask, lo=3000, hi=16000):
    m = (f >= lo) & (f < hi)
    s = P[mask][:, m].sum(axis=0)
    return float((s * f[m]).sum() / s.sum()) if mask.any() else float('nan')


def metrics(C, wet, trace=None):
    fs = C.fs
    Pw, f, _ = stft(wet, fs)
    k = min(len(Pw), len(C.P))
    Pd, Pw = C.P[:k], Pw[:k]
    m = {n: v[:k] for n, v in C.m.items()}
    r = {}
    r['S_4_12'] = red(Pd, Pw, f, m['S'], 4000, 12000)
    r['S_full'] = red(Pd, Pw, f, m['S'], 20, fs / 2)
    r['weak_4_12'] = red(Pd, Pw, f, m['weak'], 4000, 12000)
    r['burst_4_12'] = red(Pd, Pw, f, m['burst'], 4000, 12000)
    r['vowel_air'] = red(Pd, Pw, f, m['vowel'], 10000, 16000)
    r['vowel_pres'] = red(Pd, Pw, f, m['vowel'], 2000, 5000)
    r['near_air'] = red(Pd, Pw, f, m['near'], 10000, 16000)
    r['near_pres'] = red(Pd, Pw, f, m['near'], 2000, 5000)
    r['bright_4_12'] = red(Pd, Pw, f, m['bright'], 4000, 12000)
    r['breath_4_12'] = red(Pd, Pw, f, m['breath'], 4000, 12000)
    r['S_air'] = red(Pd, Pw, f, m['S'], 12000, 16000)
    r['S_low'] = red(Pd, Pw, f, m['S'], 4000, 7000)
    r['S_peak_hz'] = C.peak
    r['S_core'] = red(Pd, Pw, f, m['S'], C.peak / 2 ** 0.5, C.peak * 2 ** 0.5)
    r['S_above'] = red(Pd, Pw, f, m['S'], C.peak * 2 ** 0.5, 16000)
    r['S_cent_dry'] = centroid(Pd, f, m['S'])
    r['S_cent_wet'] = centroid(Pw, f, m['S'])
    # per S event: reduction 4-12 kHz over the whole event
    ev = []
    for (a, b) in C.events:
        if b <= k:
            ev.append(10 * np.log10(band(Pd[a:b], f, 4000, 12000).sum() / band(Pw[a:b], f, 4000, 12000).sum()))
    ev = np.array(ev)
    r['ev_med'] = float(np.median(ev)) if len(ev) else np.nan
    r['ev_p90'] = float(np.percentile(ev, 90)) if len(ev) else np.nan
    r['ev_max'] = float(ev.max()) if len(ev) else np.nan
    r['ev_untouched'] = float(100 * (ev < 0.5).mean()) if len(ev) else np.nan   # % S events losing < 0.5 dB
    r['ev_over8'] = float(100 * (ev > 8).mean()) if len(ev) else np.nan
    ctx = np.array(C.ctx[:len(ev)])
    r['ev_after_pause'] = float(np.median(ev[ctx == 'pause'])) if (ctx == 'pause').any() else np.nan
    r['ev_after_voice'] = float(np.median(ev[ctx == 'voice'])) if (ctx == 'voice').any() else np.nan
    r['nS'] = len(ev)
    r['_ev'] = ev

    # r2 research: per-S targeting (each S judged at its own spectral peak) and dynamics across S events
    own, below, above, hd, hw = [], [], [], [], []
    for (a, b) in C.events:
        if b > k:
            continue
        sd = Pd[a:b].sum(axis=0); sw = Pw[a:b].sum(axis=0)
        sel = (f >= 3500) & (f <= 12000)
        sm = np.convolve(sd, np.ones(5) / 5, mode='same')
        pk = f[sel][np.argmax(sm[sel])]
        def rr(lo, hi):
            mm = (f >= lo) & (f < hi)
            return 10 * np.log10((sd[mm].sum() + 1e-20) / (sw[mm].sum() + 1e-20))
        own.append(rr(pk / 2 ** 0.5, pk * 2 ** 0.5))
        below.append(rr(3000, pk / 2 ** 0.5))
        above.append(rr(pk * 2 ** 0.5, 16000))
        mm = (f >= 4000) & (f < 12000)
        hd.append(10 * np.log10(sd[mm].sum() / (b - a) + 1e-20)); hw.append(10 * np.log10(sw[mm].sum() / (b - a) + 1e-20))
    if len(own) > 3:
        r['ev_own'] = float(np.mean(own)); r['ev_below'] = float(np.mean(below)); r['ev_above'] = float(np.mean(above))
        hd, hw = np.array(hd), np.array(hw)
        red_e = hd - hw
        r['harsh_slope'] = float(np.polyfit(hd, red_e, 1)[0])
        r['spread'] = float(np.std(hw) / np.std(hd))
        r['ev_iqr'] = float(np.percentile(red_e, 75) - np.percentile(red_e, 25))
    if trace is not None:
        hop = C.hop
        n = int(N_MS * fs / 1000)
        tf = np.array([trace[i * hop:i * hop + n].max() for i in range(k)])
        r['vowel_gr_frames_pct'] = float(100 * (tf[m['vowel']] > 0.5).mean())
        r['bright_gr_frames_pct'] = float(100 * (tf[m['bright']] > 0.5).mean()) if m['bright'].any() else np.nan
        r['breath_gr_frames_pct'] = float(100 * (tf[m['breath']] > 0.5).mean()) if m['breath'].any() else np.nan
        r['weak_gr_frames_pct'] = float(100 * (tf[m['weak']] > 0.5).mean()) if m['weak'].any() else np.nan
        r['burst_gr_frames_pct'] = float(100 * (tf[m['burst']] > 0.5).mean()) if m['burst'].any() else np.nan
        # chatter inside S events: how often the reduction falls below 40 % of its event peak and comes back
        chat, peaks, missed = [], [], 0
        for (a, b) in C.events:
            seg = trace[a * hop:(b - 1) * hop + n]
            pk = seg.max() if len(seg) else 0
            peaks.append(pk)
            if pk < 0.5:
                missed += 1
                continue
            on = seg > 0.4 * pk
            # transitions on->off->on (count re-triggers)
            nruns = len(runs(on))
            chat.append(nruns - 1)
        r['gr_peak_med'] = float(np.median(peaks)) if peaks else np.nan
        r['gr_peak_max'] = float(np.max(peaks)) if peaks else np.nan
        r['S_missed_pct'] = 100 * missed / max(1, len(C.events))
        r['retrig_per_S'] = float(np.mean(chat)) if chat else np.nan
    return r
