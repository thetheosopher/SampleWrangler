#include "BrowserPanel.h"

namespace sw
{

    BrowserPanel::BrowserPanel()
    {
        addAndMakeVisible(addRootButton);
        addAndMakeVisible(rescanAllButton);
        addAndMakeVisible(cancelScanButton);

        addRootButton.onClick = [this]
        {
            if (onAddRootRequested)
                onAddRootRequested();
        };

        rescanAllButton.onClick = [this]
        {
            if (onRescanAllRequested)
                onRescanAllRequested();
        };

        cancelScanButton.onClick = [this]
        {
            if (onCancelScanRequested)
                onCancelScanRequested();
        };
        cancelScanButton.setEnabled(false);
    }

    void BrowserPanel::paint(juce::Graphics &g)
    {
        g.fillAll(juce::Colour(0xff2b2b2b));
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText("Browser", getLocalBounds().removeFromTop(24), juce::Justification::centred);

        constexpr int controlsBottomY = 68;
        g.setColour(juce::Colours::darkgrey.withAlpha(0.8f));
        g.drawHorizontalLine(controlsBottomY - 2, 8.0f, static_cast<float>(getWidth() - 8));

        auto listArea = getLocalBounds().reduced(8).withTrimmedTop(controlsBottomY);

        g.setColour(juce::Colours::lightgreen);
        g.setFont(11.0f);
        g.drawFittedText("Scan: " + scanStatus, listArea.removeFromTop(16), juce::Justification::left, 1);
        listArea.removeFromTop(4);

        g.setFont(12.0f);

        if (roots.empty())
        {
            g.setColour(juce::Colours::grey);
            g.drawText("No roots configured", listArea.removeFromTop(20), juce::Justification::left);
            return;
        }

        g.setColour(juce::Colours::lightgrey);
        for (const auto &root : roots)
        {
            if (listArea.getHeight() < 18)
                break;

            const juce::String line = juce::String(root.label) + "  (" + juce::String(root.path) + ")";
            g.drawFittedText(line, listArea.removeFromTop(18), juce::Justification::left, 1);
        }
    }

    void BrowserPanel::resized()
    {
        auto area = getLocalBounds().reduced(4);
        auto topRow = area.removeFromTop(28);
        addRootButton.setBounds(topRow.removeFromLeft(topRow.getWidth() / 2 - 2));
        topRow.removeFromLeft(4);
        rescanAllButton.setBounds(topRow);

        area.removeFromTop(4);
        cancelScanButton.setBounds(area.removeFromTop(28));
    }

    void BrowserPanel::setRoots(std::vector<RootRecord> newRoots)
    {
        roots = std::move(newRoots);
        repaint();
    }

    void BrowserPanel::setScanStatus(const juce::String &statusText)
    {
        scanStatus = statusText;
        repaint();
    }

    void BrowserPanel::setScanInProgress(bool inProgress)
    {
        addRootButton.setEnabled(!inProgress);
        rescanAllButton.setEnabled(!inProgress);
        cancelScanButton.setEnabled(inProgress);
    }

} // namespace sw
