#include "VoiceManager.h"
#include <cmath>
#include <cstring>

#if SW_HAVE_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
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

#if SW_HAVE_RUBBERBAND
        auto processRubberBandIfReady = [&]()
        {
            if (rubberBandStretcher == nullptr)
                return;

            while (true)
            {
                int required = static_cast<int>(rubberBandStretcher->getSamplesRequired());
                if (required <= 0 || required > kRubberBandMaxBlockSize)
                    required = rubberBandProcessBlockSize;

                required = juce::jlimit(1, kRubberBandMaxBlockSize, required);
                if (rubberBandInputFill < required)
                    break;

                for (int ch = 0; ch < kRubberBandMaxChannels; ++ch)
                {
                    rubberBandInputPtrs[static_cast<size_t>(ch)] = rubberBandInput[static_cast<size_t>(ch)].data();
                    rubberBandProcessOutputPtrs[static_cast<size_t>(ch)] = rubberBandProcessOutput[static_cast<size_t>(ch)].data();
                }

                rubberBandStretcher->process(rubberBandInputPtrs.data(), static_cast<size_t>(required), false);

                const int remaining = rubberBandInputFill - required;
                if (remaining > 0)
                {
                    for (int ch = 0; ch < kRubberBandMaxChannels; ++ch)
                    {
                        std::memmove(rubberBandInput[static_cast<size_t>(ch)].data(),
                                     rubberBandInput[static_cast<size_t>(ch)].data() + required,
                                     static_cast<size_t>(remaining) * sizeof(float));
                    }
                }
                rubberBandInputFill = remaining;

                int available = rubberBandStretcher->available();
                while (available > 0)
                {
                    const size_t toRetrieve = static_cast<size_t>(juce::jmin(available, kRubberBandMaxBlockSize));
                    const size_t retrieved = rubberBandStretcher->retrieve(rubberBandProcessOutputPtrs.data(), toRetrieve);
                    if (retrieved == 0)
                        break;

                    for (size_t frame = 0; frame < retrieved; ++frame)
                    {
                        if (rubberBandOutputFifoCount >= kRubberBandOutputFifoSize)
                        {
                            rubberBandOutputFifoRead = (rubberBandOutputFifoRead + 1) % kRubberBandOutputFifoSize;
                            --rubberBandOutputFifoCount;
                        }

                        for (int ch = 0; ch < kRubberBandMaxChannels; ++ch)
                        {
                            rubberBandOutputFifo[static_cast<size_t>(ch)][static_cast<size_t>(rubberBandOutputFifoWrite)] =
                                rubberBandProcessOutput[static_cast<size_t>(ch)][frame];
                        }

                        rubberBandOutputFifoWrite = (rubberBandOutputFifoWrite + 1) % kRubberBandOutputFifoSize;
                        ++rubberBandOutputFifoCount;
                    }

                    available = rubberBandStretcher->available();
                }
            }
        };
#endif

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
                    if (rubberBandStretcher != nullptr)
                    {
                        if (std::abs(currentPitchRatio - rubberBandLastPitchScale) > 1.0e-6)
                        {
                            rubberBandStretcher->setPitchScale(currentPitchRatio);
                            rubberBandLastPitchScale = currentPitchRatio;
                        }

                        const bool provideStartPadSample = (rubberBandPreferredStartPadRemaining > 0);

                        for (int ch = 0; ch < kRubberBandMaxChannels; ++ch)
                        {
                            float inSample = 0.0f;
                            if (!provideStartPadSample)
                            {
                                const int srcCh = (ch < numSrcChannels) ? ch : 0;
                                inSample = readInterpolatedSample(srcCh, pos);
                            }
                            rubberBandInput[static_cast<size_t>(ch)][static_cast<size_t>(rubberBandInputFill)] = inSample;
                        }

                        if (provideStartPadSample)
                        {
                            --rubberBandPreferredStartPadRemaining;
                        }
                        else
                        {
                            pos += (bufferSampleRate / currentSampleRate);
                        }

                        ++rubberBandInputFill;
                        processRubberBandIfReady();

                        while (rubberBandOutputFifoCount > 0 && rubberBandStartDelayRemaining > 0)
                        {
                            rubberBandOutputFifoRead = (rubberBandOutputFifoRead + 1) % kRubberBandOutputFifoSize;
                            --rubberBandOutputFifoCount;
                            --rubberBandStartDelayRemaining;
                        }

                        float outSampleLeft = 0.0f;
                        float outSampleRight = 0.0f;
                        if (rubberBandOutputFifoCount > 0)
                        {
                            outSampleLeft = rubberBandOutputFifo[0][static_cast<size_t>(rubberBandOutputFifoRead)];
                            outSampleRight = rubberBandOutputFifo[1][static_cast<size_t>(rubberBandOutputFifoRead)];
                            rubberBandOutputFifoRead = (rubberBandOutputFifoRead + 1) % kRubberBandOutputFifoSize;
                            --rubberBandOutputFifoCount;
                        }
                        else
                        {
                            const int fallbackIndex = juce::jlimit(0, kRubberBandMaxBlockSize - 1, rubberBandInputFill > 0 ? rubberBandInputFill - 1 : 0);
                            outSampleLeft = rubberBandInput[0][static_cast<size_t>(fallbackIndex)];
                            outSampleRight = rubberBandInput[1][static_cast<size_t>(fallbackIndex)];
                        }

                        for (int ch = 0; ch < numOutChannels; ++ch)
                        {
                            const float sample = (ch == 0) ? outSampleLeft : outSampleRight;
                            info.buffer->setSample(ch, info.startSample + i, sample);
                        }

                        continue;
                    }
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

        const auto options = RubberBand::RubberBandStretcher::OptionProcessRealTime |
                             RubberBand::RubberBandStretcher::OptionEngineFiner |
                             RubberBand::RubberBandStretcher::OptionThreadingNever |
                             RubberBand::RubberBandStretcher::OptionWindowStandard |
                             RubberBand::RubberBandStretcher::OptionChannelsTogether |
                             RubberBand::RubberBandStretcher::OptionFormantPreserved |
                             RubberBand::RubberBandStretcher::OptionPitchHighConsistency;

        rubberBandStretcher = std::make_unique<RubberBand::RubberBandStretcher>(
            static_cast<size_t>(juce::jlimit(8000, 192000, static_cast<int>(std::round(currentSampleRate)))),
            static_cast<size_t>(kRubberBandMaxChannels),
            options,
            1.0,
            pitchRatio.load(std::memory_order_relaxed));

        if (rubberBandStretcher == nullptr)
        {
            rubberBandInitialized = false;
            return;
        }

        rubberBandStretcher->setDebugLevel(0);
        rubberBandStretcher->setMaxProcessSize(static_cast<size_t>(kRubberBandMaxBlockSize));

        rubberBandProcessBlockSize = static_cast<int>(rubberBandStretcher->getSamplesRequired());
        if (rubberBandProcessBlockSize <= 0 || rubberBandProcessBlockSize > kRubberBandMaxBlockSize)
            rubberBandProcessBlockSize = 1024;

        if (rubberBandProcessBlockSize <= 0 || rubberBandProcessBlockSize > kRubberBandMaxBlockSize)
        {
            rubberBandStretcher.reset();
            rubberBandInitialized = false;
            return;
        }

        rubberBandInitialized = true;
        resetRubberBandState();
    }

    void VoiceManager::resetRubberBandState()
    {
        if (rubberBandStretcher == nullptr)
            return;

        rubberBandStretcher->reset();
        rubberBandStretcher->setTimeRatio(1.0);
        rubberBandLastPitchScale = pitchRatio.load(std::memory_order_relaxed);
        rubberBandStretcher->setPitchScale(rubberBandLastPitchScale);

        rubberBandInputFill = 0;
        rubberBandOutputFifoRead = 0;
        rubberBandOutputFifoWrite = 0;
        rubberBandOutputFifoCount = 0;
        rubberBandPreferredStartPadRemaining = static_cast<int>(rubberBandStretcher->getPreferredStartPad());
        rubberBandStartDelayRemaining = static_cast<int>(rubberBandStretcher->getStartDelay());

        const int required = static_cast<int>(rubberBandStretcher->getSamplesRequired());
        if (required > 0 && required <= kRubberBandMaxBlockSize)
            rubberBandProcessBlockSize = required;

        for (int ch = 0; ch < kRubberBandMaxChannels; ++ch)
        {
            std::fill(rubberBandInput[static_cast<size_t>(ch)].begin(), rubberBandInput[static_cast<size_t>(ch)].end(), 0.0f);
            std::fill(rubberBandProcessOutput[static_cast<size_t>(ch)].begin(), rubberBandProcessOutput[static_cast<size_t>(ch)].end(), 0.0f);
            std::fill(rubberBandOutputFifo[static_cast<size_t>(ch)].begin(), rubberBandOutputFifo[static_cast<size_t>(ch)].end(), 0.0f);
        }
    }
#endif

} // namespace sw
