#pragma once

#include <JuceHeader.h>
#include <functional>

namespace sw
{

    /// Preview controls: play/stop/loop, pitch knob, on-screen keyboard, ASIO selector.
    class PreviewPanel final : public juce::Component
    {
    public:
        PreviewPanel();
        ~PreviewPanel() override = default;

        void paint(juce::Graphics &g) override;
        void resized() override;

        /// Handle spacebar for play/stop toggle.
        bool keyPressed(const juce::KeyPress &key) override;

        juce::MidiKeyboardState &getKeyboardState() noexcept { return keyboardState; }
        void setPitchSemitones(double semitones);
        void setDarkMode(bool enabled);
        void setAvailableOutputDeviceTypes(const juce::StringArray &typeNames, const juce::String &currentTypeName);
        void setAvailableOutputDevices(const juce::StringArray &deviceNames, const juce::String &currentDeviceName);
        void setAvailableMidiInputDevices(const juce::Array<juce::MidiDeviceInfo> &devices,
                                          const juce::String &currentDeviceIdentifier);

        std::function<void()> onPlayRequested;
        std::function<void()> onStopRequested;
        std::function<void(bool enabled)> onAutoPlaybackChanged;
        std::function<void(bool enabled)> onLoopPlaybackChanged;
        std::function<void(double semitones)> onPitchChanged;
        std::function<void(const juce::String &typeName)> onApplyOutputDeviceTypeRequested;
        std::function<void(const juce::String &deviceName)> onApplyOutputDeviceRequested;
        std::function<void(const juce::String &deviceIdentifier)> onMidiInputDeviceSelected;

        void setAutoPlayEnabled(bool enabled);
        bool isAutoPlayEnabled() const noexcept;
        void setLoopEnabled(bool enabled);
        bool isLoopEnabled() const noexcept;

    private:
        juce::TextButton playButton{"Play"};
        juce::TextButton stopButton{"Stop"};
        juce::ToggleButton autoPlayButton{"Auto"};
        juce::ToggleButton loopButton{"Loop"};
        juce::Slider pitchSlider;
        juce::ComboBox outputDeviceTypeCombo;
        juce::TextButton applyOutputDeviceTypeButton{"Apply"};
        juce::ComboBox outputDeviceCombo;
        juce::TextButton applyOutputDeviceButton{"Apply"};
        juce::ComboBox midiInputCombo;
        juce::StringArray midiInputDeviceIdentifiers;
        juce::MidiKeyboardState keyboardState;
        juce::MidiKeyboardComponent keyboard;
        bool isPlaying = false;
        bool darkModeEnabled = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreviewPanel)
    };

} // namespace sw
