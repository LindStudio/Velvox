"""What the bench measures per Headliner module: matched settings for Headliner and each reference plugin,
which procedures run, and the pass/fail tolerances (spec section 8 Q6).

Headliner parameters are APVTS IDs in real units (see `HeadlinerRefHost --list --headliner eq`). Reference
parameters are (name, display text) pairs in the order they are applied (see `HeadlinerRefHost --list --plugin ...`);
`setn` pairs are normalized 0..1 values for parameters without readable text.
"""
from __future__ import annotations

from dataclasses import dataclass, field

FF = "FabFilter/FabFilter "
WAVES = "WaveShell1-VST3 14.12_x64.vst3"


@dataclass
class Ref:
    name: str
    plugin: str
    sub: str = ""
    params: list = field(default_factory=list)      # [(name, text)]
    setn: list = field(default_factory=list)        # [(name, normalized)]


@dataclass
class Calib:
    """Pick the reference setting whose probe metric is closest to Headliner's (matched amount of action)."""
    param: str
    values: list
    probe: str          # see bench.PROBES
    step: float = 0.0   # > 0: the bisection stays on this grid (plugins that reject other values)


@dataclass
class Case:
    name: str
    headliner: dict
    procedures: list
    refs: dict = field(default_factory=dict)        # ref name -> extra [(name, text)] for this case
    calibrate: dict = field(default_factory=dict)   # ref name -> Calib
    info: dict = field(default_factory=dict)        # procedure inputs (targets, set values)


@dataclass
class Module:
    key: str
    title: str
    refs: list
    cases: list
    tolerances: dict = field(default_factory=dict)  # metric -> (better, tol[, absolute]) overrides
    mix_param: str = ""                             # for the mix-0 null test


# ---------------------------------------------------------------------------------------------
# Common pieces
# ---------------------------------------------------------------------------------------------

def _pq4_band(n, used, shape, freq, gain=None, q=None, slope=None):
    p = [(f"Band {n} Used", "Used" if used else "Unused"), (f"Band {n} Shape", shape), (f"Band {n} Frequency", str(freq))]
    if gain is not None:
        p.append((f"Band {n} Gain", str(gain)))
    if q is not None:
        p.append((f"Band {n} Q", str(q)))
    if slope is not None:
        p.append((f"Band {n} Slope", slope))
    return p


EQ_OFF = {"hpfOn": 0, "lpfOn": 0, "lowGain": 0, "midGain": 0, "highGain": 0}

PRO_C2_BASE = [("Auto Gain", "Off"), ("Lookahead", "0"), ("Range", "60"), ("Wet Gain", "0"), ("Oversampling", "Off")]

MODULES: dict[str, Module] = {}


def _add(m: Module):
    MODULES[m.key] = m


# ---------------------------------------------------------------------------------------------
# EQ vs Pro-Q 4 (and the analog prototypes)
# ---------------------------------------------------------------------------------------------
_add(Module(
    key="eq", title="EQ", mix_param="eqMix",
    refs=[Ref("Pro-Q 4", FF + "Pro-Q 4.vst3")],
    cases=[
        Case("vocal",
             headliner={**EQ_OFF, "hpfOn": 1, "hpfFreq": 100, "hpfSlope": 1, "midFreq": 3000, "midGain": 4, "midQ": 1.0,
                        "highType": 0, "highFreq": 12000, "highGain": 5, "highQ": 0.71},
             refs={"Pro-Q 4": _pq4_band(1, True, "Low Cut", 100, slope="24 dB/oct", q=0.71)
                   + _pq4_band(2, True, "Bell", 3000, 4, 1.0) + _pq4_band(3, True, "High Shelf", 12000, 5, 0.71)},
             procedures=["eq_response", "vocal", "nulls", "cpu"],
             info={"analog": [("hpf", 100.0, 4), ("bell", 3000.0, 4.0, 1.0), ("highshelf", 12000.0, 5.0, 0.71)]}),
        Case("air",
             headliner={**EQ_OFF, "midFreq": 15000, "midGain": 6, "midQ": 1.5},
             refs={"Pro-Q 4": _pq4_band(1, True, "Bell", 15000, 6, 1.5)},
             procedures=["eq_response", "eq_cramping"],
             info={"analog": [("bell", 15000.0, 6.0, 1.5)],
                   "cramp": {"lo": {"headliner": {"midFreq": 1500}, "Pro-Q 4": [("Band 1 Frequency", "1500")]},
                             "hi": {"headliner": {"midFreq": 15000}, "Pro-Q 4": [("Band 1 Frequency", "15000")]}}}),
        Case("cut",
             headliner={**EQ_OFF, "midFreq": 350, "midGain": -6, "midQ": 2.0, "lowType": 0, "lowFreq": 150, "lowGain": -3,
                        "lowQ": 0.71},
             refs={"Pro-Q 4": _pq4_band(1, True, "Bell", 350, -6, 2.0) + _pq4_band(2, True, "Low Shelf", 150, -3, 0.71)},
             procedures=["eq_response"],
             info={"analog": [("bell", 350.0, -6.0, 2.0), ("lowshelf", 150.0, -3.0, 0.71)]}),
    ],
    # Pro-Q 4 uses its own Q / slope definitions (e.g. a 24 dB/oct cut is -6 dB at the corner), so the response
    # accuracy is judged against Headliner's own analog prototypes (absolute limits); cramping is compared to Pro-Q 4.
    tolerances={"resp_max_dev_db": ("lower", 0.0, 0.5), "resp_hf_max_dev_db": ("lower", 0.0, 0.5),
                "resp_rms_dev_db": ("lower", 0.0, 0.2), "cramp_dev_db": ("lower", 0.5), "cpu_pct": ("lower", 0.5)}))

# ---------------------------------------------------------------------------------------------
# Compressor vs Pro-C 2 (Vocal style)
# ---------------------------------------------------------------------------------------------
_COMP_H = {"compThreshold": -24, "compRatio": 4, "compAttack": 10, "compRelease": 120, "compKnee": 6, "compMakeup": 0,
           "compMix": 100, "compDetector": 0, "compScHpf": 20}
_COMP_R = PRO_C2_BASE + [("Style", "Vocal"), ("Threshold", "-24"), ("Ratio", "4"), ("Knee", "6"), ("Attack", "10"),
                         ("Release", "120")]
_add(Module(
    key="comp", title="Compressor", mix_param="compMix",
    refs=[Ref("Pro-C 2 Vocal", FF + "Pro-C 2.vst3", params=_COMP_R),
          Ref("Pro-C 2 Clean", FF + "Pro-C 2.vst3", params=PRO_C2_BASE + [("Style", "Clean")] + _COMP_R[6:])],
    cases=[Case("vocal", headliner=_COMP_H, procedures=["dyn_curve", "dyn_timing", "thd_lf", "vocal", "nulls", "cpu"],
                info={"threshold": -24, "ratio": 4, "knee": 6, "attack": 10, "release": 120})],
    # Pro-C 2 "Vocal" is program dependent (its static ratio steepens to ~12:1), so ratio / threshold are judged against
    # the set values; ballistics and THD against Pro-C 2 Vocal.
    tolerances={"curve_dev_db": ("lower", 0.0, 1.0), "ratio_measured": ("match", 0.5, 4.0),
                "threshold_measured_db": ("match", 2.0, -24.0), "attack_ms": ("match", 5.0),
                "release_ms": ("match", 40.0), "thd_100hz_db": ("lower", 3.0), "crest_reduction_db": ("match", 1.0),
                "st_loudness_std_lu": ("match", 0.5), "cpu_pct": ("lower", 0.5)}))

# ---------------------------------------------------------------------------------------------
# De-esser vs Pro-DS (Single Vocal, split band), threshold calibrated to the same sibilance reduction
# ---------------------------------------------------------------------------------------------
_add(Module(
    key="deess", title="De-Esser",
    refs=[Ref("Pro-DS", FF + "Pro-DS.vst3",
              params=[("Mode", "Single Vocal"), ("Band Processing", "Split Band"), ("Range", "6"), ("Lookahead Enabled", "Disabled"),
                      ("High-Pass Frequency", "6000"), ("Low-Pass Frequency", "20000")])],
    cases=[Case("vocal", headliner={"deessFreq": 6500, "deessThreshold": -30, "deessRange": 6, "deessMode": 0, "deessListen": 0},
                procedures=["deess_vocal", "deess_inactive_null", "nulls", "cpu"],
                calibrate={"Pro-DS": Calib("Threshold", [-60, -55, -50, -45, -40, -35, -30, -25, -20], "sib_reduction")})],
    tolerances={"sib_reduction_db": ("higher", 1.0), "nonsib_hf_change_db": ("higher", 0.5), "selectivity_db": ("higher", 1.0),
                "transient_change_db": ("match", 1.0, 0.0), "inactive_null_db": ("lower", 0.0, -60.0), "cpu_pct": ("lower", 0.5)}))

# ---------------------------------------------------------------------------------------------
# Smooth vs soothe2, depth calibrated to the same removed energy
# ---------------------------------------------------------------------------------------------
_SMOOTH = {"smoothAmount": 75, "smoothSensitivity": 50, "smoothLow": 800, "smoothHigh": 12000,
           "smoothAttack": 5, "smoothRelease": 80, "smoothMode": 0, "smoothOutput": 0, "smoothDelta": 0}
_SOOTHE_DEPTH = Calib("depth", [0, 0.5, 1, 1.5, 2, 3, 4, 5, 6, 8, 10], "delta_energy", step=0.1)
_add(Module(
    key="smooth", title="Smooth",
    refs=[Ref("soothe2", "oeksound/soothe2_x64.vst3", params=[("mode", "soft"), ("mix", "100"), ("delta", "off")])],
    cases=[Case("vocal", headliner=_SMOOTH,
                procedures=["resonance_vocal", "nulls", "cpu"],
                calibrate={"soothe2": _SOOTHE_DEPTH}),
           # more settings (0.6 smooth track): lighter, more selective, hard mode - each calibrated to equal removed energy
           Case("light", headliner=dict(_SMOOTH, smoothAmount=35), procedures=["resonance_vocal"],
                calibrate={"soothe2": _SOOTHE_DEPTH}),
           Case("sharp", headliner=dict(_SMOOTH, smoothSensitivity=85), procedures=["resonance_vocal"],
                calibrate={"soothe2": _SOOTHE_DEPTH}),
           Case("hard", headliner=dict(_SMOOTH, smoothAmount=60, smoothMode=1), refs={"soothe2": [("mode", "hard")]},
                procedures=["resonance_vocal"], calibrate={"soothe2": _SOOTHE_DEPTH})],
    # delta_db is the calibration match (soothe2 depth 0 already removes ~15 dB below the dry level on f7_row, so
    # Headliner runs at Amount 75 to act comparably); informative only.
    tolerances={"delta_db": ("info", 0.0), "pte_reduction_db": ("higher", 0.3), "ltas_reduction_db": ("higher", 0.5), "flatness_gain_db": ("higher", 0.2),
                "transient_change_db": ("match", 0.5, 0.0), "musical_noise_db": ("lower", 0.2), "cpu_pct": ("lower", 1.0)}))

# ---------------------------------------------------------------------------------------------
# Saturation vs Saturn 2 (drive calibrated to the same THD at 1 kHz / -12 dBFS)
# ---------------------------------------------------------------------------------------------
_SAT_R = [("Num Active Bands", "1"), ("Band 1 Mix", "100"), ("Band 1 Dynamics", "0"), ("Band 1 Feedback Amount", "0"),
          ("High Quality Mode", "Superb"), ("Output Gain", "0")]
_add(Module(
    key="sat", title="Saturation", mix_param="satMix",
    refs=[Ref("Saturn 2", FF + "Saturn 2.vst3", params=_SAT_R)],
    cases=[
        Case("tape", headliner={"satDrive": 12, "satCharacter": 1, "satMix": 100, "satOutput": 0},
             refs={"Saturn 2": [("Band 1 Style", "Warm Tape")]},
             procedures=["sat_tones", "vocal", "nulls", "cpu"],
             calibrate={"Saturn 2": Calib("Band 1 Drive", [5, 10, 15, 20, 30, 40, 50, 65, 80, 100], "thd_1k")}),
        Case("tube", headliner={"satDrive": 12, "satCharacter": 2, "satMix": 100, "satOutput": 0},
             refs={"Saturn 2": [("Band 1 Style", "Warm Tube")]},
             procedures=["sat_tones"],
             calibrate={"Saturn 2": Calib("Band 1 Drive", [5, 10, 15, 20, 30, 40, 50, 65, 80, 100], "thd_1k")}),
    ],
    tolerances={"aliasing_db": ("lower", 3.0), "thd_1k_db": ("match", 1.0), "thd_1k_m24_db": ("info", 0.0),
                "crest_reduction_db": ("info", 0.0), "st_loudness_std_lu": ("info", 0.0),
                "imd_db": ("lower", 3.0), "cpu_pct": ("lower", 1.0)}))

# ---------------------------------------------------------------------------------------------
# Pitch vs Graillon 3 (correction off, pure shift)
# ---------------------------------------------------------------------------------------------
_GRAILLON = [("Corr. Amount", "0"), ("Wet Mix", "0"), ("Dry Mix", "-inf"), ("Formant Shift", "0")]
_add(Module(
    key="pitch", title="Pitch", mix_param="pitchMix",
    refs=[Ref("Graillon 3", "Auburn Sounds Graillon 3.vst3", params=_GRAILLON)],
    cases=[Case("up12", headliner={"pitchSemitones": 12, "pitchCents": 0, "pitchPreserveFormants": 1, "pitchFormant": 0, "pitchMix": 100},
                refs={"Graillon 3": [("Pitch Shift", "12")]}, procedures=["pitch_tones", "pitch_vocal", "nulls", "cpu"],
                info={"semitones": 12}),
           Case("down5", headliner={"pitchSemitones": -5, "pitchCents": 0, "pitchPreserveFormants": 1, "pitchFormant": 0, "pitchMix": 100},
                refs={"Graillon 3": [("Pitch Shift", "-5")]}, procedures=["pitch_tones", "pitch_vocal"],
                info={"semitones": -5})],
    # Graillon tracks a pure sine 36 ct flat (its tracker wants a voice), so the tone tests use absolute limits.
    tolerances={"pitch_err_cents": ("lower", 0.0, 2.0), "pitch_jitter_cents": ("lower", 0.0, 1.0), "spurious_db": ("lower", 0.0, -60.0),
                "vocal_pitch_err_cents": ("lower", 1.0), "latency_ms": ("lower", 10.0),
                "cpu_pct": ("lower", 2.0)}))

# ---------------------------------------------------------------------------------------------
# Doubler vs Waves Doubler2; Choir vs Waves Doubler4
# ---------------------------------------------------------------------------------------------
_DBL = [("EQLowOnOff", "Off"), ("EQHighOnOff", "Off"), ("Direct OnOff", "On"), ("Direct Gain", "0")]
_add(Module(
    key="doubler", title="Doubler", mix_param="doublerMix",
    refs=[Ref("Doubler2", WAVES, "Doubler2 Stereo",
              params=_DBL + [("Voice1 Delay", "14"), ("Voice2 Delay", "20"), ("Voice1 Detune", "8"), ("Voice2 Detune", "-8"),
                             ("Voice1 Gain", "-6"), ("Voice2 Gain", "-6"), ("Voice1 Depth", "3"), ("Voice2 Depth", "3")])],
    cases=[Case("default", headliner={"doublerMix": 50, "doublerWidth": 80, "doublerDelay": 14, "doublerDetune": 8,
                                      "doublerMonoSafe": 50, "doublerTone": 20000},
                procedures=["mod_widen", "vocal_width", "nulls", "cpu"])],
    tolerances={"comb_depth_db": ("lower", 1.0), "am_roughness_db": ("lower", 0.5), "lr_corr": ("lower", 0.1),
                "mono_loss_db": ("lower", 1.0), "cpu_pct": ("lower", 1.0)}))

_add(Module(
    key="choir", title="Choir", mix_param="choirMix",
    refs=[Ref("Doubler4", WAVES, "Doubler4 Stereo",
              params=_DBL + [("Voice2 OnOff", "On"), ("Voice4 OnOff", "On")]
              + [(f"Voice{v} Gain", "-9") for v in range(1, 5)] + [(f"Voice{v} Depth", "4") for v in range(1, 5)])],
    cases=[Case("default", headliner={"choirVoices": 1, "choirDetune": 10, "choirTiming": 18, "choirHumanize": 50, "choirWidth": 80,
                                      "choirDensity": 50, "choirMix": 35},
                procedures=["mod_widen", "vocal_width", "nulls", "cpu"])],
    tolerances={"comb_depth_db": ("lower", 1.0), "am_roughness_db": ("lower", 0.5), "lr_corr": ("lower", 0.1),
                "mono_loss_db": ("lower", 1.0), "cpu_pct": ("lower", 1.0)}))

# ---------------------------------------------------------------------------------------------
# Delay vs Timeless 3 (375 ms, 30 % feedback, 300 Hz - 6 kHz filters, 100 % wet)
# ---------------------------------------------------------------------------------------------
_TL3 = [("Delay Sync", "Free"), ("Delay Time", "375"), ("Delay Read Mode", "Tape"), ("Ping Pong", "Ping Pong Off"),
        ("Feedback", "30"), ("Mix", "100"), ("Drive", "0"), ("Lo-Fi Enabled", "Off"), ("Diffuse Enabled", "Off"),
        ("Pitch Shift Enabled", "Off"), ("Dynamics Enabled", "Off"),
        ("Filter 1 Used", "Used"), ("Filter 1 Shape", "High Pass"), ("Filter 1 Freq", "300"), ("Filter 1 Slope", "12 dB/oct"),
        ("Filter 1 Style", "Clean"),
        ("Filter 2 Used", "Used"), ("Filter 2 Shape", "Low Pass"), ("Filter 2 Freq", "6000"), ("Filter 2 Slope", "12 dB/oct"),
        ("Filter 2 Style", "Clean"), ("Filter 2 Peak", "0"), ("Filter 3 Used", "Unused")]
_add(Module(
    key="delay", title="Delay", mix_param="delayMix",
    refs=[Ref("Timeless 3", FF + "Timeless 3.vst3", params=_TL3, setn=[("Delay Time Pan", 0.5)])],
    cases=[Case("plain", headliner={"delaySync": 0, "delayTime": 375, "delayFeedback": 30, "delayMix": 100, "delayLowCut": 300,
                                    "delayHighCut": 6000, "delayPingPong": 0, "delaySpread": 0, "delayDrive": 0, "delayDiffusion": 0,
                                    "delayMod": 0, "delayDuckAmount": 0},
                procedures=["delay_ir", "dry_leak", "vocal_listen", "nulls", "cpu"],
                info={"time_ms": 375, "feedback": 0.30})],
    tolerances={"echo_time_err_ms": ("lower", 0.5), "feedback_db_per_repeat": ("match", 1.0), "echo_smear_ms": ("lower", 1.0),
                "inter_echo_floor_db": ("lower", 3.0), "dry_leak_db": ("lower", 0.0, -60.0), "cpu_pct": ("lower", 1.0)}))

# ---------------------------------------------------------------------------------------------
# Reverb vs Valhalla VintageVerb and Pro-R 2 (1.8 s plate / 2.6 s hall)
# ---------------------------------------------------------------------------------------------
_VVV = [("Mix", "100"), ("PreDelay", "20"), ("ModDepth", "30"), ("LowCut", "150"), ("HighCut", "12000"), ("BassMult", "1.0")]
_PRR = [("Mix", "100"), ("Predelay", "20"), ("Lock Mix", "Unlocked"), ("Ducking", "0"), ("Auto Gate Enabled", "Disabled")]
_REV_H = {"reverbMix": 100, "reverbPreDelay": 20, "reverbLowCut": 150, "reverbHighCut": 12000, "reverbWidth": 100,
          "reverbBass": 1.0, "reverbDuckAmount": 0, "reverbMod": 30, "reverbEarly": 50, "reverbDiffusion": 70}
_add(Module(
    key="reverb", title="Reverb", mix_param="reverbMix",
    refs=[Ref("VintageVerb", "Valhalla DSP/ValhallaVintageVerb.vst3", params=_VVV),
          Ref("Pro-R 2", FF + "Pro-R 2.vst3", params=_PRR)],
    cases=[
        Case("plate", headliner={**_REV_H, "reverbType": 1, "reverbDecay": 1.8, "reverbSize": 60, "reverbDamping": 9000},
             refs={"VintageVerb": [("ReverbMode", "Plate"), ("Decay", "1.8"), ("Size", "60"), ("HighShelf", "-6"), ("HighFreq", "9000")],
                   "Pro-R 2": [("Style", "Plate"), ("Space", "1.8")]},
             procedures=["reverb_ir", "dry_leak", "vocal_listen", "nulls", "cpu"], info={"decay_s": 1.8}),
        Case("hall", headliner={**_REV_H, "reverbType": 2, "reverbDecay": 2.6, "reverbSize": 70, "reverbDamping": 6500},
             refs={"VintageVerb": [("ReverbMode", "Concert Hall"), ("Decay", "2.6"), ("Size", "70"), ("HighShelf", "-6"),
                                   ("HighFreq", "6500")],
                   "Pro-R 2": [("Style", "Modern"), ("Space", "2.6")]},
             procedures=["reverb_ir", "vocal_listen"], info={"decay_s": 2.6}),
    ],
    tolerances={"t30_err_pct": ("lower", 0.0, 10.0), "t30_s": ("info", 0.0), "edt_s": ("info", 0.0),
                "curvature_db": ("lower", 0.5), "mean_ned": ("higher", 0.05),
                "mixing_ms": ("lower", 20.0), "modal_ringing_db": ("lower", 2.0), "tail_corr": ("lower", 0.1),
                "dry_leak_db": ("lower", 0.0, -60.0), "cpu_pct": ("lower", 2.0)}))

# ---------------------------------------------------------------------------------------------
# Vocal Smash vs Pro-C 2 (Punch, fast, parallel)
# ---------------------------------------------------------------------------------------------
_add(Module(
    key="smash", title="Vocal Smash",
    refs=[Ref("Pro-C 2 Punch", FF + "Pro-C 2.vst3",
              params=PRO_C2_BASE + [("Style", "Punch"), ("Threshold", "-36"), ("Ratio", "10"), ("Knee", "6"), ("Attack", "2"),
                                    ("Release", "80"), ("Mix", "35")])],
    cases=[Case("default", headliner={"smashAmount": 50, "smashBlend": 35, "smashAttack": 2, "smashRelease": 80, "smashTone": 20,
                                      "smashOutput": 0},
                procedures=["dyn_curve", "dyn_timing", "thd_lf", "vocal", "nulls", "cpu"],
                info={"attack": 2, "release": 80})],
    # parallel compression: the static curve of the blend has no single ratio / threshold (informative only)
    tolerances={"ratio_measured": ("info", 0.0), "threshold_measured_db": ("info", 0.0),
                "thd_100hz_db": ("lower", 3.0), "attack_ms": ("match", 5.0), "release_ms": ("match", 40.0),
                "crest_reduction_db": ("match", 2.0), "cpu_pct": ("lower", 0.5)}))

# ---------------------------------------------------------------------------------------------
# Level Ride vs Waves Vocal Rider
# ---------------------------------------------------------------------------------------------
_add(Module(
    key="ride", title="Level Ride",
    refs=[Ref("Vocal Rider", WAVES, "Vocal Rider Stereo",
              params=[("Target", "-18"), ("Range Max", "8"), ("Range Min", "-8"), ("Attack", "Fast"), ("Vocal Sensitivity", "0")])],
    cases=[Case("default", headliner={"rideTarget": -18, "rideAmount": 70, "rideMaxBoost": 8, "rideMaxCut": 8, "rideResponse": 50,
                                      "ridePhrase": 40, "rideTransient": 60, "rideSilence": -50},
                procedures=["ride_vocal", "cpu"])],
    tolerances={"st_loudness_std_lu": ("lower", 0.3), "gain_jitter_db": ("lower", 0.05), "transient_change_db": ("match", 0.5),
                "cpu_pct": ("lower", 0.5)}))

# ---------------------------------------------------------------------------------------------
# No installed reference: Stutter (chop), Pulse, Vocal Check - absolute limits only
# ---------------------------------------------------------------------------------------------
_add(Module(
    key="chop", title="Stutter", refs=[],
    cases=[Case("gate", headliner={"chopPattern": 0, "chopRate": 1, "chopDepth": 100, "chopSmooth": 3},
                procedures=["gate_tone", "nulls", "cpu"])],
    tolerances={"edge_splatter_db": ("lower", 0.0, -60.0), "gate_depth_db": ("lower", 0.0, -40.0), "cpu_pct": ("lower", 0.0, 1.0)}))

_add(Module(
    key="pulse", title="Pulse", refs=[], mix_param="pulseMix",
    cases=[Case("default", headliner={"pulseRate": 2, "pulseDepth": 100, "pulseMix": 100},
                procedures=["gate_tone", "nulls", "cpu"])],
    tolerances={"edge_splatter_db": ("lower", 0.0, -60.0), "gate_depth_db": ("lower", 0.0, -30.0), "cpu_pct": ("lower", 0.0, 1.0)}))

_add(Module(
    key="check", title="Vocal Check", refs=[],
    cases=[Case("null", headliner={}, procedures=["check_null", "cpu"])],
    tolerances={"null_vs_input_db": ("lower", 0.0, -120.0), "cpu_pct": ("lower", 0.0, 2.0)}))
