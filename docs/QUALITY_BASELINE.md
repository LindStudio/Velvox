# Quality baseline – Headliner modules vs. reference plugins (0.6, 2026-10-25)

Measured state of every Headliner module on `feature/automix` c46dba8 (0.6 mix law merged, before the DSP work), against the installed
reference plugins at matched settings on dry vocals and test signals. This is the starting point and the pass/fail gate
(spec §6, §8 Q6) for the DSP-improvement agents. Rendered files (for listening):
`C:\Users\jakob\dev\velvox-bench\2026-10-25\modules\<module>\` – `raw\` (every render, incl. JSON sidecars with latency,
CPU and the parameter values read back from the plugin), `listen\` (vocal clips, loudness-matched to the dry clip, plus
the dry clip; for Smooth also the removed signal `__delta`), `metrics.json`, `report.md`.

## Re-run (one command per module)

```powershell
# once, and after DSP changes: build the reference host (also builds the Headliner sources into it)
powershell -NoProfile -File C:\Users\jakob\dev\velvox-wt\bt.ps1 -Src <worktree> -Target HeadlinerRefHost
# one module (eq, comp, deess, smooth, sat, pitch, doubler, choir, delay, reverb, smash, ride, chop, pulse, check)
C:\Users\jakob\AppData\Local\Programs\Python\Python313\python.exe -I tools\refhost\bench.py smooth
#   --no-ref        Headliner only (seconds; the reference values stay in this document)
#   --clips 2       first N vocal clips only (quick iteration)
#   --update-doc    rewrite this module's table below
#   --out DIR       somewhere else than velvox-bench\2026-10-25\modules
# everything (about 5 minutes):
C:\Users\jakob\AppData\Local\Programs\Python\Python313\python.exe -I tools\refhost\bench.py all
# metric unit tests (synthetic signals with known answers) and bench tests
C:\Users\jakob\AppData\Local\Programs\Python\Python313\python.exe -I -m unittest discover -s tools\refhost\tests -t tools\refhost
```

Exploring a plugin: `build\HeadlinerRefHost_artefacts\Release\HeadlinerRefHost.exe --list --plugin "FabFilter/FabFilter Pro-C 2.vst3"`
(paths relative to `C:\Program Files\Common Files\VST3`; `--sub "Doubler2 Stereo"` for WaveShell sub-plugins, `--subs` lists
them; `--list --headliner eq` lists Headliner's parameters; discrete parameters show their choices in `[a|b|c]`).
Rendering: `--render --plugin <p> --in a.wav --out b.wav --set "Threshold=-24" --set "Style=Vocal" [--setn "Delay Time Pan=0.5"]`
or `--render --headliner comp --set compThreshold=-24 ...` (Headliner: APVTS IDs in real units).

## Method

- **Host** (`tools/refhost`, CMake target `HeadlinerRefHost`; VST3 hosting enabled only there): offline, 48 kHz, 512-sample
  blocks, stereo, a steady 120 BPM play head, non-realtime flag set. The plugin's reported latency is compensated (output
  aligned to input sample 0). Mono clips feed both inputs. Headliner runs in-process as the real `VelvoxAudioProcessor` with a
  one-module routing (insert), the module on, safety clip and level match off – i.e. exactly the chain a user hears,
  including the current mix law. A 0.25 s silent pre-roll precedes every input (settings settle as in a DAW; the reverb
  engine discards an impulse that arrives in the same block as a type switch).
- **Parameters**: references are set by display text and verified by reading the text back (FabFilter, Valhalla, oeksound,
  Waves, iZotope, Auburn all accepted this; VintageVerb's value-to-text ignores the value, the host then probes by setting).
- **Matched settings** per module are in `tools/refhost/modules.py` (same threshold/ratio/times/frequencies where the
  controls mean the same). Where the controls cannot be matched by value, the reference is **calibrated to the same amount
  of action** first: Pro-DS threshold → same sibilance reduction on `f7_row`; soothe2 depth → same removed energy; Saturn 2
  drive → same THD at 1 kHz / −12 dBFS (grid + bisection). The chosen values are printed in each table.
- **Level matching**: vocal renders are matched to the dry clip's integrated loudness (BS.1770-4, gated) before
  listening and before level-dependent comparisons; spectral-shape metrics are level independent.
- **Clips** (VocalSet validation singers only – f9, m10, m11 stay held out – and the MYLK dry chorus), 48 kHz, −18 LUFS:
  `f7_row` (female, soft, s-sounds), `f8_belt` (female belt), `m8_caro` (male legato phrase), `m8_belt` (male belt),
  `m9_breathy` (male breathy), `mylk_chorus` (pop chorus, dense, sibilant). Vocal metrics are the mean over the six clips.
- **Mix law**: the single-module processor is a fresh instance, so it uses the 0.6 true dry/wet law (mixlaw track,
  dry-hold for Delay/Reverb inserts, Doubler, Smash). Delay and Reverb run at Mix 100 % = wet only, like the references;
  `dry_leak_db` (10 ms burst, output during the burst) checks spec §2.9. Before the merge the same run measured 0 dB dry
  leak (legacy `out = in + wet·mix`); all other numbers were identical under both laws.
- **Verdict**: `pass` when Headliner is within the tolerance of the primary (first) reference, or of an absolute limit
  where the reference does not define the target (`≤ x` / `x ± t` in the tolerance column); `info` rows describe character
  and are not gated. These tolerances are the proposed Q6 gate (next section) and live in `modules.py`, so a DSP agent
  that re-runs a module gets the same verdicts.

## Reference plugins

All references loaded offline without an authorization dialog:

| module | reference (VST3) | latency | notes |
|---|---|---|---|
| EQ | FabFilter Pro-Q 4 | 0 | its Q / slope definitions differ from the RBJ / Butterworth prototypes (a 24 dB/oct cut is −6 dB at the corner), so EQ accuracy is judged against Headliner's own analog prototypes and cramping against Pro-Q 4 |
| Compressor | FabFilter Pro-C 2 (Vocal, and Clean) | 0 | "Vocal" is program dependent (static ratio steepens to ~12:1 for a steady tone) |
| De-Esser | FabFilter Pro-DS (Single Vocal, split band, lookahead off) | 0 | threshold calibrated |
| Smooth | oeksound soothe2 | 2048 | depth calibrated; even depth 0 removes ≈ 15 dB-below-dry energy (its band sensitivities stay active) |
| Saturation | FabFilter Saturn 2 (1 band, HQ "Superb") | 0 | drive calibrated per character (Warm Tape / Warm Tube) |
| Pitch | Auburn Sounds Graillon 3 (correction 0 %, pure shift) | 1074 | tracks a pure sine 24–36 ct flat (needs a voice); tone tests use absolute limits |
| Doubler | Waves Doubler2 Stereo (WaveShell 14.12) | 0 | EQ off, 2 voices 14/20 ms, ±8 ct |
| Choir | Waves Doubler4 Stereo (WaveShell 14.12) | 0 | 4 voices |
| Delay | FabFilter Timeless 3 | 0 | 375 ms, 30 %, 300 Hz–6 kHz, 100 % wet |
| Reverb | Valhalla VintageVerb, FabFilter Pro-R 2 | 0 | Plate 1.8 s / Concert Hall (Pro-R Modern) 2.6 s, 100 % wet |
| Vocal Smash | FabFilter Pro-C 2 (Punch, 10:1, 35 % mix) | 0 | parallel |
| Level Ride | Waves Vocal Rider Stereo | 0 | no music sidechain |
| Stutter, Pulse, Vocal Check | – | – | no installed reference (Kickstart is not installed); absolute limits |

Also loadable and available for later work: Pro-C 2 other styles, Ozone 11 modules (Equalizer, Dynamics, Vintage Tape,
Exciter; German decimal commas are handled), Pro-Q 3, Pro-MB, Pro-L 2, Vocal Rider Live.

## Results per module

The tables are generated (`bench.py <module> --update-doc`). gap = Headliner − primary reference.

<!-- BEGIN eq -->
#### EQ (`eq`)

| case | metric | Headliner | Pro-Q 4 | gap | tolerance | verdict |
|---|---|---|---|---|---|---|
| vocal | resp_max_dev_db (dB) | 0.99 | 3.13 | -2.13 | ≤ 0.50 | FAIL |
| vocal | resp_rms_dev_db (dB) | 0.27 | 1.02 | -0.76 | ≤ 0.20 | FAIL |
| vocal | resp_hf_max_dev_db (dB) | 0.64 | 0.49 | 0.15 | ≤ 0.50 | FAIL |
| vocal | resp_vs_ref_max_db (dB) – vs Pro-Q 4 | 2.91 | – | – | ref + 0.50 | info |
| vocal | crest_reduction_db (dB) – mean of clips | -0.77 | -0.93 | 0.16 | ref ± 1.00 | pass |
| vocal | st_loudness_std_lu (LU) – mean of clips | 3.97 | 4.04 | -0.06 | ref + 0.50 | pass |
| vocal | mix0_null_db (dB) – latency 0 | -152 | – | – | ≤ -60.0 | pass |
| vocal | off_null_db (dB) | -152 | – | – | ≤ -100 | pass |
| vocal | cpu_pct (%) – median over this module's renders | 0.24 | 0.05 | 0.19 | ref + 0.50 | pass |
| air | resp_max_dev_db (dB) | 5.50 | 0.97 | 4.53 | ≤ 0.50 | FAIL |
| air | resp_rms_dev_db (dB) | 1.65 | 0.29 | 1.36 | ≤ 0.20 | FAIL |
| air | resp_hf_max_dev_db (dB) | 5.50 | 0.97 | 4.53 | ≤ 0.50 | FAIL |
| air | resp_vs_ref_max_db (dB) – vs Pro-Q 4 | 5.51 | – | – | ref + 0.50 | info |
| air | cramp_dev_db (dB) – bell +6 dB Q 1.5: 15 kHz vs 1.5 kHz x10 | 5.50 | 0.06 | 5.43 | ref + 0.50 | FAIL |
| cut | resp_max_dev_db (dB) | 0.03 | 1.22 | -1.19 | ≤ 0.50 | pass |
| cut | resp_rms_dev_db (dB) | 0.00 | 0.42 | -0.41 | ≤ 0.20 | pass |
| cut | resp_hf_max_dev_db (dB) | 0.00 | 0.00 | -0.00 | ≤ 0.50 | pass |
| cut | resp_vs_ref_max_db (dB) – vs Pro-Q 4 | 1.22 | – | – | ref + 0.50 | info |
<!-- END eq -->

<!-- BEGIN comp -->
#### Compressor (`comp`)

| case | metric | Headliner | Pro-C 2 Vocal | Pro-C 2 Clean | gap | tolerance | verdict |
|---|---|---|---|---|---|---|---|
| vocal | ratio_measured (:1) | 3.89 | 12.6 | 4.37 | -8.68 | 4.00 ± 0.50 | pass |
| vocal | threshold_measured_db (dB) | -22.7 | -25.5 | -21.4 | 2.82 | -24.0 ± 2.00 | pass |
| vocal | curve_dev_db (dB) | 1.13 | 5.51 | 1.63 | -4.37 | ≤ 1.00 | FAIL |
| vocal | attack_ms (ms) – set 10 ms | 16.0 | 18.0 | 6.50 | -2.00 | ref ± 5.00 | pass |
| vocal | release_ms (ms) – set 120 ms | 120 | 139 | 66.5 | -19.5 | ref ± 40.0 | pass |
| vocal | thd_100hz_db (dB) | -48.0 | -57.4 | -41.9 | 9.35 | ref + 3.00 | FAIL |
| vocal | crest_reduction_db (dB) – mean of clips | 2.29 | 1.97 | 3.05 | 0.32 | ref ± 1.00 | pass |
| vocal | st_loudness_std_lu (LU) – mean of clips | 2.43 | 2.06 | 2.47 | 0.37 | ref ± 0.50 | pass |
| vocal | mix0_null_db (dB) – latency 0 | -152 | – | – | – | ≤ -60.0 | pass |
| vocal | off_null_db (dB) | -152 | – | – | – | ≤ -100 | pass |
| vocal | cpu_pct (%) – median over this module's renders | 0.24 | 0.23 | 0.19 | 0.02 | ref + 0.50 | pass |
<!-- END comp -->

<!-- BEGIN deess -->
#### De-Esser (`deess`)

Calibrated: vocal: Pro-DS Threshold=-50.1562

| case | metric | Headliner | Pro-DS | gap | tolerance | verdict |
|---|---|---|---|---|---|---|
| vocal | sib_reduction_db (dB) | 3.87 | 3.78 | 0.09 | ref − 1.00 | pass |
| vocal | nonsib_hf_change_db (dB) | -2.70 | -2.35 | -0.35 | ref − 0.50 | pass |
| vocal | selectivity_db (dB) | 1.16 | 1.43 | -0.27 | ref − 1.00 | pass |
| vocal | transient_change_db (dB) | 0.05 | 3.06 | -3.01 | 0.00 ± 1.00 | pass |
| vocal | inactive_null_db (dB) | -152 | – | – | ≤ -60.0 | pass |
| vocal | off_null_db (dB) | -152 | – | – | ≤ -100 | pass |
| vocal | cpu_pct (%) – median over this module's renders | 0.26 | 0.32 | -0.07 | ref + 0.50 | pass |
<!-- END deess -->

<!-- BEGIN smooth -->
#### Smooth (`smooth`)

Calibrated: vocal: soothe2 depth=0

| case | metric | Headliner | soothe2 | gap | tolerance | verdict |
|---|---|---|---|---|---|---|
| vocal | delta_db (dB) | -13.5 | -16.6 | 3.07 | ref − 0.00 | info |
| vocal | pte_reduction_db (dB) | 0.05 | 0.20 | -0.15 | ref − 0.30 | pass |
| vocal | ltas_reduction_db (dB) | 0.26 | 0.98 | -0.72 | ref − 0.50 | FAIL |
| vocal | flatness_gain_db (dB) | 1.03 | 0.79 | 0.24 | ref − 0.20 | pass |
| vocal | transient_change_db (dB) | 0.42 | 0.26 | 0.16 | 0.00 ± 0.50 | pass |
| vocal | musical_noise_db (dB) | 0.11 | 0.06 | 0.05 | ref + 0.20 | pass |
| vocal | off_null_db (dB) | -152 | – | – | ≤ -100 | pass |
| vocal | cpu_pct (%) – median over this module's renders | 1.44 | 1.24 | 0.19 | ref + 1.00 | pass |
<!-- END smooth -->

<!-- BEGIN sat -->
#### Saturation (`sat`)

Calibrated: tape: Saturn 2 Band 1 Drive=56.0938; tube: Saturn 2 Band 1 Drive=81.25; tape3band: Saturn 2 Band 2 Drive=56.0938

| case | metric | Headliner | Saturn 2 | gap | tolerance | verdict |
|---|---|---|---|---|---|---|
| tape | thd_1k_db (dB) | -19.8 | -19.9 | 0.05 | ref ± 1.00 | pass |
| tape | thd_1k_m24_db (dB) | -38.4 | -45.8 | 7.35 | ref − 0.00 | info |
| tape | h2_h3_db (dB) | -116 | -113 | -2.86 | ref − 0.00 | info |
| tape | aliasing_db (dB) | -91.5 | -70.2 | -21.2 | ref + 3.00 | pass |
| tape | imd_db (dB) | -5.74 | -2.41 | -3.33 | ref + 3.00 | pass |
| tape | crest_reduction_db (dB) – mean of clips | 7.38 | 8.72 | -1.34 | ref − 0.00 | info |
| tape | st_loudness_std_lu (LU) – mean of clips | 3.20 | 3.27 | -0.07 | ref − 0.00 | info |
| tape | mix0_null_db (dB) – latency 5 | -152 | – | – | ≤ -60.0 | pass |
| tape | off_null_db (dB) | -152 | – | – | ≤ -100 | pass |
| tape | cpu_pct (%) – median over this module's renders | 0.93 | 2.50 | -1.57 | ref + 1.00 | pass |
| tube | thd_1k_db (dB) | -15.3 | -15.3 | 0.07 | ref ± 1.00 | pass |
| tube | thd_1k_m24_db (dB) | -24.9 | -23.9 | -1.06 | ref − 0.00 | info |
| tube | h2_h3_db (dB) | 13.3 | -13.5 | 26.8 | ref − 0.00 | info |
| tube | aliasing_db (dB) | -91.5 | -66.2 | -25.3 | ref + 3.00 | pass |
| tube | imd_db (dB) | -5.31 | -1.52 | -3.79 | ref + 3.00 | pass |
| tape3band | thd_1k_db (dB) | -20.0 | -20.1 | 0.12 | ref ± 1.00 | pass |
| tape3band | thd_1k_m24_db (dB) | -38.6 | -45.9 | 7.35 | ref − 0.00 | info |
| tape3band | h2_h3_db (dB) | -116 | -111 | -4.92 | ref − 0.00 | info |
| tape3band | aliasing_db (dB) | -91.5 | -69.0 | -22.6 | ref + 3.00 | pass |
| tape3band | imd_db (dB) | -109 | -36.7 | -72.1 | ref + 3.00 | pass |
| tape3band | crest_reduction_db (dB) – mean of clips | 5.48 | 6.28 | -0.80 | ref − 0.00 | info |
| tape3band | st_loudness_std_lu (LU) – mean of clips | 3.05 | 3.08 | -0.03 | ref − 0.00 | info |
| tape3band | mix0_null_db (dB) – latency 5 | -152 | – | – | ≤ -60.0 | pass |
| tape3band | off_null_db (dB) | -152 | – | – | ≤ -100 | pass |
| tape3band | cpu_pct (%) – median over this module's renders | 1.16 | 2.63 | -1.46 | ref + 1.00 | pass |
<!-- END sat -->

<!-- BEGIN pitch -->
#### Pitch (`pitch`)

| case | metric | Headliner | Graillon 3 | gap | tolerance | verdict |
|---|---|---|---|---|---|---|
| up12 | pitch_err_cents (ct) | 2.70 | 36.0 | -33.3 | ≤ 2.00 | FAIL |
| up12 | pitch_jitter_cents (ct) | 0.00 | 0.00 | 0.00 | ≤ 1.00 | pass |
| up12 | spurious_db (dB) | -73.2 | -44.4 | -28.8 | ≤ -60.0 | pass |
| up12 | latency_ms (ms) | 120 | 22.4 | 97.6 | ref + 10.0 | FAIL |
| up12 | vocal_pitch_err_cents (ct) | 1.65 | 1.95 | -0.30 | ref + 1.00 | pass |
| up12 | hnr_drop_db (dB) | -8.51 | -2.55 | -5.96 | ref − 0.00 | info |
| up12 | mix0_null_db (dB) – latency 5760 | -152 | – | – | ≤ -60.0 | pass |
| up12 | off_null_db (dB) | -152 | – | – | ≤ -100 | pass |
| up12 | cpu_pct (%) – median over this module's renders | 2.24 | 0.75 | 1.50 | ref + 2.00 | pass |
| down5 | pitch_err_cents (ct) | 2.68 | 23.7 | -21.1 | ≤ 2.00 | FAIL |
| down5 | pitch_jitter_cents (ct) | 0.00 | 0.00 | -0.00 | ≤ 1.00 | pass |
| down5 | spurious_db (dB) | -90.9 | -55.4 | -35.4 | ≤ -60.0 | pass |
| down5 | latency_ms (ms) | 120 | 22.4 | 97.6 | ref + 10.0 | FAIL |
| down5 | vocal_pitch_err_cents (ct) | 1.02 | 1.14 | -0.12 | ref + 1.00 | pass |
| down5 | hnr_drop_db (dB) | -4.09 | 0.49 | -4.58 | ref − 0.00 | info |
<!-- END pitch -->

<!-- BEGIN doubler -->
#### Doubler (`doubler`)

| case | metric | Headliner | Doubler2 | gap | tolerance | verdict |
|---|---|---|---|---|---|---|
| default | comb_depth_db (dB) | 0.32 | 0.52 | -0.21 | ref + 1.00 | pass |
| default | am_roughness_db (dB) | 0.92 | 1.90 | -0.97 | ref + 0.50 | pass |
| default | lr_corr | 0.85 | 0.79 | 0.06 | ref + 0.10 | pass |
| default | mono_loss_db (dB) | 0.34 | 0.47 | -0.14 | ref + 1.00 | pass |
| default | mix0_null_db (dB) – latency 0 | -152 | – | – | ≤ -60.0 | pass |
| default | off_null_db (dB) | -152 | – | – | ≤ -100 | pass |
| default | cpu_pct (%) – median over this module's renders | 0.34 | 0.26 | 0.08 | ref + 1.00 | pass |
<!-- END doubler -->

<!-- BEGIN choir -->
#### Choir (`choir`)

| case | metric | Headliner | Doubler4 | gap | tolerance | verdict |
|---|---|---|---|---|---|---|
| default | comb_depth_db (dB) | 0.56 | 0.53 | 0.02 | ref + 1.00 | pass |
| default | am_roughness_db (dB) | 2.12 | 1.62 | 0.50 | ref + 0.50 | FAIL |
| default | lr_corr | 0.94 | 0.79 | 0.14 | ref + 0.10 | FAIL |
| default | mono_loss_db (dB) | 0.14 | 0.47 | -0.32 | ref + 1.00 | pass |
| default | mix0_null_db (dB) – latency 0 | -152 | – | – | ≤ -60.0 | pass |
| default | off_null_db (dB) | -152 | – | – | ≤ -100 | pass |
| default | cpu_pct (%) – median over this module's renders | 0.56 | 0.29 | 0.27 | ref + 1.00 | pass |
<!-- END choir -->

<!-- BEGIN delay -->
#### Delay (`delay`)

| case | metric | Headliner | Timeless 3 | gap | tolerance | verdict |
|---|---|---|---|---|---|---|
| plain | echo_time_err_ms (ms) | 0.08 | 0.17 | -0.09 | ref + 0.50 | pass |
| plain | feedback_db_per_repeat (dB) – set feedback 30% | -11.6 | -11.8 | 0.15 | ref ± 1.00 | pass |
| plain | echo_smear_ms (ms) | 0.17 | 0.19 | -0.02 | ref + 1.00 | pass |
| plain | inter_echo_floor_db (dB) | -288 | -103 | -185 | ref + 3.00 | pass |
| plain | dry_leak_db (dB) – burst, 100 % wet | -300 | -300 | 0.00 | ≤ -60.0 | pass |
| plain | mix0_null_db (dB) – latency 0 | -152 | – | – | ≤ -60.0 | pass |
| plain | off_null_db (dB) | -152 | – | – | ≤ -100 | pass |
| plain | cpu_pct (%) – median over this module's renders | 0.33 | 0.62 | -0.29 | ref + 1.00 | pass |
| tape | cpu_pct (%) – median over this module's renders | 0.33 | 0.62 | -0.29 | ref + 1.00 | pass |
<!-- END delay -->

<!-- BEGIN reverb -->
#### Reverb (`reverb`)

| case | metric | Headliner | VintageVerb | Pro-R 2 | gap | tolerance | verdict |
|---|---|---|---|---|---|---|---|
| plate | t30_s (s) – set decay 1.8 s | 1.79 | 1.49 | 1.53 | 0.30 | ref − 0.00 | info |
| plate | t30_err_pct (%) – set decay 1.8 s | 0.79 | 17.4 | 14.9 | -16.6 | ≤ 10.0 | pass |
| plate | edt_s (s) | 1.67 | 1.46 | 1.84 | 0.20 | ref − 0.00 | info |
| plate | curvature_db (dB) | 0.18 | 0.12 | 0.21 | 0.06 | ref + 0.50 | pass |
| plate | mean_ned | 0.99 | 0.90 | 0.90 | 0.09 | ref − 0.05 | pass |
| plate | mixing_ms (ms) | 22.0 | 107 | 72.0 | -85.0 | ref + 20.0 | pass |
| plate | modal_ringing_db (dB) | 8.28 | 6.91 | 8.23 | 1.37 | ref + 2.00 | pass |
| plate | tail_corr | -0.01 | -0.00 | -0.06 | -0.00 | ref + 0.10 | pass |
| plate | bass_ratio (x) | 1.03 | 0.98 | 1.24 | 0.04 | ref ± 0.20 | pass |
| plate | treble_ratio (x) | 0.87 | 0.91 | 0.96 | -0.04 | ref ± 0.20 | pass |
| plate | dry_leak_db (dB) – burst, 100 % wet | -300 | -151 | -300 | -149 | ≤ -60.0 | pass |
| plate | mix0_null_db (dB) – latency 0 | -152 | – | – | – | ≤ -60.0 | pass |
| plate | off_null_db (dB) | -152 | – | – | – | ≤ -100 | pass |
| plate | cpu_pct (%) – median over this module's renders | 0.83 | 0.24 | 0.65 | 0.59 | ref + 2.00 | pass |
| hall | t30_s (s) – set decay 2.6 s | 2.58 | 2.67 | 2.25 | -0.09 | ref − 0.00 | info |
| hall | t30_err_pct (%) – set decay 2.6 s | 0.87 | 2.51 | 13.4 | -1.64 | ≤ 10.0 | pass |
| hall | edt_s (s) | 1.96 | 2.17 | 2.06 | -0.22 | ref − 0.00 | info |
| hall | curvature_db (dB) | 0.35 | 0.28 | 0.14 | 0.06 | ref + 0.50 | pass |
| hall | mean_ned | 0.93 | 0.70 | 0.52 | 0.23 | ref − 0.05 | pass |
| hall | mixing_ms (ms) | 69.0 | 207 | 419 | -138 | ref + 20.0 | pass |
| hall | modal_ringing_db (dB) | 6.37 | 11.1 | 4.51 | -4.77 | ref + 2.00 | pass |
| hall | tail_corr | 0.01 | 0.01 | -0.02 | 0.01 | ref + 0.10 | pass |
| hall | bass_ratio (x) | 1.05 | 1.16 | 1.20 | -0.11 | ref ± 0.20 | pass |
| hall | treble_ratio (x) | 0.70 | 0.74 | 0.90 | -0.04 | ref ± 0.20 | pass |
<!-- END reverb -->

<!-- BEGIN smash -->
#### Vocal Smash (`smash`)

| case | metric | Headliner | Pro-C 2 Punch | gap | tolerance | verdict |
|---|---|---|---|---|---|---|
| default | ratio_measured (:1) | 1.12 | 1.46 | -0.33 | ref − 0.00 | info |
| default | threshold_measured_db (dB) | 6.87 | -35.5 | 42.4 | ref − 0.00 | info |
| default | attack_ms (ms) – set 2 ms | 2.00 | 2.00 | 0.00 | ref ± 5.00 | pass |
| default | release_ms (ms) – set 80 ms | 82.5 | 52.5 | 30.0 | ref ± 40.0 | pass |
| default | thd_100hz_db (dB) | -63.5 | -44.7 | -18.8 | ref + 3.00 | pass |
| default | crest_reduction_db (dB) – mean of clips | 0.69 | 1.20 | -0.51 | ref ± 2.00 | pass |
| default | st_loudness_std_lu (LU) – mean of clips | 3.33 | 3.10 | 0.23 | ref + 0.50 | pass |
| default | off_null_db (dB) | -152 | – | – | ≤ -100 | pass |
| default | cpu_pct (%) – median over this module's renders | 0.37 | 0.15 | 0.21 | ref + 0.50 | pass |
<!-- END smash -->

<!-- BEGIN ride -->
#### Level Ride (`ride`)

| case | metric | Headliner | Vocal Rider | gap | tolerance | verdict |
|---|---|---|---|---|---|---|
| default | st_loudness_std_lu (LU) – dry: 3.80 LU | 3.30 | 2.92 | 0.38 | ref + 0.30 | FAIL |
| default | gain_jitter_db (dB) | 0.07 | 0.05 | 0.02 | ref + 0.05 | pass |
| default | transient_change_db (dB) | -0.26 | -0.04 | -0.22 | ref ± 0.50 | pass |
| default | cpu_pct (%) – median over this module's renders | 0.19 | 0.09 | 0.10 | ref + 0.50 | pass |
<!-- END ride -->

<!-- BEGIN chop -->
#### Stutter (`chop`)

| case | metric | Headliner |  | gap | tolerance | verdict |
|---|---|---|---|---|---|
| gate | edge_splatter_db (dB) | -88.5 | – | ≤ -60.0 | pass |
| gate | gate_depth_db (dB) | -185 | – | ≤ -40.0 | pass |
| gate | off_null_db (dB) | -152 | – | ≤ -100 | pass |
| gate | cpu_pct (%) – median over this module's renders | 0.22 | – | ≤ 1.00 | pass |
<!-- END chop -->

<!-- BEGIN pulse -->
#### Pulse (`pulse`)

| case | metric | Headliner |  | gap | tolerance | verdict |
|---|---|---|---|---|---|
| default | edge_splatter_db (dB) | -151 | – | ≤ -60.0 | pass |
| default | gate_depth_db (dB) | -36.5 | – | ≤ -30.0 | pass |
| default | mix0_null_db (dB) – latency 0 | -152 | – | ≤ -60.0 | pass |
| default | off_null_db (dB) | -152 | – | ≤ -100 | pass |
| default | cpu_pct (%) – median over this module's renders | 0.19 | – | ≤ 1.00 | pass |
| classic | edge_splatter_db (dB) | -150 | – | ≤ -60.0 | pass |
| classic | gate_depth_db (dB) | -48.1 | – | ≤ -30.0 | pass |
| classic | cpu_pct (%) – median over this module's renders | 0.19 | – | ≤ 1.00 | pass |
| gate16 | edge_splatter_db (dB) | -151 | – | ≤ -60.0 | pass |
| gate16 | gate_depth_db (dB) | -108 | – | ≤ -30.0 | pass |
| gate16 | cpu_pct (%) – median over this module's renders | 0.19 | – | ≤ 1.00 | pass |
<!-- END pulse -->

<!-- BEGIN check -->
#### Vocal Check (`check`)

| case | metric | Headliner |  | gap | tolerance | verdict |
|---|---|---|---|---|---|
| null | null_vs_input_db (dB) | -152 | – | ≤ -120 | pass |
| null | cpu_pct (%) – median over this module's renders | 0.16 | – | ≤ 2.00 | pass |
<!-- END check -->

## Biggest quality gaps (ranked) and DSP recommendations

Ranked by audibility on dry vocals × size of the gap. 21 gated FAILs in 9 modules; De-Esser, Doubler, Delay, Vocal Smash,
Stutter, Pulse and Vocal Check pass at baseline.

1. **Saturation – aliasing 34 dB worse than Saturn 2** (Tape −36.6 vs −70.2 dB, Tube −47.1 vs −66.2 dB at equal THD;
   4987 Hz at −6 dBFS). Audible as grainy, inharmonic fizz on bright vocals and sibilants. *Do:* 4× (8× for the Hard
   character) polyphase oversampling around the shaper (half-band IIR cascade, constant latency), plus first-order ADAA on
   the static curves so 4× is enough; keep the DC/bias path inside the oversampled section. Target: aliasing ≤ −67 dB,
   THD unchanged (`thd_1k_db` stays the calibration point). The planned 3-band split (spec 5.6) must not add aliasing:
   oversample once around all three shapers. Character note (info): Tube is even-dominant (H2/H3 +13 dB) where Saturn's
   Warm Tube is odd-dominant (−13 dB), and Tape distorts more at −24 dBFS (−38 vs −46 dB) – a choice, not a defect.
   **Resolved (overhaul/sat):** 4× hiir half-band up, ADAA (double), half-band + order-11 elliptic decimator (stop band
   from the base Nyquist – a half-band alone lets 5 × 4987 Hz fold back); aliasing −91.5 dB in all cases incl. the new
   3-band case (Saturn 2 3-band −69.0), THD within 0.12 dB of the calibration, latency 5 samples (was 3), CPU 0.9 % /
   1.2 % (3 bands) vs Saturn 2 2.5 / 2.6 %. After-renders: `velvox-bench\2026-10-25\modules-after\sat\`.
2. **EQ – cramping near Nyquist: a 15 kHz bell is 5.5 dB off** its own shape (Pro-Q 4: 0.06 dB). The +6 dB "air" bell
   lands narrower and lower; the 12 kHz shelf in the vocal case is 0.64 dB off. *Do:* Vicanek magnitude-matched
   biquads (or MZTi / 2× oversampling for bands above ~fs/8) for bell, shelves and cuts; the cut/shelf prototypes already
   match below 6 kHz (cut case 0.02 dB). This is spec 5.1's "analog-matched response". Target: `cramp_dev_db` ≤ 0.56,
   response ≤ 0.5 dB off the analog prototype in all three cases. (Remaining vocal-case deviation 0.99 dB is the HPF
   region −0.19 dB at 100 Hz plus the HF shelf.)
3. **Pitch – 120 ms latency vs 22 ms (Graillon 3), a constant +2.7 ct ratio error, 3× the CPU.** Spurious energy on a
   tone is excellent (−73 dB vs −44 dB) and vocal pitch accuracy is equal (1.7 vs 1.9 ct). *Do:* fix the semitone→ratio
   (or sample-rate) path – 220 Hz ×2 comes out at 441.1 Hz; shrink the Signalsmith block/interval to ~1024/256 at 48 kHz
   (≈ 30 ms) or add a low-latency PSOLA path for monophonic vocals with the stretch as fallback; check "phasiness" on
   sustained notes by ear in `listen\`.
4. **Reverb – Plate rings (modal_ringing 11.1 dB vs 7.5 VintageVerb / 8.9 Pro-R 2)**, Hall decays 15 % short of the set
   2.6 s (EDT 1.65 vs 2.1–2.2 s). Density, mixing time, smoothness and stereo are already better than both references.
   *Do (plate):* modulate the tank delays (slow random-walk, ±5–10 samples, different rates per line), add two more
   allpasses with incommensurate lengths in the input diffuser, and give each loop its own 1-pole absorption so no mode
   decays slower than its neighbours. *Do (hall):* derive per-line feedback from the actual loop lengths
   (g = 10^(−3·L/(fs·T60))) and calibrate the type's decay scaling so T30 = set decay ± 10 %.
   **Done (reverb track, 0.6):** passes every gated metric (table above). Plate ringing 8.3 dB (VintageVerb 6.9,
   Pro-R 2 8.2), T30 within 1 % for plate and hall and within 5.2 % for all 8 types at 1 / 2.6 / 6 s
   (was 10–31 % short: the damping shortened the highs, where a white impulse has most of its energy).
   Fixes: broadband decay compensation (`ReverbModule::decayCompensationFor`), per-line absorption shelves on
   one common RT60(f) law, short in-loop all-passes, deeper per-line modulation at incommensurate rates (a
   random walk rang 1–2 dB more than sines in the measurements), 6 input all-passes. After-renders:
   `velvox-bench\2026-10-25\modules-after\reverb\`. Character note (info): Headliner's mixing time (22 / 69 ms)
   stays much shorter than the references' (72–419 ms) – a denser onset; the mids ring ~10–20 % longer than the
   set decay so that the whole tail meets it (VintageVerb does the same).
5. **Smooth – removes broadband instead of targeting resonances.** At more removed energy than soothe2 (Amount 75 vs
   depth 0) the largest stationary resonance drops 0.26 dB (soothe2 0.98 dB) and the STFT peak-to-envelope only 0.05 dB
   (0.20 dB); flatness gain is higher (1.03 vs 0.79) – the hallmark of an overall dulling. Musical noise and transients are
   on par. *Do:* the spec 5.4 STFT design – 2048/512 Hann (latency ≤ 1024 if using 1024/256), spectral envelope from a
   1/3-oct (or cepstral) smoothing, gain only where |X| exceeds the envelope by > sensitivity, per-bin attack/release and
   ±2-bin frequency smoothing of the gain mask, phase-coherent overlap-add, depth scaled by excess (soothe-like). Re-check
   `musical_noise_db` and `transient_change_db` – those are where STFT designs usually regress.
6. **Compressor – ballistics distortion at 100 Hz: −48 dB vs −57 dB (Pro-C 2 Vocal)**; static curve 1.13 dB off the
   ideal soft knee (limit 1.0). Audible as grit on low male notes with 10/120 ms. *Do:* move the smoothing into the log
   domain (Giannoulis et al. 2012, branching smooth detector) with a short peak-hold (~ one 50 Hz period) or an RMS
   detector option, and a program-dependent release (fast/slow dual release); check the knee polynomial against the
   ideal (the knee is 6 dB wide at threshold −24, ratio 4).
7. **Choir – narrow and rough:** L/R correlation 0.94 vs 0.79 (Waves Doubler4), AM roughness 2.12 vs 1.62 dB on a
   sustained tone. *Do:* spread voices to fixed, alternating pan positions scaled by Width (outer voices ±80–100 %),
   decorrelate per-voice modulation (independent smoothed random walks, 0.1–0.6 Hz), and normalise the voice sum so
   beating between equal-gain voices does not dominate (slightly different voice gains / delays).
8. **Level Ride – evens the vocal less than Vocal Rider:** short-term loudness spread 3.30 LU vs 2.92 LU (dry 3.80) at
   ±8 dB range, with similar smoothness (gain jitter 0.07 vs 0.05 dB). *Do:* faster word-level response (or 50–100 ms
   lookahead with constant latency), raise the effective Amount mapping, keep the transient preservation (−0.26 dB now).

Passing modules – what to keep: De-Esser equals Pro-DS at equal sibilance reduction (selectivity 1.16 vs 1.43 dB,
lisp −2.7 vs −2.35 dB, transients untouched where Pro-DS adds +3 dB) and nulls perfectly when inactive; Delay matches
Timeless 3 timing/feedback with a cleaner floor; Doubler is smoother than Doubler2 (roughness 0.92 vs 1.90 dB) – the
new engine (5.8) must not lose that; Smash has 19 dB less LF distortion than Pro-C 2 Punch; Stutter/Pulse edges are
click-free (> 5 kHz splatter −88 / −151 dB). CPU: everything is within budget except Pitch (2.2 % of real time per
instance vs 0.75 % for Graillon; under the 2 % margin, but the most expensive module).

## Pass/fail tolerances for the DSP agents (Q6)

A module **passes** when every gated metric of its tables is `pass` (`bench.py <module>` prints the FAIL count). Key
metrics and rules (source of truth: `tolerances` in `tools/refhost/modules.py`; "ref" = primary reference value from a fresh
run, so the gate follows the reference at the same settings):

| module | key metrics (must pass) | rule |
|---|---|---|
| EQ | `cramp_dev_db`; `resp_max_dev_db`, `resp_hf_max_dev_db`, `resp_rms_dev_db` | cramping ≤ Pro-Q 4 + 0.5 dB; response vs own analog prototype ≤ 0.5 / 0.5 / 0.2 dB (all cases, 20 Hz–20 kHz) |
| Compressor | `thd_100hz_db`, `curve_dev_db`, `ratio_measured`, `threshold_measured_db`, `attack_ms`, `release_ms` | THD ≤ Pro-C 2 Vocal + 3 dB; curve ≤ 1 dB off ideal; ratio 4 ± 0.5, threshold −24 ± 2 dB; attack ± 5 ms, release ± 40 ms of Pro-C 2 |
| De-Esser | `sib_reduction_db`, `selectivity_db`, `nonsib_hf_change_db`, `transient_change_db`, `inactive_null_db` | at calibrated equal action: selectivity ≥ Pro-DS − 1 dB, lisp ≥ Pro-DS − 0.5 dB, transients 0 ± 1 dB, inactive null ≤ −60 dB |
| Smooth | `ltas_reduction_db`, `pte_reduction_db`, `flatness_gain_db`, `musical_noise_db`, `transient_change_db` | ≥ soothe2 − 0.5 / − 0.3 / − 0.2 dB; musical noise ≤ soothe2 + 0.2 dB; transients 0 ± 0.5 dB; CPU ≤ soothe2 + 1 % |
| Saturation | `aliasing_db`, `imd_db`, `thd_1k_db` (calibration check) | aliasing and IMD ≤ Saturn 2 (HQ) + 3 dB at equal THD |
| Pitch | `pitch_err_cents`, `pitch_jitter_cents`, `spurious_db`, `vocal_pitch_err_cents`, `latency_ms` | tone error ≤ 2 ct, jitter ≤ 1 ct, spurious ≤ −60 dB; vocal error ≤ Graillon + 1 ct; latency ≤ Graillon + 10 ms |
| Doubler / Choir | `comb_depth_db`, `am_roughness_db`, `lr_corr`, `mono_loss_db` | ≤ Waves Doubler + 1 dB / + 0.5 dB / + 0.1 / + 1 dB |
| Delay | `echo_time_err_ms`, `feedback_db_per_repeat`, `echo_smear_ms`, `dry_leak_db` | ≤ 0.5 ms, Timeless ± 1 dB, ≤ Timeless + 1 ms, dry at 100 % wet ≤ −60 dB |
| Reverb | `modal_ringing_db`, `t30_err_pct`, `curvature_db`, `mean_ned`, `mixing_ms`, `tail_corr`, `dry_leak_db` | ringing ≤ VintageVerb + 2 dB; T30 within 10 % of the set decay; curvature ≤ ref + 0.5 dB; density ≥ ref − 0.05; mixing ≤ ref + 20 ms; dry ≤ −60 dB |
| Vocal Smash | `thd_100hz_db`, `attack_ms`, `release_ms`, `crest_reduction_db` | THD ≤ Pro-C 2 + 3 dB; times ± 5 / ± 40 ms; crest ± 2 dB |
| Level Ride | `st_loudness_std_lu`, `gain_jitter_db`, `transient_change_db` | ≤ Vocal Rider + 0.3 LU; jitter ≤ ref + 0.05 dB; transients ± 0.5 dB |
| Stutter / Pulse | `edge_splatter_db`, `gate_depth_db` | splatter ≤ −60 dB; closed level ≤ −40 dB (Stutter) / −30 dB (Pulse) |
| Vocal Check | `null_vs_input_db` | ≤ −120 dB (never changes the audio) |
| all with Mix / On | `mix0_null_db`, `off_null_db`, `cpu_pct` | mix 0 ≤ −60 dB, off ≤ −100 dB vs dry; CPU ≤ ref + 0.5–2 % per module |

Rounds (Q6): at most 3 improvement rounds per module; a remaining FAIL is documented in `docs/BENCHMARK.md` and work moves on.
A DSP change that improves the key metric but makes another gated metric FAIL is not a pass.

## Limits of this baseline

- No parameter automation during a render: zipper / time-change smoothness (delay time sweeps, EQ gain rides) is not
  measured here; those stay unit tests in `VelvoxTests` (the delay agent should add one).
- Metrics are objective proxies. They catch aliasing, cramping, ringing, ballistics distortion and broadband-vs-targeted
  removal reliably; they do not replace the user's listening test (files in `listen\`).
- Doubler/Choir: Headliner's current Doubler has no per-voice controls, so only global behaviour is compared; the new
  engine (spec §5.8) should get per-voice delay/detune tracking added to `proc_mod_widen`.
- CPU is single-instance wall time per 512-sample block on this PC (28 cores, other builds running in parallel) – compare
  ratios, not absolute values.
