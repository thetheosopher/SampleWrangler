#include "BrowserPanel.h"

namespace sw
{

    static int measureTextWidthPx(const juce::String &text, const juce::Font &font)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, text, 0.0f, 0.0f);
        return juce::roundToInt(glyphs.getBoundingBox(0, -1, true).getWidth());
    }

    static juce::String ellipsizeToWidth(const juce::String &text, const juce::Font &font, int maxWidthPx)
    {
        if (maxWidthPx <= 0)
            return {};

        if (measureTextWidthPx(text, font) <= maxWidthPx)
            return text;

        const juce::String ellipsis{"..."};
        const int ellipsisWidth = measureTextWidthPx(ellipsis, font);
        if (ellipsisWidth >= maxWidthPx)
            return ellipsis;

        juce::String trimmed = text;
        while (trimmed.isNotEmpty() && (measureTextWidthPx(trimmed, font) + ellipsisWidth) > maxWidthPx)
            trimmed = trimmed.dropLastCharacters(1);

        return trimmed + ellipsis;
    }

    BrowserPanel::BrowserPanel()
    {
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(true);
    }

    void BrowserPanel::paint(juce::Graphics &g)
    {
        const auto background = darkModeEnabled ? juce::Colour(0xff2b2b2b) : juce::Colour(0xfff0f0f0);
        const auto titleColour = darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020);
        const auto separatorColour = darkModeEnabled ? juce::Colours::darkgrey.withAlpha(0.8f) : juce::Colour(0xffb5b5b5);
        const auto emptyColour = darkModeEnabled ? juce::Colours::grey : juce::Colour(0xff6f6f6f);
        const auto selectedColour = darkModeEnabled ? juce::Colour(0xff35506b) : juce::Colour(0xffc9dcf1);
        const auto rowTextColour = darkModeEnabled ? juce::Colours::lightgrey : juce::Colour(0xff2f2f2f);

        g.fillAll(background);
        g.setColour(titleColour);
        g.setFont(14.0f);
        g.drawText("Sources", getLocalBounds().removeFromTop(24), juce::Justification::centred);

        constexpr int controlsBottomY = 30;
        g.setColour(separatorColour);
        g.drawHorizontalLine(controlsBottomY - 2, 8.0f, static_cast<float>(getWidth() - 8));

        auto listArea = getLocalBounds().reduced(8);
        listArea.removeFromTop(controlsBottomY);

        g.setFont(12.0f);

        if (roots.empty())
        {
            g.setColour(emptyColour);
            g.drawText("No sources configured", listArea.removeFromTop(20), juce::Justification::left);
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
                g.setColour(selectedColour);
                g.fillRect(rowArea);
            }

            g.setColour(rowTextColour);
            const auto textArea = rowArea.reduced(2, 0);
            const juce::String line = ellipsizeToWidth(juce::String(root.label), g.getCurrentFont(), textArea.getWidth());
            g.drawText(line, textArea, juce::Justification::left, false);
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

    void BrowserPanel::setSelectedRootId(std::optional<int64_t> rootId)
    {
        selectedRootId = rootId;
        repaint();
    }

    void BrowserPanel::setDarkMode(bool enabled)
    {
        if (darkModeEnabled == enabled)
            return;

        darkModeEnabled = enabled;
        repaint();
    }

    void BrowserPanel::mouseDown(const juce::MouseEvent &event)
    {
        grabKeyboardFocus();

        const auto rowIndex = rootRowIndexForY(event.y);
        if (!rowIndex.has_value() || *rowIndex >= static_cast<int>(roots.size()))
            return;

        const auto clickedRootId = roots[static_cast<size_t>(*rowIndex)].id;

        if (event.mods.isPopupMenu())
        {
            selectedRootId = clickedRootId;
            repaint();

            if (onRootSelected)
                onRootSelected(selectedRootId);

            juce::PopupMenu menu;
            menu.addItem(1, "Rename");

            auto safeThis = juce::Component::SafePointer<BrowserPanel>(this);
            const auto clickArea = juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(clickArea),
                               [safeThis](int selected)
                               {
                                   if (safeThis == nullptr)
                                       return;

                                   if (selected == 1 && safeThis->onRenameSelectedRootRequested)
                                       safeThis->onRenameSelectedRootRequested();
                               });
            return;
        }

        if (selectedRootId.has_value() && *selectedRootId == clickedRootId)
            selectedRootId.reset();
        else
            selectedRootId = clickedRootId;

        repaint();
        if (onRootSelected)
            onRootSelected(selectedRootId);
    }

    void BrowserPanel::mouseMove(const juce::MouseEvent &event)
    {
        const auto rowIndex = rootRowIndexForY(event.y);
        if (!rowIndex.has_value() || *rowIndex >= static_cast<int>(roots.size()))
        {
            setTooltip({});
            return;
        }

        const auto &root = roots[static_cast<size_t>(*rowIndex)];
        setTooltip("Source: " + juce::String(root.label) + "\nPath: " + juce::String(root.path));
    }

    void BrowserPanel::mouseExit(const juce::MouseEvent &)
    {
        setTooltip({});
    }

    bool BrowserPanel::keyPressed(const juce::KeyPress &key)
    {
        if (selectedRootId.has_value() && (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey))
        {
            if (onDeleteSelectedRootRequested)
                onDeleteSelectedRootRequested();
            return true;
        }

        return false;
    }

    std::optional<int> BrowserPanel::rootRowIndexForY(int y) const
    {
        constexpr int controlsBottomY = 30;
        constexpr int rowHeight = 18;

        auto listArea = getLocalBounds().reduced(8);
        listArea.removeFromTop(controlsBottomY);

        if (y < listArea.getY() || y >= listArea.getBottom())
            return std::nullopt;

        const int yOffset = y - listArea.getY();
        if (yOffset < 0)
            return std::nullopt;

        const int rowIndex = yOffset / rowHeight;
        if (rowIndex < 0 || rowIndex >= static_cast<int>(roots.size()))
            return std::nullopt;

        return rowIndex;
    }

} // namespace sw
