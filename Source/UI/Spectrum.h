#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Theme.h"
#include "../DSP/DspUtilities.h"

namespace velvox::ui
{
    // Turns the chain's sample taps into smoothed spectra on a log-frequency
    // axis. UI thread only: the editor calls update() once per frame and every
    // SpectrumView reads the shared result.
    class SpectrumSource
    {
    public:
        static constexpr int fftOrder = 12;
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int numPoints = 240;
        static constexpr float minHz = 20.0f, maxHz = 20000.0f, floorDb = -100.0f;

        SpectrumSource (dsp::SampleTap& inputTap, dsp::SampleTap& outputTap);

        void update (double sampleRate);

        using Curve = std::array<float, numPoints>;
        const Curve& getInput() const noexcept { return in.smoothed; }
        const Curve& getOutput() const noexcept { return out.smoothed; }

        static float frequencyAt (float point) noexcept
        {
            return minHz * std::pow (maxHz / minHz, point / (float) (numPoints - 1));
        }

    private:
        struct Channel
        {
            explicit Channel (dsp::SampleTap& t) : tap (t) { smoothed.fill (floorDb); }
            dsp::SampleTap& tap;
            std::array<float, fftSize> ring {};
            int writePos = 0;
            Curve smoothed;
        };

        void analyse (Channel& c, double sampleRate, bool hasNewData);

        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
        std::array<float, fftSize * 2> work {};
        std::vector<float> scratch;
        Channel in, out;
    };

    // Draws the input (faint outline) and output (lit, filled) spectrum with an
    // optional frequency grid. Repainted by refresh() at the editor frame rate.
    class SpectrumView : public juce::Component
    {
    public:
        explicit SpectrumView (const SpectrumSource* source = nullptr) : spectrum (source)
        {
            setInterceptsMouseClicks (false, false);
        }

        void setSource (const SpectrumSource* s) { spectrum = s; repaint(); }
        void setShowGrid (bool shouldShow) { grid = shouldShow; repaint(); }
        void setShowLabels (bool shouldShow) { labels = shouldShow; repaint(); }
        void setRange (float bottomDb, float topDb) { minDb = bottomDb; maxDb = topDb; repaint(); }
        void setIntensity (float i) { intensity = i; repaint(); }

        void refresh() { if (spectrum != nullptr && isShowing()) repaint(); }
        void paint (juce::Graphics&) override;

        // Shared frequency mapping (20 Hz .. 20 kHz, log) for overlays
        static float xForFrequency (float hz, juce::Rectangle<float> r) noexcept
        {
            return r.getX() + r.getWidth() * std::log (hz / SpectrumSource::minHz) / std::log (SpectrumSource::maxHz / SpectrumSource::minHz);
        }

        static void drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> r, bool withLabels);

    private:
        juce::Path makePath (const SpectrumSource::Curve& curve, juce::Rectangle<float> r) const;

        const SpectrumSource* spectrum = nullptr;
        bool grid = true, labels = true;
        float minDb = -90.0f, maxDb = 0.0f;
        float intensity = 1.0f;
    };
}
