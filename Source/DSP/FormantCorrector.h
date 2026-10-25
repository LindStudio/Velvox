#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <complex>
#include <memory>
#include <vector>

namespace velvox::dsp
{
    // Formant (spectral envelope) correction for a resampling pitch shifter.
    //
    // The shifter's output y has the input's envelope scaled in frequency by rho
    // (E_y(f) = E_x(f / rho)). Every 10 ms the corrector analyses the input x (frame centred
    // `analysisDelay` samples behind the newest input, i.e. at the shifter's output time):
    // Blackman-Harris spectrum, peak hold over the harmonic spacing, cepstral smoothing with a
    // lifter below the pitch period, ~12 ms smoothing over time. The correction
    // G(f) = E_x(f / target) / E_x(f / rho) (clamped: up to +-36 dB in the voice band, less
    // boost where y has no content) is realised as a minimum-phase FIR (folded cepstrum of
    // log G) and crossfaded from the previous filter, so it changes without zipper.
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

        int getFilterLength() const noexcept { return taps; }

    private:
        void analyse (float period) noexcept;
        void envelopeFrom (const std::vector<float>& logEnv, float warp, std::vector<float>& logOut) const noexcept;
        void designFilter() noexcept;   // log gain (logG) -> minimum-phase taps (next)
        void startFilter (bool identityFilter) noexcept;
        void inverseEven (const std::vector<float>& halfSpectrum, std::vector<float>& time) noexcept;   // real even spectrum -> IDFT

        double fs = 48000.0;
        int delay = 700, frameSize = 1024, hop = 480, hopCounter = 0, taps = 256, fadeLen = 120;
        struct Fft;                                  // Signalsmith RealFFT (pimpl)
        std::unique_ptr<Fft> rfft;
        float fwdScale = 1.0f, roundTrip = 1.0f;     // DFT = fwdScale * fft;  ifft (fft (x)) = roundTrip * x

        std::vector<float> history;   // mono input for the analysis, circular, power of 2
        int histMask = 0, histPos = 0;

        std::vector<float> window, work, logEnv, logY, logT, logG;
        std::vector<std::complex<float>> spec;      // N/2 bins, packed (bin 0 = DC, Nyquist)

        // filter state: per channel a doubled history (contiguous windows), reversed taps
        std::array<std::vector<float>, 2> line;
        int linePos = 0;
        std::vector<float> curTaps, oldTaps, nextTaps;
        int fadeLeft = 0;

        float rho = 1.0f, target = 1.0f;
        bool identity = true, bypassed = true, envValid = false, curIdentity = true;
        float smoothing = 0.56f;   // per analysis hop
    };
}
