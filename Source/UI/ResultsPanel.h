#pragma once

#include <JuceHeader.h>
#include "Catalog/CatalogModels.h"

#include <functional>
#include <optional>
#include <vector>

namespace sw
{

    /// Centre panel: search bar + results list (file table/list).
    class ResultsPanel final : public juce::Component,
                               public juce::DragAndDropContainer,
                               private juce::ListBoxModel
    {
    public:
        ResultsPanel();
        ~ResultsPanel() override = default;

        void paint(juce::Graphics &g) override;
        void resized() override;

        void setResults(std::vector<FileRecord> newResults);
        void selectFirstRowIfNoneSelected();
        bool selectFile(int64_t rootId, const std::string &relativePath);
        bool selectRow(int row);
        int getSelectedRow() const noexcept;
        int getResultCount() const noexcept;
        const FileRecord *getResultAt(int row) const noexcept;
        void setDarkMode(bool enabled);

        std::function<void(const std::string &query)> onSearchQueryChanged;
        std::function<void(const FileRecord &file)> onFileSelected;
        std::function<void(const FileRecord &file)> onFileActivated;
        std::function<std::optional<juce::String>(const FileRecord &file)> onResolveAbsolutePathForFile;

    private:
        enum class SortMode
        {
            Name,
            Path
        };

        void applySort();

        // --- ListBoxModel overrides ---
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent &) override;
        juce::var getDragSourceDescription(const juce::SparseSet<int> &rowsToDescribe) override;

        // --- DragAndDropContainer override ---
        bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails &sourceDetails,
                                                  juce::StringArray &files,
                                                  bool &canMoveFiles) override;

        juce::TextEditor searchBox;
        juce::ComboBox sortSelector;
        juce::ListBox resultsList;
        std::vector<FileRecord> results;
        SortMode sortMode = SortMode::Name;
        bool darkModeEnabled = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResultsPanel)
    };

} // namespace sw
