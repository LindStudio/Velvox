#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ParameterIDs.h"
#include "../DSP/EqualizerModule.h"
#include <map>

// The parameter side of the 0.6 EQ bands: role bands (macros, automatic mix, Vocal Check), the
// migration of 0.5 states / presets, and the bridge that keeps the hidden 0.5 fixed-band parameters
// working for code that still writes them (the 0.5 EQ panel, the assistant).
//
// Indices: `instance` 0..3 (0 = "EQ", 1 = "EQ 2" ...), `band` 0..15 (parameter IDs count from 1:
// band 0 = "eqBand1...").
namespace velvox::eqbands
{
    // Reads and writes parameter values in REAL units (Hz, dB, Q; bools 0/1; choices as index) by ID
    struct Access
    {
        std::function<float (const juce::String& id)> get;
        std::function<void (const juce::String& id, float value)> set;
    };

    // Live parameters (message thread). Writes are host gestures; unchanged values are skipped.
    Access accessFor (juce::AudioProcessorValueTreeState& apvts);
    // A value map (e.g. a plan): reads `values`, then `fallback`, then the parameter default; writes
    // into `values`, clamped to the parameter range (choices rounded)
    Access accessFor (std::map<juce::String, float>& values, const std::map<juce::String, float>* fallback = nullptr);

    juce::String bandParamId (int instance, int band, const char* field);
    dsp::EqBand readBand (const Access& access, int instance, int band);
    void writeBand (const Access& access, int instance, int band, const dsp::EqBand& value);

    int findBand (const Access& access, int instance, dsp::EqRole role);   // -1: none (role none: always -1)
    int findFreeBand (const Access& access, int instance);                 // -1: all 16 in use

    // The band that carries `role` (also when it is switched off), or a free slot claimed for it.
    // Sets type, frequency, gain, Q (and the slope when >= 0), the role, and switches it on.
    // Bands of other roles and user bands (role none) are never changed. Returns the band, or -1 when
    // every slot is taken (or role is none).
    int findOrCreateBand (const Access& access, int instance, dsp::EqRole role, dsp::EqBandType type,
                          float freqHz, float gainDb, float q, int slope = -1);

    // ---- Migration of 0.5 states (no "eqBand*" values) --------------------------------------
    bool containsBandValues (const juce::StringArray& ids);
    bool containsBandValues (const std::map<juce::String, float>& values);

    // True when the 0.5 fixed bands of `instance` are all at their parameter defaults
    bool fixedBandsAtDefaults (const Access& access, int instance);

    // Writes bands 1-5 of `instance` from its 0.5 fixed bands (EqualizerModule::legacyBands) and,
    // when `legacyResponse`, sets eqLegacyResponse so they keep the 0.5 sound bit for bit
    void migrateInstance (const Access& access, int instance, bool legacyResponse);

    // Every EQ instance of a 0.5 state: instance 0 always, further instances only when their fixed
    // bands were changed from the defaults (an unused "EQ 2" stays empty)
    void migrateState (const Access& access, bool legacyResponse);

    // The migrated EQ switches to the matched (analog) response (message thread, one gesture)
    void clearLegacyResponse (juce::AudioProcessorValueTreeState& apvts, int instance);

    // ---- Bridge: 0.5 fixed-band parameters -> role bands --------------------------------------
    // While a ScopedBridgeSuspend exists for a plugin instance's APVTS (state loading, presets,
    // snapshots, the automatic mix) that instance's bridge does nothing; other instances in the same
    // process are not affected. The bridge itself only acts on the message thread; the suspend may
    // be taken on any thread (a host restoring a state), so the per-instance depth is locked.
    struct ScopedBridgeSuspend
    {
        explicit ScopedBridgeSuspend (const juce::AudioProcessorValueTreeState& apvts);
        ~ScopedBridgeSuspend();
        static bool isActive (const juce::AudioProcessorValueTreeState& apvts);
        JUCE_DECLARE_NON_COPYABLE (ScopedBridgeSuspend)
    private:
        const juce::AudioProcessorValueTreeState& state;
    };

    // Owned by the processor. A change of a hidden 0.5 fixed-band parameter on the message thread
    // (the old EQ panel, the assistant, tests) is written into the band of the matching role
    // (low cut -> lowCut, low -> warmth, mid -> autoMix, high -> air, high cut -> highCut). A missing
    // role band is created only when it would be audible. Host automation (audio thread) is ignored:
    // these IDs are no longer automatable.
    class LegacyBridge final : private juce::AudioProcessorValueTreeState::Listener
    {
    public:
        explicit LegacyBridge (juce::AudioProcessorValueTreeState& apvts);
        ~LegacyBridge() override;

    private:
        void parameterChanged (const juce::String& parameterID, float newValue) override;

        juce::AudioProcessorValueTreeState& apvts;
        juce::StringArray ids;
        // Last value seen per ID: JUCE also reports a first "change" to the same value
        std::map<juce::String, std::atomic<float>> lastValues;
    };
}

namespace velvox
{
    // API for Vocal Check "Anwenden" (message thread, on the processor's APVTS): finds the EQ band
    // of `role` in EQ `instance` or creates it in a free slot, and sets it to type / frequency / gain /
    // Q, switched on. One host gesture per changed parameter. Never touches user bands (role none) or
    // bands of other roles. Returns the band index 0..15, or -1 when all 16 bands are in use.
    // It does not switch the EQ module on or insert it into the chain - the caller does that.
    int findOrCreateEqBand (juce::AudioProcessorValueTreeState& apvts, dsp::EqRole role, dsp::EqBandType type,
                            float freqHz, float gainDb, float q, int instance = 0);
}
