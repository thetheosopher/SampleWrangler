#pragma once

#include <JuceHeader.h>
#include "Catalog/CatalogModels.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sw
{

    /// Centre panel: search bar + results list (file table/list).
    class ResultsPanel final : public juce::Component,
                               public juce::DragAndDropContainer,
                               private juce::ListBoxModel
    {
    public:
        enum class ViewMode
        {
            Recent,
            Favorites
        };

        ResultsPanel();
        ~ResultsPanel() override = default;

        void paint(juce::Graphics &g) override;
        void resized() override;

        void setResults(std::vector<FileRecord> newResults);
        void setSearchQuery(const std::string &query);
        void setViewMode(ViewMode mode);
        ViewMode getViewMode() const noexcept;
        void setSelectedFileMetadata(std::optional<FileUserDataRecord> userData);
        void selectFirstRowIfNoneSelected();
        bool selectFile(int64_t rootId, const std::string &relativePath);
        bool selectRow(int row);
        int getSelectedRow() const noexcept;
        int getResultCount() const noexcept;
        const FileRecord *getResultAt(int row) const noexcept;
        void setDarkMode(bool enabled);

        std::function<void(const std::string &query)> onSearchQueryChanged;
        std::function<void(ViewMode mode)> onViewModeChanged;
        std::function<void(bool isFavorite)> onSelectedFileFavoriteChanged;
        std::function<void(const FileRecord &file)> onFileSelected;
        std::function<void(const FileRecord &file)> onFileActivated;
        std::function<std::optional<juce::String>(const FileRecord &file)> onResolveAbsolutePathForFile;
        std::function<std::optional<std::vector<float>>(const FileRecord &file)> onResolveWaveformCachePeaksForFile;

    private:
        void applySort();
        void updateMetadataControlState();
        const std::vector<float> *loadWaveformPeaksForFile(const FileRecord &file);
        void paintWaveformPreview(juce::Graphics &g, juce::Rectangle<int> bounds, const FileRecord &item);

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
        juce::ComboBox viewSelector;
        juce::ToggleButton favoriteToggle{"Favorite"};
        juce::ListBox resultsList;
        std::vector<FileRecord> results;
        std::unordered_map<int64_t, std::vector<float>> waveformPeaksByFileId;
        std::unordered_set<int64_t> waveformCacheMisses;
        ViewMode viewMode = ViewMode::Recent;
        bool darkModeEnabled = false;
        bool suppressControlCallbacks = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResultsPanel)
    };

} // namespace sw
