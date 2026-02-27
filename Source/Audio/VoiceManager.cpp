#include "VoiceManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
        const juce::MidiBuffer emptyMidi;
        getNextAudioBlock(info,
                          emptyMidi,
                          lastBasePitchSemitones.load(std::memory_order_relaxed));
    }

    void VoiceManager::getNextAudioBlock(const juce::AudioSourceChannelInfo &info,
                                         const juce::MidiBuffer &midiMessages,
                                         double basePitchSemitones)
    {
        lastBasePitchSemitones.store(basePitchSemitones, std::memory_order_relaxed);

        // RT-SAFE — no allocations, locks, I/O, or logging.

        // 1. Drain command FIFO — all voice mutations happen here on audio thread.
        drainCommandFifo();

        // 2. Load sample buffer. Keep a local copy for this callback.
        auto loadedBuffer = sampleBuffer.load(std::memory_order_acquire);
        auto *buf = loadedBuffer.get();

        if (buf == nullptr || buf->getNumSamples() == 0)
        {
            info.clearActiveBufferRegion();
            anyVoiceActive.store(false, std::memory_order_relaxed);
            primaryVoiceActive.store(false, std::memory_order_relaxed);
            return;
        }

        // Clear output buffer — voices will add into it
        info.clearActiveBufferRegion();

        const int primaryIdx = juce::jlimit(0, kMaxVoices - 1, primaryVoiceIndex.load(std::memory_order_relaxed));
        const bool wasPrimaryActive = voices[static_cast<size_t>(primaryIdx)].active;

        auto allocateMidiVoice = [&]() -> Voice *
        {
            const bool reservePrimaryVoice = voices[0].active && voices[0].midiNote < 0;
            const int firstMidiVoiceIndex = reservePrimaryVoice ? 1 : 0;

            for (int i = firstMidiVoiceIndex; i < kMaxVoices; ++i)
            {
                if (!voices[static_cast<size_t>(i)].active)
                    return &voices[static_cast<size_t>(i)];
            }

            Voice *oldest = &voices[static_cast<size_t>(firstMidiVoiceIndex)];
            for (int i = firstMidiVoiceIndex + 1; i < kMaxVoices; ++i)
            {
                auto &candidate = voices[static_cast<size_t>(i)];
                if (candidate.triggerAge < oldest->triggerAge)
                    oldest = &candidate;
            }

            oldest->forceOff();
            return oldest;
        };

        auto applyMidiMessage = [&](const juce::MidiMessage &message)
        {
            if (message.isNoteOn())
            {
                playbackFinished.store(false, std::memory_order_relaxed);

                const int note = message.getNoteNumber();
                const int rootNote = previewRootMidiNote.load(std::memory_order_relaxed);
                const double totalSemitones = basePitchSemitones + static_cast<double>(note - rootNote);
                const double ratio = std::pow(2.0, totalSemitones / 12.0);

                if (auto *voice = allocateMidiVoice(); voice != nullptr)
                {
                    voice->noteOn(note, ratio, ratio, ++voiceAgeCounter);
                    anyVoiceActive.store(true, std::memory_order_relaxed);
                }
                return;
            }

            if (message.isAllNotesOff() || message.isAllSoundOff())
            {
                for (auto &v : voices)
                {
                    if (v.active)
                        v.noteOff();
                }
                return;
            }

            if (message.isNoteOff())
            {
                const int note = message.getNoteNumber();
                for (auto &v : voices)
                {
                    if (v.active && v.midiNote == note)
                        v.noteOff();
                }
            }
        };

        auto renderSegment = [&](int segmentOffset, int segmentSamples)
        {
            if (segmentSamples <= 0)
                return;

            RenderContext renderContext;
            renderContext.configuredLoopStart = loopStartSample.load(std::memory_order_relaxed);
            renderContext.configuredLoopEnd = loopEndSample.load(std::memory_order_relaxed);
            renderContext.loopEnabled = loopEnabled.load(std::memory_order_relaxed);
            renderContext.preserveLengthEnabled = preserveLengthEnabled.load(std::memory_order_relaxed);
            renderContext.bufferSampleRate = bufferSampleRate.load(std::memory_order_relaxed);

            std::array<int, kMaxVoices> activeVoiceIndices{};
            int activeVoiceCount = 0;

            for (int voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
            {
                auto &v = voices[static_cast<size_t>(voiceIndex)];
                if (v.active)
                    activeVoiceIndices[static_cast<size_t>(activeVoiceCount++)] = voiceIndex;
            }

            for (int index = 0; index < activeVoiceCount; ++index)
            {
                auto &v = voices[static_cast<size_t>(activeVoiceIndices[static_cast<size_t>(index)])];
                renderVoice(v,
                            *buf,
                            *info.buffer,
                            info.startSample + segmentOffset,
                            segmentSamples,
                            renderContext);
            }
        };

        int renderedSamples = 0;
        for (const auto metadata : midiMessages)
        {
            const int sampleOffset = juce::jlimit(0, info.numSamples, metadata.samplePosition);
            const int segmentSamples = sampleOffset - renderedSamples;
            if (segmentSamples > 0)
            {
                renderSegment(renderedSamples, segmentSamples);
                renderedSamples = sampleOffset;
            }

            applyMidiMessage(metadata.getMessage());
        }

        if (renderedSamples < info.numSamples)
            renderSegment(renderedSamples, info.numSamples - renderedSamples);

        bool anyStillActive = false;
        for (const auto &v : voices)
        {
            if (v.active)
            {
                anyStillActive = true;
                break;
            }
        }

        anyVoiceActive.store(anyStillActive, std::memory_order_relaxed);
        const bool primaryStillActive = voices[static_cast<size_t>(primaryIdx)].active;
        primaryVoiceActive.store(primaryStillActive, std::memory_order_relaxed);

        // Signal playback finished when the primary transport voice ends.
        if (wasPrimaryActive && !primaryStillActive)
        {
            playbackFinished.store(true, std::memory_order_relaxed);
        }
    }

    // ---------------------------------------------------------------------------
    // Command FIFO (lock-free MPSC)
    // ---------------------------------------------------------------------------

    void VoiceManager::enqueueCommand(const VoiceCommand &cmd)
    {
        // Spinlock for multiple producers (MIDI thread + message thread).
        // Contention is extremely rare and brief.
        while (fifoProducerLock.test_and_set(std::memory_order_acquire))
        {
            // spin
        }

        const int wp = fifoWritePos.load(std::memory_order_relaxed);
        const int nextWp = (wp + 1) % kCommandFifoSize;

        if (nextWp != fifoReadPos.load(std::memory_order_acquire))
        {
            commandFifo[static_cast<size_t>(wp)] = cmd;
            fifoWritePos.store(nextWp, std::memory_order_release);
        }
        // else: FIFO full — drop command (should never happen with 256 slots)

        fifoProducerLock.clear(std::memory_order_release);
    }

    void VoiceManager::drainCommandFifo()
    {
        // Called exclusively on the audio thread.
        const int wp = fifoWritePos.load(std::memory_order_acquire);
        int rp = fifoReadPos.load(std::memory_order_relaxed);

        while (rp != wp)
        {
            const auto &cmd = commandFifo[static_cast<size_t>(rp)];

            switch (cmd.type)
            {
            case VoiceCommand::Type::NoteOn:
            {
                if (sampleBuffer.load(std::memory_order_relaxed) == nullptr)
                    break;

                playbackFinished.store(false, std::memory_order_relaxed);

                const bool reservePrimaryVoice = voices[0].active && voices[0].midiNote < 0;
                const int firstMidiVoiceIndex = reservePrimaryVoice ? 1 : 0;

                Voice *selectedVoice = nullptr;
                for (int i = firstMidiVoiceIndex; i < kMaxVoices; ++i)
                {
                    if (!voices[static_cast<size_t>(i)].active)
                    {
                        selectedVoice = &voices[static_cast<size_t>(i)];
                        break;
                    }
                }

                if (selectedVoice == nullptr)
                {
                    Voice *oldest = &voices[static_cast<size_t>(firstMidiVoiceIndex)];
                    for (int i = firstMidiVoiceIndex + 1; i < kMaxVoices; ++i)
                    {
                        auto &candidate = voices[static_cast<size_t>(i)];
                        if (candidate.triggerAge < oldest->triggerAge)
                            oldest = &candidate;
                    }
                    selectedVoice = oldest;
                    selectedVoice->forceOff();
                }

                auto &v = *selectedVoice;
                v.noteOn(cmd.midiNote, cmd.playbackRate, cmd.pitchRatio, ++voiceAgeCounter);
                anyVoiceActive.store(true, std::memory_order_relaxed);
                break;
            }

            case VoiceCommand::Type::NoteOff:
            {
                for (auto &v : voices)
                {
                    if (v.active && v.midiNote == cmd.midiNote)
                        v.noteOff();
                }
                break;
            }

            case VoiceCommand::Type::AllNotesOff:
            {
                for (auto &v : voices)
                {
                    if (v.active)
                        v.noteOff();
                }
                break;
            }

            case VoiceCommand::Type::UpdatePitch:
            {
                const int rootNote = previewRootMidiNote.load(std::memory_order_relaxed);
                const double base = cmd.playbackRate; // reused field for baseSemitones

                for (auto &v : voices)
                {
                    if (!v.active)
                        continue;

                    double totalSemitones = base;
                    if (v.midiNote >= 0)
                        totalSemitones += static_cast<double>(v.midiNote - rootNote);

                    const double ratio = std::pow(2.0, totalSemitones / 12.0);
                    v.updatePitch(ratio, ratio);
                }
                break;
            }

            case VoiceCommand::Type::Play:
            {
                if (sampleBuffer.load(std::memory_order_relaxed) == nullptr)
                    break;

                playbackFinished.store(false, std::memory_order_relaxed);

                auto &v = voices[0];
                const double preservedPos = v.playbackPos;
                v.noteOn(/*note=*/-1, /*rate=*/1.0, /*pitch=*/1.0, ++voiceAgeCounter);
                v.playbackPos = preservedPos;
                primaryVoiceIndex.store(0, std::memory_order_relaxed);
                anyVoiceActive.store(true, std::memory_order_relaxed);
                break;
            }

            case VoiceCommand::Type::Stop:
            {
                for (auto &v : voices)
                {
                    if (v.active && v.midiNote < 0)
                        v.noteOff();
                }
                break;
            }

            case VoiceCommand::Type::SetPlaybackProgress:
            {
                const int length = loadedSampleLength.load(std::memory_order_relaxed);
                if (length <= 0)
                    break;

                const double clamped = juce::jlimit(0.0, 1.0, cmd.normalizedProgress);
                const int idx = primaryVoiceIndex.load(std::memory_order_relaxed);
                const int clampedIdx = juce::jlimit(0, kMaxVoices - 1, idx);
                voices[static_cast<size_t>(clampedIdx)].playbackPos = clamped * static_cast<double>(length);
                voices[static_cast<size_t>(clampedIdx)].granularResetRequested = true;
                break;
            }
            }

            rp = (rp + 1) % kCommandFifoSize;
        }

        fifoReadPos.store(rp, std::memory_order_release);
    }

    // ---------------------------------------------------------------------------
    // Retire ring for RT-safe shared_ptr handoff
    // ---------------------------------------------------------------------------

    void VoiceManager::reclaimRetiredBuffers()
    {
        // Called from message thread to deallocate old buffers.
        int rp = retireReadPos.load(std::memory_order_relaxed);
        const int wp = retireWritePos.load(std::memory_order_acquire);

        while (rp != wp)
        {
            retireRing[static_cast<size_t>(rp)].reset(); // deallocate on message thread
            rp = (rp + 1) % kRetireRingSize;
        }

        retireReadPos.store(rp, std::memory_order_release);
    }

    // ---------------------------------------------------------------------------
    // Per-voice rendering (audio thread)
    // ---------------------------------------------------------------------------

    void VoiceManager::renderVoice(Voice &voice,
                                   const juce::AudioBuffer<float> &srcBuffer,
                                   juce::AudioBuffer<float> &outputBuffer,
                                   int startSample,
                                   int numSamples,
                                   const RenderContext &renderContext)
    {
        const int numOutChannels = outputBuffer.getNumChannels();
        const int numSrcChannels = srcBuffer.getNumChannels();
        const int srcLength = srcBuffer.getNumSamples();
        const float *srcReadPtr0 = srcBuffer.getReadPointer(0);
        const float *srcReadPtr1 = srcBuffer.getReadPointer((numSrcChannels > 1) ? 1 : 0);

        const int64_t configuredLoopStart = renderContext.configuredLoopStart;
        const int64_t configuredLoopEnd = renderContext.configuredLoopEnd;

        const int loopStart = static_cast<int>(juce::jlimit<int64_t>(0, srcLength - 1, configuredLoopStart));
        const int loopEnd = static_cast<int>(juce::jlimit<int64_t>(0, srcLength - 1, configuredLoopEnd));
        const bool isLoopOn = renderContext.loopEnabled;
        const bool hasLoopRegion = isLoopOn && configuredLoopStart >= 0 && configuredLoopEnd >= 0 && loopEnd > loopStart;
        const double loopEndExclusive = static_cast<double>(loopEnd) + 1.0;
        const double loopLength = static_cast<double>(loopEnd - loopStart) + 1.0;

        double pos = voice.playbackPos;
        const double bsr = renderContext.bufferSampleRate;
        const double rate = voice.playbackRate * (bsr / currentSampleRate);
#if SW_HAVE_RUBBERBAND
        const double currentPitchRatio = voice.pitchRatio;
#endif
        const bool preserveLength = renderContext.preserveLengthEnabled && std::abs(rate - 1.0) > 0.0001;
#if SW_HAVE_RUBBERBAND
        const bool useRubberBandRt = preserveLength && voice.rubberBandInitialized;
#else
        constexpr bool useRubberBandRt = false;
#endif

        const float fadeRate = 1.0f / (Voice::kFadeTimeMs * 0.001f * static_cast<float>(currentSampleRate));

        if (!preserveLength &&
            !isLoopOn &&
            !hasLoopRegion &&
            std::abs(rate - 1.0) < 1.0e-9)
        {
            const int srcStart = juce::jlimit(0, srcLength - 1, static_cast<int>(pos));
            const int remainingSrc = srcLength - srcStart;
            const int samplesToRender = juce::jmin(numSamples, remainingSrc);

            if (samplesToRender > 0)
            {
                const auto fadeState = voice.fadeState;
                if (fadeState == Voice::FadeState::Active)
                {
                    for (int ch = 0; ch < numOutChannels; ++ch)
                    {
                        const int srcCh = (ch < numSrcChannels) ? ch : 0;
                        outputBuffer.addFrom(ch, startSample, srcBuffer, srcCh, srcStart, samplesToRender);
                    }
                }
                else if (fadeState == Voice::FadeState::FadingIn || fadeState == Voice::FadeState::FadingOut)
                {
                    const float startGain = voice.fadeGain;
                    const float signedRate = (fadeState == Voice::FadeState::FadingIn) ? fadeRate : -fadeRate;
                    const float endGain = juce::jlimit(0.0f,
                                                       1.0f,
                                                       startGain + signedRate * static_cast<float>(samplesToRender));

                    for (int ch = 0; ch < numOutChannels; ++ch)
                    {
                        const int srcCh = (ch < numSrcChannels) ? ch : 0;
                        outputBuffer.addFromWithRamp(ch,
                                                     startSample,
                                                     srcBuffer.getReadPointer(srcCh, srcStart),
                                                     samplesToRender,
                                                     startGain,
                                                     endGain);
                    }

                    voice.fadeGain = endGain;
                    if (fadeState == Voice::FadeState::FadingIn)
                    {
                        if (endGain >= 1.0f)
                            voice.fadeState = Voice::FadeState::Active;
                    }
                    else
                    {
                        if (endGain <= 0.0f)
                            voice.forceOff();
                    }
                }
                else
                {
                    voice.forceOff();
                }

                pos += static_cast<double>(samplesToRender);
            }

            if (samplesToRender < numSamples)
                voice.forceOff();

            voice.playbackPos = pos;
            return;
        }

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
            const float *srcRead = nullptr;
            if (channel == 0)
                srcRead = srcReadPtr0;
            else if (channel == 1)
                srcRead = srcReadPtr1;
            else
                srcRead = srcBuffer.getReadPointer(channel);

            const float s0 = srcRead[idx0];
            const float s1 = srcRead[idx1];
            return s0 + frac * (s1 - s0);
        };

        if (voice.granularResetRequested)
        {
            voice.granularResetRequested = false;
            voice.grainSamplesRemaining = 0;
#if SW_HAVE_RUBBERBAND
            voice.resetRubberBand();
#endif
        }

#if SW_HAVE_RUBBERBAND
        if (preserveLength && useRubberBandRt && voice.rubberBandStretcher != nullptr && voice.rubberBandBuffers != nullptr)
        {
            constexpr int kRenderChunkSize = 256;
            std::array<float, kRenderChunkSize> mixLeft{};
            std::array<float, kRenderChunkSize> mixRight{};

            int rendered = 0;
            while (rendered < numSamples && voice.active)
            {
                const int chunkSamples = juce::jmin(kRenderChunkSize, numSamples - rendered);
                int produced = 0;

                while (produced < chunkSamples && voice.active)
                {
                    while (voice.rubberBandOutputFifoCount <= 0 && voice.active)
                    {
                        const int needed = juce::jmax(1, voice.rubberBandProcessBlockSize - voice.rubberBandInputFill);
                        bool fedAny = false;

                        for (int feed = 0; feed < needed; ++feed)
                        {
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
                                    voice.forceOff();
                                    break;
                                }
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

                                voice.rubberBandBuffers->input[static_cast<size_t>(ch)][static_cast<size_t>(voice.rubberBandInputFill)] = inSample;
                            }

                            if (provideStartPadSample)
                                --voice.rubberBandPreferredStartPadRemaining;
                            else
                                pos += (bsr / currentSampleRate);

                            ++voice.rubberBandInputFill;
                            fedAny = true;
                        }

                        if (voice.rubberBandInputFill >= voice.rubberBandProcessBlockSize || fedAny)
                            voice.processRubberBandIfReady();

                        while (voice.rubberBandOutputFifoCount > 0 && voice.rubberBandStartDelayRemaining > 0)
                        {
                            voice.rubberBandOutputFifoRead = (voice.rubberBandOutputFifoRead + 1) % Voice::kRubberBandOutputFifoSize;
                            --voice.rubberBandOutputFifoCount;
                            --voice.rubberBandStartDelayRemaining;
                        }

                        if (!fedAny && voice.rubberBandOutputFifoCount <= 0)
                            break;
                    }

                    const float gain = voice.advanceFade(fadeRate);
                    if (gain <= 0.0f)
                        break;

                    float outSampleLeft = voice.rubberBandLastOutputLeft;
                    float outSampleRight = voice.rubberBandLastOutputRight;

                    if (voice.rubberBandOutputFifoCount > 0)
                    {
                        outSampleLeft = voice.rubberBandBuffers->outputFifo[0][static_cast<size_t>(voice.rubberBandOutputFifoRead)];
                        outSampleRight = voice.rubberBandBuffers->outputFifo[1][static_cast<size_t>(voice.rubberBandOutputFifoRead)];
                        voice.rubberBandOutputFifoRead = (voice.rubberBandOutputFifoRead + 1) % Voice::kRubberBandOutputFifoSize;
                        --voice.rubberBandOutputFifoCount;
                        voice.rubberBandLastOutputLeft = outSampleLeft;
                        voice.rubberBandLastOutputRight = outSampleRight;
                    }

                    float rubberBandAttackGain = 1.0f;
                    if (voice.rubberBandOnsetBlendRemaining > 0)
                    {
                        const int clampedRemaining = juce::jlimit(0,
                                                                  Voice::kRubberBandOnsetBlendSamples,
                                                                  voice.rubberBandOnsetBlendRemaining);
                        rubberBandAttackGain = 1.0f - (static_cast<float>(clampedRemaining) /
                                                       static_cast<float>(Voice::kRubberBandOnsetBlendSamples));
                        --voice.rubberBandOnsetBlendRemaining;
                    }

                    mixLeft[static_cast<size_t>(produced)] = outSampleLeft * rubberBandAttackGain * gain;
                    mixRight[static_cast<size_t>(produced)] = outSampleRight * rubberBandAttackGain * gain;
                    ++produced;
                }

                if (produced <= 0)
                    break;

                outputBuffer.addFrom(0,
                                     startSample + rendered,
                                     mixLeft.data(),
                                     produced);

                for (int ch = 1; ch < numOutChannels; ++ch)
                {
                    outputBuffer.addFrom(ch,
                                         startSample + rendered,
                                         mixRight.data(),
                                         produced);
                }

                rendered += produced;
            }

            voice.playbackPos = pos;
            return;
        }
#endif

        for (int i = 0; i < numSamples; ++i)
        {
            // Check if voice finished fading out
            if (!voice.active)
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
                if (useRubberBandRt)
                {
#if SW_HAVE_RUBBERBAND
                    if (voice.rubberBandStretcher != nullptr && voice.rubberBandBuffers != nullptr)
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
                            voice.rubberBandBuffers->input[static_cast<size_t>(ch)][static_cast<size_t>(voice.rubberBandInputFill)] = inSample;
                        }

                        if (provideStartPadSample)
                            --voice.rubberBandPreferredStartPadRemaining;
                        else
                            pos += (bsr / currentSampleRate);

                        ++voice.rubberBandInputFill;
                        if (voice.rubberBandInputFill >= voice.rubberBandProcessBlockSize)
                        {
                            voice.processRubberBandIfReady();
                        }
                        else if (voice.rubberBandOutputFifoCount == 0 && (i & 0x0F) == 0)
                        {
                            voice.processRubberBandIfReady();
                        }

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
                            outSampleLeft = voice.rubberBandBuffers->outputFifo[0][static_cast<size_t>(voice.rubberBandOutputFifoRead)];
                            outSampleRight = voice.rubberBandBuffers->outputFifo[1][static_cast<size_t>(voice.rubberBandOutputFifoRead)];
                            voice.rubberBandOutputFifoRead = (voice.rubberBandOutputFifoRead + 1) % Voice::kRubberBandOutputFifoSize;
                            --voice.rubberBandOutputFifoCount;
                            voice.rubberBandLastOutputLeft = outSampleLeft;
                            voice.rubberBandLastOutputRight = outSampleRight;
                        }
                        else
                        {
                            outSampleLeft = voice.rubberBandLastOutputLeft;
                            outSampleRight = voice.rubberBandLastOutputRight;
                        }

                        float rubberBandAttackGain = 1.0f;
                        if (voice.rubberBandOnsetBlendRemaining > 0)
                        {
                            const int clampedRemaining = juce::jlimit(0,
                                                                      Voice::kRubberBandOnsetBlendSamples,
                                                                      voice.rubberBandOnsetBlendRemaining);
                            rubberBandAttackGain = 1.0f - (static_cast<float>(clampedRemaining) /
                                                           static_cast<float>(Voice::kRubberBandOnsetBlendSamples));
                            --voice.rubberBandOnsetBlendRemaining;
                        }

                        outSampleLeft *= rubberBandAttackGain;
                        outSampleRight *= rubberBandAttackGain;

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
                    const float *srcRead = nullptr;
                    if (srcCh == 0)
                        srcRead = srcReadPtr0;
                    else if (srcCh == 1)
                        srcRead = srcReadPtr1;
                    else
                        srcRead = srcBuffer.getReadPointer(srcCh);

                    float s0 = srcRead[idx0];
                    float s1 = srcRead[idx1];
                    outputBuffer.addSample(ch, startSample + i, (s0 + frac * (s1 - s0)) * gain);
                }

                pos += rate;
            }
        }

        voice.playbackPos = pos;
    }

    // ---------------------------------------------------------------------------
    // Voice allocation (audio thread only)
    // ---------------------------------------------------------------------------

    Voice &VoiceManager::allocateVoice()
    {
        // 1. Find an idle voice
        for (auto &v : voices)
        {
            if (!v.active)
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
    // Control (message/MIDI thread — enqueue commands)
    // ---------------------------------------------------------------------------

    void VoiceManager::loadBuffer(std::unique_ptr<juce::AudioBuffer<float>> buffer, double fileSampleRate)
    {
        std::shared_ptr<juce::AudioBuffer<float>> sharedBuffer;
        if (buffer != nullptr)
            sharedBuffer = std::shared_ptr<juce::AudioBuffer<float>>(std::move(buffer));

        loadBuffer(std::move(sharedBuffer), fileSampleRate);
    }

    void VoiceManager::loadBuffer(std::shared_ptr<juce::AudioBuffer<float>> buffer, double fileSampleRate)
    {
        // Reclaim any previously retired buffers (deallocate on message thread).
        reclaimRetiredBuffers();

        // Stop all voices via command FIFO
        enqueueCommand({VoiceCommand::Type::AllNotesOff});
        enqueueCommand({VoiceCommand::Type::Stop});

        playbackFinished.store(false, std::memory_order_relaxed);
        primaryVoiceActive.store(false, std::memory_order_relaxed);
        loadedSampleLength.store(buffer != nullptr ? buffer->getNumSamples() : 0, std::memory_order_relaxed);

        bufferSampleRate.store(fileSampleRate, std::memory_order_relaxed);
        sampleBuffer.store(std::move(buffer), std::memory_order_release);
    }

    void VoiceManager::play()
    {
        enqueueCommand({VoiceCommand::Type::Play});
    }

    void VoiceManager::stop()
    {
        enqueueCommand({VoiceCommand::Type::Stop});
    }

    void VoiceManager::noteOn(int midiNote, double rate, double pitch)
    {
        VoiceCommand cmd;
        cmd.type = VoiceCommand::Type::NoteOn;
        cmd.midiNote = midiNote;
        cmd.playbackRate = rate;
        cmd.pitchRatio = pitch;
        enqueueCommand(cmd);
    }

    void VoiceManager::noteOff(int midiNote)
    {
        VoiceCommand cmd;
        cmd.type = VoiceCommand::Type::NoteOff;
        cmd.midiNote = midiNote;
        enqueueCommand(cmd);
    }

    void VoiceManager::allNotesOff()
    {
        enqueueCommand({VoiceCommand::Type::AllNotesOff});
    }

    void VoiceManager::updateAllVoicePitch(double baseSemitones)
    {
        lastBasePitchSemitones.store(baseSemitones, std::memory_order_relaxed);

        VoiceCommand cmd;
        cmd.type = VoiceCommand::Type::UpdatePitch;
        cmd.playbackRate = baseSemitones; // reuse field for baseSemitones
        enqueueCommand(cmd);
    }

    void VoiceManager::setPreserveLengthEnabled(bool enabled)
    {
        preserveLengthEnabled.store(enabled, std::memory_order_relaxed);
    }

    bool VoiceManager::isPreserveLengthEnabled() const noexcept
    {
        return preserveLengthEnabled.load(std::memory_order_relaxed);
    }

    void VoiceManager::setStretchHighQualityEnabled(bool enabled)
    {
        (void)enabled;
    }

    bool VoiceManager::isStretchHighQualityEnabled() const noexcept
    {
#if SW_HAVE_RUBBERBAND
        return true;
#else
        return false;
#endif
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
        return anyVoiceActive.load(std::memory_order_relaxed);
    }

    bool VoiceManager::isPrimaryPlaying() const noexcept
    {
        return primaryVoiceActive.load(std::memory_order_relaxed);
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
        // Reading playbackPos from a non-audio thread: this is a benign race
        // (used only for UI progress display, exact accuracy not required).
        const double position = voices[static_cast<size_t>(clampedIdx)].playbackPos;
        return juce::jlimit(0.0, 1.0, position / static_cast<double>(length));
    }

    void VoiceManager::setPlaybackProgressNormalized(double normalizedProgress)
    {
        VoiceCommand cmd;
        cmd.type = VoiceCommand::Type::SetPlaybackProgress;
        cmd.normalizedProgress = normalizedProgress;
        enqueueCommand(cmd);
    }

    void VoiceManager::getActiveMidiPlaybackHeadsNormalized(std::vector<float> &headsOut) const
    {
        headsOut.clear();

        const int length = loadedSampleLength.load(std::memory_order_relaxed);
        if (length <= 0)
            return;

        for (const auto &voice : voices)
        {
            if (!voice.active || voice.midiNote < 0)
                continue;

            const float normalized = juce::jlimit(0.0f,
                                                  1.0f,
                                                  static_cast<float>(voice.playbackPos / static_cast<double>(length)));
            headsOut.push_back(normalized);
        }
    }

} // namespace sw
