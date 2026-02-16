#include "PreviewPanel.h"

namespace sw
{

    PreviewPanel::PreviewPanel()
        : keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        playButton.onClick = [this]
        {
            isPlaying = true;
            if (onPlayRequested)
                onPlayRequested();
        };
        playButton.setTooltip("Start preview playback");

        stopButton.onClick = [this]
        {
            isPlaying = false;
            if (onStopRequested)
                onStopRequested();
        };
        stopButton.setTooltip("Stop preview playback");

        loopButton.onClick = [this]
        {
            if (onLoopPlaybackChanged)
                onLoopPlaybackChanged(loopButton.getToggleState());
        };
        loopButton.setTooltip("Loop playback (restarts sample when it reaches the end)");

        addAndMakeVisible(playButton);
        addAndMakeVisible(stopButton);
        addAndMakeVisible(loopButton);

        pitchSlider.setSliderStyle(juce::Slider::Rotary);
        pitchSlider.setRange(-24.0, 24.0, 0.5); // semitones
        pitchSlider.setValue(0.0);
        pitchSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
        pitchSlider.onValueChange = [this]
        {
            if (onPitchChanged)
                onPitchChanged(pitchSlider.getValue());
        };
        addAndMakeVisible(pitchSlider);

        outputDeviceTypeCombo.setTextWhenNoChoicesAvailable("No device types");
        addAndMakeVisible(outputDeviceTypeCombo);

        applyOutputDeviceTypeButton.onClick = [this]
        {
            if (onApplyOutputDeviceTypeRequested)
                onApplyOutputDeviceTypeRequested(outputDeviceTypeCombo.getText());
        };
        addAndMakeVisible(applyOutputDeviceTypeButton);

        outputDeviceCombo.setTextWhenNoChoicesAvailable("No output devices");
        addAndMakeVisible(outputDeviceCombo);

        applyOutputDeviceButton.onClick = [this]
        {
            if (onApplyOutputDeviceRequested)
                onApplyOutputDeviceRequested(outputDeviceCombo.getText());
        };
        addAndMakeVisible(applyOutputDeviceButton);

        midiInputCombo.setTextWhenNoChoicesAvailable("No MIDI inputs");
        midiInputCombo.onChange = [this]
        {
            if (!onMidiInputDeviceSelected)
                return;

            const int index = midiInputCombo.getSelectedItemIndex();
            if (index < 0 || index >= midiInputDeviceIdentifiers.size())
            {
                onMidiInputDeviceSelected({});
                return;
            }

            onMidiInputDeviceSelected(midiInputDeviceIdentifiers[index]);
        };
        addAndMakeVisible(midiInputCombo);

        keyboard.setAvailableRange(36, 96); // C2–C7 range for preview
        addAndMakeVisible(keyboard);

        setWantsKeyboardFocus(true);
        setDarkMode(true);
    }

    void PreviewPanel::paint(juce::Graphics &g)
    {
        g.fillAll(darkModeEnabled ? juce::Colour(0xff333333) : juce::Colour(0xfff4f4f4));
    }

    void PreviewPanel::resized()
    {
        auto area = getLocalBounds().reduced(3);

        constexpr int rowSpacing = 3;
        constexpr int topRowHeight = 30;
        constexpr int comboRowHeight = 24;

        auto topRow = area.removeFromTop(topRowHeight);
        playButton.setBounds(topRow.removeFromLeft(60));
        topRow.removeFromLeft(4);
        stopButton.setBounds(topRow.removeFromLeft(60));
        topRow.removeFromLeft(8);
        loopButton.setBounds(topRow.removeFromLeft(62));
        topRow.removeFromLeft(8);
        pitchSlider.setBounds(topRow.removeFromLeft(70));

        area.removeFromTop(rowSpacing);
        auto typeRow = area.removeFromTop(comboRowHeight);
        outputDeviceTypeCombo.setBounds(typeRow.removeFromLeft(jmax(100, typeRow.getWidth() - 58)));
        typeRow.removeFromLeft(4);
        applyOutputDeviceTypeButton.setBounds(typeRow);

        area.removeFromTop(rowSpacing);
        auto deviceRow = area.removeFromTop(comboRowHeight);
        outputDeviceCombo.setBounds(deviceRow.removeFromLeft(jmax(100, deviceRow.getWidth() - 58)));
        deviceRow.removeFromLeft(4);
        applyOutputDeviceButton.setBounds(deviceRow);

        area.removeFromTop(rowSpacing);
        auto midiRow = area.removeFromTop(comboRowHeight);
        midiInputCombo.setBounds(midiRow);

        area.removeFromTop(rowSpacing);
        keyboard.setBounds(area);
    }

    void PreviewPanel::setAvailableOutputDeviceTypes(const juce::StringArray &typeNames, const juce::String &currentTypeName)
    {
        outputDeviceTypeCombo.clear();

        int id = 1;
        for (const auto &name : typeNames)
            outputDeviceTypeCombo.addItem(name, id++);

        if (typeNames.contains(currentTypeName))
            outputDeviceTypeCombo.setText(currentTypeName, juce::dontSendNotification);
        else if (typeNames.isEmpty())
            outputDeviceTypeCombo.setText("", juce::dontSendNotification);
        else
            outputDeviceTypeCombo.setSelectedItemIndex(0, juce::dontSendNotification);
    }

    void PreviewPanel::setPitchSemitones(double semitones)
    {
        pitchSlider.setValue(semitones, juce::dontSendNotification);
    }

    void PreviewPanel::setDarkMode(bool enabled)
    {
        if (darkModeEnabled == enabled)
            return;

        darkModeEnabled = enabled;

        const auto textColour = darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020);
        const auto controlBg = darkModeEnabled ? juce::Colour(0xff2b2b2b) : juce::Colour(0xffffffff);
        const auto outline = darkModeEnabled ? juce::Colour(0xff4d4d4d) : juce::Colour(0xffb8b8b8);

        outputDeviceTypeCombo.setColour(juce::ComboBox::backgroundColourId, controlBg);
        outputDeviceTypeCombo.setColour(juce::ComboBox::textColourId, textColour);
        outputDeviceTypeCombo.setColour(juce::ComboBox::outlineColourId, outline);

        outputDeviceCombo.setColour(juce::ComboBox::backgroundColourId, controlBg);
        outputDeviceCombo.setColour(juce::ComboBox::textColourId, textColour);
        outputDeviceCombo.setColour(juce::ComboBox::outlineColourId, outline);

        midiInputCombo.setColour(juce::ComboBox::backgroundColourId, controlBg);
        midiInputCombo.setColour(juce::ComboBox::textColourId, textColour);
        midiInputCombo.setColour(juce::ComboBox::outlineColourId, outline);

        pitchSlider.setColour(juce::Slider::textBoxTextColourId, textColour);
        pitchSlider.setColour(juce::Slider::textBoxBackgroundColourId, controlBg);
        pitchSlider.setColour(juce::Slider::textBoxOutlineColourId, outline);
        pitchSlider.setColour(juce::Slider::rotarySliderFillColourId, darkModeEnabled ? juce::Colour(0xff4fc3f7) : juce::Colour(0xff2b78c6));

        repaint();
    }

    void PreviewPanel::setLoopEnabled(bool enabled)
    {
        loopButton.setToggleState(enabled, juce::dontSendNotification);
    }

    bool PreviewPanel::isLoopEnabled() const noexcept
    {
        return loopButton.getToggleState();
    }

    void PreviewPanel::setAvailableOutputDevices(const juce::StringArray &deviceNames, const juce::String &currentDeviceName)
    {
        outputDeviceCombo.clear();

        int id = 1;
        for (const auto &name : deviceNames)
            outputDeviceCombo.addItem(name, id++);

        if (deviceNames.contains(currentDeviceName))
            outputDeviceCombo.setText(currentDeviceName, juce::dontSendNotification);
        else if (deviceNames.isEmpty())
            outputDeviceCombo.setText("", juce::dontSendNotification);
        else
            outputDeviceCombo.setSelectedItemIndex(0, juce::dontSendNotification);
    }

    void PreviewPanel::setAvailableMidiInputDevices(const juce::Array<juce::MidiDeviceInfo> &devices,
                                                    const juce::String &currentDeviceIdentifier)
    {
        midiInputCombo.clear(juce::dontSendNotification);
        midiInputDeviceIdentifiers.clear();

        int id = 1;
        int selectedIndex = -1;
        for (const auto &device : devices)
        {
            midiInputCombo.addItem(device.name, id++);
            midiInputDeviceIdentifiers.add(device.identifier);

            if (device.identifier == currentDeviceIdentifier)
                selectedIndex = midiInputDeviceIdentifiers.size() - 1;
        }

        if (selectedIndex >= 0)
            midiInputCombo.setSelectedItemIndex(selectedIndex, juce::dontSendNotification);
        else if (devices.isEmpty())
            midiInputCombo.setText("", juce::dontSendNotification);
        else
            midiInputCombo.setSelectedItemIndex(0, juce::dontSendNotification);
    }

    bool PreviewPanel::keyPressed(const juce::KeyPress &key)
    {
        if (key == juce::KeyPress::spaceKey)
        {
            if (isPlaying)
            {
                isPlaying = false;
                if (onStopRequested)
                    onStopRequested();
            }
            else
            {
                isPlaying = true;
                if (onPlayRequested)
                    onPlayRequested();
            }
            return true;
        }
        return false;
    }

} // namespace sw
