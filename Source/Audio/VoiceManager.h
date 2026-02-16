#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

namespace sw
{

    /// Manages playback voices for sample preview.
    /// Designed for basic polyphony (MVP: 1 voice with resample pitch).
    ///
    /// RT-SAFE: getNextAudioBlock is called on the audio thread and must not
    /// allocate, lock, log, or perform I/O.
    class VoiceManager final : public juce::AudioSource
    {
    public:
        VoiceManager();
        ~VoiceManager() override;

        // --- AudioSource ----------------------------------------------------------
        void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;

        // --- Control (message thread) ---------------------------------------------

        /// Load a decoded sample buffer. Thread-safe swap via atomic pointer.
        void loadBuffer(std::unique_ptr<juce::AudioBuffer<float>> buffer, double fileSampleRate);

        void play();
        void stop();

        /// Set resample-style pitch shift in semitones.
        void setPitchSemitones(double semitones);

        void setLoopEnabled(bool enabled);
        bool isLoopEnabled() const noexcept;
        void setLoopRegionSamples(int64_t startSample, int64_t endSample);
        void setPreviewRootMidiNote(int midiNote);
        int getPreviewRootMidiNote() const noexcept;

        /// True while the current buffer is actively playing.
        bool isPlaying() const noexcept;
        bool consumePlaybackFinishedFlag() noexcept;

        /// Normalized playback position in [0..1] for the loaded sample.
        double getPlaybackProgressNormalized() const noexcept;
        void setPlaybackProgressNormalized(double normalizedProgress);

    private:
        // Sample data — swapped atomically from message thread, read on audio thread.
        // Using a simple pointer swap; real implementation should use a lock-free
        // exchange or JUCE's AbstractFifo for safe hand-off.
        std::atomic<juce::AudioBuffer<float> *> sampleBuffer{nullptr};
        std::unique_ptr<juce::AudioBuffer<float>> ownedBuffer; // message-thread ownership
        double bufferSampleRate = 44100.0;

        std::atomic<bool> playing{false};
        std::atomic<bool> playbackFinished{false};
        std::atomic<bool> loopEnabled{false};
        std::atomic<int64_t> loopStartSample{-1};
        std::atomic<int64_t> loopEndSample{-1};
        std::atomic<int> previewRootMidiNote{60};
        std::atomic<double> playbackPos{0.0};
        std::atomic<double> playbackRate{1.0}; // ratio: 1.0 = original pitch
        std::atomic<int> loadedSampleLength{0};

        double currentSampleRate = 44100.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceManager)
    };

} // namespace sw
