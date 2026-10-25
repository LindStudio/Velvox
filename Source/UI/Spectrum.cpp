#include "Spectrum.h"

namespace velvox::ui
{
    SpectrumSource::SpectrumSource (dsp::SampleTap& inputTap, dsp::SampleTap& outputTap)
        : scratch ((size_t) dsp::SampleTap::capacity), in (inputTap), out (outputTap)
    {
    }

    void SpectrumSource::update (double sampleRate)
    {
        const double fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto* c : { &in, &out })
        {
            const int n = c->tap.pull (scratch.data(), (int) scratch.size());
            // Only the newest fftSize samples matter
            const int skip = std::max (0, n - fftSize);
            for (int i = skip; i < n; ++i)
            {
                c->ring[(size_t) c->writePos] = scratch[(size_t) i];
                c->writePos = (c->writePos + 1) % fftSize;
            }
            analyse (*c, fs, n > 0);
        }
    }

    void SpectrumSource::analyse (Channel& c, double fs, bool hasNewData)
    {
        if (! hasNewData)
        {
            // Host stopped calling process: let the display fall away gently
            for (auto& v : c.smoothed)
                v = std::max (floorDb, v - 1.25f);
            return;
        }

        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            work[(size_t) i] = c.ring[(size_t) ((c.writePos + i) % fftSize)];
        window.multiplyWithWindowingTable (work.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);

        // Hann window: a full-scale sine reads 0 dB
        const float norm = 4.0f / (float) fftSize;
        const float binHz = (float) fs / (float) fftSize;
        const int maxBin = fftSize / 2 - 1;
        const float halfStep = std::pow (maxHz / minHz, 0.5f / (float) (numPoints - 1));

        for (int p = 0; p < numPoints; ++p)
        {
            const float f = frequencyAt ((float) p);
            float mag = 0.0f;

            if (f < (float) fs * 0.5f)
            {
                const float lo = f / halfStep / binHz, hi = f * halfStep / binHz;
                if (hi - lo < 1.0f)
                {
                    // Narrower than a bin: interpolate
                    const float b = juce::jlimit (0.0f, (float) maxBin - 1.0f, f / binHz);
                    const int i0 = (int) b;
                    const float t = b - (float) i0;
                    mag = work[(size_t) i0] * (1.0f - t) + work[(size_t) i0 + 1] * t;
                }
                else
                {
                    for (int b = std::max (0, (int) std::ceil (lo)); b <= std::min (maxBin, (int) hi); ++b)
                        mag = std::max (mag, work[(size_t) b]);
                }
            }

            // +4.5 dB/oct tilt around 1 kHz: a vocal reads roughly flat
            float db = juce::Decibels::gainToDecibels (mag * norm, floorDb) + 4.5f * std::log2 (f / 1000.0f);
            db = std::max (floorDb, db);

            auto& s = c.smoothed[(size_t) p];
            s += (db - s) * (db > s ? 0.4f : 0.09f);
        }
    }

    //==========================================================================
    void SpectrumView::drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> r, bool withLabels)
    {
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xForFrequency (f, r);
            const bool major = f == 100.0f || f == 1000.0f || f == 10000.0f;
            g.setColour (colours::accent.withAlpha (major ? 0.055f : 0.03f));
            g.fillRect (juce::Rectangle<float> (x, r.getY(), 1.0f, r.getHeight()));
            if (withLabels)
            {
                g.setColour (colours::textFaint);
                g.setFont (medium (11.0f));
                g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                            juce::Rectangle<float> (x + 4.0f, r.getBottom() - 18.0f, 34.0f, 14.0f), juce::Justification::left, false);
            }
        }
    }

    juce::Path SpectrumView::makePath (const SpectrumSource::Curve& curve, juce::Rectangle<float> r) const
    {
        juce::Path p;
        const auto y = [&] (float db) { return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, r.getBottom(), r.getY()); };
        const auto x = [&] (int i) { return r.getX() + r.getWidth() * (float) i / (float) (SpectrumSource::numPoints - 1); };

        p.startNewSubPath (x (0), y (curve[0]));
        for (int i = 1; i < SpectrumSource::numPoints - 1; ++i)
        {
            // Quadratic through midpoints: smooth, no overshoot
            const float mx = (x (i) + x (i + 1)) * 0.5f;
            const float my = (y (curve[(size_t) i]) + y (curve[(size_t) i + 1])) * 0.5f;
            p.quadraticTo (x (i), y (curve[(size_t) i]), mx, my);
        }
        p.lineTo (x (SpectrumSource::numPoints - 1), y (curve.back()));
        return p;
    }

    void SpectrumView::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        if (grid)
            drawFrequencyGrid (g, r, false);   // lines under the spectrum, labels on top (below)

        if (spectrum == nullptr)
        {
            if (grid && labels)
                drawFrequencyLabels (g, r);
            return;
        }

        // Input: faint outline for reference
        const auto inPath = makePath (spectrum->getInput(), r);
        if (! inputDashed)
        {
            g.setColour (colours::accent.withAlpha (0.16f * intensity));
            g.strokePath (inPath, juce::PathStrokeType (1.0f));
        }

        // Output: lit fill with a soft glow along the edge
        const auto outPath = makePath (spectrum->getOutput(), r);
        juce::Path fill (outPath);
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        // Aurora light (teal lows -> blue highs), fading into the dark towards the floor
        g.setGradientFill (auroraGradient (r.getTopLeft(), r.getTopRight(), 0.30f * intensity));
        g.fillPath (fill);
        g.setGradientFill (juce::ColourGradient (colours::background.withAlpha (0.0f), 0.0f, r.getY() + r.getHeight() * 0.15f,
                                                 colours::background.withAlpha (0.92f), 0.0f, r.getBottom(), false));
        g.fillPath (fill);

        g.setGradientFill (auroraGradient (r.getTopLeft(), r.getTopRight(), 0.14f * intensity));
        g.strokePath (outPath, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto core = auroraGradient (r.getTopLeft(), r.getTopRight(), 0.95f * intensity);
        core.addColour (0.45, colours::teal.interpolatedWith (juce::Colours::white, 0.35f).withAlpha (0.95f * intensity));
        g.setGradientFill (core);
        g.strokePath (outPath, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Before (dashed) on top, so it stays visible over the filled after-curve
        if (inputDashed)
        {
            juce::Path dashed;
            const float dashes[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, inPath, dashes, 2);
            g.setColour (colours::textSoft.withAlpha (0.6f * intensity));
            g.fillPath (dashed);
        }

        // Frequency labels last: above the fill, on a subtle backing, so they stay readable
        if (grid && labels)
            drawFrequencyLabels (g, r);
    }

    void SpectrumView::drawFrequencyLabels (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // Fixed strings (paint never allocates); widths from the font
        static const char* const texts[] { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
        static const float freqs[] { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
        g.setFont (medium (11.0f));
        for (int i = 0; i < 8; ++i)
        {
            const float x = xForFrequency (freqs[i], r);
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), texts[i]);
            const juce::Rectangle<float> box (x + 2.0f, r.getBottom() - 18.0f, w + 6.0f, 14.0f);
            g.setColour (colours::background.withAlpha (0.72f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (colours::textSoft.withAlpha (0.85f));
            g.drawText (texts[i], box.withTrimmedLeft (3.0f), juce::Justification::centredLeft, false);
        }
    }
}
