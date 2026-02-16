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

        double pos = playbackPos.load(std::memory_order_relaxed);
        double rate = playbackRate.load(std::memory_order_relaxed) * (bufferSampleRate / currentSampleRate);

        for (int i = 0; i < info.numSamples; ++i)
        {
            if (pos >= static_cast<double>(srcLength))
            {
                // End of sample — stop and zero-fill remainder
                for (int ch = 0; ch < numOutChannels; ++ch)
                    info.buffer->clear(ch, info.startSample + i, info.numSamples - i);
                playing.store(false, std::memory_order_relaxed);
                pos = 0.0;
                break;
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
        playing.store(true, std::memory_order_relaxed);
    }

    void VoiceManager::stop()
    {
        playing.store(false, std::memory_order_relaxed);
        playbackPos.store(0.0, std::memory_order_relaxed);
    }

    void VoiceManager::setPitchSemitones(double semitones)
    {
        // Resample-style: ratio = 2^(semitones/12)
        double ratio = std::pow(2.0, semitones / 12.0);
        playbackRate.store(ratio, std::memory_order_relaxed);
    }

    bool VoiceManager::isPlaying() const noexcept
    {
        return playing.load(std::memory_order_relaxed);
    }

    double VoiceManager::getPlaybackProgressNormalized() const noexcept
    {
        const int length = loadedSampleLength.load(std::memory_order_relaxed);
        if (length <= 0)
            return 0.0;

        const double position = playbackPos.load(std::memory_order_relaxed);
        return juce::jlimit(0.0, 1.0, position / static_cast<double>(length));
    }

} // namespace sw
