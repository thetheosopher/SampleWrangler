#include "VoiceManager.h"
#include <cmath>

namespace sw
{

    VoiceManager::VoiceManager() = default;
    VoiceManager::~VoiceManager() = default;

    // ---------------------------------------------------------------------------
    // AudioSource
    // ---------------------------------------------------------------------------

    void VoiceManager::prepareToPlay(int /*samplesPerBlockExpected*/, double sampleRate)
    {
        currentSampleRate = sampleRate;
    }

    void VoiceManager::releaseResources()
    {
        playing.store(false);
    }

    void VoiceManager::getNextAudioBlock(const juce::AudioSourceChannelInfo &info)
    {
        // RT-SAFE — no allocations, locks, I/O, or logging.
        auto *buf = sampleBuffer.load(std::memory_order_acquire);

        if (!playing.load(std::memory_order_relaxed) || buf == nullptr || buf->getNumSamples() == 0)
        {
            info.clearActiveBufferRegion();
            return;
        }

        const int numOutChannels = info.buffer->getNumChannels();
        const int numSrcChannels = buf->getNumChannels();
        const int srcLength = buf->getNumSamples();
        const int64_t configuredLoopStart = loopStartSample.load(std::memory_order_relaxed);
        const int64_t configuredLoopEnd = loopEndSample.load(std::memory_order_relaxed);

        const int loopStart = static_cast<int>(juce::jlimit<int64_t>(0, srcLength - 1, configuredLoopStart));
        const int loopEnd = static_cast<int>(juce::jlimit<int64_t>(0, srcLength - 1, configuredLoopEnd));
        const bool hasLoopRegion = loopEnabled.load(std::memory_order_relaxed) && configuredLoopStart >= 0 && configuredLoopEnd >= 0 && loopEnd > loopStart;
        const double loopEndExclusive = static_cast<double>(loopEnd) + 1.0;
        const double loopLength = static_cast<double>(loopEnd - loopStart) + 1.0;

        double pos = playbackPos.load(std::memory_order_relaxed);
        double rate = playbackRate.load(std::memory_order_relaxed) * (bufferSampleRate / currentSampleRate);

        for (int i = 0; i < info.numSamples; ++i)
        {
            if (hasLoopRegion && pos >= loopEndExclusive)
            {
                pos = static_cast<double>(loopStart) + std::fmod(pos - static_cast<double>(loopStart), loopLength);
            }

            if (pos >= static_cast<double>(srcLength))
            {
                if (loopEnabled.load(std::memory_order_relaxed) && srcLength > 0)
                {
                    pos = std::fmod(pos, static_cast<double>(srcLength));
                }
                else
                {
                    // End of sample — stop and zero-fill remainder
                    for (int ch = 0; ch < numOutChannels; ++ch)
                        info.buffer->clear(ch, info.startSample + i, info.numSamples - i);
                    playing.store(false, std::memory_order_relaxed);
                    playbackFinished.store(true, std::memory_order_relaxed);
                    pos = 0.0;
                    break;
                }
            }

            // Linear interpolation between two samples
            int idx0 = static_cast<int>(pos);
            int idx1 = std::min(idx0 + 1, srcLength - 1);
            float frac = static_cast<float>(pos - static_cast<double>(idx0));

            for (int ch = 0; ch < numOutChannels; ++ch)
            {
                int srcCh = (ch < numSrcChannels) ? ch : 0; // mono → stereo fallback
                float s0 = buf->getSample(srcCh, idx0);
                float s1 = buf->getSample(srcCh, idx1);
                info.buffer->setSample(ch, info.startSample + i, s0 + frac * (s1 - s0));
            }

            pos += rate;
        }

        playbackPos.store(pos, std::memory_order_relaxed);
    }

    // ---------------------------------------------------------------------------
    // Control (message thread)
    // ---------------------------------------------------------------------------

    void VoiceManager::loadBuffer(std::unique_ptr<juce::AudioBuffer<float>> buffer, double fileSampleRate)
    {
        stop();
        playbackFinished.store(false, std::memory_order_relaxed);
        loadedSampleLength.store(buffer != nullptr ? buffer->getNumSamples() : 0, std::memory_order_relaxed);
        ownedBuffer = std::move(buffer);
        bufferSampleRate = fileSampleRate;
        sampleBuffer.store(ownedBuffer.get(), std::memory_order_release);
        playbackPos.store(0.0, std::memory_order_relaxed);
    }

    void VoiceManager::play()
    {
        if (sampleBuffer.load(std::memory_order_acquire) == nullptr)
            return;
        playbackFinished.store(false, std::memory_order_relaxed);
        playing.store(true, std::memory_order_relaxed);
    }

    void VoiceManager::stop()
    {
        playing.store(false, std::memory_order_relaxed);
    }

    void VoiceManager::setPitchSemitones(double semitones)
    {
        // Resample-style: ratio = 2^(semitones/12)
        double ratio = std::pow(2.0, semitones / 12.0);
        playbackRate.store(ratio, std::memory_order_relaxed);
    }

    void VoiceManager::setLoopEnabled(bool enabled)
    {
        loopEnabled.store(enabled, std::memory_order_relaxed);
    }

    bool VoiceManager::isLoopEnabled() const noexcept
    {
        return loopEnabled.load(std::memory_order_relaxed);
    }

    void VoiceManager::setLoopRegionSamples(int64_t startSample, int64_t endSample)
    {
        loopStartSample.store(startSample, std::memory_order_relaxed);
        loopEndSample.store(endSample, std::memory_order_relaxed);
    }

    void VoiceManager::setPreviewRootMidiNote(int midiNote)
    {
        previewRootMidiNote.store(juce::jlimit(0, 127, midiNote), std::memory_order_relaxed);
    }

    int VoiceManager::getPreviewRootMidiNote() const noexcept
    {
        return previewRootMidiNote.load(std::memory_order_relaxed);
    }

    bool VoiceManager::isPlaying() const noexcept
    {
        return playing.load(std::memory_order_relaxed);
    }

    bool VoiceManager::consumePlaybackFinishedFlag() noexcept
    {
        return playbackFinished.exchange(false, std::memory_order_acq_rel);
    }

    double VoiceManager::getPlaybackProgressNormalized() const noexcept
    {
        const int length = loadedSampleLength.load(std::memory_order_relaxed);
        if (length <= 0)
            return 0.0;

        const double position = playbackPos.load(std::memory_order_relaxed);
        return juce::jlimit(0.0, 1.0, position / static_cast<double>(length));
    }

    void VoiceManager::setPlaybackProgressNormalized(double normalizedProgress)
    {
        const int length = loadedSampleLength.load(std::memory_order_relaxed);
        if (length <= 0)
            return;

        const double clamped = juce::jlimit(0.0, 1.0, normalizedProgress);
        playbackPos.store(clamped * static_cast<double>(length), std::memory_order_relaxed);
    }

} // namespace sw
