#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <memory>

namespace sw
{

    // --- Lock-free MIDI command FIFO for thread-safe voice control. -----------
    // Message/MIDI thread enqueues; audio thread dequeues and applies.
    struct VoiceCommand
    {
        enum class Type : uint8_t
        {
            NoteOn,
            NoteOff,
            AllNotesOff,
            UpdatePitch,
            Play, // UI play button (non-MIDI, voice 0)
            Stop,
            SetPlaybackProgress,
            SwapSampleBuffer
        };

        Type type = Type::NoteOn;
        int midiNote = -1;
        double playbackRate = 1.0;
        double pitchRatio = 1.0;
        double normalizedProgress = 0.0; // for SetPlaybackProgress
        int sampleSlot = -1;
        int sampleBufferLength = 0;
        double sampleBufferSampleRate = 44100.0;
    };

    /// Per-voice playback state for polyphonic sample preview.
    ///
    /// All state is now exclusively owned by the audio thread.
    /// Trigger/release happens via the VoiceCommand FIFO.
    struct Voice
    {
        // --- Constants -----------------------------------------------------------
        static constexpr int kMaxChannels = 2;
        static constexpr float kFadeTimeMs = 5.0f;
        static constexpr int kGrainLengthSamples = 256;
        static constexpr double kGrainSpacingSamples = 128.0;

        struct RubberBandState;

        // --- Fade envelope -------------------------------------------------------
        enum class FadeState
        {
            Idle,
            FadingIn,
            Active,
            FadingOut
        };

        Voice();
        ~Voice();
        Voice(const Voice &) = delete;
        Voice &operator=(const Voice &) = delete;
        Voice(Voice &&) = delete;
        Voice &operator=(Voice &&) = delete;

        // --- Audio-thread-only state (ALL fields written only by audio thread) ---
        bool active = false;
        FadeState fadeState = FadeState::Idle;
        int midiNote = -1;
        double playbackRate = 1.0;
        double pitchRatio = 1.0;
        bool granularResetRequested = true;
        bool preserveLengthModeLatched = false;
        bool preserveLengthHighQualityLatched = false;
        bool lastPreserveLengthUsedRubberBandRt = false;
        bool rubberBandRtFailedForCurrentNote = false;

        double playbackPos = 0.0;
        float fadeGain = 0.0f;

        // Granular pitch-shift state
        double grainReadPosA = 0.0;
        double grainReadPosB = 0.0;
        int grainSamplesRemaining = 0;

        // Age counter — incremented each time a note-on triggers this voice,
        // used for voice-stealing (steal the oldest).
        uint64_t triggerAge = 0;
        std::unique_ptr<RubberBandState> rubberBandState;

        // --- Trigger / Release (audio thread ONLY) --------------------------------

        void noteOn(int note, double rate, double pitch, uint64_t age)
        {
            playbackPos = 0.0;
            fadeGain = 0.0f;
            grainSamplesRemaining = 0;
            granularResetRequested = true;
            preserveLengthModeLatched = false;
            preserveLengthHighQualityLatched = false;
            lastPreserveLengthUsedRubberBandRt = false;
            rubberBandRtFailedForCurrentNote = false;
            midiNote = note;
            playbackRate = rate;
            pitchRatio = pitch;
            triggerAge = age;
            active = true;
            fadeState = FadeState::FadingIn;
        }

        void noteOff()
        {
            if (fadeState == FadeState::FadingIn || fadeState == FadeState::Active)
                fadeState = FadeState::FadingOut;
            else
                forceOff();
        }

        void forceOff()
        {
            active = false;
            fadeState = FadeState::Idle;
            midiNote = -1;
        }

        void updatePitch(double rate, double pitch)
        {
            playbackRate = rate;
            pitchRatio = pitch;
        }

        // --- Render helpers (audio thread only) ----------------------------------

        /// Advance fade envelope by one sample. Returns current gain.
        float advanceFade(float fadeRate)
        {
            if (fadeState == FadeState::FadingIn)
            {
                fadeGain = juce::jmin(1.0f, fadeGain + fadeRate);
                if (fadeGain >= 1.0f)
                    fadeState = FadeState::Active;
            }
            else if (fadeState == FadeState::FadingOut)
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

        void initialiseRubberBand(double sampleRate, double initialPitchRatio);
        void resetRubberBand();
        bool isRubberBandReady() const noexcept;
        int getRubberBandInputDeficit() const noexcept;
        bool shouldProvideRubberBandStartPadSample() const noexcept;
        void consumeRubberBandStartPadSample() noexcept;
        void pushRubberBandInput(float leftSample, float rightSample) noexcept;
        void setRubberBandPitchScale(double pitchScale);
        void processRubberBandIfReady();
        void skipRubberBandStartDelay() noexcept;
        bool hasRubberBandOutput() const noexcept;
        void popRubberBandOutputOrReuseLast(float &leftSample, float &rightSample) noexcept;
        float consumeRubberBandAttackGain() noexcept;
    };

} // namespace sw
