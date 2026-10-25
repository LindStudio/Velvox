#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>
#include <vector>

namespace velvox::dsp
{
    // Two lattice filters per channel sharing one order: the all-zero A(z) (whitening) and the
    // all-pole 1/A(z) (colouring). Reflection coefficients may change every sample: with |k| < 1
    // the all-pole lattice stays stable however fast they move (unlike a direct form).
    struct LatticePair
    {
        static constexpr int order = 32;

        float processAllZero (float x, const float* k, int ch) noexcept
        {
            auto& s = zeroState[(size_t) ch];
            float f = x, b = x;
            for (int i = 0; i < order; ++i)
            {
                const float old = s[(size_t) i];
                const float fNew = f + k[i] * old;
                const float bNew = k[i] * f + old;
                s[(size_t) i] = b;
                f = fNew;
                b = bNew;
            }
            return f;
        }

        float processAllPole (float e, const float* k, int ch) noexcept
        {
            auto& s = poleState[(size_t) ch];
            float f = e;
            for (int i = order - 1; i >= 0; --i)
            {
                f -= k[i] * s[(size_t) i];
                s[(size_t) i + 1] = k[i] * f + s[(size_t) i];
            }
            s[0] = f;
            return f;
        }

        // Both channels at once (same coefficients): two independent recursions interleaved
        void processStereo (float& l, float& r, const float* kz, const float* kp) noexcept
        {
            auto& zl = zeroState[0];
            auto& zr = zeroState[1];
            float fl = l, bl = l, fr = r, br = r;
            for (int i = 0; i < order; ++i)
            {
                const float k = kz[i];
                const float ol = zl[(size_t) i], orr = zr[(size_t) i];
                zl[(size_t) i] = bl;
                zr[(size_t) i] = br;
                const float nfl = fl + k * ol, nfr = fr + k * orr;
                bl = k * fl + ol;
                br = k * fr + orr;
                fl = nfl;
                fr = nfr;
            }
            auto& pl = poleState[0];
            auto& pr = poleState[1];
            for (int i = order - 1; i >= 0; --i)
            {
                const float k = kp[i];
                fl -= k * pl[(size_t) i];
                fr -= k * pr[(size_t) i];
                pl[(size_t) i + 1] = k * fl + pl[(size_t) i];
                pr[(size_t) i + 1] = k * fr + pr[(size_t) i];
            }
            pl[0] = fl;
            pr[0] = fr;
            l = fl;
            r = fr;
        }

        void reset() noexcept
        {
            for (auto& s : zeroState) s.fill (0.0f);
            for (auto& s : poleState) s.fill (0.0f);
        }

        bool isFinite() const noexcept
        {
            for (const auto& s : poleState)
                for (auto v : s)
                    if (! std::isfinite (v)) return false;
            return true;
        }

        std::array<std::array<float, order>, 2> zeroState {};
        std::array<std::array<float, order + 1>, 2> poleState {};
    };

    // Formant (spectral envelope) correction for a resampling pitch shifter.
    //
    // The shifter's output y has the input's envelope scaled in frequency by rho
    // (E_y(f) = E_x(f / rho)). The corrector analyses the input x every 10 ms (frame centred
    // `analysisDelay` samples behind the newest input, i.e. at the shifter's output time),
    // takes a cepstrally smoothed envelope (lifter tied to the period, so harmonics are
    // removed), warps it to the envelope y has and to the target (E_x(f / target)), fits an
    // all-pole model to both and filters y through  g * A_y(z) / A_t(z).  Reflection
    // coefficients are interpolated per sample (no zipper, always stable).
    //
    // rho == target is the identity and processes nothing (bit exact).
    class FormantCorrector
    {
    public:
        FormantCorrector();
        ~FormantCorrector();

        void prepare (double sampleRate, int analysisDelay, int maxBlockSize);
        void reset() noexcept;

        // rho: frequency scaling already in the signal; target: where the input's envelope should end up
        void setWarp (float rho, float target) noexcept;

        // monoIn: the shifter's input (analysis), io: the shifted signal (corrected in place),
        // period: per-sample input period from the tracker (0 = unvoiced)
        void process (const float* monoIn, float* const* io, int numChannels, int numSamples, const float* period) noexcept;

    private:
        void analyse (float period) noexcept;
        void envelopeFrom (const std::vector<float>& logEnv, float warp, std::vector<float>& logOut) const noexcept;
        float levinson (const std::vector<float>& power, float* k) noexcept;   // returns the prediction error

        double fs = 48000.0;
        int delay = 700, frameSize = 1024, order10 = 10, hop = 240, hopCounter = 0;
        std::unique_ptr<juce::dsp::FFT> fft;

        std::vector<float> history;   // mono input, circular, power of 2
        int histMask = 0, histPos = 0;

        std::vector<float> window, work, logEnv, logY, logT, powerY, powerT, autoc, acTable;
        std::array<double, LatticePair::order + 1> lev {}, levTmp {};

        float rho = 1.0f, target = 1.0f;
        bool identity = true, bypassed = true, envValid = false;
        float smoothing = 0.34f;   // per analysis hop

        std::array<float, LatticePair::order> kyCur {}, kyStep {}, ktCur {}, ktStep {};
        float gainCur = 1.0f, gainStep = 0.0f;
        int stepsLeft = 0;

        LatticePair lattice;
    };
}
