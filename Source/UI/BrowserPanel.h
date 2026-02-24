#pragma once

#include <JuceHeader.h>
#include "Catalog/CatalogModels.h"

#include <functional>
#include <optional>
#include <vector>

namespace sw
{

    /// Left panel: source folder management, directory tree, and file browser.
    class BrowserPanel final : public juce::Component,
                               public juce::SettableTooltipClient,
                               private juce::ScrollBar::Listener
    {
    public:
        BrowserPanel();
        ~BrowserPanel() override = default;

        void paint(juce::Graphics &g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent &event) override;
        void mouseWheelMove(const juce::MouseEvent &event, const juce::MouseWheelDetails &wheel) override;
        void mouseMove(const juce::MouseEvent &event) override;
        void mouseExit(const juce::MouseEvent &event) override;
        bool keyPressed(const juce::KeyPress &key) override;

        void setRoots(std::vector<RootRecord> roots);
        void setSelectedRootId(std::optional<int64_t> rootId);
        void setDarkMode(bool enabled);

        std::function<void(std::optional<int64_t> rootId)> onRootSelected;
        std::function<void()> onRenameSelectedRootRequested;
        std::function<void()> onDeleteSelectedRootRequested;

    private:
        void scrollBarMoved(juce::ScrollBar *scrollBarThatHasMoved, double newRangeStart) override;
        void updateScrollBar();
        std::optional<int> rootRowIndexForY(int y) const;

        juce::ScrollBar verticalScrollBar{true};
        std::vector<RootRecord> roots;
        std::optional<int64_t> selectedRootId;
        int firstVisibleRow = 0;
        bool darkModeEnabled = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowserPanel)
    };

} // namespace sw
