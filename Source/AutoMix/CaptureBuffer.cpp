#include "CaptureBuffer.h"

namespace velvox::automix
{
    int CaptureBuffer::capacityFor (double sampleRate, float maxSeconds) noexcept
    {
        // Above 96 kHz the duration shrinks so the memory stays bounded (~37 MB max)
        const double rate = std::max (8000.0, sampleRate);
        const double seconds = juce::jlimit (4.0, 60.0, (double) maxSeconds) * std::min (1.0, 96000.0 / rate);
        return (int) (rate * seconds);
    }

    void CaptureBuffer::waitForWriter() const noexcept
    {
        // The audio thread finishes copying one block within microseconds
        while (writing.load())
            std::this_thread::yield();
    }

    void CaptureBuffer::arm (double sampleRate, int vocalChannels, const CaptureConfig& config)
    {
        state.store ((int) State::idle);
        waitForWriter();

        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        channels = juce::jlimit (1, 2, vocalChannels);
        cfg = config;

        const int needed = capacityFor (fs, cfg.maxSeconds);
        if (needed != capacity)
        {
            for (auto* set : { &vocalBuf, &beatBuf })
                for (auto& b : *set)
                {
                    b.assign ((size_t) needed, 0.0f);
                    b.shrink_to_fit();
                }
            capacity = needed;
        }

        // A short buffer (very high sample rates) still has to be completable
        const float capacitySeconds = (float) (capacity / fs);
        cfg.targetVoicedSeconds = std::min (cfg.targetVoicedSeconds, capacitySeconds * 0.8f);
        cfg.minVoicedSeconds = std::min (cfg.minVoicedSeconds, cfg.targetVoicedSeconds);

        written.store (0);
        voicedSamples.store (0);
        clips.store (0);
        recentRms.store (-100.0f);
        peak.store (0.0f);
        stopped.store (false);
        beatPresent.store (false);
        beatStored.store (false);
        early.store (false);
        sawPlaying = false;

        state.store ((int) State::waiting);
    }

    void CaptureBuffer::cancel()
    {
        state.store ((int) State::idle);
        waitForWriter();
    }

    bool CaptureBuffer::finishNow()
    {
        int expected = (int) State::capturing;
        if (! state.compare_exchange_strong (expected, (int) State::complete))
            return false;
        waitForWriter();
        early.store (true);
        return true;
    }

    void CaptureBuffer::push (const float* const* vocal, int vocalChannels, const float* const* beat, int beatChannels,
                              int numSamples, bool hostPlaying) noexcept
    {
        if (numSamples <= 0 || vocalChannels <= 0)
            return;

        writing.store (true);
        const auto s = (State) state.load();
        if (s != State::waiting && s != State::capturing)
        {
            writing.store (false);
            return;
        }

        // Level of this block (vocal, channel maximum)
        float blockPeak = 0.0f;
        double sum = 0.0;
        const int vc = std::min (vocalChannels, channels);
        for (int ch = 0; ch < vc; ++ch)
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = vocal[ch][i];
                const float a = std::abs (x);
                blockPeak = std::max (blockPeak, std::isfinite (a) ? a : 0.0f);
                sum += std::isfinite (x) ? (double) x * x : 0.0;
            }
        const float rmsDb = (float) (10.0 * std::log10 (std::max (1.0e-20, sum / ((double) numSamples * vc))));
        recentRms.store (rmsDb, std::memory_order_relaxed);

        if (hostPlaying)
            sawPlaying = true;
        else if (sawPlaying && s == State::capturing)
            stopped.store (true, std::memory_order_relaxed);

        State current = s;
        if (current == State::waiting)
        {
            if (rmsDb < cfg.startThresholdDb)
            {
                writing.store (false);
                return;
            }
            int expected = (int) State::waiting;
            state.compare_exchange_strong (expected, (int) State::capturing);
            current = State::capturing;
        }

        // Digital silence (stopped transport, muted track) is not stored
        if (juce::Decibels::gainToDecibels (blockPeak, -200.0f) < cfg.silenceDb)
        {
            writing.store (false);
            return;
        }

        const int pos = written.load (std::memory_order_relaxed);
        const int n = std::min (numSamples, capacity - pos);
        if (n > 0)
        {
            int clipCount = 0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = vocal[std::min (ch, vc - 1)];
                float* dst = vocalBuf[(size_t) ch].data() + pos;
                for (int i = 0; i < n; ++i)
                {
                    const float x = std::isfinite (src[i]) ? src[i] : 0.0f;
                    dst[i] = x;
                    if (ch < vc && std::abs (x) >= 0.999f)
                        ++clipCount;
                }
            }

            if (beat != nullptr && beatChannels > 0)
            {
                float beatPeak = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* src = beat[std::min (ch, beatChannels - 1)];
                    float* dst = beatBuf[(size_t) ch].data() + pos;
                    for (int i = 0; i < n; ++i)
                    {
                        const float x = std::isfinite (src[i]) ? src[i] : 0.0f;
                        dst[i] = x;
                        beatPeak = std::max (beatPeak, std::abs (x));
                    }
                }
                beatStored.store (true, std::memory_order_relaxed);
                if (beatPeak > 0.003f)
                    beatPresent.store (true, std::memory_order_relaxed);
            }
            else if (beatStored.load (std::memory_order_relaxed))
            {
                // Sidechain disappeared mid-capture: keep the timelines aligned
                for (auto& b : beatBuf)
                    std::fill (b.begin() + pos, b.begin() + pos + n, 0.0f);
            }

            clips.fetch_add (clipCount, std::memory_order_relaxed);
            if (rmsDb >= cfg.startThresholdDb)
                voicedSamples.fetch_add (n, std::memory_order_relaxed);
            if (blockPeak > peak.load (std::memory_order_relaxed))
                peak.store (blockPeak, std::memory_order_relaxed);
            written.store (pos + n, std::memory_order_release);
        }

        const bool enoughVoice = voicedSamples.load (std::memory_order_relaxed) >= (int) (cfg.targetVoicedSeconds * fs);
        if (enoughVoice || pos + n >= capacity)
        {
            int expected = (int) State::capturing;
            state.compare_exchange_strong (expected, (int) State::complete);
        }

        writing.store (false);
    }

    CaptureBuffer::Progress CaptureBuffer::getProgress() const noexcept
    {
        Progress p;
        p.state = (State) state.load();
        p.capturedSeconds = (float) (written.load (std::memory_order_relaxed) / fs);
        p.voicedSeconds = (float) (voicedSamples.load (std::memory_order_relaxed) / fs);
        p.targetSeconds = cfg.targetVoicedSeconds;
        p.minVoicedSeconds = cfg.minVoicedSeconds;
        p.recentRmsDb = recentRms.load (std::memory_order_relaxed);
        p.peakDb = juce::Decibels::gainToDecibels (peak.load (std::memory_order_relaxed), -100.0f);
        p.clipSamples = clips.load (std::memory_order_relaxed);
        p.transportStopped = stopped.load (std::memory_order_relaxed);
        p.beatSeen = beatPresent.load (std::memory_order_relaxed);
        return p;
    }

    CapturedAudio CaptureBuffer::copyOut() const
    {
        CapturedAudio a;
        if ((State) state.load() != State::complete)
            return a;

        const int n = written.load (std::memory_order_acquire);
        a.sampleRate = fs;
        a.channels = channels;
        a.clipSamples = clips.load();
        a.stoppedEarly = early.load();
        for (int ch = 0; ch < channels; ++ch)
            a.vocal.emplace_back (vocalBuf[(size_t) ch].begin(), vocalBuf[(size_t) ch].begin() + n);
        if (beatStored.load() && beatPresent.load())
            for (int ch = 0; ch < 2; ++ch)
                a.beat.emplace_back (beatBuf[(size_t) ch].begin(), beatBuf[(size_t) ch].begin() + n);
        return a;
    }
}
