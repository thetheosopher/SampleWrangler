#pragma once

#include <JuceHeader.h>
#include "VoiceManager.h"
#include <atomic>
#include <array>
#include <mutex>
#include <vector>

namespace sw
{

    /// RT-safe audio engine that owns the JUCE AudioDeviceManager and
    /// processes audio via the VoiceManager.
    ///
    /// THREADING CONTRACT:
    ///   - prepareToPlay / releaseResources / getNextAudioBlock run on the audio thread.
    ///   - All other public methods are called from the message thread.
    ///   - Communication from UI → audio uses a lock-free command queue.
    ///
    /// RT-SAFE RULES (audio callback):
    ///   - NO heap allocations
    ///   - NO blocking locks / mutexes
    ///   - NO file I/O
    ///   - NO logging
    class AudioEngine final : public juce::AudioSource
    {
    public:
        AudioEngine();
        ~AudioEngine() override;

        // --- AudioSource ----------------------------------------------------------
        void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
        void releaseResources() override;
        void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;

        // --- High-level API (message thread) --------------------------------------

        /// Initialise the device manager; call once at startup.
        void initialiseDeviceManager();

        /// Access the device manager (e.g. to show ASIO settings dialog).
        juce::AudioDeviceManager &getDeviceManager() noexcept { return deviceManager; }

        /// Load a sample buffer for preview. Ownership is transferred.
        /// The actual loading / decoding should be done on a worker thread;
        /// this method receives the ready-to-play buffer.
        void loadPreviewBuffer(std::unique_ptr<juce::AudioBuffer<float>> buffer, double fileSampleRate);
        void loadPreviewBuffer(std::shared_ptr<juce::AudioBuffer<float>> buffer, double fileSampleRate);

        /// Start / stop preview playback.
        void play();
        void stop();

        /// Set pitch shift in semitones (resample-style, changes duration).
        void setPitchSemitones(double semitones);
        void setPreserveLengthEnabled(bool enabled);
        bool isPreserveLengthEnabled() const noexcept;
        void setStretchHighQualityEnabled(bool enabled);
        bool isStretchHighQualityEnabled() const noexcept;
        bool isStretchHighQualityAvailable() const noexcept;

        /// Enable/disable loop playback for preview.
        void setLoopEnabled(bool enabled);
        bool isLoopEnabled() const noexcept;

        /// Handle MIDI input (physical device or on-screen keyboard).
        void handleMidiMessage(const juce::MidiMessage &message);

        juce::StringArray getAvailableOutputDeviceTypes();
        juce::StringArray getAvailableOutputDevicesForType(const juce::String &typeName);
        juce::String getCurrentOutputDeviceType() const;
        bool setCurrentOutputDeviceType(const juce::String &typeName, juce::String *errorMessage = nullptr);

        juce::StringArray getAvailableOutputDevices() const;
        juce::String getCurrentOutputDeviceName() const;
        bool setCurrentOutputDevice(const juce::String &deviceName, juce::String *errorMessage = nullptr);

        bool isPreviewPlaying() const noexcept;
        bool consumePreviewFinishedFlag() noexcept;
        double getPreviewPlaybackProgressNormalized() const noexcept;
        void setPreviewPlaybackProgressNormalized(double normalizedProgress);
        void setPreviewRootMidiNote(int midiNote);
        void clearPreviewLoopRegion();
        void setPreviewLoopRegionSamples(int64_t startSample, int64_t endSample);

        /// Returns the latest realtime oscilloscope frame from live audio output.
        /// Call from the message thread (e.g. 60Hz UI timer).
        void getOscilloscopeFrame(std::vector<float> &destination) const;

    private:
        void applyCurrentPitch();

        juce::AudioDeviceManager deviceManager;
        juce::AudioSourcePlayer sourcePlayer;
        VoiceManager voiceManager;
        std::mutex deviceConfigMutex;

        std::atomic<double> basePitchSemitones{0.0};

        static constexpr int kOscilloscopeRingSize = 8192;
        static constexpr int kOscilloscopeFrameSize = 4096;
        std::array<float, kOscilloscopeRingSize> oscilloscopeRing{};
        std::atomic<uint32_t> oscilloscopeWriteIndex{0};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
    };

} // namespace sw
