#pragma once

#include "../DSP/EqDesign.h"
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

// 0.8 (docs/superpowers/specs/2026-11-02-automix-08-tailored-design.md): what a finished vocal of a
// genre should measure - the target the automatic mix shapes the voice towards. Jakob chooses the
// genre and the vibe when he starts the auto mix; the vibe moves the genre's target.
//
// The party curve is measured on finished professional party vocals (PartyTargetCurve.h, generated
// by `VocalypseTests --target-curve`); the other genres and all vibes are estimated offsets from it
// until there is data for them (marked `estimated`).
namespace vocalypse::automix
{
    enum class Genre { party = 0, rap, pop, edm, ballad };               // persisted as the int value
    enum class Vibe { punchy = 0, aggressive, floaty, warm, natural };   // persisted as the int value
    inline constexpr int numGenres = 5, numVibes = 5;

    // ---- Third-octave grid, 80 Hz .. 16 kHz (thirdCentre (k) = 80 Hz * 2^(k/3))
    inline constexpr int numThirds = 24;
    float thirdCentre (int k) noexcept;

    // Power per third of a VocalProfiler spectrum (1/12 octave from 40 Hz; band 12 + 4k is the
    // centre of third k) with `bands` applied, in dB relative to the 300 Hz - 2 kHz core - the same
    // measure as autoeq::levelsOf. A third without content is -200.
    std::vector<float> thirdsOf (const std::vector<float>& spectrumDb, const std::vector<dsp::EqBand>& bands = {});

    struct DynamicsTarget { float phraseLo, phraseHi, crestLo, crestHi; };   // dB (VocalProfiler dynamicsDb, crestDb)

    // Effect amounts relative to the planner's 0.7 values (1 = unchanged); doublerMix in %;
    // decayBars: reverb time in bars of 4 beats when the host tempo is known
    struct SpaceTarget { float reverbScale, delayScale, doublerMix, decayBars; };

    struct MixTarget
    {
        Genre genre = Genre::party;
        Vibe vibe = Vibe::punchy;
        std::array<float, numThirds> curveDb {};    // relative to the core, like thirdsOf
        std::array<float, numThirds> spreadDb {};   // what still counts as on target (dB)
        // The vibe's share of curveDb: a deliberate shift, applied in full on top of the spread
        // (residualDb), so a voice already on the genre's curve still moves by the vibe
        std::array<float, numThirds> styleDb {};
        DynamicsTarget dynamics { 3.0f, 6.0f, 6.5f, 10.0f };
        float densityBlend = 18.0f;                 // Vocal Smash blend (%) for the target density
        int satCharacter = 1;                       // 0 Warm, 1 Tape, 2 Tube, 3 Hard
        float satDrive = 5.0f, satMix = 30.0f;
        float sibilanceMax = -6.0f;                 // S frames against vowel frames (dB)
        SpaceTarget space { 1.0f, 1.0f, 20.0f, 1.0f };
        bool estimated = false;                     // offsets, not measured
    };

    MixTarget targetFor (Genre genre, Vibe vibe);

    // How far third k of a voice (relative to the core) is off the target: the deviation from the
    // genre's curve outside its spread, minus the vibe's shift. > 0 = too much, 0 = on target.
    float residualDb (const MixTarget& target, int k, float voiceDb) noexcept;

    // What the user chose at the start of the auto mix
    struct MixIntent
    {
        Genre genre = Genre::party;
        Vibe vibe = Vibe::punchy;
    };

    juce::String genreName (Genre genre);   // both languages (txt::both)
    juce::String vibeName (Vibe vibe);
}
