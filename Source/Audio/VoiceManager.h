#pragma once

#include <JuceHeader.h>
#include "Voice.h"
#include <atomic>
#include <array>
#include <memory>
#include <vector>

namespace sw
{

    /// Manages a pool of playback voices for polyphonic sample preview.
    ///
    /// RT-SAFE: getNextAudioBlock is called on the audio thread and must not
    /// allocate, lock, log, or perform I/O.
    ///
    /// THREADING MODEL: All voice state mutations happen exclusively on the
    /// audio thread. The message/MIDI thread enqueues commands via a lock-free
    /// FIFO, which the audio callback drains at the start of each block. This
    /// eliminates data races on Voice fields and enables sample-accurate MIDI.
    class VoiceManager final : public juce::AudioSource
    {
    public:
        static constexpr int kMaxVoices = 8;

        /// Lock-free SPSC (single-producer single-consumer) command FIFO.
        /// Multiple producers are serialized via a spinlock since both the
        /// MIDI input thread and the message thread may enqueue commands.
        static constexpr int kCommandFifoSize = 256;

        VoiceManager();
        ~VoiceManager() override;

        // --- AudioSource ----------------------------------------------------------
        void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;

        // --- Control (message/MIDI thread — enqueue to FIFO) ----------------------

        /// Load a decoded sample buffer. Thread-safe swap via atomic pointer.
        void loadBuffer(std::unique_ptr<juce::AudioBuffer<float>> buffer, double fileSampleRate);
        void loadBuffer(std::shared_ptr<juce::AudioBuffer<float>> buffer, double fileSampleRate);

        /// Start playback of the primary voice (non-MIDI, UI play button).
        void play();

        /// Stop all voices.
        void stop();

        // --- Polyphonic MIDI voice control (message/MIDI thread) ------------------

        /// Enqueue a note-on command.
        void noteOn(int midiNote, double playbackRate, double pitchRatio);

        /// Enqueue a note-off command.
        void noteOff(int midiNote);

        /// Enqueue an all-notes-off command.
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

        /// Enqueue a pitch update for all active voices.
        void updateAllVoicePitch(double baseSemitones);

        /// True while any voice is actively playing (approximate, read from any thread).
        bool isPlaying() const noexcept;
        bool isPrimaryPlaying() const noexcept;
        bool consumePlaybackFinishedFlag() noexcept;

        /// Normalized playback position of primary voice in [0..1].
        double getPlaybackProgressNormalized() const noexcept;

        /// Enqueue a playback-position scrub.
        void setPlaybackProgressNormalized(double normalizedProgress);
        void getActiveMidiPlaybackHeadsNormalized(std::vector<float> &headsOut) const;

    private:
        /// Render one voice for the given block, adding its output into the buffer.
        void renderVoice(Voice &voice,
                         const juce::AudioBuffer<float> &srcBuffer,
                         juce::AudioBuffer<float> &outputBuffer,
                         int startSample,
                         int numSamples);

        /// Find an idle voice slot, or steal the oldest if all are occupied.
        /// Called ONLY from the audio thread.
        Voice &allocateVoice();

        /// Drain pending commands from the FIFO. Called at the start of
        /// each audio callback — all Voice mutations happen here.
        void drainCommandFifo();

        /// Enqueue a command into the lock-free FIFO (thread-safe for
        /// concurrent producers via spinlock).
        void enqueueCommand(const VoiceCommand &cmd);

        /// Reclaim retired shared_ptrs from the audio thread's retire ring.
        /// Called from the message thread (e.g. in loadBuffer).
        void reclaimRetiredBuffers();

        // Voice pool — all preallocated, zero runtime allocation.
        std::array<Voice, kMaxVoices> voices;
        uint64_t voiceAgeCounter = 0;

        // Index of the "primary" voice for UI progress display.
        std::atomic<int> primaryVoiceIndex{0};

        // Sample data — shared ownership. Audio thread loads; old pointers
        // are placed in a retire ring to avoid RT deallocation.
        std::atomic<std::shared_ptr<juce::AudioBuffer<float>>> sampleBuffer;
        std::atomic<double> bufferSampleRate{44100.0};

        // Retire ring: audio thread stores old shared_ptrs here;
        // message thread reclaims them.
        static constexpr int kRetireRingSize = 4;
        std::array<std::shared_ptr<juce::AudioBuffer<float>>, kRetireRingSize> retireRing;
        std::atomic<int> retireWritePos{0}; // written by audio thread
        std::atomic<int> retireReadPos{0};  // written by message thread

        // Lock-free command FIFO (MPSC via spinlock for producers).
        std::array<VoiceCommand, kCommandFifoSize> commandFifo;
        std::atomic<int> fifoWritePos{0};
        std::atomic<int> fifoReadPos{0};
        std::atomic_flag fifoProducerLock = ATOMIC_FLAG_INIT;

        // Global settings
        std::atomic<bool> anyVoiceActive{false}; // approximate, for UI queries
        std::atomic<bool> primaryVoiceActive{false};
        std::atomic<bool> playbackFinished{false};
        std::atomic<bool> loopEnabled{false};
        std::atomic<int64_t> loopStartSample{-1};
        std::atomic<int64_t> loopEndSample{-1};
        std::atomic<int> previewRootMidiNote{60};
        std::atomic<bool> preserveLengthEnabled{false};
        std::atomic<bool> stretchHighQualityEnabled{false};
        std::atomic<int> loadedSampleLength{0};

        // Cached pitch for updateAllVoicePitch commands
        std::atomic<double> lastBasePitchSemitones{0.0};

        double currentSampleRate = 44100.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceManager)
    };

} // namespace sw
