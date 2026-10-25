"""Unit tests for tools/refhost/qmetrics.py: every metric on a synthetic signal with a known answer.

Run: python -I -m unittest discover -s tools/refhost/tests -t tools/refhost
"""
import os
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import qmetrics as q  # noqa: E402

FS = 48000


def sine(f, amp=0.5, sec=2.0, fs=FS, phase=0.0):
    t = np.arange(int(sec * fs)) / fs
    return amp * np.sin(2 * np.pi * f * t + phase)


class WavAndLoudness(unittest.TestCase):
    def test_wav_roundtrip_stereo_float(self):
        x = np.stack([sine(440), sine(660)], axis=1).astype(np.float64)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "a.wav")
            q.write_wav(p, x, FS)
            y, fs = q.read_wav(p)
        self.assertEqual(fs, FS)
        self.assertEqual(y.shape, x.shape)
        self.assertLess(np.max(np.abs(y - x)), 1e-6)

    def test_loudness_1k_sine_calibration(self):
        # BS.1770: a 0 dBFS 997 Hz sine in one channel reads -3.01 LUFS
        x = sine(997, amp=1.0, sec=5.0)
        self.assertAlmostEqual(q.integrated_lufs(x, FS), -3.01, delta=0.1)
        # stereo, both channels: +3 dB
        self.assertAlmostEqual(q.integrated_lufs(np.stack([x, x], 1), FS), 0.0, delta=0.1)

    def test_loudness_silence(self):
        self.assertEqual(q.integrated_lufs(np.zeros(FS * 2), FS), -np.inf)
        self.assertEqual(q.match_gain_db(np.zeros(FS), sine(1000), FS), 0.0)

    def test_match_gain(self):
        a, b = sine(1000, 0.5, 3), sine(1000, 0.125, 3)
        self.assertAlmostEqual(q.match_gain_db(a, b, FS), 12.04, delta=0.1)


class Distortion(unittest.TestCase):
    def test_pure_sine_thd_is_tiny(self):
        self.assertLess(q.thd_db(sine(1000), FS, 1000), -100)

    def test_known_third_harmonic(self):
        y = sine(1000) + 0.005 * np.sin(2 * np.pi * 3000 * np.arange(2 * FS) / FS)
        self.assertAlmostEqual(q.thd_db(y, FS, 1000), -40.0, delta=0.3)

    def test_aliasing_counts_non_harmonic_energy_only(self):
        f0 = 4987.0
        t = np.arange(2 * FS) / FS
        y = sine(f0) + 0.05 * np.sin(2 * np.pi * 2 * f0 * t) + 0.0005 * np.sin(2 * np.pi * 17311 * t)
        self.assertAlmostEqual(q.aliasing_db(y, FS, f0), -60.0, delta=1.0)


class Dynamics(unittest.TestCase):
    def test_static_curve_of_a_hard_knee_compressor(self):
        levels = np.arange(-60, 1, 6.0)
        step = int(0.3 * FS)
        x = np.concatenate([sine(1000, 10 ** (L / 20), 0.3) for L in levels])

        def comp(L):  # threshold -30, ratio 4
            return L if L < -30 else -30 + (L + 30) / 4

        y = np.concatenate([sine(1000, 10 ** (comp(L) / 20), 0.3) for L in levels])
        out = q.static_curve(y, FS, step, len(levels))
        expected = np.array([comp(L) for L in levels]) - 3.01  # RMS of a sine
        np.testing.assert_allclose(out, expected, atol=0.1)
        ratio, thr = q.curve_ratio_threshold(levels, out)
        self.assertAlmostEqual(ratio, 4.0, delta=0.2)
        self.assertAlmostEqual(thr, -30.0, delta=2.0)

    def test_attack_release_of_exponential_gain(self):
        fs = FS
        n = 3 * fs
        x = sine(1000, 1.0, 3.0)
        x[: fs] *= 0.03
        x[2 * fs:] *= 0.03
        t = np.arange(n) / fs
        # gain: 0 dB, then towards -10 dB with tau 10 ms, then back with tau 100 ms
        g = np.zeros(n)
        up = (t >= 1) & (t < 2)
        g[up] = -10 * (1 - np.exp(-(t[up] - 1) / 0.010))
        g_end = g[up][-1]
        dn = t >= 2
        g[dn] = g_end * np.exp(-(t[dn] - 2) / 0.100)
        y = x * 10 ** (g / 20)
        a, r = q.attack_release_ms(x, y, fs, fs, 2 * fs)
        self.assertAlmostEqual(a, 10.0, delta=2.0)     # 63 % time constant
        self.assertAlmostEqual(r, 100.0, delta=10.0)


class Spectral(unittest.TestCase):
    def test_freq_response_of_a_gain_and_lowpass(self):
        rng = np.random.default_rng(3)
        x = rng.standard_normal(FS * 4) * 0.1
        y = 2.0 * x
        f, h = q.freq_response(x, y, FS)
        sel = (f > 50) & (f < 18000)
        np.testing.assert_allclose(h[sel], 6.02, atol=0.05)

    def test_response_deviation(self):
        f = np.geomspace(20, 20000, 200)
        a = np.zeros_like(f)
        b = a.copy()
        b[f > 10000] = 1.0
        dev = q.response_deviation(f, a, b)
        self.assertAlmostEqual(dev["max_db"], 1.0, delta=1e-9)
        self.assertAlmostEqual(dev["hf_max_db"], 1.0, delta=1e-9)
        self.assertLess(dev["rms_db"], 1.0)

    def test_null(self):
        a = sine(500)
        self.assertLess(q.null_db(a, a), -200)
        self.assertAlmostEqual(q.null_db(a * 1.1, a), -20.0, delta=0.01)

    def test_resonance_metrics_see_a_fixed_peak(self):
        rng = np.random.default_rng(5)
        noise = rng.standard_normal(FS * 3) * 0.05
        reso = noise + sine(3000, 0.2, 3.0)
        self.assertGreater(q.ltas_peakiness_db(reso, FS), q.ltas_peakiness_db(noise, FS) + 6)
        self.assertGreater(q.peak_to_envelope_db(reso, FS), q.peak_to_envelope_db(noise, FS) + 3)
        # removing the resonance: delta = the sine's energy relative to the input
        d = q.delta_energy_db(reso, noise)
        expected = 10 * np.log10(np.sum(sine(3000, 0.2, 3.0) ** 2) / np.sum(reso ** 2))
        self.assertAlmostEqual(d, expected, delta=0.1)

    def test_musical_noise_zero_for_static_gain_high_for_random_mask(self):
        rng = np.random.default_rng(9)
        x = rng.standard_normal(FS * 2) * 0.1
        self.assertLess(q.musical_noise_db(x, 0.5 * x, FS), 0.05)
        # random per-bin on/off gains (like a badly smoothed spectral gate)
        f, t, Z = q._stft(x, FS)
        mask = rng.choice([0.25, 1.0], size=Z.shape)
        y = q._istft(Z * mask, FS, len(x))
        self.assertGreater(q.musical_noise_db(x, y, FS), 1.0)

    def test_transient_preservation(self):
        n = FS * 2
        x = 0.05 * np.sin(2 * np.pi * 300 * np.arange(n) / FS)
        g = np.ones(n)
        for k in range(8):
            i = int((0.1 + 0.2 * k) * FS)
            x[i:i + 2400] += np.exp(-np.arange(2400) / 200) * np.sin(2 * np.pi * 2000 * np.arange(2400) / FS)
            g[i:i + 960] = np.linspace(0.2, 1.0, 960)  # a slow attack that flattens the transient
        self.assertAlmostEqual(q.transient_change_db(x, x, FS), 0.0, delta=0.01)
        self.assertAlmostEqual(q.transient_change_db(x, 0.5 * x, FS), 0.0, delta=0.01)
        self.assertLess(q.transient_change_db(x, x * g, FS), -3.0)


class Reverb(unittest.TestCase):
    def synthetic_ir(self, rt60=2.0, sec=3.0, seed=1):
        rng = np.random.default_rng(seed)
        n = int(sec * FS)
        t = np.arange(n) / FS
        return rng.standard_normal(n) * 10 ** (-3 * t / rt60)

    def test_schroeder_rt(self):
        d = q.schroeder_decay(self.synthetic_ir(2.0), FS)
        self.assertAlmostEqual(d["t30"], 2.0, delta=0.1)
        self.assertAlmostEqual(d["edt"], 2.0, delta=0.2)
        self.assertLess(d["curvature_db"], 0.5)

    def test_echo_density_of_noise_is_one_sparse_is_low(self):
        ned = q.echo_density(self.synthetic_ir(2.0), FS)
        self.assertGreater(ned["mean_ned"], 0.9)
        self.assertLess(ned["mixing_ms"], 50)
        sparse = np.zeros(FS)
        sparse[::2400] = 1.0
        self.assertLess(q.echo_density(sparse, FS)["mean_ned"], 0.3)

    def test_modal_ringing(self):
        ir = self.synthetic_ir(2.0)
        t = np.arange(len(ir)) / FS
        ringing = ir + 0.5 * np.sin(2 * np.pi * 1234 * t) * 10 ** (-3 * t / 3.0)
        self.assertGreater(q.modal_ringing_db(ringing, FS), q.modal_ringing_db(ir, FS) + 6)


class Modulation(unittest.TestCase):
    def test_delay_track_constant_and_modulated(self):
        rng = np.random.default_rng(2)
        x = rng.standard_normal(FS * 3) * 0.1
        d = 480  # 10 ms
        y = np.concatenate([np.zeros(d), x[:-d]])
        tr = q.delay_track_ms(x, y, FS)
        self.assertAlmostEqual(float(np.median(tr)), 10.0, delta=0.05)
        self.assertLess(q.track_jitter(tr), 0.01)

    def test_pitch_track_cents(self):
        y = sine(220 * 2 ** (25 / 1200), sec=2.0)
        cents = q.pitch_track_cents(y, FS, 220.0)
        self.assertAlmostEqual(float(np.median(cents)), 25.0, delta=2.0)

    def test_comb_depth(self):
        rng = np.random.default_rng(4)
        x = rng.standard_normal(FS * 3) * 0.1
        d = 48  # 1 ms -> deep comb
        comb = x + np.concatenate([np.zeros(d), x[:-d]])
        self.assertGreater(q.comb_depth_db(x, comb, FS), 6.0)
        self.assertLess(q.comb_depth_db(x, 0.7 * x, FS), 0.5)

    def test_f0_and_hnr(self):
        y = sine(200, sec=1.0)
        f0 = q.f0_track(y, FS)
        self.assertAlmostEqual(float(np.median(f0[f0 > 0])), 200.0, delta=1.0)
        rng = np.random.default_rng(1)
        noisy = y + rng.standard_normal(len(y)) * 0.2
        self.assertGreater(q.hnr_db(y, FS), q.hnr_db(noisy, FS) + 10)

    @staticmethod
    def _vowel(f0, F1, F2, sec=2.0):
        t = np.arange(int(sec * FS)) / FS
        y = np.zeros_like(t)
        k = 1
        while k * f0 < 8000:
            f = k * f0
            a = 1 / (1 + ((f - F1) / 80) ** 2) + 0.5 / (1 + ((f - F2) / 110) ** 2) + 0.005
            y += a / np.sqrt(k) * np.sin(2 * np.pi * f * t + 0.3 * k * k)
            k += 1
        return 0.3 * y / np.max(np.abs(y))

    def test_formants(self):
        F1, F2 = q.formants(self._vowel(140, 650, 1700), FS)
        self.assertLess(abs(np.log2(F1 / 650)), 0.1)
        self.assertLess(abs(np.log2(F2 / 1700)), 0.1)
        # same vowel, higher pitch: same formants
        G1, G2 = q.formants(self._vowel(280, 650, 1700), FS)
        self.assertLess(abs(np.log2(G1 / 650)), 0.2)
        self.assertLess(abs(np.log2(G2 / 1700)), 0.2)
        # formants an octave up are found an octave up
        H1, H2 = q.formants(self._vowel(140, 900, 2400), FS)
        self.assertLess(abs(np.log2(H1 / 900)), 0.1)
        self.assertLess(abs(np.log2(H2 / 2400)), 0.1)

    def test_formant_shift_oct(self):
        dry = self._vowel(140, 650, 1700)
        # same vowel an octave higher in pitch: envelope unchanged
        self.assertLess(abs(q.formant_shift_oct(dry, self._vowel(280, 650, 1700), FS)), 0.12)
        # chipmunk: formants up an octave
        self.assertAlmostEqual(q.formant_shift_oct(dry, self._vowel(280, 1300, 3400), FS), 1.0, delta=0.12)
        # a vowel with a low F1 sung an octave higher: F1 lies below the new f0 and cannot show; the
        # metric only compares the band above the fundamental, so kept formants still read ~0
        low = self._vowel(220, 320, 2300)
        self.assertLess(abs(q.formant_shift_oct(low, self._vowel(440, 320, 2300), FS)), 0.15)
        # formants down a fifth
        self.assertAlmostEqual(q.formant_shift_oct(dry, self._vowel(140, 650 / 1.5, 1700 / 1.5), FS), -np.log2(1.5), delta=0.12)


if __name__ == "__main__":
    unittest.main()
