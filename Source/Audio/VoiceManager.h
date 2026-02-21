#pragma once

#include <JuceHeader.h>
#include "Voice.h"
#include <atomic>
#include <array>
#include <memory>

namespace sw
{

    /// Manages a pool of playback voices for polyphonic sample preview.
    ///
    /// RT-SAFE: getNextAudioBlock is called on the audio thread and must not
    /// allocate, lock, log, or perform I/O.
    class VoiceManager final : public juce::AudioSource
    {
    public:
        static constexpr int kMaxVoices = 8;

        VoiceManager();
        ~VoiceManager() override;

        // --- AudioSource ----------------------------------------------------------
        void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;

        // --- Control (message thread) ---------------------------------------------

        /// Load a decoded sample buffer. Thread-safe swap via atomic pointer.
        void loadBuffer(std::unique_ptr<juce::AudioBuffer<float>> buffer, double fileSampleRate);

        /// Start playback of the primary voice (non-MIDI, UI play button).
        void play();

        /// Stop all voices.
        void stop();

        // --- Polyphonic MIDI voice control (message thread) -----------------------

        /// Trigger a new voice for the given MIDI note with the computed
        /// playback rate and pitch ratio.
        void noteOn(int midiNote, double playbackRate, double pitchRatio);

        /// Release the voice playing the given MIDI note.
        void noteOff(int midiNote);

        /// Release all active voices.
        void allNotesOff();

        // --- Settings (message thread) -------------------------------------------

        void setPreserveLengthEnabled(bool enabled);
        bool isPreserveLengthEnabled() const noexcept;
        void setStretchHighQualityEnabled(bool enabled);
        bool isStretchHighQualityEnabled() const noexcept;
        bool isStretchHighQualityAvailable() const noexcept;

        void setLoopEnabled(bool enabled);
        bool isLoopEnabled() const noexcept;
        void setLoopRegionSamples(int64_t startSample, int64_t endSample);
        void setPreviewRootMidiNote(int midiNote);
        int getPreviewRootMidiNote() const noexcept;

        /// Update pitch on all active voices (e.g. when base pitch knob changes).
        void updateAllVoicePitch(double baseSemitones);

        /// True while any voice is actively playing.
        bool isPlaying() const noexcept;
        bool consumePlaybackFinishedFlag() noexcept;

        /// Normalized playback position of primary voice in [0..1].
        double getPlaybackProgressNormalized() const noexcept;
        void setPlaybackProgressNormalized(double normalizedProgress);

    private:
        /// Render one voice for the given block, adding its output into the buffer.
        void renderVoice(Voice &voice,
                         const juce::AudioBuffer<float> &srcBuffer,
                         juce::AudioBuffer<float> &outputBuffer,
                         int startSample,
                         int numSamples);

        /// Find an idle voice slot, or steal the oldest if all are occupied.
        Voice &allocateVoice();

        // Voice pool — all preallocated, zero runtime allocation.
        std::array<Voice, kMaxVoices> voices;
        uint64_t voiceAgeCounter = 0;

        // Index of the "primary" voice for UI progress display.
        std::atomic<int> primaryVoiceIndex{0};

        // Sample data — shared ownership across all voices.
        std::atomic<std::shared_ptr<juce::AudioBuffer<float>>> sampleBuffer;
        double bufferSampleRate = 44100.0;

        // Global settings
        std::atomic<bool> playbackFinished{false};
        std::atomic<bool> loopEnabled{false};
        std::atomic<int64_t> loopStartSample{-1};
        std::atomic<int64_t> loopEndSample{-1};
        std::atomic<int> previewRootMidiNote{60};
        std::atomic<bool> preserveLengthEnabled{false};
        std::atomic<bool> stretchHighQualityEnabled{false};
        std::atomic<int> loadedSampleLength{0};

        double currentSampleRate = 44100.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceManager)
    };

} // namespace sw
