#include "BrowserPanel.h"

namespace sw
{

    BrowserPanel::BrowserPanel() = default;

    void BrowserPanel::paint(juce::Graphics &g)
    {
        g.fillAll(juce::Colour(0xff2b2b2b));
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText("Browser", getLocalBounds().removeFromTop(24), juce::Justification::centred);

        constexpr int controlsBottomY = 30;
        g.setColour(juce::Colours::darkgrey.withAlpha(0.8f));
        g.drawHorizontalLine(controlsBottomY - 2, 8.0f, static_cast<float>(getWidth() - 8));

        auto listArea = getLocalBounds().reduced(8);
        listArea.removeFromTop(controlsBottomY);

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

        for (const auto &root : roots)
        {
            if (listArea.getHeight() < 18)
                break;

            auto rowArea = listArea.removeFromTop(18);
            const bool isSelected = selectedRootId.has_value() && *selectedRootId == root.id;

            if (isSelected)
            {
                g.setColour(juce::Colour(0xff35506b));
                g.fillRect(rowArea);
            }

            g.setColour(juce::Colours::lightgrey);
            const juce::String line = juce::String(root.label) + "  (" + juce::String(root.path) + ")";
            g.drawFittedText(line, rowArea.reduced(2, 0), juce::Justification::left, 1);
        }
    }

    void BrowserPanel::resized()
    {
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
        (void)inProgress;
    }

    void BrowserPanel::setSelectedRootId(std::optional<int64_t> rootId)
    {
        selectedRootId = rootId;
        repaint();
    }

    void BrowserPanel::mouseDown(const juce::MouseEvent &event)
    {
        constexpr int controlsBottomY = 30;
        constexpr int scanRowHeight = 16;
        constexpr int rowGap = 4;
        constexpr int rowHeight = 18;

        auto listArea = getLocalBounds().reduced(8);
        listArea.removeFromTop(controlsBottomY + scanRowHeight + rowGap);

        if (!listArea.contains(event.getPosition()))
            return;

        const int yOffset = event.y - listArea.getY();
        if (yOffset < 0)
            return;

        const int rowIndex = yOffset / rowHeight;
        if (rowIndex < 0 || rowIndex >= static_cast<int>(roots.size()))
            return;

        const auto clickedRootId = roots[static_cast<size_t>(rowIndex)].id;
        if (selectedRootId.has_value() && *selectedRootId == clickedRootId)
            selectedRootId.reset();
        else
            selectedRootId = clickedRootId;

        repaint();
        if (onRootSelected)
            onRootSelected(selectedRootId);
    }

} // namespace sw
