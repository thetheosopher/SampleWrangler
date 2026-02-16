#include "AudioEngine.h"
#include "Util/Logging.h"

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
    }

    // ---------------------------------------------------------------------------
    // High-level API (message thread)
    // ---------------------------------------------------------------------------

    void AudioEngine::initialiseDeviceManager()
    {
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
        voiceManager.loadBuffer(std::move(buffer), fileSampleRate);
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
        applyCurrentPitch();
    }

    void AudioEngine::handleMidiMessage(const juce::MidiMessage &message)
    {
        if (message.isNoteOn())
        {
            activeMidiNote.store(message.getNoteNumber(), std::memory_order_relaxed);
            applyCurrentPitch();
            play();
            return;
        }

        if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            activeMidiNote.store(-1, std::memory_order_relaxed);
            stop();
            applyCurrentPitch();
            return;
        }

        if (message.isNoteOff())
        {
            const int noteNumber = message.getNoteNumber();
            if (activeMidiNote.load(std::memory_order_relaxed) == noteNumber)
            {
                activeMidiNote.store(-1, std::memory_order_relaxed);
                stop();
                applyCurrentPitch();
            }
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

    juce::String AudioEngine::getCurrentOutputDeviceType() const
    {
        return deviceManager.getCurrentAudioDeviceType();
    }

    bool AudioEngine::setCurrentOutputDeviceType(const juce::String &typeName, juce::String *errorMessage)
    {
        deviceManager.setCurrentAudioDeviceType(typeName, true);

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
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.outputDeviceName = deviceName;

        const auto error = deviceManager.setAudioDeviceSetup(setup, true);
        if (errorMessage != nullptr)
            *errorMessage = error;

        return error.isEmpty();
    }

    void AudioEngine::applyCurrentPitch()
    {
        const double base = basePitchSemitones.load(std::memory_order_relaxed);
        const int note = activeMidiNote.load(std::memory_order_relaxed);

        if (note < 0)
        {
            voiceManager.setPitchSemitones(base);
            return;
        }

        constexpr int middleC = 60;
        const double midiOffset = static_cast<double>(note - middleC);
        voiceManager.setPitchSemitones(base + midiOffset);
    }

} // namespace sw
