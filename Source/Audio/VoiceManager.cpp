#include "VoiceManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sw
{

    namespace
    {
        constexpr double kRateEpsilon = 1.0e-9;
        constexpr uint64_t kPhaseFractionBits = 32;
        constexpr uint64_t kPhaseFractionMask = (uint64_t{1} << kPhaseFractionBits) - 1;
        constexpr double kPhaseScale = static_cast<double>(uint64_t{1} << kPhaseFractionBits);
        constexpr float kPhaseScaleInv = 1.0f / static_cast<float>(uint64_t{1} << kPhaseFractionBits);

        using InterpolatedSampleAtIndexFn = float (*)(const float *srcRead, int srcLength, int idx0, float frac) noexcept;

        float readLinearInterpolatedSampleAtIndex(const float *srcRead, int srcLength, int idx0, float frac) noexcept
        {
            const int clampedIdx0 = juce::jlimit(0, srcLength - 1, idx0);
            const int idx1 = juce::jmin(clampedIdx0 + 1, srcLength - 1);
            const float s0 = srcRead[clampedIdx0];
            const float s1 = srcRead[idx1];
            return s0 + frac * (s1 - s0);
        }

        float readLinearInterpolatedSample(const float *srcRead, int srcLength, double readPos) noexcept
        {
            const int idx0 = static_cast<int>(readPos);
            const float frac = static_cast<float>(readPos - static_cast<double>(idx0));
            return readLinearInterpolatedSampleAtIndex(srcRead, srcLength, idx0, frac);
        }

        float readHermiteInterpolatedSampleAtIndex(const float *srcRead, int srcLength, int idx0, float frac) noexcept
        {
            if (srcLength < 4)
                return readLinearInterpolatedSampleAtIndex(srcRead, srcLength, idx0, frac);

            const int clampedIdx0 = juce::jlimit(0, srcLength - 1, idx0);
            const int idxM1 = juce::jmax(0, clampedIdx0 - 1);
            const int idx1 = juce::jmin(clampedIdx0 + 1, srcLength - 1);
            const int idx2 = juce::jmin(clampedIdx0 + 2, srcLength - 1);

            const float xm1 = srcRead[idxM1];
            const float x0 = srcRead[clampedIdx0];
            const float x1 = srcRead[idx1];
            const float x2 = srcRead[idx2];

            const float c0 = x0;
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * frac + c2) * frac + c1) * frac + c0;
        }

        float readHermiteInterpolatedSample(const float *srcRead, int srcLength, double readPos) noexcept
        {
            const int idx0 = static_cast<int>(readPos);
            const float frac = static_cast<float>(readPos - static_cast<double>(idx0));
            return readHermiteInterpolatedSampleAtIndex(srcRead, srcLength, idx0, frac);
        }

        InterpolatedSampleAtIndexFn selectInterpolatedSampleReader(bool useHighQualityInterpolation) noexcept
        {
            return useHighQualityInterpolation ? &readHermiteInterpolatedSampleAtIndex
                                               : &readLinearInterpolatedSampleAtIndex;
        }

        uint64_t readPositionToPhase(double readPos) noexcept
        {
            return static_cast<uint64_t>(juce::jmax(0.0, readPos) * kPhaseScale);
        }

        uint64_t readRateToPhaseIncrement(double readRate) noexcept
        {
            return static_cast<uint64_t>(std::llround(juce::jmax(0.0, readRate) * kPhaseScale));
        }

        float smoothCrossfadeBlend(float blend) noexcept
        {
            const float clampedBlend = juce::jlimit(0.0f, 1.0f, blend);
            return clampedBlend * clampedBlend * (3.0f - 2.0f * clampedBlend);
        }

        double phaseToReadPosition(uint64_t phase) noexcept
        {
            return static_cast<double>(phase) / kPhaseScale;
        }

        int computeContiguousSpan(double readPos,
                                  double boundaryExclusive,
                                  double rate,
                                  int remainingSamples) noexcept
        {
            if (remainingSamples <= 0)
                return 0;

            if (rate <= kRateEpsilon)
                return 1;

            const double distance = boundaryExclusive - readPos;
            if (distance <= kRateEpsilon)
                return 1;

            const int span = static_cast<int>(std::ceil(distance / rate));
            return juce::jlimit(1, remainingSamples, span);
        }
    }

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

        // 2. Load the atomically published sample pointer for this callback.
        auto *buf = publishedSampleBuffer.load(std::memory_order_acquire);

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
            renderContext.stretchHighQualityEnabled = stretchHighQualityEnabled.load(std::memory_order_relaxed);
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
            case VoiceCommand::Type::SwapSampleBuffer:
            {
                for (auto &v : voices)
                {
                    v.forceOff();
                    v.playbackPos = 0.0;
                    v.fadeGain = 0.0f;
                    v.grainSamplesRemaining = 0;
                    v.granularResetRequested = true;
                }

                anyVoiceActive.store(false, std::memory_order_relaxed);
                primaryVoiceActive.store(false, std::memory_order_relaxed);
                playbackFinished.store(false, std::memory_order_relaxed);
                primaryVoiceIndex.store(0, std::memory_order_relaxed);
                loadedSampleLength.store(cmd.sampleBufferLength, std::memory_order_relaxed);
                bufferSampleRate.store(cmd.sampleBufferSampleRate, std::memory_order_relaxed);

                const int newSlot = cmd.sampleSlot;
                juce::AudioBuffer<float> *newBuffer = nullptr;
                if (newSlot >= 0 && newSlot < kSampleBufferSlotCount)
                    newBuffer = sampleBufferSlots[static_cast<size_t>(newSlot)].get();

                const int oldSlot = publishedSampleSlot.exchange(newSlot, std::memory_order_acq_rel);
                publishedSampleBuffer.store(newBuffer, std::memory_order_release);

                if (oldSlot >= 0 && oldSlot != newSlot)
                {
                    const int retireWp = retireWritePos.load(std::memory_order_relaxed);
                    const int nextRetireWp = (retireWp + 1) % kRetireRingSize;
                    const int retireRp = retireReadPos.load(std::memory_order_acquire);
                    if (nextRetireWp != retireRp)
                    {
                        retireRing[static_cast<size_t>(retireWp)] = oldSlot;
                        retireWritePos.store(nextRetireWp, std::memory_order_release);
                    }
                    else
                    {
                        jassertfalse;
                    }
                }
                break;
            }

            case VoiceCommand::Type::NoteOn:
            {
                if (publishedSampleBuffer.load(std::memory_order_relaxed) == nullptr)
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
                if (publishedSampleBuffer.load(std::memory_order_relaxed) == nullptr)
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
            const int slot = retireRing[static_cast<size_t>(rp)];
            if (slot >= 0 && slot < kSampleBufferSlotCount)
                sampleBufferSlots[static_cast<size_t>(slot)].reset();

            rp = (rp + 1) % kRetireRingSize;
        }

        retireReadPos.store(rp, std::memory_order_release);
    }

    int VoiceManager::findAvailableSampleBufferSlot() const noexcept
    {
        const int publishedSlot = publishedSampleSlot.load(std::memory_order_acquire);
        for (int slot = 0; slot < kSampleBufferSlotCount; ++slot)
        {
            if (slot == publishedSlot)
                continue;

            if (sampleBufferSlots[static_cast<size_t>(slot)] == nullptr)
                return slot;
        }

        return -1;
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
        const double sourceAdvancePerOutputSample = bsr / currentSampleRate;
        const double rate = voice.playbackRate * (bsr / currentSampleRate);
        const bool preserveLength = renderContext.preserveLengthEnabled && std::abs(rate - 1.0) > 0.0001;
        if (preserveLength)
        {
            if (!voice.preserveLengthModeLatched)
            {
                voice.preserveLengthModeLatched = true;
                voice.preserveLengthHighQualityLatched = renderContext.stretchHighQualityEnabled;
                voice.lastPreserveLengthUsedRubberBandRt = false;
                voice.rubberBandRtFailedForCurrentNote = false;
                voice.granularResetRequested = true;
            }
        }
        else if (voice.preserveLengthModeLatched)
        {
            voice.preserveLengthModeLatched = false;
            voice.preserveLengthHighQualityLatched = false;
            voice.lastPreserveLengthUsedRubberBandRt = false;
            voice.rubberBandRtFailedForCurrentNote = false;
        }

        const bool useHighQualityInterpolation = preserveLength
                                                     ? voice.preserveLengthHighQualityLatched
                                                     : renderContext.stretchHighQualityEnabled;
        const auto interpolatedSampleReader = selectInterpolatedSampleReader(useHighQualityInterpolation);
        const bool useRubberBandRt = preserveLength && useHighQualityInterpolation && voice.isRubberBandReady() && !voice.rubberBandRtFailedForCurrentNote;

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

            const float *srcRead = nullptr;
            if (channel == 0)
                srcRead = srcReadPtr0;
            else if (channel == 1)
                srcRead = srcReadPtr1;
            else
                srcRead = srcBuffer.getReadPointer(channel);

            if (useHighQualityInterpolation)
                return readHermiteInterpolatedSample(srcRead, srcLength, wrappedPos);

            return readLinearInterpolatedSample(srcRead, srcLength, wrappedPos);
        };

        if (voice.lastPreserveLengthUsedRubberBandRt != useRubberBandRt)
        {
            voice.lastPreserveLengthUsedRubberBandRt = useRubberBandRt;
            voice.granularResetRequested = true;
        }

        if (voice.granularResetRequested)
        {
            voice.granularResetRequested = false;
            voice.grainSamplesRemaining = 0;
            voice.resetRubberBand();
        }

        if (preserveLength && useRubberBandRt)
        {
            constexpr int kRenderChunkSize = 256;
            std::array<float, kRenderChunkSize> mixLeft{};
            std::array<float, kRenderChunkSize> mixRight{};
            voice.setRubberBandPitchScale(voice.pitchRatio);
            bool rubberBandSourceExhausted = false;
            const double rubberBandStartPos = pos;

            int rendered = 0;
            while (rendered < numSamples && voice.active)
            {
                const int chunkSamples = juce::jmin(kRenderChunkSize, numSamples - rendered);
                int produced = 0;

                while (produced < chunkSamples && voice.active)
                {
                    while (!voice.hasRubberBandOutput() && voice.active)
                    {
                        const int needed = voice.getRubberBandInputDeficit();
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
                                    rubberBandSourceExhausted = true;
                                    break;
                                }
                            }

                            const bool provideStartPadSample = voice.shouldProvideRubberBandStartPadSample();
                            float inputLeft = 0.0f;
                            float inputRight = 0.0f;
                            if (!provideStartPadSample)
                            {
                                inputLeft = readInterpolatedSample(0, pos);
                                inputRight = readInterpolatedSample((numSrcChannels > 1) ? 1 : 0, pos);
                            }

                            voice.pushRubberBandInput(inputLeft, inputRight);

                            if (provideStartPadSample)
                                voice.consumeRubberBandStartPadSample();
                            else
                                pos += sourceAdvancePerOutputSample;

                            fedAny = true;
                        }

                        if (fedAny)
                            voice.processRubberBandIfReady();

                        voice.skipRubberBandStartDelay();

                        if ((!fedAny || rubberBandSourceExhausted) && !voice.hasRubberBandOutput())
                            break;
                    }

                    if (rubberBandSourceExhausted && !voice.hasRubberBandOutput())
                        break;

                    const float gain = voice.advanceFade(fadeRate);
                    if (gain <= 0.0f)
                        break;

                    float outSampleLeft = 0.0f;
                    float outSampleRight = 0.0f;
                    voice.popRubberBandOutputOrReuseLast(outSampleLeft, outSampleRight);

                    const float rubberBandAttackGain = voice.consumeRubberBandAttackGain();

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

            if (rendered <= 0 && rubberBandSourceExhausted)
            {
                voice.rubberBandRtFailedForCurrentNote = true;
                voice.lastPreserveLengthUsedRubberBandRt = false;
                voice.grainSamplesRemaining = 0;
                pos = rubberBandStartPos;
            }
            else
            {
                if (rubberBandSourceExhausted && !voice.hasRubberBandOutput())
                    voice.forceOff();

                voice.playbackPos = pos;
                return;
            }
        }

        constexpr int kMaxDirectMixChannels = 32;
        const int directMixChannels = juce::jmin(numOutChannels, kMaxDirectMixChannels);
        std::array<float *, kMaxDirectMixChannels> outputWritePtrs{};
        for (int ch = 0; ch < directMixChannels; ++ch)
            outputWritePtrs[static_cast<size_t>(ch)] = outputBuffer.getWritePointer(ch, startSample);

        int renderedSamples = 0;
        while (renderedSamples < numSamples)
        {
            if (!voice.active)
                break;

            if (hasLoopRegion)
            {
                while (pos >= loopEndExclusive)
                    pos = static_cast<double>(loopStart) + std::fmod(pos - static_cast<double>(loopStart), loopLength);
            }
            else if (isLoopOn && srcLength > 0)
            {
                while (pos >= static_cast<double>(srcLength))
                    pos = std::fmod(pos, static_cast<double>(srcLength));
            }
            else if (pos >= static_cast<double>(srcLength))
            {
                voice.forceOff();
                break;
            }

            const double boundaryExclusive = hasLoopRegion
                                                 ? loopEndExclusive
                                                 : static_cast<double>(srcLength);
            const int spanSamples = computeContiguousSpan(pos,
                                                          boundaryExclusive,
                                                          preserveLength ? sourceAdvancePerOutputSample : rate,
                                                          numSamples - renderedSamples);
            if (spanSamples <= 0)
                break;

            if (preserveLength)
            {
                const int outputBase = renderedSamples;
                for (int i = 0; i < spanSamples; ++i)
                {
                    const float gain = voice.advanceFade(fadeRate);
                    if (gain <= 0.0f)
                    {
                        renderedSamples += i;
                        voice.playbackPos = pos;
                        return;
                    }

                    const int outputIndex = outputBase + i;

                    // Granular pitch-shift fallback
                    if (voice.grainSamplesRemaining <= 0)
                    {
                        voice.grainReadPosA = pos;
                        voice.grainReadPosB = pos + Voice::kGrainSpacingSamples;
                        voice.grainSamplesRemaining = Voice::kGrainLengthSamples;
                    }

                    const float blend = smoothCrossfadeBlend(1.0f - (static_cast<float>(voice.grainSamplesRemaining) / static_cast<float>(Voice::kGrainLengthSamples)));
                    const float gainA = 1.0f - blend;
                    const float gainB = blend;
                    if (numSrcChannels <= 1)
                    {
                        const float sampleA = readInterpolatedSample(0, voice.grainReadPosA);
                        const float sampleB = readInterpolatedSample(0, voice.grainReadPosB);
                        const float mixedSample = ((sampleA * gainA) + (sampleB * gainB)) * gain;

                        for (int ch = 0; ch < directMixChannels; ++ch)
                            outputWritePtrs[static_cast<size_t>(ch)][outputIndex] += mixedSample;

                        for (int ch = directMixChannels; ch < numOutChannels; ++ch)
                            outputBuffer.addSample(ch, startSample + outputIndex, mixedSample);
                    }
                    else if (numSrcChannels == 2)
                    {
                        const float sampleALeft = readInterpolatedSample(0, voice.grainReadPosA);
                        const float sampleBLeft = readInterpolatedSample(0, voice.grainReadPosB);
                        const float mixedLeft = ((sampleALeft * gainA) + (sampleBLeft * gainB)) * gain;

                        const float sampleARight = readInterpolatedSample(1, voice.grainReadPosA);
                        const float sampleBRight = readInterpolatedSample(1, voice.grainReadPosB);
                        const float mixedRight = ((sampleARight * gainA) + (sampleBRight * gainB)) * gain;

                        if (directMixChannels > 0)
                            outputWritePtrs[0][outputIndex] += mixedLeft;

                        if (directMixChannels > 1)
                            outputWritePtrs[1][outputIndex] += mixedRight;

                        for (int ch = 2; ch < directMixChannels; ++ch)
                            outputWritePtrs[static_cast<size_t>(ch)][outputIndex] += mixedLeft;

                        for (int ch = directMixChannels; ch < numOutChannels; ++ch)
                        {
                            const float sample = (ch == 1) ? mixedRight : mixedLeft;
                            outputBuffer.addSample(ch, startSample + outputIndex, sample);
                        }
                    }
                    else
                    {
                        const int cachedSrcChannels = juce::jmin(numSrcChannels, directMixChannels);
                        std::array<float, kMaxDirectMixChannels> cachedMixedSamples{};

                        for (int srcCh = 0; srcCh < cachedSrcChannels; ++srcCh)
                        {
                            const float sampleA = readInterpolatedSample(srcCh, voice.grainReadPosA);
                            const float sampleB = readInterpolatedSample(srcCh, voice.grainReadPosB);
                            cachedMixedSamples[static_cast<size_t>(srcCh)] = ((sampleA * gainA) + (sampleB * gainB)) * gain;
                        }

                        const auto getMixedGranularSample = [&](int outputChannel)
                        {
                            const int srcCh = (outputChannel < numSrcChannels) ? outputChannel : 0;
                            if (srcCh < cachedSrcChannels)
                                return cachedMixedSamples[static_cast<size_t>(srcCh)];

                            const float sampleA = readInterpolatedSample(srcCh, voice.grainReadPosA);
                            const float sampleB = readInterpolatedSample(srcCh, voice.grainReadPosB);
                            return ((sampleA * gainA) + (sampleB * gainB)) * gain;
                        };

                        for (int ch = 0; ch < directMixChannels; ++ch)
                            outputWritePtrs[static_cast<size_t>(ch)][outputIndex] += getMixedGranularSample(ch);

                        for (int ch = directMixChannels; ch < numOutChannels; ++ch)
                            outputBuffer.addSample(ch, startSample + outputIndex, getMixedGranularSample(ch));
                    }

                    voice.grainReadPosA += rate;
                    voice.grainReadPosB += rate;
                    --voice.grainSamplesRemaining;

                    if (voice.grainSamplesRemaining <= 0)
                    {
                        voice.grainReadPosA = voice.grainReadPosB;
                        voice.grainReadPosB = voice.grainReadPosA + Voice::kGrainSpacingSamples;
                    }

                    pos += sourceAdvancePerOutputSample;
                }

                renderedSamples += spanSamples;
            }
            else
            {
                const bool canUseSteadyStateFastPath = (voice.fadeState == Voice::FadeState::Active);
                if (canUseSteadyStateFastPath)
                {
                    const int outputBase = renderedSamples;
                    uint64_t phase = readPositionToPhase(pos);
                    const uint64_t phaseIncrement = juce::jmax<uint64_t>(1, readRateToPhaseIncrement(rate));

                    if (numSrcChannels <= 1)
                    {
                        for (int i = 0; i < spanSamples; ++i)
                        {
                            const int outputIndex = outputBase + i;
                            const int idx0 = static_cast<int>(phase >> kPhaseFractionBits);
                            const float frac = static_cast<float>(phase & kPhaseFractionMask) * kPhaseScaleInv;
                            const float sample = interpolatedSampleReader(srcReadPtr0, srcLength, idx0, frac);

                            for (int ch = 0; ch < directMixChannels; ++ch)
                                outputWritePtrs[static_cast<size_t>(ch)][outputIndex] += sample;

                            for (int ch = directMixChannels; ch < numOutChannels; ++ch)
                                outputBuffer.addSample(ch, startSample + outputIndex, sample);

                            phase += phaseIncrement;
                        }
                    }
                    else
                    {
                        for (int i = 0; i < spanSamples; ++i)
                        {
                            const int outputIndex = outputBase + i;
                            const int idx0 = static_cast<int>(phase >> kPhaseFractionBits);
                            const float frac = static_cast<float>(phase & kPhaseFractionMask) * kPhaseScaleInv;
                            const float sampleLeft = interpolatedSampleReader(srcReadPtr0, srcLength, idx0, frac);
                            const float sampleRight = interpolatedSampleReader(srcReadPtr1, srcLength, idx0, frac);

                            if (directMixChannels > 0)
                                outputWritePtrs[0][outputIndex] += sampleLeft;

                            if (directMixChannels > 1)
                                outputWritePtrs[1][outputIndex] += sampleRight;

                            for (int ch = 2; ch < directMixChannels; ++ch)
                                outputWritePtrs[static_cast<size_t>(ch)][outputIndex] += sampleLeft;

                            for (int ch = directMixChannels; ch < numOutChannels; ++ch)
                            {
                                const float sample = (ch == 1) ? sampleRight : sampleLeft;
                                outputBuffer.addSample(ch, startSample + outputIndex, sample);
                            }

                            phase += phaseIncrement;
                        }
                    }

                    pos = phaseToReadPosition(phase);
                    renderedSamples += spanSamples;
                    continue;
                }

                for (int i = 0; i < spanSamples; ++i)
                {
                    const float gain = voice.advanceFade(fadeRate);
                    if (gain <= 0.0f)
                        break;

                    const int outputIndex = renderedSamples + i;
                    const int idx0 = static_cast<int>(pos);
                    const float frac = static_cast<float>(pos - static_cast<double>(idx0));
                    const int cachedSrcChannels = juce::jmin(numSrcChannels, directMixChannels);
                    std::array<float, kMaxDirectMixChannels> cachedScaledSamples{};

                    for (int srcCh = 0; srcCh < cachedSrcChannels; ++srcCh)
                    {
                        const float *srcRead = (srcCh == 0) ? srcReadPtr0
                                                            : ((srcCh == 1) ? srcReadPtr1 : srcBuffer.getReadPointer(srcCh));
                        cachedScaledSamples[static_cast<size_t>(srcCh)] = interpolatedSampleReader(srcRead, srcLength, idx0, frac) * gain;
                    }

                    const auto getScaledSample = [&](int outputChannel)
                    {
                        const int srcCh = (outputChannel < numSrcChannels) ? outputChannel : 0;
                        if (srcCh < cachedSrcChannels)
                            return cachedScaledSamples[static_cast<size_t>(srcCh)];

                        const float *srcRead = (srcCh == 0) ? srcReadPtr0
                                                            : ((srcCh == 1) ? srcReadPtr1 : srcBuffer.getReadPointer(srcCh));
                        return interpolatedSampleReader(srcRead, srcLength, idx0, frac) * gain;
                    };

                    for (int ch = 0; ch < directMixChannels; ++ch)
                        outputWritePtrs[static_cast<size_t>(ch)][outputIndex] += getScaledSample(ch);

                    for (int ch = directMixChannels; ch < numOutChannels; ++ch)
                        outputBuffer.addSample(ch, startSample + outputIndex, getScaledSample(ch));

                    pos += rate;
                }

                renderedSamples += spanSamples;
            }

            if (hasLoopRegion && pos >= loopEndExclusive)
            {
                pos = static_cast<double>(loopStart) + std::fmod(pos - static_cast<double>(loopStart), loopLength);
            }
            else if (isLoopOn && srcLength > 0 && pos >= static_cast<double>(srcLength))
            {
                pos = std::fmod(pos, static_cast<double>(srcLength));
            }
            else if (!isLoopOn && pos >= static_cast<double>(srcLength))
            {
                voice.forceOff();
                break;
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

        playbackFinished.store(false, std::memory_order_relaxed);
        primaryVoiceActive.store(false, std::memory_order_relaxed);

        VoiceCommand cmd;
        cmd.type = VoiceCommand::Type::SwapSampleBuffer;
        cmd.sampleBufferSampleRate = fileSampleRate;

        if (buffer != nullptr)
        {
            int slot = findAvailableSampleBufferSlot();
            if (slot < 0)
            {
                reclaimRetiredBuffers();
                slot = findAvailableSampleBufferSlot();
            }

            if (slot < 0)
            {
                jassertfalse;
                return;
            }

            sampleBufferSlots[static_cast<size_t>(slot)] = std::move(buffer);
            cmd.sampleSlot = slot;
            cmd.sampleBufferLength = sampleBufferSlots[static_cast<size_t>(slot)]->getNumSamples();
        }

        enqueueCommand(cmd);
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
        stretchHighQualityEnabled.store(enabled, std::memory_order_relaxed);
    }

    bool VoiceManager::isStretchHighQualityEnabled() const noexcept
    {
        return stretchHighQualityEnabled.load(std::memory_order_relaxed);
    }

    bool VoiceManager::isStretchHighQualityAvailable() const noexcept
    {
        return true;
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
