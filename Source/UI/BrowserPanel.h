#pragma once

#include <JuceHeader.h>
#include "Catalog/CatalogModels.h"

#include <functional>
#include <optional>
#include <vector>

namespace sw
{

    /// Left panel: root folder management, directory tree, and file browser.
    class BrowserPanel final : public juce::Component
    {
    public:
        BrowserPanel();
        ~BrowserPanel() override = default;

        void paint(juce::Graphics &g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent &event) override;

        void setRoots(std::vector<RootRecord> roots);
        void setScanStatus(const juce::String &statusText);
        void setScanInProgress(bool inProgress);
        void setSelectedRootId(std::optional<int64_t> rootId);

        std::function<void(std::optional<int64_t> rootId)> onRootSelected;

    private:
        std::vector<RootRecord> roots;
        juce::String scanStatus{"Idle"};
        std::optional<int64_t> selectedRootId;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowserPanel)
    };

} // namespace sw
