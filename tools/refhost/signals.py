"""Test inputs for the module bench: the six dry vocal clips and synthetic signals (all 48 kHz)."""
from __future__ import annotations

import os
from dataclasses import dataclass

import numpy as np
from scipy import signal as sps

import qmetrics as q

FS = 48000

VOCALSET = r"C:\Users\jakob\dev\velvox-data\sources\vocalset11\FULL"
MYLK = (r"C:\Users\jakob\Documents\Splice\Samples\packs\MYLK's Jpop n' Go Sample Pack\MYLK_sample_pack"
        r"\MYLK_level_up\MYLK_level_up_dry\MYLK_140_vocal_loop_level_up_dry_chorus_extended_Dbmaj.wav")


@dataclass(frozen=True)
class Clip:
    name: str
    path: str
    start: float = 0.0      # seconds
    length: float = 12.0    # seconds (shorter files are used whole)
    note: str = ""


# VocalSet validation singers only (f7, f8, m8, m9); f9, m10, m11 are held out and never used here.
CLIPS = [
    Clip("f7_row", os.path.join(VOCALSET, r"female7\excerpts\straight\f7_row_straight.wav"), 0.0, 12.0,
         "female, soft straight tone, lyrics with s-sounds"),
    Clip("f8_belt", os.path.join(VOCALSET, r"female8\arpeggios\belt\f8_arpeggios_belt_a.wav"), 0.0, 12.0,
         "female, belted arpeggio, loud"),
    Clip("m8_caro", os.path.join(VOCALSET, r"male8\excerpts\straight\m8_caro_straight.wav"), 0.0, 12.0,
         "male, legato phrase with consonants"),
    Clip("m8_belt", os.path.join(VOCALSET, r"male8\arpeggios\belt\m8_arpeggios_belt_a.wav"), 0.0, 12.0,
         "male, belted arpeggio"),
    Clip("m9_breathy", os.path.join(VOCALSET, r"male9\arpeggios\breathy\m9_arpeggios_breathy_a.wav"), 0.0, 12.0,
         "male, soft breathy"),
    Clip("mylk_chorus", MYLK, 0.0, 14.0, "pop chorus (J-pop, dry, stereo), sibilant, dense"),
]


def load_clip(clip: Clip, fs: int = FS) -> np.ndarray:
    """The clip as float64 at 48 kHz (mono VocalSet stays mono), normalised to -18 LUFS."""
    x, sr = q.read_wav(clip.path)
    a = int(clip.start * sr)
    x = x[a:a + int(clip.length * sr)]
    if sr != fs:
        g = np.gcd(int(fs), int(sr))
        x = sps.resample_poly(x, fs // g, sr // g, axis=0)
    gain = -18.0 - q.integrated_lufs(x, fs)
    return q.apply_gain_db(x, gain if np.isfinite(gain) else 0.0)


def sine(fs, f, dbfs, seconds):
    t = np.arange(int(seconds * fs)) / fs
    return 10 ** (dbfs / 20) * np.sin(2 * np.pi * f * t)


def level_steps(fs, levels_db, step_s, f):
    """A sine whose peak level steps through levels_db; returns (signal, samples per step)."""
    step = int(step_s * fs)
    t = np.arange(step) / fs
    tone = np.sin(2 * np.pi * f * t)
    return np.concatenate([10 ** (L / 20) * tone for L in levels_db]), step


def step_tone(fs, low_db, high_db, f, pre=1.0, hold=1.0, post=1.5):
    """Sine at low_db, then high_db, then low_db again; returns (signal, i_up, i_down)."""
    n = int((pre + hold + post) * fs)
    t = np.arange(n) / fs
    x = np.sin(2 * np.pi * f * t)
    i_up, i_down = int(pre * fs), int((pre + hold) * fs)
    g = np.full(n, 10 ** (low_db / 20))
    g[i_up:i_down] = 10 ** (high_db / 20)
    return x * g, i_up, i_down


def impulse(fs, seconds, amp=0.5):
    x = np.zeros(int(seconds * fs))
    x[0] = amp
    return x


def white_noise(fs, seconds, rms_db, seed=1):
    x = np.random.default_rng(seed).standard_normal(int(seconds * fs))
    return x / np.sqrt(np.mean(x ** 2)) * 10 ** (rms_db / 20)


def pink_noise(fs, seconds, rms_db, seed=2):
    n = int(seconds * fs)
    X = np.fft.rfft(np.random.default_rng(seed).standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / fs)
    X[1:] /= np.sqrt(f[1:])
    X[0] = 0
    x = np.fft.irfft(X, n)
    return x / np.sqrt(np.mean(x ** 2)) * 10 ** (rms_db / 20)


def bursts(fs, seconds, period_s, burst_s, dbfs, seed=3):
    """Noise bursts (for dry-leak tests of time-based effects)."""
    x = np.zeros(int(seconds * fs))
    rng = np.random.default_rng(seed)
    for a in range(0, len(x), int(period_s * fs)):
        b = min(len(x), a + int(burst_s * fs))
        x[a:b] = rng.standard_normal(b - a) * 10 ** (dbfs / 20)
    return x
