#include "AudioEngine.h"
#include "Util/Logging.h"
#include <cmath>

namespace sw
{

    AudioEngine::AudioEngine() = default;

    AudioEngine::~AudioEngine()
    {
        sourcePlayer.setSource(nullptr);
        deviceManager.removeAudioCallback(&sourcePlayer);
    }

    // ---------------------------------------------------------------------------
    // AudioSource
    // ---------------------------------------------------------------------------

    void AudioEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
    {
        voiceManager.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void AudioEngine::releaseResources()
    {
        voiceManager.releaseResources();
    }

    void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill)
    {
        // RT-safe: delegate to voice manager which reads from pre-decoded buffers
        voiceManager.getNextAudioBlock(bufferToFill);

        // RT-safe oscilloscope tap: capture composite output waveform.
        if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0)
            return;

        const int numChannels = bufferToFill.buffer->getNumChannels();
        if (numChannels <= 0)
            return;

        uint32_t write = oscilloscopeWriteIndex.load(std::memory_order_relaxed);
        for (int i = 0; i < bufferToFill.numSamples; ++i)
        {
            float composite = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                composite += bufferToFill.buffer->getSample(ch, bufferToFill.startSample + i);

            composite /= static_cast<float>(numChannels);
            oscilloscopeRing[write % kOscilloscopeRingSize] = composite;
            ++write;
        }

        oscilloscopeWriteIndex.store(write, std::memory_order_release);
    }

    // ---------------------------------------------------------------------------
    // High-level API (message thread)
    // ---------------------------------------------------------------------------

    void AudioEngine::initialiseDeviceManager()
    {
        const std::lock_guard<std::mutex> lock(deviceConfigMutex);

        // Attempt to open a default device; prefer ASIO if available, fall back otherwise.
        auto setup = deviceManager.getAudioDeviceSetup();
        (void)setup;

        auto result = deviceManager.initialise(
            0,       // numInputChannelsNeeded
            2,       // numOutputChannelsNeeded
            nullptr, // savedState XML
            true,    // selectDefaultDeviceOnFailure
            {},      // preferredDefaultDeviceName
            nullptr  // preferredSetupOptions
        );

        if (result.isNotEmpty())
        {
            SW_LOG_ERR("Audio device initialisation failed: " << result);
        }

        sourcePlayer.setSource(this);
        deviceManager.addAudioCallback(&sourcePlayer);
    }

    void AudioEngine::loadPreviewBuffer(std::unique_ptr<juce::AudioBuffer<float>> buffer,
                                        double fileSampleRate)
    {
        std::shared_ptr<juce::AudioBuffer<float>> sharedBuffer;
        if (buffer != nullptr)
            sharedBuffer = std::shared_ptr<juce::AudioBuffer<float>>(std::move(buffer));

        loadPreviewBuffer(std::move(sharedBuffer), fileSampleRate);
    }

    void AudioEngine::loadPreviewBuffer(std::shared_ptr<juce::AudioBuffer<float>> buffer,
                                        double fileSampleRate)
    {
        voiceManager.loadBuffer(std::move(buffer), fileSampleRate);
        voiceManager.setPlaybackProgressNormalized(0.0);
    }

    void AudioEngine::play()
    {
        voiceManager.play();
    }

    void AudioEngine::stop()
    {
        voiceManager.stop();
    }

    void AudioEngine::setPitchSemitones(double semitones)
    {
        basePitchSemitones.store(semitones, std::memory_order_relaxed);
        voiceManager.updateAllVoicePitch(semitones);
    }

    void AudioEngine::setPreserveLengthEnabled(bool enabled)
    {
        voiceManager.setPreserveLengthEnabled(enabled);
    }

    bool AudioEngine::isPreserveLengthEnabled() const noexcept
    {
        return voiceManager.isPreserveLengthEnabled();
    }

    void AudioEngine::setStretchHighQualityEnabled(bool enabled)
    {
        voiceManager.setStretchHighQualityEnabled(enabled);
    }

    bool AudioEngine::isStretchHighQualityEnabled() const noexcept
    {
        return voiceManager.isStretchHighQualityEnabled();
    }

    bool AudioEngine::isStretchHighQualityAvailable() const noexcept
    {
        return voiceManager.isStretchHighQualityAvailable();
    }

    void AudioEngine::setLoopEnabled(bool enabled)
    {
        voiceManager.setLoopEnabled(enabled);
    }

    bool AudioEngine::isLoopEnabled() const noexcept
    {
        return voiceManager.isLoopEnabled();
    }

    void AudioEngine::handleMidiMessage(const juce::MidiMessage &message)
    {
        if (message.isNoteOn())
        {
            const int note = message.getNoteNumber();
            const int rootNote = voiceManager.getPreviewRootMidiNote();
            const double base = basePitchSemitones.load(std::memory_order_relaxed);
            const double totalSemitones = base + static_cast<double>(note - rootNote);
            const double ratio = std::pow(2.0, totalSemitones / 12.0);

            voiceManager.noteOn(note, ratio, ratio);
            return;
        }

        if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            voiceManager.allNotesOff();
            return;
        }

        if (message.isNoteOff())
        {
            voiceManager.noteOff(message.getNoteNumber());
        }
    }

    juce::StringArray AudioEngine::getAvailableOutputDeviceTypes()
    {
        juce::StringArray types;
        for (auto *type : deviceManager.getAvailableDeviceTypes())
        {
            if (type != nullptr)
                types.add(type->getTypeName());
        }
        return types;
    }

    juce::StringArray AudioEngine::getAvailableOutputDevicesForType(const juce::String &typeName)
    {
        juce::StringArray devices;
        if (typeName.isEmpty())
            return devices;

        for (auto *type : deviceManager.getAvailableDeviceTypes())
        {
            if (type == nullptr)
                continue;

            if (type->getTypeName() == typeName)
            {
                devices = type->getDeviceNames(false);
                break;
            }
        }

        return devices;
    }

    juce::String AudioEngine::getCurrentOutputDeviceType() const
    {
        return deviceManager.getCurrentAudioDeviceType();
    }

    bool AudioEngine::setCurrentOutputDeviceType(const juce::String &typeName, juce::String *errorMessage)
    {
        const std::lock_guard<std::mutex> lock(deviceConfigMutex);

        if (typeName.isEmpty())
        {
            if (errorMessage != nullptr)
                *errorMessage = "Device type cannot be empty.";
            return false;
        }

        if (deviceManager.getCurrentAudioDeviceType() == typeName)
        {
            if (errorMessage != nullptr)
                *errorMessage = {};
            return true;
        }

        stop();
        sourcePlayer.setSource(nullptr);
        deviceManager.removeAudioCallback(&sourcePlayer);

        deviceManager.setCurrentAudioDeviceType(typeName, true);

        sourcePlayer.setSource(this);
        deviceManager.addAudioCallback(&sourcePlayer);

        const bool success = (deviceManager.getCurrentAudioDeviceType() == typeName);
        if (errorMessage != nullptr)
            *errorMessage = success ? juce::String() : juce::String("Requested device type was not applied.");

        return success;
    }

    juce::StringArray AudioEngine::getAvailableOutputDevices() const
    {
        if (auto *deviceType = deviceManager.getCurrentDeviceTypeObject())
            return deviceType->getDeviceNames(false);

        return {};
    }

    juce::String AudioEngine::getCurrentOutputDeviceName() const
    {
        if (auto *device = deviceManager.getCurrentAudioDevice())
            return device->getName();

        return {};
    }

    bool AudioEngine::setCurrentOutputDevice(const juce::String &deviceName, juce::String *errorMessage)
    {
        const std::lock_guard<std::mutex> lock(deviceConfigMutex);

        if (deviceName.isEmpty())
        {
            if (errorMessage != nullptr)
                *errorMessage = "Device name cannot be empty.";
            return false;
        }

        if (deviceManager.getCurrentAudioDevice() != nullptr &&
            deviceManager.getCurrentAudioDevice()->getName() == deviceName)
        {
            if (errorMessage != nullptr)
                *errorMessage = {};
            return true;
        }

        stop();
        sourcePlayer.setSource(nullptr);
        deviceManager.removeAudioCallback(&sourcePlayer);

        auto setup = deviceManager.getAudioDeviceSetup();
        setup.outputDeviceName = deviceName;

        const auto error = deviceManager.setAudioDeviceSetup(setup, true);

        sourcePlayer.setSource(this);
        deviceManager.addAudioCallback(&sourcePlayer);

        if (errorMessage != nullptr)
            *errorMessage = error;

        return error.isEmpty();
    }

    bool AudioEngine::isPreviewPlaying() const noexcept
    {
        return voiceManager.isPrimaryPlaying();
    }

    bool AudioEngine::consumePreviewFinishedFlag() noexcept
    {
        return voiceManager.consumePlaybackFinishedFlag();
    }

    double AudioEngine::getPreviewPlaybackProgressNormalized() const noexcept
    {
        return voiceManager.getPlaybackProgressNormalized();
    }

    void AudioEngine::setPreviewPlaybackProgressNormalized(double normalizedProgress)
    {
        voiceManager.setPlaybackProgressNormalized(normalizedProgress);
    }

    void AudioEngine::getActiveMidiPlaybackHeadsNormalized(std::vector<float> &headsOut) const
    {
        voiceManager.getActiveMidiPlaybackHeadsNormalized(headsOut);
    }

    void AudioEngine::setPreviewRootMidiNote(int midiNote)
    {
        voiceManager.setPreviewRootMidiNote(midiNote);
        voiceManager.updateAllVoicePitch(basePitchSemitones.load(std::memory_order_relaxed));
    }

    void AudioEngine::clearPreviewLoopRegion()
    {
        voiceManager.setLoopRegionSamples(-1, -1);
    }

    void AudioEngine::setPreviewLoopRegionSamples(int64_t startSample, int64_t endSample)
    {
        voiceManager.setLoopRegionSamples(startSample, endSample);
    }

    void AudioEngine::getOscilloscopeFrame(std::vector<float> &destination) const
    {
        constexpr int kTriggerSearchSamples = 256;
        constexpr float kTriggerMinSlope = 1.0e-5f;

        destination.resize(static_cast<size_t>(kOscilloscopeFrameSize));

        const uint32_t write = oscilloscopeWriteIndex.load(std::memory_order_acquire);
        const uint32_t totalWindow = static_cast<uint32_t>(kOscilloscopeFrameSize + kTriggerSearchSamples);
        const uint32_t windowStart = (write >= totalWindow) ? (write - totalWindow) : 0u;

        int triggerOffset = 0;
        for (int i = 1; i < kTriggerSearchSamples; ++i)
        {
            const uint32_t prevIdx = (windowStart + static_cast<uint32_t>(i - 1)) % static_cast<uint32_t>(kOscilloscopeRingSize);
            const uint32_t currIdx = (windowStart + static_cast<uint32_t>(i)) % static_cast<uint32_t>(kOscilloscopeRingSize);
            const float prev = oscilloscopeRing[prevIdx];
            const float curr = oscilloscopeRing[currIdx];

            if (prev <= 0.0f && curr > 0.0f && (curr - prev) > kTriggerMinSlope)
            {
                triggerOffset = i;
                break;
            }
        }

        const uint32_t frameStart = windowStart + static_cast<uint32_t>(triggerOffset);
        for (int i = 0; i < kOscilloscopeFrameSize; ++i)
        {
            const uint32_t idx = (frameStart + static_cast<uint32_t>(i)) % static_cast<uint32_t>(kOscilloscopeRingSize);
            destination[static_cast<size_t>(i)] = oscilloscopeRing[idx];
        }
    }

    void AudioEngine::applyCurrentPitch()
    {
        const double base = basePitchSemitones.load(std::memory_order_relaxed);
        voiceManager.updateAllVoicePitch(base);
    }

} // namespace sw
