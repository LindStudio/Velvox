# Ledger – overhaul track "eqpanel"

Baseline: build of overhaul/eqpanel at 63bdf92 ok.

Task 4: complete (shell finding "hpfSlope switch jumps"): CompareTests "Switching the low-cut slope while playing never jumps
(legacy hpfSlope and band slope)" — 0.3 sine 130 Hz, slope switched 11 times: largest step 0.0051 / 0.0052 against a steady
0.0048 / 0.0051, i.e. no jump. The 0.6 EqualizerModule already crossfades topology changes (30 ms out / switch / in); the finding
was made on the 0.5 EQ before the EQ merge. No DSP change. The test was not watched failing on the current code (the fix
pre-existed); mutation check instead: adopting a slope change instantly makes it fail with steps 0.51 / 0.49. The hpfSlope pin
in "Switching Vorher / Nachher while playing never clicks" is removed (still green: 0.0102 vs steady 0.0072).
