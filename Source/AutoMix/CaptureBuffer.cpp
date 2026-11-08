#include "CaptureBuffer.h"

namespace vocalypse::automix
{
    int CaptureBuffer::capacityFor (double sampleRate, float maxSeconds) noexcept
    {
        // Bounded memory: at most 120 s at 48 kHz (5.76 M samples per channel), shorter at higher rates
        constexpr double maxSamples = 120.0 * 48000.0;
        const double rate = std::max (8000.0, sampleRate);
        const double seconds = std::min (juce::jlimit (4.0, 120.0, (double) maxSeconds), maxSamples / rate);
        return (int) (rate * seconds);
    }

    size_t CaptureBuffer::memoryBytes() const noexcept
    {
        size_t bytes = beatBuf.capacity() * sizeof (float) + ppqBuf.capacity() * sizeof (double);
        for (auto& b : vocalBuf)
            bytes += b.capacity() * sizeof (float);
        return bytes;
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
            for (auto& b : vocalBuf)
            {
                b.assign ((size_t) needed, 0.0f);
                b.shrink_to_fit();
            }
            beatBuf.assign ((size_t) needed, 0.0f);
            beatBuf.shrink_to_fit();
            ppqBuf.assign ((size_t) (needed / CapturedAudio::ppqBlock + 2), -1.0);
            ppqBuf.shrink_to_fit();
            capacity = needed;
        }
        else
        {
            std::fill (ppqBuf.begin(), ppqBuf.end(), -1.0);
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
        lastBpm.store (0.0);
        beatStored.store (false);
        ppqStored.store (false);
        early.store (false);
        sawPlaying = false;
        runningMaxDb = -200.0f;
        ppqSeen = false;

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
                              int numSamples, bool hostPlaying, double bpm, double ppq) noexcept
    {
        if (numSamples <= 0 || vocalChannels <= 0)
            return;
        if (bpm > 0.0)
            lastBpm.store (bpm, std::memory_order_relaxed);

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
        {
            sawPlaying = true;
            stopped.store (false, std::memory_order_relaxed);   // the host plays again: the stop is over
        }
        else if (sawPlaying && s == State::capturing)
            stopped.store (true, std::memory_order_relaxed);

        // Digital silence neither starts the capture nor is stored while the transport is stopped. While the host
        // plays, silence is part of the song and is stored, so the stored audio keeps its timeline and the capture does
        // not depend on where it began. The floor is far below any recording, so a level shift of the same audio
        // cannot change what is captured.
        const bool silent = juce::Decibels::gainToDecibels (blockPeak, -200.0f) < cfg.silenceDb;
        if (silent && (s == State::waiting || ! hostPlaying))
        {
            writing.store (false);
            return;
        }

        if (s == State::waiting)
        {
            int expected = (int) State::waiting;
            state.compare_exchange_strong (expected, (int) State::capturing);
        }

        runningMaxDb = std::max (runningMaxDb, rmsDb);

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
                // The beat is stored as one mid channel (it is only analysed, never heard)
                float beatPeak = 0.0f;
                float* dst = beatBuf.data() + pos;
                const int bc = std::min (beatChannels, 2);
                for (int i = 0; i < n; ++i)
                {
                    float x = 0.0f;
                    for (int ch = 0; ch < bc; ++ch)
                        x += std::isfinite (beat[ch][i]) ? beat[ch][i] : 0.0f;
                    x /= (float) bc;
                    dst[i] = x;
                    beatPeak = std::max (beatPeak, std::abs (x));
                }
                beatStored.store (true, std::memory_order_relaxed);
                if (beatPeak > 0.003f)
                    beatPresent.store (true, std::memory_order_relaxed);
            }
            else if (beatStored.load (std::memory_order_relaxed))
            {
                // Sidechain disappeared mid-capture: keep the timelines aligned
                std::fill (beatBuf.begin() + pos, beatBuf.begin() + pos + n, 0.0f);
            }

            // Song position of the 512-sample blocks that start in this block
            const double tempo = bpm > 0.0 ? bpm : lastBpm.load (std::memory_order_relaxed);
            if (ppq >= 0.0 && tempo > 0.0)
            {
                const double perSample = tempo / 60.0 / fs;
                constexpr int pb = CapturedAudio::ppqBlock;
                for (int c = (pos + pb - 1) / pb; c * pb < pos + n && (size_t) c < ppqBuf.size(); ++c)
                    ppqBuf[(size_t) c] = ppq + (double) (c * pb - pos) * perSample;
                ppqStored.store (true, std::memory_order_relaxed);
            }

            clips.fetch_add (clipCount, std::memory_order_relaxed);
            if (rmsDb >= std::max (cfg.voiceFloorDb, runningMaxDb - cfg.voiceRangeDb))
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
        a.hostBpm = lastBpm.load();
        for (int ch = 0; ch < channels; ++ch)
            a.vocal.emplace_back (vocalBuf[(size_t) ch].begin(), vocalBuf[(size_t) ch].begin() + n);
        if (beatStored.load() && beatPresent.load())
            a.beat.emplace_back (beatBuf.begin(), beatBuf.begin() + n);
        if (ppqStored.load())
        {
            const size_t blocks = (size_t) ((n + CapturedAudio::ppqBlock - 1) / CapturedAudio::ppqBlock);
            a.blockPpq.assign (ppqBuf.begin(), ppqBuf.begin() + (long) std::min (blocks, ppqBuf.size()));
        }
        return a;
    }
}
