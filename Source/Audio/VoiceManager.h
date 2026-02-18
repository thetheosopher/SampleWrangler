#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <memory>

#if SW_HAVE_RUBBERBAND
namespace RubberBand
{
    class RubberBandStretcher;
}
#endif

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

        /// True while the current buffer is actively playing.
        bool isPlaying() const noexcept;
        bool consumePlaybackFinishedFlag() noexcept;

        /// Normalized playback position in [0..1] for the loaded sample.
        double getPlaybackProgressNormalized() const noexcept;
        void setPlaybackProgressNormalized(double normalizedProgress);

    private:
        // Sample data — shared ownership avoids use-after-free when swapping buffers
        // while the audio thread is still rendering the previous block.
        std::atomic<std::shared_ptr<juce::AudioBuffer<float>>> sampleBuffer;
        double bufferSampleRate = 44100.0;

        std::atomic<bool> playing{false};
        std::atomic<bool> playbackFinished{false};
        std::atomic<bool> loopEnabled{false};
        std::atomic<int64_t> loopStartSample{-1};
        std::atomic<int64_t> loopEndSample{-1};
        std::atomic<int> previewRootMidiNote{60};
        std::atomic<double> playbackPos{0.0};
        std::atomic<double> playbackRate{1.0}; // ratio: 1.0 = original pitch
        std::atomic<double> pitchRatio{1.0};   // ratio from semitone setting
        std::atomic<bool> preserveLengthEnabled{false};
        std::atomic<bool> granularResetRequested{true};
        std::atomic<bool> stretchHighQualityEnabled{false};
        std::atomic<int> loadedSampleLength{0};

        // Granular pitch-shift state (audio thread only)
        double grainReadPosA = 0.0;
        double grainReadPosB = 0.0;
        int grainSamplesRemaining = 0;

#if SW_HAVE_RUBBERBAND
        static constexpr int kRubberBandMaxChannels = 2;
        static constexpr int kRubberBandMaxBlockSize = 4096;
        static constexpr int kRubberBandOutputFifoSize = 32768;

        std::unique_ptr<RubberBand::RubberBandStretcher> rubberBandStretcher;
        int rubberBandProcessBlockSize = 0;
        int rubberBandInputFill = 0;
        int rubberBandStartDelayRemaining = 0;
        int rubberBandPreferredStartPadRemaining = 0;
        bool rubberBandInitialized = false;
        double rubberBandLastPitchScale = 1.0;
        std::array<std::array<float, kRubberBandMaxBlockSize>, kRubberBandMaxChannels> rubberBandInput{};
        std::array<std::array<float, kRubberBandMaxBlockSize>, kRubberBandMaxChannels> rubberBandProcessOutput{};
        std::array<std::array<float, kRubberBandOutputFifoSize>, kRubberBandMaxChannels> rubberBandOutputFifo{};
        int rubberBandOutputFifoRead = 0;
        int rubberBandOutputFifoWrite = 0;
        int rubberBandOutputFifoCount = 0;
        std::array<const float *, kRubberBandMaxChannels> rubberBandInputPtrs{};
        std::array<float *, kRubberBandMaxChannels> rubberBandProcessOutputPtrs{};

        void initialiseRubberBandIfNeeded();
        void resetRubberBandState();
#endif

        double currentSampleRate = 44100.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceManager)
    };

} // namespace sw
