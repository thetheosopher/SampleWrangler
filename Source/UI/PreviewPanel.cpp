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

        stopButton.onClick = [this]
        {
            isPlaying = false;
            if (onStopRequested)
                onStopRequested();
        };

        addAndMakeVisible(playButton);
        addAndMakeVisible(stopButton);

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

        keyboard.setAvailableRange(36, 96); // C2–C7 range for preview
        addAndMakeVisible(keyboard);

        setWantsKeyboardFocus(true);
    }

    void PreviewPanel::paint(juce::Graphics &g)
    {
        g.fillAll(juce::Colour(0xff333333));
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
