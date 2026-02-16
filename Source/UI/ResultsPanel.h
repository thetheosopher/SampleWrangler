#pragma once

#include <JuceHeader.h>
#include "Catalog/CatalogModels.h"

#include <functional>
#include <vector>

namespace sw
{

    /// Centre panel: search bar + results list (file table/list).
    class ResultsPanel final : public juce::Component, private juce::ListBoxModel
    {
    public:
        ResultsPanel();
        ~ResultsPanel() override = default;

        void paint(juce::Graphics &g) override;
        void resized() override;

        void setResults(std::vector<FileRecord> newResults);
        void selectFirstRowIfNoneSelected();
        bool selectFile(int64_t rootId, const std::string &relativePath);

        std::function<void(const std::string &query)> onSearchQueryChanged;
        std::function<void(const FileRecord &file)> onFileSelected;
        std::function<void(const FileRecord &file)> onFileActivated;

    private:
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent &) override;

        juce::TextEditor searchBox;
        juce::ListBox resultsList;
        std::vector<FileRecord> results;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResultsPanel)
    };

} // namespace sw
