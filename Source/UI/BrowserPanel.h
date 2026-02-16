#pragma once

#include <JuceHeader.h>
#include "Catalog/CatalogModels.h"

#include <functional>
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

        void setRoots(std::vector<RootRecord> roots);
        void setScanStatus(const juce::String &statusText);
        void setScanInProgress(bool inProgress);

        std::function<void()> onAddRootRequested;
        std::function<void()> onRescanAllRequested;
        std::function<void()> onCancelScanRequested;

    private:
        juce::TextButton addRootButton{"Add Root..."};
        juce::TextButton rescanAllButton{"Rescan All"};
        juce::TextButton cancelScanButton{"Cancel Scan"};
        std::vector<RootRecord> roots;
        juce::String scanStatus{"Idle"};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowserPanel)
    };

} // namespace sw
