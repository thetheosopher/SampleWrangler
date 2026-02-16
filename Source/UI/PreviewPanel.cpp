#include "PreviewPanel.h"

namespace sw
{

    void PreviewPanel::PitchValueField::mouseDown(const juce::MouseEvent &event)
    {
        dragStartScreenX = event.getScreenX();
        if (onScrubStart)
            onScrubStart();
    }

    void PreviewPanel::PitchValueField::mouseDrag(const juce::MouseEvent &event)
    {
        if (onScrubPixels)
            onScrubPixels(event.getScreenX() - dragStartScreenX);
    }

    PreviewPanel::PreviewPanel()
        : keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        playButton.onClick = [this]
        {
            setPlaybackActive(true);
            if (onPlayRequested)
                onPlayRequested();
        };
        playButton.setTooltip("Start preview playback");

        stopButton.onClick = [this]
        {
            setPlaybackActive(false);
            if (onStopRequested)
                onStopRequested();
        };
        stopButton.setTooltip("Stop preview playback");

        autoPlayButton.onClick = [this]
        {
            if (onAutoPlaybackChanged)
                onAutoPlaybackChanged(autoPlayButton.getToggleState());
        };
        autoPlayButton.setTooltip("Automatically play selected samples and advance after playback completes");

        loopButton.onClick = [this]
        {
            if (onLoopPlaybackChanged)
                onLoopPlaybackChanged(loopButton.getToggleState());
        };
        loopButton.setTooltip("Loop playback; with Auto enabled, loops through the results list");

        preserveLengthButton.onClick = [this]
        {
            if (onPreserveLengthChanged)
                onPreserveLengthChanged(preserveLengthButton.getToggleState());
        };
        preserveLengthButton.setTooltip("Maintain duration while shifting pitch using time-stretch/pitch-shift");

        addAndMakeVisible(playButton);
        addAndMakeVisible(stopButton);
        addAndMakeVisible(autoPlayButton);
        addAndMakeVisible(loopButton);
        addAndMakeVisible(preserveLengthButton);

        pitchSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        pitchSlider.setRange(-24.0, 24.0, 0.5); // semitones
        pitchSlider.setValue(0.0);
        pitchSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pitchSlider.onValueChange = [this]
        {
            updatePitchValueFieldText();
            if (onPitchChanged)
                onPitchChanged(pitchSlider.getValue());
        };

        transposeLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(transposeLabel);

        pitchValueField.setJustificationType(juce::Justification::centred);
        pitchValueField.setEditable(false, false, false);
        pitchValueField.setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        pitchValueField.onScrubStart = [this]
        {
            pitchScrubStartValue = pitchSlider.getValue();
        };
        pitchValueField.onScrubPixels = [this](int deltaPixels)
        {
            constexpr double semitonesPerPixel = 0.1;
            pitchSlider.setValue(pitchScrubStartValue + (static_cast<double>(deltaPixels) * semitonesPerPixel));
        };
        addAndMakeVisible(pitchValueField);
        updatePitchValueFieldText();

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
        updatePlaybackButtonStates();
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
        constexpr int topRowHeight = 36;
        constexpr int comboRowHeight = 24;
        constexpr int maxKeyboardHeight = 92;

        auto topRow = area.removeFromTop(topRowHeight);
        playButton.setBounds(topRow.removeFromLeft(60));
        topRow.removeFromLeft(4);
        stopButton.setBounds(topRow.removeFromLeft(60));
        topRow.removeFromLeft(8);
        auto toggleArea = topRow.removeFromLeft(128);
        auto toggleTopRow = toggleArea.removeFromTop(16);
        autoPlayButton.setBounds(toggleTopRow.removeFromLeft(62));
        loopButton.setBounds(toggleTopRow.removeFromLeft(62));
        preserveLengthButton.setBounds(toggleArea.removeFromTop(16));
        topRow.removeFromLeft(8);
        auto pitchArea = topRow.removeFromLeft(72);
        transposeLabel.setBounds(pitchArea.removeFromTop(12));
        pitchValueField.setBounds(pitchArea.removeFromTop(16));

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
        const int keyboardHeight = juce::jmin(maxKeyboardHeight, area.getHeight());
        keyboard.setBounds(area.removeFromBottom(keyboardHeight));
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
        updatePitchValueFieldText();
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

        transposeLabel.setColour(juce::Label::textColourId, textColour);
        pitchValueField.setColour(juce::Label::textColourId, textColour);
        pitchValueField.setColour(juce::Label::backgroundColourId, controlBg);
        pitchValueField.setColour(juce::Label::outlineColourId, outline);

        loopButton.setColour(juce::ToggleButton::textColourId, textColour);
        loopButton.setColour(juce::ToggleButton::tickColourId, darkModeEnabled ? juce::Colour(0xff4fc3f7) : juce::Colour(0xff1f5fa1));
        loopButton.setColour(juce::ToggleButton::tickDisabledColourId, darkModeEnabled ? juce::Colour(0xff6b6b6b) : juce::Colour(0xff8a8a8a));

        autoPlayButton.setColour(juce::ToggleButton::textColourId, textColour);
        autoPlayButton.setColour(juce::ToggleButton::tickColourId, darkModeEnabled ? juce::Colour(0xff4fc3f7) : juce::Colour(0xff1f5fa1));
        autoPlayButton.setColour(juce::ToggleButton::tickDisabledColourId, darkModeEnabled ? juce::Colour(0xff6b6b6b) : juce::Colour(0xff8a8a8a));

        preserveLengthButton.setColour(juce::ToggleButton::textColourId, textColour);
        preserveLengthButton.setColour(juce::ToggleButton::tickColourId, darkModeEnabled ? juce::Colour(0xff4fc3f7) : juce::Colour(0xff1f5fa1));
        preserveLengthButton.setColour(juce::ToggleButton::tickDisabledColourId, darkModeEnabled ? juce::Colour(0xff6b6b6b) : juce::Colour(0xff8a8a8a));

        const auto buttonBg = darkModeEnabled ? juce::Colour(0xff2f3f4d) : juce::Colour(0xffe7ecef);
        const auto buttonBgHover = darkModeEnabled ? juce::Colour(0xff425362) : juce::Colour(0xffd9e1e6);
        const auto buttonText = darkModeEnabled ? juce::Colours::white : juce::Colour(0xff1f2a33);

        playButton.setColour(juce::TextButton::buttonColourId, buttonBg);
        playButton.setColour(juce::TextButton::buttonOnColourId, buttonBgHover);
        playButton.setColour(juce::TextButton::textColourOffId, buttonText);
        playButton.setColour(juce::TextButton::textColourOnId, buttonText);

        stopButton.setColour(juce::TextButton::buttonColourId, buttonBg);
        stopButton.setColour(juce::TextButton::buttonOnColourId, buttonBgHover);
        stopButton.setColour(juce::TextButton::textColourOffId, buttonText);
        stopButton.setColour(juce::TextButton::textColourOnId, buttonText);

        applyOutputDeviceTypeButton.setColour(juce::TextButton::buttonColourId, buttonBg);
        applyOutputDeviceTypeButton.setColour(juce::TextButton::buttonOnColourId, buttonBgHover);
        applyOutputDeviceTypeButton.setColour(juce::TextButton::textColourOffId, buttonText);
        applyOutputDeviceTypeButton.setColour(juce::TextButton::textColourOnId, buttonText);

        applyOutputDeviceButton.setColour(juce::TextButton::buttonColourId, buttonBg);
        applyOutputDeviceButton.setColour(juce::TextButton::buttonOnColourId, buttonBgHover);
        applyOutputDeviceButton.setColour(juce::TextButton::textColourOffId, buttonText);
        applyOutputDeviceButton.setColour(juce::TextButton::textColourOnId, buttonText);

        repaint();
    }

    void PreviewPanel::setPlaybackActive(bool playing)
    {
        if (isPlaying == playing)
            return;

        isPlaying = playing;
        updatePlaybackButtonStates();
    }

    void PreviewPanel::setAutoPlayEnabled(bool enabled)
    {
        autoPlayButton.setToggleState(enabled, juce::dontSendNotification);
    }

    bool PreviewPanel::isAutoPlayEnabled() const noexcept
    {
        return autoPlayButton.getToggleState();
    }

    void PreviewPanel::setLoopEnabled(bool enabled)
    {
        loopButton.setToggleState(enabled, juce::dontSendNotification);
    }

    bool PreviewPanel::isLoopEnabled() const noexcept
    {
        return loopButton.getToggleState();
    }

    void PreviewPanel::setPreserveLengthEnabled(bool enabled)
    {
        preserveLengthButton.setToggleState(enabled, juce::dontSendNotification);
    }

    bool PreviewPanel::isPreserveLengthEnabled() const noexcept
    {
        return preserveLengthButton.getToggleState();
    }

    void PreviewPanel::updatePlaybackButtonStates()
    {
        playButton.setEnabled(!isPlaying);
        stopButton.setEnabled(isPlaying);
    }

    void PreviewPanel::updatePitchValueFieldText()
    {
        pitchValueField.setText(juce::String(pitchSlider.getValue(), 1), juce::dontSendNotification);
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
                setPlaybackActive(false);
                if (onStopRequested)
                    onStopRequested();
            }
            else
            {
                setPlaybackActive(true);
                if (onPlayRequested)
                    onPlayRequested();
            }
            return true;
        }
        return false;
    }

} // namespace sw
