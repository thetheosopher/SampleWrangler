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

        void setPitchSemitones(double semitones);
        void setDarkMode(bool enabled);
        void setAvailableOutputDeviceTypes(const juce::StringArray &typeNames, const juce::String &currentTypeName);
        void setAvailableOutputDevices(const juce::StringArray &deviceNames, const juce::String &currentDeviceName);
        void setPlaybackActive(bool playing);

        std::function<void()> onPlayRequested;
        std::function<void()> onStopRequested;
        std::function<void(bool enabled)> onAutoPlaybackChanged;
        std::function<void(bool enabled)> onLoopPlaybackChanged;
        std::function<void(bool enabled)> onStretchChanged;
        std::function<void(bool enabled)> onStretchHighQualityChanged;
        std::function<void(double semitones)> onPitchChanged;
        std::function<void(const juce::String &typeName)> onOutputDeviceTypeChanged;
        std::function<void(const juce::String &deviceName)> onOutputDeviceChanged;

        void setAutoPlayEnabled(bool enabled);
        bool isAutoPlayEnabled() const noexcept;
        void setLoopEnabled(bool enabled);
        bool isLoopEnabled() const noexcept;
        void setStretchEnabled(bool enabled);
        bool isStretchEnabled() const noexcept;
        void setStretchHighQualityEnabled(bool enabled);
        bool isStretchHighQualityEnabled() const noexcept;
        void setStretchHighQualityAvailable(bool available);
        void updatePlaybackButtonStates();

        class PitchValueField final : public juce::Label
        {
        public:
            std::function<void()> onScrubStart;
            std::function<void(int deltaPixels)> onScrubPixels;

            void mouseDown(const juce::MouseEvent &event) override;
            void mouseDrag(const juce::MouseEvent &event) override;

        private:
            int dragStartScreenX = 0;
        };

        class FixedFontLookAndFeel : public juce::LookAndFeel_V4
        {
        public:
            void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button,
                                  bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
            {
                // Call parent to draw everything
                juce::LookAndFeel_V4::drawToggleButton(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
            }

            juce::Font getTextButtonFont(juce::TextButton &, int) override
            {
                return juce::FontOptions(12.0f);
            }
        };

    private:
        void updatePitchValueFieldText();

        juce::TextButton playButton{"Play"};
        juce::TextButton stopButton{"Stop"};
        juce::ToggleButton autoPlayButton{"Auto"};
        juce::ToggleButton loopButton{"Loop"};
        juce::ToggleButton stretchButton{"Stretch"};
        juce::ToggleButton stretchHighQualityButton{"HQ"};
        FixedFontLookAndFeel fixedFontLookAndFeel;
        juce::Slider pitchSlider;
        juce::Label transposeLabel{"TransposeLabel", "Transpose"};
        PitchValueField pitchValueField;
        juce::ComboBox outputDeviceTypeCombo;
        juce::ComboBox outputDeviceCombo;
        bool isPlaying = false;
        bool darkModeEnabled = false;
        double pitchScrubStartValue = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreviewPanel)
    };

} // namespace sw
