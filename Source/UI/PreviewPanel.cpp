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
    {
        playButton.setButtonText(juce::String::charToString(static_cast<juce_wchar>(0x25B6))); // ▶
        stopButton.setButtonText(juce::String::charToString(static_cast<juce_wchar>(0x25A0))); // ■

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

        stretchButton.onClick = [this]
        {
            if (onStretchChanged)
                onStretchChanged(stretchButton.getToggleState());
        };
        stretchButton.setTooltip("Maintain duration while shifting pitch using time-stretch/pitch-shift");

        stretchHighQualityButton.onClick = [this]
        {
            if (onStretchHighQualityChanged)
                onStretchHighQualityChanged(stretchHighQualityButton.getToggleState());
        };
        stretchHighQualityButton.setTooltip("Use Rubber Band v4 high-quality pitch shifting for Stretch mode");

        addAndMakeVisible(playButton);
        addAndMakeVisible(stopButton);

        // Apply fixed font LookAndFeel to toggle buttons
        autoPlayButton.setLookAndFeel(&fixedFontLookAndFeel);
        loopButton.setLookAndFeel(&fixedFontLookAndFeel);
        stretchButton.setLookAndFeel(&fixedFontLookAndFeel);
        stretchHighQualityButton.setLookAndFeel(&fixedFontLookAndFeel);

        addAndMakeVisible(autoPlayButton);
        addAndMakeVisible(loopButton);
        addAndMakeVisible(stretchButton);
        addAndMakeVisible(stretchHighQualityButton);

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

        transposeLabel.setJustificationType(juce::Justification::centredLeft);
        transposeLabel.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(transposeLabel);

        pitchValueField.setJustificationType(juce::Justification::centred);
        pitchValueField.setEditable(false, false, false);
        pitchValueField.setFont(juce::FontOptions(12.0f));
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
        outputDeviceTypeCombo.onChange = [this]
        {
            if (onOutputDeviceTypeChanged)
                onOutputDeviceTypeChanged(outputDeviceTypeCombo.getText());
        };
        addAndMakeVisible(outputDeviceTypeCombo);

        outputDeviceCombo.setTextWhenNoChoicesAvailable("No output devices");
        outputDeviceCombo.onChange = [this]
        {
            if (onOutputDeviceChanged)
                onOutputDeviceChanged(outputDeviceCombo.getText());
        };
        addAndMakeVisible(outputDeviceCombo);

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

        auto topRow = area.removeFromTop(topRowHeight);
        playButton.setBounds(topRow.removeFromLeft(60));
        topRow.removeFromLeft(4);
        stopButton.setBounds(topRow.removeFromLeft(60));
        topRow.removeFromLeft(8);

        // Grid layout for checkboxes: 2x2 grid with aligned columns
        auto toggleArea = topRow.removeFromLeft(128);
        constexpr int col1Width = 64; // Width for first column (Auto, Stretch)
        constexpr int col2Width = 64; // Width for second column (Loop, HQ)
        constexpr int toggleRowHeight = 16;
        constexpr int toggleRowGap = 2;

        auto toggleTopRow = toggleArea.removeFromTop(toggleRowHeight);
        autoPlayButton.setBounds(toggleTopRow.removeFromLeft(col1Width));
        loopButton.setBounds(toggleTopRow.removeFromLeft(col2Width));

        toggleArea.removeFromTop(toggleRowGap); // Small spacing between rows
        auto toggleBottomRow = toggleArea.removeFromTop(toggleRowHeight);
        stretchButton.setBounds(toggleBottomRow.removeFromLeft(col1Width));
        stretchHighQualityButton.setBounds(toggleBottomRow.removeFromLeft(col2Width));

        topRow.removeFromLeft(8);
        auto pitchArea = topRow.removeFromLeft(90);
        const auto transposeRow = pitchArea.removeFromTop(14);
        pitchArea.removeFromTop(2); // Padding between label and textbox
        auto pitchValueBounds = pitchArea.removeFromTop(16);
        const auto centeredPitchValueBounds = pitchValueBounds.withSizeKeepingCentre(pitchValueBounds.getWidth() / 2,
                                                                                     pitchValueBounds.getHeight());
        pitchValueField.setBounds(centeredPitchValueBounds);
        transposeLabel.setBounds(transposeRow.withSizeKeepingCentre(centeredPitchValueBounds.getWidth(),
                                                                    transposeRow.getHeight()));

        area.removeFromTop(rowSpacing);
        auto typeRow = area.removeFromTop(comboRowHeight);
        outputDeviceTypeCombo.setBounds(typeRow);

        area.removeFromTop(rowSpacing);
        auto deviceRow = area.removeFromTop(comboRowHeight);
        outputDeviceCombo.setBounds(deviceRow);
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
        const auto arrowColour = darkModeEnabled ? juce::Colour(0xffcccccc) : juce::Colour(0xff606060);

        outputDeviceTypeCombo.setColour(juce::ComboBox::backgroundColourId, controlBg);
        outputDeviceTypeCombo.setColour(juce::ComboBox::textColourId, textColour);
        outputDeviceTypeCombo.setColour(juce::ComboBox::outlineColourId, outline);
        outputDeviceTypeCombo.setColour(juce::ComboBox::arrowColourId, arrowColour);

        outputDeviceCombo.setColour(juce::ComboBox::backgroundColourId, controlBg);
        outputDeviceCombo.setColour(juce::ComboBox::textColourId, textColour);
        outputDeviceCombo.setColour(juce::ComboBox::outlineColourId, outline);
        outputDeviceCombo.setColour(juce::ComboBox::arrowColourId, arrowColour);

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

        stretchButton.setColour(juce::ToggleButton::textColourId, textColour);
        stretchButton.setColour(juce::ToggleButton::tickColourId, darkModeEnabled ? juce::Colour(0xff4fc3f7) : juce::Colour(0xff1f5fa1));
        stretchButton.setColour(juce::ToggleButton::tickDisabledColourId, darkModeEnabled ? juce::Colour(0xff6b6b6b) : juce::Colour(0xff8a8a8a));

        stretchHighQualityButton.setColour(juce::ToggleButton::textColourId, textColour);
        stretchHighQualityButton.setColour(juce::ToggleButton::tickColourId, darkModeEnabled ? juce::Colour(0xff4fc3f7) : juce::Colour(0xff1f5fa1));
        stretchHighQualityButton.setColour(juce::ToggleButton::tickDisabledColourId, darkModeEnabled ? juce::Colour(0xff6b6b6b) : juce::Colour(0xff8a8a8a));

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

    void PreviewPanel::setStretchEnabled(bool enabled)
    {
        stretchButton.setToggleState(enabled, juce::dontSendNotification);
    }

    bool PreviewPanel::isStretchEnabled() const noexcept
    {
        return stretchButton.getToggleState();
    }

    void PreviewPanel::setStretchHighQualityEnabled(bool enabled)
    {
        stretchHighQualityButton.setToggleState(enabled, juce::dontSendNotification);
    }

    bool PreviewPanel::isStretchHighQualityEnabled() const noexcept
    {
        return stretchHighQualityButton.getToggleState();
    }

    void PreviewPanel::setStretchHighQualityAvailable(bool available)
    {
        stretchHighQualityButton.setEnabled(available);
        if (!available)
            stretchHighQualityButton.setToggleState(false, juce::dontSendNotification);
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
