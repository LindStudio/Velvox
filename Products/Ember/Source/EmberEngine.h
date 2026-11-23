#pragma once

#include "DSP/DspUtilities.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#if defined (_M_X64) || defined (__SSE2__)
 #include <emmintrin.h>
 #define EMBER_SSE 1
#else
 #define EMBER_SSE 0
#endif

// EMBER De-Esser - the audio engine (no JUCE plugin classes, so the tests drive it directly).
//
//   input ─► (L/R or M/S) ──┬──────────────────── delay (latency L) ──► band / wide gain ──► (back to L/R) ─► mix ─► out
//                           └─ detector per channel (4th-order high-pass at `freq`, relative to the full-band level)
//                              + smart gate (S band against the voice body, or against the band under the S in
//                              Allround) ─► stereo link ─► soft-knee 4:1 ─► range ─► attack / release ─► delay
//
// The latency is constant (maxLookaheadMs) whatever the lookahead, so hosts never see it change; the lookahead
// only decides how early the reduction starts before an S.
//
// 2026-11-23 (fix/ember-dsp, towards FabFilter Pro-DS):
//  - Split: a dynamic bell built as  y = x + c * (bell(x) - x),  bell = a fixed matched (Vicanek) cut of
//    bandRefDb. The bell runs all the time and only the blend c changes, per sample: no stepped coefficients,
//    no zipper, the same shape at 44.1 and 192 kHz (no bilinear cramping), and c = 0 is an exact identity
//    (bit-exact null without reduction). Analog equivalent: a Regalia-Mitra bell with fixed poles.
//  - Band width in octaves (Width): narrow = only the S, wide = the S and the air above it.
//  - The smart gate is smoothed by a running minimum + moving average (no fast flutter, and by construction
//    never more reduction than the gate allows: nothing leaks into the next vowel).
//  - Stereo link 0..100 % and Stereo / Mid-Side / Mid / Side processing.
//  - Allround detection (mixes, buses, instruments: no voice body needed).
//  - Listen S band = what is processed: in Split the band itself (4th-order high-passed at its lower edge, so the voice
//    under it does not bleed through the gentle bell skirt), in Wideband the detector band.
//  - Switching listen, mode, lookahead or stereo mode cross-fades (no clicks).
//
// 2026-11-24 (r2 research, docs/products/ember-r2/DSP_FORSCHUNG.md):
//  - S tracking: the split band is a blend of three fixed matched bells half an octave apart. A tracker (three
//    band-passes around the band behind a high-pass at its lower edge, 5 ms energies) finds where the energy of the current S sits and the blend
//    weights follow it, so the band sits on every S ("sh" lower, "s" higher). Only the weights move: no
//    coefficient modulation, and with no reduction the output is still bit-exact.
namespace ember
{
    using namespace vocalypse::dsp;

    struct Settings
    {
        float freq = 6200.0f;        // detection corner, Hz (the split band sits on splitCenterRatio x freq)
        float thresholdDb = -32.0f;
        float rangeDb = 5.0f;        // most the S band is ever reduced (before Amount)
        float amount = 0.5f;         // Amount 0..1; 0.5 = the reduction threshold / range ask for, 1 = 1.5x (amountScale)
        float attackMs = 0.5f;
        float releaseMs = 60.0f;
        float lookaheadMs = 5.0f;    // 0 .. maxLookaheadMs
        float mix = 1.0f;
        float outputDb = 0.0f;
        int mode = 0;                // 0 = split (only the S band), 1 = wideband
        bool relative = true;        // detection relative to the full-band level (loud and quiet phrases alike)
        bool smart = true;           // only where the S band outweighs the body of the voice (relative implied)
        int listen = 0;              // 0 = off, 1 = S band (what is processed), 2 = delta (what is removed)
        bool bypass = false;
        // added 2026-11-23 (fix/ember-dsp)
        float widthOct = 3.0f;       // split band width (between the half-reduction points), octaves
        float stereoLink = 1.0f;     // 0 = every channel on its own, 1 = fully linked
        int stereoMode = 0;          // 0 = stereo (L/R), 1 = mid/side, 2 = mid only, 3 = side only
        bool allround = false;       // with smart: compare against the band under the S (mixes, buses, instruments)
        // added 2026-11-24 (r2 research)
        bool trackS = true;          // split band follows each S (false = one fixed bell, the band before 2026-11-24)
    };

    // Fixed latency = the longest lookahead
    inline constexpr float maxLookaheadMs = 10.0f, defaultLookaheadMs = 5.0f;
    // Split band: centred on the typical S of a voice whose detection corner is `freq` (AUTO puts the corner at
    // 0.8 x the S centre). Width = octaves between the points where the band takes half its dB; its lower edge
    // never goes under splitLowestRatio x freq (a wide band grows into the air, not into the presence).
    inline constexpr float splitCenterRatio = 1.25f;
    inline constexpr float minWidthOct = 1.0f, maxWidthOct = 4.0f, defaultWidthOct = 3.0f;
    inline constexpr float splitLowestRatio = 0.45f;
    // The fixed matched cut the dynamic band blends towards (any reduction is a blend / extrapolation of it)
    inline constexpr float bandRefDb = 12.0f;
    // Width is measured at this depth (the half-dB points of a widthRefDb cut)
    inline constexpr float widthRefDb = 6.0f;
    // Centre frequency and pole Q (Regalia-Mitra form) of the split band for a detection corner and a width
    struct BandShape { double centreHz, poleQ, lowHz, highHz; };   // low / high = the half-reduction points
    BandShape splitBand (double freq, double widthOct) noexcept;
    // The fixed matched cut of the split band (what the engine runs) and the band's response in dB at `hz` when
    // it takes `reductionDb` off (tests, display)
    BiquadCoeffs splitBell (double sampleRate, double freq, double widthOct) noexcept;
    double splitResponseDb (double sampleRate, double freq, double widthOct, double reductionDb, double hz, bool tracked = true) noexcept;
    // |gain| of a cut at its centre (clamped): the blend is scaled by 1 / (1 - it), so the centre takes exactly the
    // asked-for depth, also far past the fixed cut (2026-11-24: a 24 dB reduction was ~1.2 dB short before)
    double bellCentreGain (const BiquadCoeffs& c, double sampleRate, double centreHz) noexcept;
    // The blend b that gives the gain g at the bell's centre, D = 1 - its response there: |1 - b D| = g
    // (exact also far past the fixed cut and with the matched design's phase at its centre); g >= 1 -> exactly 0
    inline float blendForGain (float g, float dRe, float dAbs2, float dIm2) noexcept
    {
        if (g >= 1.0f)
            return 0.0f;
        return (dRe - std::sqrt (std::max (0.0f, dAbs2 * g * g - dIm2))) / dAbs2;
    }
    // S tracking (2026-11-24): three bells, trackSpreadOct apart around the split band's centre, each
    // trackSpreadOct x 2 narrower than the band (at least trackMinBellOct): with the S in the middle the band is
    // the middle bell; a lower or higher S moves the weight to the outer bells, so together they still cover
    // the band's width. Tracker: band-passes on the three centres (two sections each), trackEnvMs energies.
    inline constexpr float trackSpreadOct = 0.5f, trackMinBellOct = 1.0f, trackEnvMs = 5.0f, trackDeadZone = 0.1f;
    struct TrackBells { double centreHz[3]; double widthOct; };
    TrackBells trackBells (double freq, double widthOct) noexcept;
    // Matched (Vicanek) cut of bandRefDb on `centreHz`, `widthOct` between its half-reduction points (widthRefDb)
    BiquadCoeffs bellAt (double sampleRate, double centreHz, double widthOct) noexcept;
    // Blend weights of the three bells for tracker energies e[0..2] (low, middle, high): a triangle around the
    // energy centroid; silence -> the middle bell
    void trackWeights (const float* e, float* w) noexcept;
    // Old split bell (2026-11-22, RBJ Q 0.4 at 1.25 x freq) - about the same as splitBand at 3 octaves
    inline constexpr float splitQ = 0.4f;

    // The detector band loses ~5 dB per octave of S energy as the corner rises (15 takes: 5 kHz +1.5 dB,
    // 8 kHz -1.9 dB against 6.5 kHz). The detector adds it back, so a threshold means the same at every
    // frequency and the voice buttons aim instead of turning EMBER up or down.
    inline constexpr float detectorTiltDbPerOct = 5.0f, detectorTiltRefHz = 6500.0f;
    // Amount: 0 % = off, 50 % = as set, 100 % = 1.5x
    inline float amountScale (float amount) noexcept
    {
        return amount <= 0.5f ? 2.0f * amount : 1.0f + (amount - 0.5f);
    }
    inline constexpr float ratio = 4.0f;
    inline constexpr float kneeDb = 6.0f;
    inline constexpr float maxReductionDb = 24.0f;
    // Relative detection: band level - full-band envelope + referenceDb, so a threshold means the same as in
    // absolute mode for a voice peaking at -18 dBFS. Below fullFloorDb (near-silence) nothing is de-essed.
    inline constexpr float referenceDb = -18.0f;
    inline constexpr float fullFloorDb = -60.0f;
    // Smart detection (2026-11-13, measured against FabFilter Pro-DS on 9 real takes): the energy above the
    // smart band against the body of the voice (150 Hz - 3 kHz), smartEnvMs RMS envelopes. Real S sit at
    // +1 .. +20 dB, vowels under -13 dB, the loud S-into-vowel transitions at -10 .. -1 dB. Weight 0 at
    // smartNoneDb, 1 at smartFullDb: a transition keeps its vowel, only the S itself is taken down.
    // The smart band follows the frequency (smartBandRatio x freq: 4 kHz at the default 6.2 kHz).
    inline constexpr float smartNoneDb = -1.0f, smartFullDb = 5.0f;
    inline constexpr float smartBandRatio = 0.65f, smartBandMinHz = 3500.0f, smartBandMaxHz = 6000.0f;
    inline constexpr float smartEnvMs = 5.0f;
    // The smart band at the default frequency (the fixed value before 2026-11-23)
    inline constexpr float smartBandHz = 4000.0f;
    // Allround: the smart band against the band right under it (allroundLowRatio .. allroundHighRatio x freq)
    // instead of the voice body - works on mixes and instruments too, where the body is always full
    inline constexpr float allroundLowRatio = 0.3f, allroundHighRatio = 0.6f;
    inline constexpr float allroundNoneDb = -6.0f, allroundFullDb = 0.0f;
    // Gate smoothing window (running minimum + moving average) in split / wideband: wideband moves the whole
    // voice, so its gate glides slower. The gate leads the audio by the lookahead (at most latency - window).
    inline constexpr float gateSplitMs = 2.0f, gateWideMs = 6.0f;
    // Cross-fade time for listen / mode / lookahead / stereo mode switches
    inline constexpr float switchFadeMs = 15.0f;

    // Static curve: reduction in dB (>= 0) for a detector level, soft knee
    float staticReductionDb (float inDb, float thresholdDb) noexcept;

    // Fast log2 / exp2 (|error| < 0.0002 dB in level terms): the detector runs them for every sample
    namespace fast
    {
        inline float log2 (float x) noexcept
        {
            x = std::max (x, 1.0e-30f);
            std::uint32_t i;
            std::memcpy (&i, &x, 4);
            const float e = (float) ((int) ((i >> 23) & 0xffu) - 127);
            i = (i & 0x007fffffu) | 0x3f800000u;
            float m;
            std::memcpy (&m, &i, 4);
            const float t = m - 1.0f;
            return e + t * (1.4418258f + t * (-0.7086821f + t * (0.41542195f + t * (-0.19442267f + t * 0.04588553f))));
        }
        inline float exp2 (float x) noexcept
        {
            x = std::clamp (x, -126.0f, 126.0f);
            int ip = (int) x;
            if ((float) ip > x)
                --ip;
            const float fl = (float) ip;
            float f = x - fl;
            if (f >= 1.0f)
            {
                // x a hair under an integer: x - floor (x) rounds up to 1 (exp2 (-1e-9) gave 0.99999986, not 1, so the
                // gain never got back to exactly 1 after an S until the envelope underflowed seconds later)
                f = 0.0f;
                ++ip;
            }
            const float p = 1.0f + f * (0.69315275f + f * (0.24015319f + f * (0.05582818f + f * (0.00898895f + f * 0.00187664f))));
            const std::uint32_t i = (std::uint32_t) (ip + 127) << 23;
            float s;
            std::memcpy (&s, &i, 4);
            return p * s;
        }
        inline float gainToDb (float g) noexcept { return 6.0205999f * log2 (g); }
        // reduction in dB (>= 0) -> gain; exactly 1 for no reduction
        inline float reductionToGain (float db) noexcept { return db <= 0.0f ? 1.0f : exp2 (-0.16609640f * db); }
    }

    // Four float biquads side by side (TDF II, one SSE register per coefficient / state where available): the
    // detector runs its three filter chains for both channels as two of these per stage
    struct Biquad4
    {
       #if EMBER_SSE
        __m128 b0 = _mm_set1_ps (1.0f), b1 = _mm_setzero_ps(), b2 = _mm_setzero_ps(), a1 = _mm_setzero_ps(), a2 = _mm_setzero_ps();
        __m128 s1 = _mm_setzero_ps(), s2 = _mm_setzero_ps();
        void setLane (int lane, const BiquadCoeffs& c) noexcept
        {
            alignas (16) float t[4];
            const auto put = [&] (__m128& r, double v) { _mm_store_ps (t, r); t[lane] = (float) v; r = _mm_load_ps (t); };
            put (b0, c.b0); put (b1, c.b1); put (b2, c.b2); put (a1, c.a1); put (a2, c.a2);
        }
        void reset() noexcept { s1 = s2 = _mm_setzero_ps(); }
        inline __m128 process (__m128 x) noexcept
        {
            const __m128 y = _mm_add_ps (_mm_mul_ps (b0, x), s1);
            s1 = _mm_add_ps (_mm_sub_ps (_mm_mul_ps (b1, x), _mm_mul_ps (a1, y)), s2);
            s2 = _mm_sub_ps (_mm_mul_ps (b2, x), _mm_mul_ps (a2, y));
            return y;
        }
        bool isFinite() const noexcept
        {
            alignas (16) float t[8];
            _mm_store_ps (t, s1);
            _mm_store_ps (t + 4, s2);
            for (float v : t)
                if (! std::isfinite (v))
                    return false;
            return true;
        }
       #else
        float b0[4] { 1, 1, 1, 1 }, b1[4] {}, b2[4] {}, a1[4] {}, a2[4] {}, s1[4] {}, s2[4] {};
        void setLane (int lane, const BiquadCoeffs& c) noexcept
        {
            b0[lane] = (float) c.b0; b1[lane] = (float) c.b1; b2[lane] = (float) c.b2; a1[lane] = (float) c.a1; a2[lane] = (float) c.a2;
        }
        void reset() noexcept { for (int l = 0; l < 4; ++l) s1[l] = s2[l] = 0.0f; }
        inline void process (const float* x, float* y) noexcept
        {
            for (int l = 0; l < 4; ++l)
            {
                y[l] = b0[l] * x[l] + s1[l];
                s1[l] = b1[l] * x[l] - a1[l] * y[l] + s2[l];
                s2[l] = b2[l] * x[l] - a2[l] * y[l];
            }
        }
        bool isFinite() const noexcept
        {
            for (int l = 0; l < 4; ++l)
                if (! std::isfinite (s1[l]) || ! std::isfinite (s2[l]))
                    return false;
            return true;
        }
       #endif
    };

    // The detector alone (used by the engine and by the Auto analysis, so both measure the same thing)
    class Detector
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void setFrequency (float hz) noexcept;   // recomputes the filters (detector, smart and allround bands)
        void setAllround (bool on) noexcept;      // the gate compares against the band under the S
        // One frame of all channels -> linked detector level in dB (relative or absolute). `band` receives the
        // filtered band of channel 0 / 1 when not null.
        float process (const float* frame, int numChannels, bool relative, float* band = nullptr) noexcept;

        // The same with every channel's own level and weight as well (for stereo link < 100 %)
        struct Frame
        {
            float linkedDb = -200.0f, linkedWeight = 0.0f;
            float levelDb[maxChannels] {}, weight[maxChannels] {}, band[maxChannels] {};
        };
        void process (const float* frame, int numChannels, bool relative, bool perChannel, Frame& out) noexcept;
        bool isFinite() const noexcept;

        // Smart detection: how much the energy of the S band outweighs the body of the voice (or, in Allround,
        // the band under the S), in dB, from 5 ms RMS envelopes, and the resulting weight 0..1 (linked).
        float getSibilanceIndexDb() const noexcept { return indexDb; }
        float getSibilanceWeight() const noexcept { return weight; }
        static float weightForIndex (float indexDb) noexcept { return weightForIndex (indexDb, false); }
        static float weightForIndex (float indexDb, bool allround) noexcept;

    private:
        float levelFor (float peak, float fullEnvValue, bool relative) const noexcept;
        float indexFor (float sE, float bE) const noexcept;
        void designBands() noexcept;

        double fs = 44100.0, freqHz = 6200.0;
        bool allround = false;
        // stage 1 / 2: lanes [band L, band R, smart L, smart R] and [body L, body R, -, -]
        Biquad4 bandIdx1, bandIdx2, body1, body2;
        float fullEnv = 0.0f, fullRelease = 0.0f, envCoeff = 0.0f;
        float sEnv = 0.0f, bodyEnv = 0.0f;   // energies (mean square), linked
        float fullEnvCh[maxChannels] {}, sEnvCh[maxChannels] {}, bodyEnvCh[maxChannels] {};
        float indexDb = -60.0f, weight = 0.0f, tiltDb = 0.0f, weightCh[maxChannels] {};
        unsigned indexPhase = 0;
    };

    // Running minimum over `window` samples followed by a moving average over `window` samples: a smooth gate
    // that never exceeds its input (out(t) <= in(t)), with linear ramps of `window` samples. Delay window - 1.
    class MinAverage
    {
    public:
        void prepare (int maxWindow);
        void setWindow (int window) noexcept;   // resets
        void reset() noexcept;
        float process (float x) noexcept;
        int getWindow() const noexcept { return window; }

    private:
        int window = 1, cap = 1;
        std::vector<float> dqVal, hBuf;
        std::vector<int> dqIdx;
        int head = 0, count = 0, hPos = 0, t = 0, run = 0;
        double sum = 0.0, invWindow = 1.0;
        float lastX = -1.0f;
    };

    class Engine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();
        void setSettings (const Settings& s) noexcept;
        const Settings& getSettings() const noexcept { return settings; }
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        int getLatencySamples() const noexcept { return latency; }
        static int latencyFor (double sampleRate) noexcept { return (int) std::lround (maxLookaheadMs * 0.001 * sampleRate); }

        // UI feed (message thread reads)
        PeakMeter gainReductionDb, inputPeak, outputPeak;
        SampleTap inputTap, outputTap;
        std::atomic<float> currentReductionDb { 0.0f };
        // Loudest detector level of the last block (dB, relative or absolute as set) and the smart weight there
        std::atomic<float> detectorDb { -100.0f }, detectorWeight { 0.0f };

    private:
        void updateBand (bool tracker = true) noexcept;   // tracker = also the tracker's band-passes
        void resetTrack() noexcept;
        int lookaheadSamples (float ms) const noexcept;

        double fs = 44100.0;
        int latency = 0;
        Settings settings;
        int activeStereoMode = 0;    // the stereo mode the filters run in (switches while the wet signal is faded out)
        Detector detector;
        // Split band: three fixed matched cuts of bandRefDb (lanes 0..2: low, middle, high) per processing channel,
        // blended by the tracker weights; the tracker's band-passes on the same centres (two sections)
        Biquad4 bells[maxChannels], trackBp1[maxChannels], trackBp2[maxChannels], trackHp[maxChannels];
        float trackE[3] {}, trackCoeff = 0.0f;
        bool trackOn = true;
        Biquad listenHp, listenHp2;  // listening to the split band: 4th-order high-pass at its lower edge
        bool listenHpLive = false;
        float bandScale = 1.0f;      // 1 / (1 - bell gain at its centre): (bell(x) - x) * bandScale = the band
        // D = 1 - (middle) bell response at its centre (complex): the blend b with |1 - b D| = the asked-for gain
        float dRe = 0.75f, dAbs2 = 0.5625f, dIm2 = 0.0f;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freq { 6200.0f }, width { 3.0f };
        juce::SmoothedValue<float> mixSm { 1.0f }, outSm { 1.0f }, bypassSm { 0.0f }, modeSm { 0.0f }, wetSm { 1.0f };
        juce::SmoothedValue<float> listenSm[3];
        float attackCoeff = 0.0f, releaseCoeff = 0.0f;
        // control slots: 0 / 1 = channel (or the shared one in slot 0 when fully linked), 2 = linked beside own
        static constexpr int numSlots = maxChannels + 1;
        float envDb[numSlots] {};

        // Lookahead (samples) and the cross-fade from the previous one
        int laCur = 0, laPrev = 0, laFadeLeft = 0, laFadeLen = 1;
        // Smart gate smoothing per processing channel, one per mode (cross-faded with the mode)
        MinAverage gate[2][numSlots];
        int gateWin[2] { 1, 1 };

        // Delay lines: audio (per input channel), envelope / gates / detector band (per processing channel)
        std::array<std::vector<float>, maxChannels> audioDelay, bandDelay;
        std::array<std::vector<float>, numSlots> envDelay, gateDelay0, gateDelay1;
        std::array<std::vector<float>, 3> trackDelay;   // tracker weights (linked), lead the audio like the envelope
        int writePos = 0, delaySize = 1, gridPhase = 0;
        bool wasPerChannel = false;
        int wetHold = 0;   // samples the wet signal stays faded out after a stereo mode switch
    };
}
