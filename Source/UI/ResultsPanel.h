#pragma once

#include <JuceHeader.h>
#include "Catalog/CatalogModels.h"

#include <deque>
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
                               private juce::ListBoxModel,
                               private juce::Timer
    {
    public:
        enum class ViewMode
        {
            Recent,
            Favorites,
            Duplicates
        };

        enum class SortMode
        {
            NameAsc,
            NameDesc,
            NewestFirst,
            OldestFirst,
            SizeLargestFirst,
            SizeSmallestFirst,
            DurationShortest,
            DurationLongest,
            BpmSlowest,
            BpmFastest,
            KeyAsc
        };

        enum class FormatFilter
        {
            Any,
            AudioOnly,
            IndexedPresetOnly,
            Wav,
            Aiff,
            Flac,
            Mp3,
            Ogg,
            Rex,
            Sfz,
            Sf2
        };

        enum class ChannelsFilter
        {
            Any,
            Mono,
            Stereo,
            MultiChannel
        };

        enum class LoopFilter
        {
            Any,
            LoopedOnly,
            OneShotOnly
        };

        enum class BpmFilter
        {
            Any,
            HasTempo,
            Below90,
            From90To110,
            From110To130,
            From130To150,
            From150Up
        };

        struct FacetFilters
        {
            FormatFilter format = FormatFilter::Any;
            ChannelsFilter channels = ChannelsFilter::Any;
            LoopFilter loop = LoopFilter::Any;
            BpmFilter bpm = BpmFilter::Any;
            std::optional<int> keyPitchClass; // 0 = C ... 11 = B

            bool operator==(const FacetFilters &) const = default;
            int activeCount() const noexcept;
        };

        enum class EmptyStateAction
        {
            None,
            AddSource,
            ClearSearch,
            ClearFilters,
            ShowAllFiles,
            ExitNearDuplicates
        };

        ResultsPanel();
        ~ResultsPanel() override = default;

        void paint(juce::Graphics &g) override;
        void resized() override;

        /// Replaces the list contents. The current selection is kept (without
        /// re-firing selection callbacks) when the same file is still present.
        void setResults(std::vector<FileRecord> newResults);
        void setSearchQuery(const std::string &query);
        std::string getSearchQuery() const;
        void focusSearchBox();
        void focusResultsList();
        /// Shows a "near-duplicates of X" banner with an exit button; nullopt hides it.
        void setNearDuplicateReference(std::optional<juce::String> referenceName);
        void setEmptyState(const juce::String &message, EmptyStateAction action);
        void setViewMode(ViewMode mode);
        ViewMode getViewMode() const noexcept;
        void setSortMode(SortMode mode);
        SortMode getSortMode() const noexcept;
        void setFacetFilters(FacetFilters filters);
        FacetFilters getFacetFilters() const noexcept;
        void setSelectedFileMetadata(std::optional<FileUserDataRecord> userData);
        void selectFirstRowIfNoneSelected();
        bool selectFile(int64_t rootId, const std::string &relativePath);
        bool selectRow(int row);
        int getSelectedRow() const noexcept;
        int getResultCount() const noexcept;
        const FileRecord *getResultAt(int row) const noexcept;
        void setDarkMode(bool enabled);

        /// Fired after a short debounce while typing, or immediately on Return.
        std::function<void(const std::string &query)> onSearchQueryChanged;
        std::function<void()> onExitNearDuplicatesRequested;
        std::function<void(EmptyStateAction action)> onEmptyStateActionRequested;
        std::function<void(ViewMode mode)> onViewModeChanged;
        std::function<void(SortMode mode)> onSortModeChanged;
        std::function<void(FacetFilters filters)> onFacetFiltersChanged;
        std::function<void(bool isFavorite)> onSelectedFileFavoriteChanged;
        std::function<void(const FileRecord &file)> onNearDuplicatesRequested;
        std::function<void(const FileRecord &file)> onRevealFileRequested;
        std::function<void(const FileRecord &file)> onFileSelected;
        std::function<void(const FileRecord &file)> onFileActivated;
        std::function<std::optional<juce::String>(const FileRecord &file)> onResolveAbsolutePathForFile;
        std::function<std::optional<std::vector<float>>(const FileRecord &file)> onResolveWaveformCachePeaksForFile;

    private:
        void applySort();
        void reapplySortPreservingSelection();
        void updateMetadataControlState();
        void updateFilterControlState();
        void updateEmptyStateVisibility();
        void fireSearchQueryChanged();
        void timerCallback() override;
        const std::vector<float> *loadWaveformPeaksForFile(const FileRecord &file);
        void paintWaveformPreview(juce::Graphics &g, juce::Rectangle<int> bounds, const FileRecord &item);

        // --- ListBoxModel overrides ---
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent &) override;
        void returnKeyPressed(int lastRowSelected) override;
        juce::String getTooltipForRow(int row) override;
        juce::var getDragSourceDescription(const juce::SparseSet<int> &rowsToDescribe) override;

        // --- DragAndDropContainer override ---
        bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails &sourceDetails,
                                                  juce::StringArray &files,
                                                  bool &canMoveFiles) override;

        juce::TextEditor searchBox;
        juce::ComboBox viewSelector;
        juce::ComboBox sortSelector;
        juce::ComboBox formatSelector;
        juce::ComboBox channelsSelector;
        juce::ComboBox loopSelector;
        juce::ComboBox bpmSelector;
        juce::ComboBox keySelector;
        juce::TextButton clearFiltersButton{"Clear Filters"};
        juce::ToggleButton favoriteToggle{"Favorite"};
        juce::TextButton nearDuplicatesButton{"Near Duplicates"};
        juce::TextButton revealButton{"Reveal"};
        juce::Label nearDuplicateBannerLabel;
        juce::TextButton exitNearDuplicatesButton{"Back to Results"};
        juce::ListBox resultsList;
        juce::Label emptyStateLabel;
        juce::TextButton emptyStateActionButton;
        EmptyStateAction emptyStateAction = EmptyStateAction::None;
        std::vector<FileRecord> results;
        std::unordered_map<int64_t, std::vector<float>> waveformPeaksByFileId;
        std::deque<int64_t> waveformPeaksInsertionOrder;
        std::unordered_set<int64_t> waveformCacheMisses;
        ViewMode viewMode = ViewMode::Recent;
        SortMode sortMode = SortMode::NameAsc;
        FacetFilters facetFilters{};
        bool nearDuplicateResultsActive = false;
        bool nearDuplicateBannerVisible = false;
        bool darkModeEnabled = false;
        bool suppressControlCallbacks = false;
        bool suppressSelectionCallbacks = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResultsPanel)
    };

} // namespace sw
