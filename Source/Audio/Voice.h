#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

#if SW_HAVE_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

namespace sw
{

    /// Per-voice playback state for polyphonic sample preview.
    ///
    /// All render methods are RT-SAFE (audio thread only).
    /// Trigger/release methods are called from the message thread
    /// and communicate via atomics.
    struct Voice
    {
        // --- Constants -----------------------------------------------------------
        static constexpr int kMaxChannels = 2;
        static constexpr float kFadeTimeMs = 5.0f;
        static constexpr int kGrainLengthSamples = 256;
        static constexpr double kGrainSpacingSamples = 128.0;

#if SW_HAVE_RUBBERBAND
        static constexpr int kRubberBandMaxBlockSize = 4096;
        static constexpr int kRubberBandOutputFifoSize = 32768;
#endif

        // --- Fade envelope -------------------------------------------------------
        enum class FadeState
        {
            Idle,
            FadingIn,
            Active,
            FadingOut
        };

        // --- Voice state (atomics for message thread <-> audio thread) -----------
        std::atomic<bool> active{false};
        std::atomic<FadeState> fadeState{FadeState::Idle};
        std::atomic<int> midiNote{-1};
        std::atomic<double> playbackRate{1.0};
        std::atomic<double> pitchRatio{1.0};
        std::atomic<bool> granularResetRequested{true};

        // --- Audio-thread-only state --------------------------------------------
        double playbackPos = 0.0;
        float fadeGain = 0.0f;

        // Granular pitch-shift state
        double grainReadPosA = 0.0;
        double grainReadPosB = 0.0;
        int grainSamplesRemaining = 0;

        // Age counter — incremented each time a note-on triggers this voice,
        // used for voice-stealing (steal the oldest).
        uint64_t triggerAge = 0;

#if SW_HAVE_RUBBERBAND
        std::unique_ptr<RubberBand::RubberBandStretcher> rubberBandStretcher;
        int rubberBandProcessBlockSize = 0;
        int rubberBandInputFill = 0;
        int rubberBandStartDelayRemaining = 0;
        int rubberBandPreferredStartPadRemaining = 0;
        bool rubberBandInitialized = false;
        double rubberBandLastPitchScale = 1.0;
        std::array<std::array<float, kRubberBandMaxBlockSize>, kMaxChannels> rubberBandInput{};
        std::array<std::array<float, kRubberBandMaxBlockSize>, kMaxChannels> rubberBandProcessOutput{};
        std::array<std::array<float, kRubberBandOutputFifoSize>, kMaxChannels> rubberBandOutputFifo{};
        int rubberBandOutputFifoRead = 0;
        int rubberBandOutputFifoWrite = 0;
        int rubberBandOutputFifoCount = 0;
        std::array<const float *, kMaxChannels> rubberBandInputPtrs{};
        std::array<float *, kMaxChannels> rubberBandProcessOutputPtrs{};
#endif

        // --- Trigger / Release (message thread) ---------------------------------

        void noteOn(int note, double rate, double pitch, uint64_t age)
        {
            playbackPos = 0.0;
            fadeGain = 0.0f;
            grainSamplesRemaining = 0;
            granularResetRequested.store(true, std::memory_order_release);
            midiNote.store(note, std::memory_order_relaxed);
            playbackRate.store(rate, std::memory_order_relaxed);
            pitchRatio.store(pitch, std::memory_order_relaxed);
            triggerAge = age;
            active.store(true, std::memory_order_relaxed);
            fadeState.store(FadeState::FadingIn, std::memory_order_release);
        }

        void noteOff()
        {
            FadeState fs = fadeState.load(std::memory_order_relaxed);
            if (fs == FadeState::FadingIn || fs == FadeState::Active)
                fadeState.store(FadeState::FadingOut, std::memory_order_relaxed);
            else
                forceOff();
        }

        void forceOff()
        {
            active.store(false, std::memory_order_relaxed);
            fadeState.store(FadeState::Idle, std::memory_order_relaxed);
            midiNote.store(-1, std::memory_order_relaxed);
        }

        void updatePitch(double rate, double pitch)
        {
            playbackRate.store(rate, std::memory_order_relaxed);
            pitchRatio.store(pitch, std::memory_order_relaxed);
        }

        // --- Render helpers (audio thread only) ----------------------------------

        /// Advance fade envelope by one sample. Returns current gain.
        float advanceFade(float fadeRate)
        {
            FadeState fs = fadeState.load(std::memory_order_relaxed);
            if (fs == FadeState::FadingIn)
            {
                fadeGain = juce::jmin(1.0f, fadeGain + fadeRate);
                if (fadeGain >= 1.0f)
                    fadeState.store(FadeState::Active, std::memory_order_relaxed);
            }
            else if (fs == FadeState::FadingOut)
            {
                fadeGain = juce::jmax(0.0f, fadeGain - fadeRate);
                if (fadeGain <= 0.0f)
                {
                    forceOff();
                    return 0.0f;
                }
            }
            return fadeGain;
        }

#if SW_HAVE_RUBBERBAND
        void initialiseRubberBand(double sampleRate, double initialPitchRatio)
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
                static_cast<size_t>(juce::jlimit(8000, 192000, static_cast<int>(std::round(sampleRate)))),
                static_cast<size_t>(kMaxChannels),
                options,
                1.0,
                initialPitchRatio);

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

            rubberBandInitialized = true;
            resetRubberBand();
        }

        void resetRubberBand()
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

            for (int ch = 0; ch < kMaxChannels; ++ch)
            {
                std::fill(rubberBandInput[static_cast<size_t>(ch)].begin(), rubberBandInput[static_cast<size_t>(ch)].end(), 0.0f);
                std::fill(rubberBandProcessOutput[static_cast<size_t>(ch)].begin(), rubberBandProcessOutput[static_cast<size_t>(ch)].end(), 0.0f);
                std::fill(rubberBandOutputFifo[static_cast<size_t>(ch)].begin(), rubberBandOutputFifo[static_cast<size_t>(ch)].end(), 0.0f);
            }
        }

        void processRubberBandIfReady()
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

                for (int ch = 0; ch < kMaxChannels; ++ch)
                {
                    rubberBandInputPtrs[static_cast<size_t>(ch)] = rubberBandInput[static_cast<size_t>(ch)].data();
                    rubberBandProcessOutputPtrs[static_cast<size_t>(ch)] = rubberBandProcessOutput[static_cast<size_t>(ch)].data();
                }

                rubberBandStretcher->process(rubberBandInputPtrs.data(), static_cast<size_t>(required), false);

                const int remaining = rubberBandInputFill - required;
                if (remaining > 0)
                {
                    for (int ch = 0; ch < kMaxChannels; ++ch)
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

                        for (int ch = 0; ch < kMaxChannels; ++ch)
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
        }
#endif
    };

} // namespace sw
