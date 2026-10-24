"""Tests for the bench runner: clip selection, synthetic signals, analog EQ targets, verdicts, and that a
reference which cannot be loaded is recorded instead of aborting the module.

Run: python -I -m unittest discover -s tools/refhost/tests -t tools/refhost
"""
import os
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import bench  # noqa: E402
import modules  # noqa: E402
import qmetrics as q  # noqa: E402
import signals  # noqa: E402

FS = 48000


class Clips(unittest.TestCase):
    def test_six_clips_val_singers_and_mylk_only(self):
        self.assertEqual(len(signals.CLIPS), 6)
        for c in signals.CLIPS:
            if "vocalset" in c.path.lower():
                singer = os.path.basename(c.path).split("_")[0]
                self.assertIn(singer, ("f7", "f8", "m8", "m9"), c.path)
        self.assertTrue(any("MYLK" in c.path for c in signals.CLIPS))
        self.assertEqual(len({c.name for c in signals.CLIPS}), 6)


class Signals(unittest.TestCase):
    def test_level_steps(self):
        x, step = signals.level_steps(FS, np.arange(-60, 1, 6.0), 0.3, 1000)
        self.assertEqual(step, int(0.3 * FS))
        self.assertEqual(len(x), 11 * step)
        last = x[-step // 2:]
        self.assertAlmostEqual(20 * np.log10(np.sqrt(np.mean(last ** 2))), -3.01, delta=0.05)

    def test_step_tone_and_impulse(self):
        x, i_up, i_down = signals.step_tone(FS, -40, -10, 1000)
        self.assertEqual(i_up, FS)
        self.assertEqual(i_down, 2 * FS)
        self.assertAlmostEqual(20 * np.log10(np.max(np.abs(x[i_up:i_down]))), -10, delta=0.01)
        imp = signals.impulse(FS, 2.0)
        self.assertEqual(len(imp), 2 * FS)
        self.assertEqual(np.count_nonzero(imp), 1)

    def test_noise_level(self):
        n = signals.white_noise(FS, 4.0, -20.0)
        self.assertAlmostEqual(20 * np.log10(np.sqrt(np.mean(n ** 2))), -20.0, delta=0.1)


class Targets(unittest.TestCase):
    def test_analog_bell_and_shelf(self):
        f = np.array([100.0, 3000.0, 20000.0])
        h = bench.analog_response(f, [("bell", 3000.0, 6.0, 1.0)])
        self.assertAlmostEqual(h[1], 6.0, delta=1e-6)
        self.assertLess(abs(h[0]), 0.1)
        hs = bench.analog_response(np.array([10.0, 100000.0]), [("highshelf", 10000.0, 4.0, 0.707)])
        self.assertAlmostEqual(hs[0], 0.0, delta=0.01)
        self.assertAlmostEqual(hs[1], 4.0, delta=0.05)
        hp = bench.analog_response(np.array([100.0]), [("hpf", 100.0, 4)])
        self.assertAlmostEqual(hp[0], -3.01, delta=0.02)

    def test_verdicts(self):
        self.assertEqual(bench.verdict(-60.0, -55.0, "lower", 1.0), "pass")
        self.assertEqual(bench.verdict(-50.0, -55.0, "lower", 1.0), "FAIL")
        self.assertEqual(bench.verdict(0.9, 1.0, "higher", 0.2), "pass")
        self.assertEqual(bench.verdict(2.4, 2.0, "match", 0.5), "pass")
        self.assertEqual(bench.verdict(3.0, None, "lower", 1.0, absolute=-2.0), "FAIL")
        self.assertEqual(bench.verdict(float("nan"), 1.0, "lower", 1.0), "n/a")


class MissingReference(unittest.TestCase):
    @unittest.skipUnless(os.path.exists(bench.HOST), "HeadlinerRefHost not built")
    def test_bench_missing_reference(self):
        spec = modules.Module(
            key="check", title="Vocal Check",
            refs=[modules.Ref("Nope", "Does/Not/Exist.vst3")],
            cases=[modules.Case("null", headliner={}, procedures=["check_null"])])
        with tempfile.TemporaryDirectory() as d:
            res = bench.run_module(spec, d, clips=signals.CLIPS[:1])
        self.assertIn("Nope", res["ref_errors"])
        self.assertTrue(any(m["metric"] == "null_vs_input_db" and m["headliner"] < -100 for m in res["metrics"]))


if __name__ == "__main__":
    unittest.main()
