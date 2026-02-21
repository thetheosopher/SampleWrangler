#include "VoiceManager.h"
#include <cmath>
#include <cstring>
#include <algorithm>

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
        for (auto &v : voices)
            v.initialiseRubberBand(sampleRate, 1.0);
#endif
    }

    void VoiceManager::releaseResources()
    {
        for (auto &v : voices)
            v.forceOff();
    }

    void VoiceManager::getNextAudioBlock(const juce::AudioSourceChannelInfo &info)
    {
        // RT-SAFE — no allocations, locks, I/O, or logging.
        const auto loadedBuffer = sampleBuffer.load(std::memory_order_acquire);
        auto *buf = loadedBuffer.get();

        if (buf == nullptr || buf->getNumSamples() == 0)
        {
            info.clearActiveBufferRegion();
            return;
        }

        // Check if any voice is active
        bool anyActive = false;
        for (const auto &v : voices)
        {
            if (v.active.load(std::memory_order_relaxed))
            {
                anyActive = true;
                break;
            }
        }

        if (!anyActive)
        {
            info.clearActiveBufferRegion();
            return;
        }

        // Clear output buffer — voices will add into it
        info.clearActiveBufferRegion();

        bool anyStillActive = false;
        for (auto &v : voices)
        {
            if (!v.active.load(std::memory_order_relaxed))
                continue;

            renderVoice(v, *buf, *info.buffer, info.startSample, info.numSamples);

            if (v.active.load(std::memory_order_relaxed))
                anyStillActive = true;
        }

        // If no voices remain active after rendering and no MIDI notes are held,
        // signal playback finished (for autoplay).
        if (!anyStillActive)
        {
            playbackFinished.store(true, std::memory_order_relaxed);
        }
    }

    // ---------------------------------------------------------------------------
    // Per-voice rendering (audio thread)
    // ---------------------------------------------------------------------------

    void VoiceManager::renderVoice(Voice &voice,
                                   const juce::AudioBuffer<float> &srcBuffer,
                                   juce::AudioBuffer<float> &outputBuffer,
                                   int startSample,
                                   int numSamples)
    {
        const int numOutChannels = outputBuffer.getNumChannels();
        const int numSrcChannels = srcBuffer.getNumChannels();
        const int srcLength = srcBuffer.getNumSamples();

        const int64_t configuredLoopStart = loopStartSample.load(std::memory_order_relaxed);
        const int64_t configuredLoopEnd = loopEndSample.load(std::memory_order_relaxed);

        const int loopStart = static_cast<int>(juce::jlimit<int64_t>(0, srcLength - 1, configuredLoopStart));
        const int loopEnd = static_cast<int>(juce::jlimit<int64_t>(0, srcLength - 1, configuredLoopEnd));
        const bool isLoopOn = loopEnabled.load(std::memory_order_relaxed);
        const bool hasLoopRegion = isLoopOn && configuredLoopStart >= 0 && configuredLoopEnd >= 0 && loopEnd > loopStart;
        const double loopEndExclusive = static_cast<double>(loopEnd) + 1.0;
        const double loopLength = static_cast<double>(loopEnd - loopStart) + 1.0;

        double pos = voice.playbackPos;
        const double rate = voice.playbackRate.load(std::memory_order_relaxed) * (bufferSampleRate / currentSampleRate);
#if SW_HAVE_RUBBERBAND
        const double currentPitchRatio = voice.pitchRatio.load(std::memory_order_relaxed);
#endif
        const bool preserveLength = preserveLengthEnabled.load(std::memory_order_relaxed) && std::abs(rate - 1.0) > 0.0001;
#if SW_HAVE_RUBBERBAND
        const bool useRubberBandHq = preserveLength &&
                                     stretchHighQualityEnabled.load(std::memory_order_relaxed) &&
                                     voice.rubberBandInitialized;
#else
        constexpr bool useRubberBandHq = false;
#endif

        const float fadeRate = 1.0f / (Voice::kFadeTimeMs * 0.001f * static_cast<float>(currentSampleRate));

        // Wrap position helper
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

            if (isLoopOn && srcLength > 0)
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
            const float s0 = srcBuffer.getSample(channel, idx0);
            const float s1 = srcBuffer.getSample(channel, idx1);
            return s0 + frac * (s1 - s0);
        };

        if (voice.granularResetRequested.exchange(false, std::memory_order_acq_rel))
        {
            voice.grainSamplesRemaining = 0;
#if SW_HAVE_RUBBERBAND
            voice.resetRubberBand();
#endif
        }

        for (int i = 0; i < numSamples; ++i)
        {
            // Check if voice finished fading out
            if (!voice.active.load(std::memory_order_relaxed))
                break;

            // Loop wrapping
            if (hasLoopRegion && pos >= loopEndExclusive)
            {
                pos = static_cast<double>(loopStart) + std::fmod(pos - static_cast<double>(loopStart), loopLength);
            }

            if (pos >= static_cast<double>(srcLength))
            {
                if (isLoopOn && srcLength > 0)
                {
                    pos = std::fmod(pos, static_cast<double>(srcLength));
                }
                else
                {
                    // End of sample — deactivate this voice
                    voice.forceOff();
                    break;
                }
            }

            const float gain = voice.advanceFade(fadeRate);
            if (gain <= 0.0f)
                break;

            if (preserveLength)
            {
                if (useRubberBandHq)
                {
#if SW_HAVE_RUBBERBAND
                    if (voice.rubberBandStretcher != nullptr)
                    {
                        if (std::abs(currentPitchRatio - voice.rubberBandLastPitchScale) > 1.0e-6)
                        {
                            voice.rubberBandStretcher->setPitchScale(currentPitchRatio);
                            voice.rubberBandLastPitchScale = currentPitchRatio;
                        }

                        const bool provideStartPadSample = (voice.rubberBandPreferredStartPadRemaining > 0);

                        for (int ch = 0; ch < Voice::kMaxChannels; ++ch)
                        {
                            float inSample = 0.0f;
                            if (!provideStartPadSample)
                            {
                                const int srcCh = (ch < numSrcChannels) ? ch : 0;
                                inSample = readInterpolatedSample(srcCh, pos);
                            }
                            voice.rubberBandInput[static_cast<size_t>(ch)][static_cast<size_t>(voice.rubberBandInputFill)] = inSample;
                        }

                        if (provideStartPadSample)
                            --voice.rubberBandPreferredStartPadRemaining;
                        else
                            pos += (bufferSampleRate / currentSampleRate);

                        ++voice.rubberBandInputFill;
                        voice.processRubberBandIfReady();

                        while (voice.rubberBandOutputFifoCount > 0 && voice.rubberBandStartDelayRemaining > 0)
                        {
                            voice.rubberBandOutputFifoRead = (voice.rubberBandOutputFifoRead + 1) % Voice::kRubberBandOutputFifoSize;
                            --voice.rubberBandOutputFifoCount;
                            --voice.rubberBandStartDelayRemaining;
                        }

                        float outSampleLeft = 0.0f;
                        float outSampleRight = 0.0f;
                        if (voice.rubberBandOutputFifoCount > 0)
                        {
                            outSampleLeft = voice.rubberBandOutputFifo[0][static_cast<size_t>(voice.rubberBandOutputFifoRead)];
                            outSampleRight = voice.rubberBandOutputFifo[1][static_cast<size_t>(voice.rubberBandOutputFifoRead)];
                            voice.rubberBandOutputFifoRead = (voice.rubberBandOutputFifoRead + 1) % Voice::kRubberBandOutputFifoSize;
                            --voice.rubberBandOutputFifoCount;
                        }
                        else
                        {
                            const int fallbackIndex = juce::jlimit(0, Voice::kRubberBandMaxBlockSize - 1,
                                                                   voice.rubberBandInputFill > 0 ? voice.rubberBandInputFill - 1 : 0);
                            outSampleLeft = voice.rubberBandInput[0][static_cast<size_t>(fallbackIndex)];
                            outSampleRight = voice.rubberBandInput[1][static_cast<size_t>(fallbackIndex)];
                        }

                        for (int ch = 0; ch < numOutChannels; ++ch)
                        {
                            const float sample = (ch == 0) ? outSampleLeft : outSampleRight;
                            outputBuffer.addSample(ch, startSample + i, sample * gain);
                        }

                        continue;
                    }
#endif
                }

                // Granular pitch-shift fallback
                if (voice.grainSamplesRemaining <= 0)
                {
                    voice.grainReadPosA = pos;
                    voice.grainReadPosB = pos + Voice::kGrainSpacingSamples;
                    voice.grainSamplesRemaining = Voice::kGrainLengthSamples;
                }

                const float blend = 1.0f - (static_cast<float>(voice.grainSamplesRemaining) / static_cast<float>(Voice::kGrainLengthSamples));

                for (int ch = 0; ch < numOutChannels; ++ch)
                {
                    const int srcCh = (ch < numSrcChannels) ? ch : 0;
                    const float sampleA = readInterpolatedSample(srcCh, voice.grainReadPosA);
                    const float sampleB = readInterpolatedSample(srcCh, voice.grainReadPosB);
                    outputBuffer.addSample(ch, startSample + i, (sampleA + (sampleB - sampleA) * blend) * gain);
                }

                voice.grainReadPosA += rate;
                voice.grainReadPosB += rate;
                --voice.grainSamplesRemaining;

                if (voice.grainSamplesRemaining <= 0)
                {
                    voice.grainReadPosA = voice.grainReadPosB;
                    voice.grainReadPosB = voice.grainReadPosA + Voice::kGrainSpacingSamples;
                }

                pos += 1.0;
            }
            else
            {
                // Standard resample-style playback with linear interpolation
                int idx0 = static_cast<int>(pos);
                int idx1 = std::min(idx0 + 1, srcLength - 1);
                float frac = static_cast<float>(pos - static_cast<double>(idx0));

                for (int ch = 0; ch < numOutChannels; ++ch)
                {
                    int srcCh = (ch < numSrcChannels) ? ch : 0;
                    float s0 = srcBuffer.getSample(srcCh, idx0);
                    float s1 = srcBuffer.getSample(srcCh, idx1);
                    outputBuffer.addSample(ch, startSample + i, (s0 + frac * (s1 - s0)) * gain);
                }

                pos += rate;
            }
        }

        voice.playbackPos = pos;
    }

    // ---------------------------------------------------------------------------
    // Voice allocation
    // ---------------------------------------------------------------------------

    Voice &VoiceManager::allocateVoice()
    {
        // 1. Find an idle voice
        for (auto &v : voices)
        {
            if (!v.active.load(std::memory_order_relaxed))
                return v;
        }

        // 2. All voices occupied — steal the oldest (lowest triggerAge)
        Voice *oldest = &voices[0];
        for (size_t i = 1; i < voices.size(); ++i)
        {
            if (voices[i].triggerAge < oldest->triggerAge)
                oldest = &voices[i];
        }

        // Force-stop the stolen voice (the fade-in of the new note provides crossfade)
        oldest->forceOff();
        return *oldest;
    }

    // ---------------------------------------------------------------------------
    // Control (message thread)
    // ---------------------------------------------------------------------------

    void VoiceManager::loadBuffer(std::unique_ptr<juce::AudioBuffer<float>> buffer, double fileSampleRate)
    {
        // Stop all voices before swapping the buffer
        for (auto &v : voices)
            v.forceOff();

        playbackFinished.store(false, std::memory_order_relaxed);
        loadedSampleLength.store(buffer != nullptr ? buffer->getNumSamples() : 0, std::memory_order_relaxed);

        std::shared_ptr<juce::AudioBuffer<float>> sharedBuffer;
        if (buffer != nullptr)
            sharedBuffer = std::shared_ptr<juce::AudioBuffer<float>>(std::move(buffer));

        bufferSampleRate = fileSampleRate;
        sampleBuffer.store(std::move(sharedBuffer), std::memory_order_release);
    }

    void VoiceManager::play()
    {
        if (sampleBuffer.load(std::memory_order_acquire) == nullptr)
            return;

        playbackFinished.store(false, std::memory_order_relaxed);

        // Use voice 0 as the "primary" non-MIDI voice
        auto &v = voices[0];
        v.noteOn(/*note=*/-1, /*rate=*/1.0, /*pitch=*/1.0, ++voiceAgeCounter);
        primaryVoiceIndex.store(0, std::memory_order_relaxed);
    }

    void VoiceManager::stop()
    {
        allNotesOff();
    }

    void VoiceManager::noteOn(int midiNote, double rate, double pitch)
    {
        if (sampleBuffer.load(std::memory_order_acquire) == nullptr)
            return;

        playbackFinished.store(false, std::memory_order_relaxed);

        auto &v = allocateVoice();
        v.noteOn(midiNote, rate, pitch, ++voiceAgeCounter);

        // Track the most recently triggered voice as primary for progress display
        const int idx = static_cast<int>(&v - &voices[0]);
        primaryVoiceIndex.store(idx, std::memory_order_relaxed);
    }

    void VoiceManager::noteOff(int midiNote)
    {
        for (auto &v : voices)
        {
            if (v.active.load(std::memory_order_relaxed) &&
                v.midiNote.load(std::memory_order_relaxed) == midiNote)
            {
                v.noteOff();
            }
        }
    }

    void VoiceManager::allNotesOff()
    {
        for (auto &v : voices)
        {
            if (v.active.load(std::memory_order_relaxed))
                v.noteOff();
        }
    }

    void VoiceManager::updateAllVoicePitch(double baseSemitones)
    {
        const int rootNote = previewRootMidiNote.load(std::memory_order_relaxed);

        for (auto &v : voices)
        {
            if (!v.active.load(std::memory_order_relaxed))
                continue;

            const int note = v.midiNote.load(std::memory_order_relaxed);
            double totalSemitones = baseSemitones;
            if (note >= 0)
                totalSemitones += static_cast<double>(note - rootNote);

            const double ratio = std::pow(2.0, totalSemitones / 12.0);
            v.updatePitch(ratio, ratio);
        }
    }

    void VoiceManager::setPreserveLengthEnabled(bool enabled)
    {
        preserveLengthEnabled.store(enabled, std::memory_order_relaxed);
        for (auto &v : voices)
            v.granularResetRequested.store(true, std::memory_order_release);
    }

    bool VoiceManager::isPreserveLengthEnabled() const noexcept
    {
        return preserveLengthEnabled.load(std::memory_order_relaxed);
    }

    void VoiceManager::setStretchHighQualityEnabled(bool enabled)
    {
#if SW_HAVE_RUBBERBAND
        stretchHighQualityEnabled.store(enabled, std::memory_order_relaxed);
        for (auto &v : voices)
            v.granularResetRequested.store(true, std::memory_order_release);
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
        for (const auto &v : voices)
        {
            if (v.active.load(std::memory_order_relaxed))
                return true;
        }
        return false;
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

        const int idx = primaryVoiceIndex.load(std::memory_order_relaxed);
        const int clampedIdx = juce::jlimit(0, kMaxVoices - 1, idx);
        const double position = voices[static_cast<size_t>(clampedIdx)].playbackPos;
        return juce::jlimit(0.0, 1.0, position / static_cast<double>(length));
    }

    void VoiceManager::setPlaybackProgressNormalized(double normalizedProgress)
    {
        const int length = loadedSampleLength.load(std::memory_order_relaxed);
        if (length <= 0)
            return;

        const double clamped = juce::jlimit(0.0, 1.0, normalizedProgress);
        const int idx = primaryVoiceIndex.load(std::memory_order_relaxed);
        const int clampedIdx = juce::jlimit(0, kMaxVoices - 1, idx);
        voices[static_cast<size_t>(clampedIdx)].playbackPos = clamped * static_cast<double>(length);
        voices[static_cast<size_t>(clampedIdx)].granularResetRequested.store(true, std::memory_order_release);
    }

} // namespace sw
