#include "VoiceManager.h"
#include <cmath>

#if SW_HAVE_RUBBERBAND
#include <rubberband/RubberBandLiveShifter.h>
#endif

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
#if SW_HAVE_RUBBERBAND
        initialiseRubberBandIfNeeded();
        resetRubberBandState();
#endif
    }

    void VoiceManager::releaseResources()
    {
        playing.store(false);
    }

    void VoiceManager::getNextAudioBlock(const juce::AudioSourceChannelInfo &info)
    {
        // RT-SAFE — no allocations, locks, I/O, or logging.
        const auto loadedBuffer = sampleBuffer.load(std::memory_order_acquire);
        auto *buf = loadedBuffer.get();

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
#if SW_HAVE_RUBBERBAND
        const double currentPitchRatio = pitchRatio.load(std::memory_order_relaxed);
#endif
        const bool preserveLength = preserveLengthEnabled.load(std::memory_order_relaxed) && std::abs(rate - 1.0) > 0.0001;
#if SW_HAVE_RUBBERBAND
        const bool useRubberBandHq = preserveLength && stretchHighQualityEnabled.load(std::memory_order_relaxed) && rubberBandInitialized;
#else
        constexpr bool useRubberBandHq = false;
#endif

        auto wrapReadPosition = [&](double readPos)
        {
            if (hasLoopRegion)
            {
                while (readPos < static_cast<double>(loopStart))
                    readPos += loopLength;
                while (readPos >= loopEndExclusive)
                    readPos -= loopLength;
                return readPos;
            }

            if (loopEnabled.load(std::memory_order_relaxed) && srcLength > 0)
            {
                while (readPos < 0.0)
                    readPos += static_cast<double>(srcLength);
                while (readPos >= static_cast<double>(srcLength))
                    readPos -= static_cast<double>(srcLength);
                return readPos;
            }

            return readPos;
        };

        auto readInterpolatedSample = [&](int channel, double readPos)
        {
            const double wrappedPos = wrapReadPosition(readPos);
            if (wrappedPos < 0.0 || wrappedPos >= static_cast<double>(srcLength))
                return 0.0f;

            const int idx0 = juce::jlimit(0, srcLength - 1, static_cast<int>(wrappedPos));
            const int idx1 = juce::jmin(idx0 + 1, srcLength - 1);
            const float frac = static_cast<float>(wrappedPos - static_cast<double>(idx0));
            const float s0 = buf->getSample(channel, idx0);
            const float s1 = buf->getSample(channel, idx1);
            return s0 + frac * (s1 - s0);
        };

        if (granularResetRequested.exchange(false, std::memory_order_acq_rel))
        {
            grainSamplesRemaining = 0;
#if SW_HAVE_RUBBERBAND
            resetRubberBandState();
#endif
        }

        constexpr int grainLengthSamples = 256;
        constexpr double grainSpacingSamples = 128.0;

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

            if (preserveLength)
            {
                if (useRubberBandHq)
                {
#if SW_HAVE_RUBBERBAND
                    if (rubberBandLiveShifter != nullptr)
                        rubberBandLiveShifter->setPitchScale(currentPitchRatio);

                    for (int ch = 0; ch < numOutChannels; ++ch)
                    {
                        const int srcCh = (ch < numSrcChannels) ? ch : 0;
                        rubberBandInput[static_cast<size_t>(ch)][static_cast<size_t>(rubberBandInputFill)] =
                            readInterpolatedSample(srcCh, pos);
                    }

                    ++rubberBandInputFill;

                    if (rubberBandInputFill >= rubberBandBlockSize)
                    {
                        for (int ch = 0; ch < kRubberBandMaxChannels; ++ch)
                        {
                            rubberBandInputPtrs[static_cast<size_t>(ch)] = rubberBandInput[static_cast<size_t>(ch)].data();
                            rubberBandOutputPtrs[static_cast<size_t>(ch)] = rubberBandOutput[static_cast<size_t>(ch)].data();
                        }

                        rubberBandLiveShifter->shift(rubberBandInputPtrs.data(), rubberBandOutputPtrs.data());
                        rubberBandInputFill = 0;
                        rubberBandOutputRead = 0;
                        rubberBandOutputAvailable = rubberBandBlockSize;
                    }

                    float outSampleLeft = 0.0f;
                    float outSampleRight = 0.0f;
                    bool outputProduced = false;

                    while (rubberBandOutputAvailable > 0 && rubberBandStartDelayRemaining > 0)
                    {
                        ++rubberBandOutputRead;
                        --rubberBandOutputAvailable;
                        --rubberBandStartDelayRemaining;
                    }

                    if (rubberBandOutputAvailable > 0)
                    {
                        const int rbIndex = rubberBandOutputRead;
                        outSampleLeft = rubberBandOutput[0][static_cast<size_t>(rbIndex)];
                        outSampleRight = rubberBandOutput[1][static_cast<size_t>(rbIndex)];
                        ++rubberBandOutputRead;
                        --rubberBandOutputAvailable;
                        outputProduced = true;
                    }

                    for (int ch = 0; ch < numOutChannels; ++ch)
                    {
                        const float sample = (ch == 0) ? outSampleLeft : outSampleRight;
                        info.buffer->setSample(ch, info.startSample + i, sample);
                    }

                    // Only advance position when we're actually outputting audio (past startup delay)
                    if (outputProduced || rubberBandStartDelayRemaining == 0)
                        pos += (bufferSampleRate / currentSampleRate);
                    continue;
#endif
                }

                if (grainSamplesRemaining <= 0)
                {
                    grainReadPosA = pos;
                    grainReadPosB = pos + grainSpacingSamples;
                    grainSamplesRemaining = grainLengthSamples;
                }

                const float blend = 1.0f - (static_cast<float>(grainSamplesRemaining) / static_cast<float>(grainLengthSamples));

                for (int ch = 0; ch < numOutChannels; ++ch)
                {
                    const int srcCh = (ch < numSrcChannels) ? ch : 0;
                    const float sampleA = readInterpolatedSample(srcCh, grainReadPosA);
                    const float sampleB = readInterpolatedSample(srcCh, grainReadPosB);
                    info.buffer->setSample(ch, info.startSample + i, sampleA + (sampleB - sampleA) * blend);
                }

                grainReadPosA += rate;
                grainReadPosB += rate;
                --grainSamplesRemaining;

                if (grainSamplesRemaining <= 0)
                {
                    grainReadPosA = grainReadPosB;
                    grainReadPosB = grainReadPosA + grainSpacingSamples;
                }

                pos += 1.0;
            }
            else
            {
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
        granularResetRequested.store(true, std::memory_order_release);
        loadedSampleLength.store(buffer != nullptr ? buffer->getNumSamples() : 0, std::memory_order_relaxed);
        std::shared_ptr<juce::AudioBuffer<float>> sharedBuffer;
        if (buffer != nullptr)
            sharedBuffer = std::shared_ptr<juce::AudioBuffer<float>>(std::move(buffer));

        bufferSampleRate = fileSampleRate;
        sampleBuffer.store(std::move(sharedBuffer), std::memory_order_release);
        playbackPos.store(0.0, std::memory_order_relaxed);
    }

    void VoiceManager::play()
    {
        if (sampleBuffer.load(std::memory_order_acquire) == nullptr)
            return;
        playbackFinished.store(false, std::memory_order_relaxed);
        granularResetRequested.store(true, std::memory_order_release);
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
        pitchRatio.store(ratio, std::memory_order_relaxed);
        granularResetRequested.store(true, std::memory_order_release);
    }

    void VoiceManager::setPreserveLengthEnabled(bool enabled)
    {
        preserveLengthEnabled.store(enabled, std::memory_order_relaxed);
        granularResetRequested.store(true, std::memory_order_release);
    }

    bool VoiceManager::isPreserveLengthEnabled() const noexcept
    {
        return preserveLengthEnabled.load(std::memory_order_relaxed);
    }

    void VoiceManager::setStretchHighQualityEnabled(bool enabled)
    {
#if SW_HAVE_RUBBERBAND
        stretchHighQualityEnabled.store(enabled, std::memory_order_relaxed);
        granularResetRequested.store(true, std::memory_order_release);
#else
        (void)enabled;
#endif
    }

    bool VoiceManager::isStretchHighQualityEnabled() const noexcept
    {
        return stretchHighQualityEnabled.load(std::memory_order_relaxed);
    }

    bool VoiceManager::isStretchHighQualityAvailable() const noexcept
    {
#if SW_HAVE_RUBBERBAND
        return true;
#else
        return false;
#endif
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
        granularResetRequested.store(true, std::memory_order_release);
    }

#if SW_HAVE_RUBBERBAND
    void VoiceManager::initialiseRubberBandIfNeeded()
    {
        if (rubberBandInitialized)
            return;

        const auto options = RubberBand::RubberBandLiveShifter::OptionWindowMedium |
                             RubberBand::RubberBandLiveShifter::OptionChannelsTogether |
                             RubberBand::RubberBandLiveShifter::OptionFormantPreserved;

        rubberBandLiveShifter = std::make_unique<RubberBand::RubberBandLiveShifter>(
            static_cast<size_t>(juce::jlimit(8000, 192000, static_cast<int>(std::round(currentSampleRate)))),
            static_cast<size_t>(kRubberBandMaxChannels),
            options);

        rubberBandLiveShifter->setDebugLevel(0);
        rubberBandLiveShifter->setPitchScale(playbackRate.load(std::memory_order_relaxed));

        rubberBandBlockSize = static_cast<int>(rubberBandLiveShifter->getBlockSize());
        if (rubberBandBlockSize <= 0 || rubberBandBlockSize > kRubberBandMaxBlockSize)
        {
            rubberBandLiveShifter.reset();
            rubberBandInitialized = false;
            return;
        }

        rubberBandInitialized = true;
        resetRubberBandState();
    }

    void VoiceManager::resetRubberBandState()
    {
        if (rubberBandLiveShifter == nullptr)
            return;

        rubberBandLiveShifter->reset();
        rubberBandLiveShifter->setPitchScale(playbackRate.load(std::memory_order_relaxed));

        rubberBandInputFill = 0;
        rubberBandOutputRead = 0;
        rubberBandOutputAvailable = 0;
        rubberBandStartDelayRemaining = static_cast<int>(rubberBandLiveShifter->getStartDelay());

        for (int ch = 0; ch < kRubberBandMaxChannels; ++ch)
        {
            std::fill(rubberBandInput[static_cast<size_t>(ch)].begin(), rubberBandInput[static_cast<size_t>(ch)].end(), 0.0f);
            std::fill(rubberBandOutput[static_cast<size_t>(ch)].begin(), rubberBandOutput[static_cast<size_t>(ch)].end(), 0.0f);
        }
    }
#endif

} // namespace sw
