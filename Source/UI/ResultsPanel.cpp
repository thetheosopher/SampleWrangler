#include "ResultsPanel.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kViewRecentId = 1;
    constexpr int kViewFavoritesId = 2;
    constexpr int kViewDuplicatesId = 3;
    constexpr int kSortNameAscId = 1;
    constexpr int kSortNameDescId = 2;
    constexpr int kSortNewestId = 3;
    constexpr int kSortOldestId = 4;
    constexpr int kSortSizeLargestId = 5;
    constexpr int kSortSizeSmallestId = 6;
    constexpr int kFormatAnyId = 1;
    constexpr int kFormatAudioOnlyId = 2;
    constexpr int kFormatIndexedPresetId = 3;
    constexpr int kFormatWavId = 4;
    constexpr int kFormatAiffId = 5;
    constexpr int kFormatFlacId = 6;
    constexpr int kFormatMp3Id = 7;
    constexpr int kFormatOggId = 8;
    constexpr int kFormatRexId = 9;
    constexpr int kFormatSfzId = 10;
    constexpr int kChannelsAnyId = 1;
    constexpr int kChannelsMonoId = 2;
    constexpr int kChannelsStereoId = 3;
    constexpr int kChannelsMultiId = 4;
    constexpr int kLoopAnyId = 1;
    constexpr int kLoopLoopedOnlyId = 2;
    constexpr int kLoopOneShotOnlyId = 3;
    constexpr int kWaveformColumnWidth = 120;

    int selectorIdForViewMode(sw::ResultsPanel::ViewMode mode)
    {
        switch (mode)
        {
        case sw::ResultsPanel::ViewMode::Duplicates:
            return kViewDuplicatesId;
        case sw::ResultsPanel::ViewMode::Favorites:
            return kViewFavoritesId;
        case sw::ResultsPanel::ViewMode::Recent:
        default:
            return kViewRecentId;
        }
    }

    sw::ResultsPanel::ViewMode viewModeForSelectorId(int id)
    {
        switch (id)
        {
        case kViewDuplicatesId:
            return sw::ResultsPanel::ViewMode::Duplicates;
        case kViewFavoritesId:
            return sw::ResultsPanel::ViewMode::Favorites;
        case kViewRecentId:
        default:
            return sw::ResultsPanel::ViewMode::Recent;
        }
    }

    int selectorIdForSortMode(sw::ResultsPanel::SortMode mode)
    {
        switch (mode)
        {
        case sw::ResultsPanel::SortMode::NameDesc:
            return kSortNameDescId;
        case sw::ResultsPanel::SortMode::NewestFirst:
            return kSortNewestId;
        case sw::ResultsPanel::SortMode::OldestFirst:
            return kSortOldestId;
        case sw::ResultsPanel::SortMode::SizeLargestFirst:
            return kSortSizeLargestId;
        case sw::ResultsPanel::SortMode::SizeSmallestFirst:
            return kSortSizeSmallestId;
        case sw::ResultsPanel::SortMode::NameAsc:
        default:
            return kSortNameAscId;
        }
    }

    sw::ResultsPanel::SortMode sortModeForSelectorId(int id)
    {
        switch (id)
        {
        case kSortNameDescId:
            return sw::ResultsPanel::SortMode::NameDesc;
        case kSortNewestId:
            return sw::ResultsPanel::SortMode::NewestFirst;
        case kSortOldestId:
            return sw::ResultsPanel::SortMode::OldestFirst;
        case kSortSizeLargestId:
            return sw::ResultsPanel::SortMode::SizeLargestFirst;
        case kSortSizeSmallestId:
            return sw::ResultsPanel::SortMode::SizeSmallestFirst;
        case kSortNameAscId:
        default:
            return sw::ResultsPanel::SortMode::NameAsc;
        }
    }

    int selectorIdForFormatFilter(sw::ResultsPanel::FormatFilter filter)
    {
        switch (filter)
        {
        case sw::ResultsPanel::FormatFilter::AudioOnly:
            return kFormatAudioOnlyId;
        case sw::ResultsPanel::FormatFilter::IndexedPresetOnly:
            return kFormatIndexedPresetId;
        case sw::ResultsPanel::FormatFilter::Wav:
            return kFormatWavId;
        case sw::ResultsPanel::FormatFilter::Aiff:
            return kFormatAiffId;
        case sw::ResultsPanel::FormatFilter::Flac:
            return kFormatFlacId;
        case sw::ResultsPanel::FormatFilter::Mp3:
            return kFormatMp3Id;
        case sw::ResultsPanel::FormatFilter::Ogg:
            return kFormatOggId;
        case sw::ResultsPanel::FormatFilter::Rex:
            return kFormatRexId;
        case sw::ResultsPanel::FormatFilter::Sfz:
            return kFormatSfzId;
        case sw::ResultsPanel::FormatFilter::Any:
        default:
            return kFormatAnyId;
        }
    }

    sw::ResultsPanel::FormatFilter formatFilterForSelectorId(int id)
    {
        switch (id)
        {
        case kFormatAudioOnlyId:
            return sw::ResultsPanel::FormatFilter::AudioOnly;
        case kFormatIndexedPresetId:
            return sw::ResultsPanel::FormatFilter::IndexedPresetOnly;
        case kFormatWavId:
            return sw::ResultsPanel::FormatFilter::Wav;
        case kFormatAiffId:
            return sw::ResultsPanel::FormatFilter::Aiff;
        case kFormatFlacId:
            return sw::ResultsPanel::FormatFilter::Flac;
        case kFormatMp3Id:
            return sw::ResultsPanel::FormatFilter::Mp3;
        case kFormatOggId:
            return sw::ResultsPanel::FormatFilter::Ogg;
        case kFormatRexId:
            return sw::ResultsPanel::FormatFilter::Rex;
        case kFormatSfzId:
            return sw::ResultsPanel::FormatFilter::Sfz;
        case kFormatAnyId:
        default:
            return sw::ResultsPanel::FormatFilter::Any;
        }
    }

    int selectorIdForChannelsFilter(sw::ResultsPanel::ChannelsFilter filter)
    {
        switch (filter)
        {
        case sw::ResultsPanel::ChannelsFilter::Mono:
            return kChannelsMonoId;
        case sw::ResultsPanel::ChannelsFilter::Stereo:
            return kChannelsStereoId;
        case sw::ResultsPanel::ChannelsFilter::MultiChannel:
            return kChannelsMultiId;
        case sw::ResultsPanel::ChannelsFilter::Any:
        default:
            return kChannelsAnyId;
        }
    }

    sw::ResultsPanel::ChannelsFilter channelsFilterForSelectorId(int id)
    {
        switch (id)
        {
        case kChannelsMonoId:
            return sw::ResultsPanel::ChannelsFilter::Mono;
        case kChannelsStereoId:
            return sw::ResultsPanel::ChannelsFilter::Stereo;
        case kChannelsMultiId:
            return sw::ResultsPanel::ChannelsFilter::MultiChannel;
        case kChannelsAnyId:
        default:
            return sw::ResultsPanel::ChannelsFilter::Any;
        }
    }

    int selectorIdForLoopFilter(sw::ResultsPanel::LoopFilter filter)
    {
        switch (filter)
        {
        case sw::ResultsPanel::LoopFilter::LoopedOnly:
            return kLoopLoopedOnlyId;
        case sw::ResultsPanel::LoopFilter::OneShotOnly:
            return kLoopOneShotOnlyId;
        case sw::ResultsPanel::LoopFilter::Any:
        default:
            return kLoopAnyId;
        }
    }

    sw::ResultsPanel::LoopFilter loopFilterForSelectorId(int id)
    {
        switch (id)
        {
        case kLoopLoopedOnlyId:
            return sw::ResultsPanel::LoopFilter::LoopedOnly;
        case kLoopOneShotOnlyId:
            return sw::ResultsPanel::LoopFilter::OneShotOnly;
        case kLoopAnyId:
        default:
            return sw::ResultsPanel::LoopFilter::Any;
        }
    }

    bool isAcidizedLoop(const sw::FileRecord &item)
    {
        return item.loopType.has_value() && *item.loopType == "acidized";
    }

    bool isAppleLoop(const sw::FileRecord &item)
    {
        return item.loopType.has_value() && *item.loopType == "apple-loop";
    }

    juce::String formatDuration(const std::optional<double> &durationSec)
    {
        if (!durationSec.has_value() || *durationSec < 0.0)
            return "--";

        const int totalMs = static_cast<int>(*durationSec * 1000.0);
        const int minutes = totalMs / 60000;
        const int seconds = (totalMs / 1000) % 60;
        const int ms = totalMs % 1000;
        return juce::String::formatted("%02d:%02d.%03d", minutes, seconds, ms);
    }

    juce::String midiNoteToName(int note)
    {
        static constexpr const char *kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        if (note < 0 || note > 127)
            return "--";

        const int octave = (note / 12) - 1;
        return juce::String(kNames[note % 12]) + juce::String(octave);
    }

    juce::String formatMidiRange(const std::optional<int> &low, const std::optional<int> &high)
    {
        if (!low.has_value() && !high.has_value())
            return "--";

        const int resolvedLow = low.value_or(*high);
        const int resolvedHigh = high.value_or(*low);
        if (resolvedLow == resolvedHigh)
            return midiNoteToName(resolvedLow);

        return midiNoteToName(resolvedLow) + "-" + midiNoteToName(resolvedHigh);
    }

    juce::String formatIntegerRange(const std::optional<int> &low, const std::optional<int> &high)
    {
        if (!low.has_value() && !high.has_value())
            return "--";

        const int resolvedLow = low.value_or(*high);
        const int resolvedHigh = high.value_or(*low);
        if (resolvedLow == resolvedHigh)
            return juce::String(resolvedLow);

        return juce::String(resolvedLow) + "-" + juce::String(resolvedHigh);
    }

    struct MetadataToken
    {
        juce::String text;
        bool isValue = false;
    };

    void appendMetadataPair(std::vector<MetadataToken> &tokens,
                            const juce::String &label,
                            const juce::String &value)
    {
        if (!tokens.empty())
            tokens.push_back({"  ", false});

        tokens.push_back({label, false});
        tokens.push_back({value, true});
    }

    std::vector<MetadataToken> buildMetadataTokens(const sw::FileRecord &item)
    {
        auto valueOrDash = [](const auto &opt, const juce::String &suffix = juce::String())
        {
            if (!opt.has_value())
                return juce::String("--");
            return juce::String(*opt) + suffix;
        };

        std::vector<MetadataToken> tokens;
        tokens.reserve(32);

        const juce::String sampleRate = valueOrDash(item.sampleRate, " Hz");
        const juce::String channels = valueOrDash(item.channels);
        const juce::String bitDepth = valueOrDash(item.bitDepth);
        const juce::String bitrate = valueOrDash(item.bitrateKbps, " kbps");
        const juce::String totalSamples = item.totalSamples.has_value() ? juce::String(*item.totalSamples) : juce::String("--");
        const juce::String codec = item.codec.has_value() ? juce::String(*item.codec) : juce::String("--");

        appendMetadataPair(tokens, "SR: ", sampleRate);
        appendMetadataPair(tokens, "Ch: ", channels);
        appendMetadataPair(tokens, "Bit Depth: ", bitDepth);
        appendMetadataPair(tokens, "Bitrate: ", bitrate);
        appendMetadataPair(tokens, "Duration: ", formatDuration(item.durationSec));
        appendMetadataPair(tokens, "Samples: ", totalSamples);
        appendMetadataPair(tokens, "Codec/Enc: ", codec);

        if (item.presetName.has_value())
            appendMetadataPair(tokens, "Preset: ", juce::String(*item.presetName));

        if (item.zoneCount.has_value())
            appendMetadataPair(tokens, "Zones: ", juce::String(*item.zoneCount));

        if (item.keyLow.has_value() || item.keyHigh.has_value())
            appendMetadataPair(tokens, "Keys: ", formatMidiRange(item.keyLow, item.keyHigh));

        if (item.velocityLow.has_value() || item.velocityHigh.has_value())
            appendMetadataPair(tokens, "Vel: ", formatIntegerRange(item.velocityLow, item.velocityHigh));

        if (isAcidizedLoop(item) || isAppleLoop(item))
        {
            const juce::String acidRoot = item.acidRootNote.has_value() ? midiNoteToName(*item.acidRootNote) : juce::String("--");
            const juce::String acidBeats = item.acidBeats.has_value() ? juce::String(*item.acidBeats) : juce::String("--");
            const juce::String acidBpm = item.bpm.has_value() ? juce::String(*item.bpm, 2) : juce::String("--");
            const juce::String loopStart = item.loopStartSample.has_value() ? juce::String(*item.loopStartSample) : juce::String("--");
            const juce::String loopEnd = item.loopEndSample.has_value() ? juce::String(*item.loopEndSample) : juce::String("--");

            appendMetadataPair(tokens,
                               isAcidizedLoop(item) ? "Acid: " : "Apple Loop: ",
                               "yes");
            appendMetadataPair(tokens, "Root: ", acidRoot);
            if (isAcidizedLoop(item))
                appendMetadataPair(tokens, "Beats: ", acidBeats);
            if (item.bpm.has_value())
                appendMetadataPair(tokens, "Tempo: ", acidBpm);
            appendMetadataPair(tokens, "Loop: ", loopStart + "-" + loopEnd);
        }

        return tokens;
    }

    void drawMetadataTokens(juce::Graphics &g,
                            juce::Rectangle<int> bounds,
                            const std::vector<MetadataToken> &tokens,
                            juce::Colour labelColour,
                            juce::Colour valueColour)
    {
        g.saveState();
        g.reduceClipRegion(bounds);

        float x = static_cast<float>(bounds.getX());
        const float right = static_cast<float>(bounds.getRight());

        const auto measureTextWidth = [&g](const juce::String &text)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText(g.getCurrentFont(), text, 0.0f, 0.0f);
            return glyphs.getBoundingBox(0, glyphs.getNumGlyphs(), true).getWidth();
        };

        for (const auto &token : tokens)
        {
            const float tokenWidth = measureTextWidth(token.text);
            if (x >= right)
                break;

            if (x + tokenWidth > right)
            {
                g.setColour(token.isValue ? valueColour : labelColour);
                g.drawFittedText(token.text,
                                 static_cast<int>(x),
                                 bounds.getY(),
                                 static_cast<int>(right - x),
                                 bounds.getHeight(),
                                 juce::Justification::centredLeft,
                                 1);
                break;
            }

            g.setColour(token.isValue ? valueColour : labelColour);
            g.drawText(token.text,
                       static_cast<int>(x),
                       bounds.getY(),
                       static_cast<int>(tokenWidth + 1.0f),
                       bounds.getHeight(),
                       juce::Justification::centredLeft,
                       false);
            x += tokenWidth;
        }

        g.restoreState();
    }
}

namespace sw
{

    ResultsPanel::ResultsPanel()
    {
        searchBox.setTextToShowWhenEmpty("Search samples...", juce::Colours::grey);
        searchBox.onTextChange = [this]
        {
            if (onSearchQueryChanged)
                onSearchQueryChanged(searchBox.getText().toStdString());
        };
        addAndMakeVisible(searchBox);

        viewSelector.addItem("All Files", kViewRecentId);
        viewSelector.addItem("Favorites", kViewFavoritesId);
        viewSelector.addItem("Duplicates", kViewDuplicatesId);
        viewSelector.setSelectedId(kViewRecentId, juce::dontSendNotification);
        viewSelector.onChange = [this]
        {
            if (suppressControlCallbacks)
                return;

            viewMode = viewModeForSelectorId(viewSelector.getSelectedId());
            updateMetadataControlState();
            if (onViewModeChanged)
                onViewModeChanged(viewMode);
        };
        addAndMakeVisible(viewSelector);

        sortSelector.addItem("Name A-Z", kSortNameAscId);
        sortSelector.addItem("Name Z-A", kSortNameDescId);
        sortSelector.addItem("Date Newest", kSortNewestId);
        sortSelector.addItem("Date Oldest", kSortOldestId);
        sortSelector.addItem("Size Largest", kSortSizeLargestId);
        sortSelector.addItem("Size Smallest", kSortSizeSmallestId);
        sortSelector.setSelectedId(kSortNameAscId, juce::dontSendNotification);
        sortSelector.onChange = [this]
        {
            if (suppressControlCallbacks)
                return;

            const int selectedRow = resultsList.getSelectedRow();
            const int64_t selectedFileId = (selectedRow >= 0 && selectedRow < static_cast<int>(results.size()))
                                               ? results[static_cast<size_t>(selectedRow)].id
                                               : -1;

            sortMode = sortModeForSelectorId(sortSelector.getSelectedId());
            applySort();
            resultsList.updateContent();

            if (selectedFileId >= 0)
            {
                const auto it = std::find_if(results.begin(), results.end(), [selectedFileId](const FileRecord &file)
                                             { return file.id == selectedFileId; });
                if (it != results.end())
                    resultsList.selectRow(static_cast<int>(std::distance(results.begin(), it)));
            }

            if (onSortModeChanged)
                onSortModeChanged(sortMode);

            repaint();
        };
        addAndMakeVisible(sortSelector);

        const auto facetChanged = [this]
        {
            if (suppressControlCallbacks)
                return;

            facetFilters.format = formatFilterForSelectorId(formatSelector.getSelectedId());
            facetFilters.channels = channelsFilterForSelectorId(channelsSelector.getSelectedId());
            facetFilters.loop = loopFilterForSelectorId(loopSelector.getSelectedId());

            if (onFacetFiltersChanged)
                onFacetFiltersChanged(facetFilters);
        };

        formatSelector.addItem("Format: Any", kFormatAnyId);
        formatSelector.addItem("Format: Audio", kFormatAudioOnlyId);
        formatSelector.addItem("Format: Indexed", kFormatIndexedPresetId);
        formatSelector.addItem("WAV", kFormatWavId);
        formatSelector.addItem("AIFF", kFormatAiffId);
        formatSelector.addItem("FLAC", kFormatFlacId);
        formatSelector.addItem("MP3", kFormatMp3Id);
        formatSelector.addItem("OGG", kFormatOggId);
        formatSelector.addItem("REX/RX2", kFormatRexId);
        formatSelector.addItem("SFZ", kFormatSfzId);
        formatSelector.setSelectedId(kFormatAnyId, juce::dontSendNotification);
        formatSelector.onChange = facetChanged;
        addAndMakeVisible(formatSelector);

        channelsSelector.addItem("Channels: Any", kChannelsAnyId);
        channelsSelector.addItem("Mono", kChannelsMonoId);
        channelsSelector.addItem("Stereo", kChannelsStereoId);
        channelsSelector.addItem("Multi", kChannelsMultiId);
        channelsSelector.setSelectedId(kChannelsAnyId, juce::dontSendNotification);
        channelsSelector.onChange = facetChanged;
        addAndMakeVisible(channelsSelector);

        loopSelector.addItem("Loop: Any", kLoopAnyId);
        loopSelector.addItem("Looped", kLoopLoopedOnlyId);
        loopSelector.addItem("One-shot", kLoopOneShotOnlyId);
        loopSelector.setSelectedId(kLoopAnyId, juce::dontSendNotification);
        loopSelector.onChange = facetChanged;
        addAndMakeVisible(loopSelector);

        favoriteToggle.onClick = [this]
        {
            if (suppressControlCallbacks)
                return;

            if (onSelectedFileFavoriteChanged)
                onSelectedFileFavoriteChanged(favoriteToggle.getToggleState());
        };
        addAndMakeVisible(favoriteToggle);

        resultsList.setModel(this);
        resultsList.setRowHeight(40);
        addAndMakeVisible(resultsList);

        setDarkMode(true);
        updateMetadataControlState();
    }

    void ResultsPanel::paint(juce::Graphics &g)
    {
        g.fillAll(darkModeEnabled ? juce::Colour(0xff1e1e1e) : juce::Colour(0xfffafafa));
    }

    void ResultsPanel::resized()
    {
        auto area = getLocalBounds().reduced(4);
        auto topRow = area.removeFromTop(28);
        auto metadataRow = area.removeFromTop(28);
        constexpr int viewWidth = 110;
        constexpr int sortWidth = 130;
        constexpr int formatWidth = 140;
        constexpr int channelsWidth = 120;
        constexpr int loopWidth = 120;
        constexpr int favoriteWidth = 88;
        constexpr int controlGap = 6;

        viewSelector.setBounds(topRow.removeFromRight(viewWidth));
        topRow.removeFromRight(controlGap);
        sortSelector.setBounds(topRow.removeFromRight(sortWidth));
        topRow.removeFromRight(controlGap);
        searchBox.setBounds(topRow);

        favoriteToggle.setBounds(metadataRow.removeFromRight(favoriteWidth));
        metadataRow.removeFromRight(controlGap);
        loopSelector.setBounds(metadataRow.removeFromLeft(loopWidth));
        metadataRow.removeFromLeft(controlGap);
        channelsSelector.setBounds(metadataRow.removeFromLeft(channelsWidth));
        metadataRow.removeFromLeft(controlGap);
        formatSelector.setBounds(metadataRow.removeFromLeft(formatWidth));

        area.removeFromTop(4);
        resultsList.setBounds(area);
    }

    void ResultsPanel::setResults(std::vector<FileRecord> newResults)
    {
        // Deselect before updating to avoid stale selection indices
        resultsList.deselectAllRows();
        waveformCacheMisses.clear();

        results = std::move(newResults);
        applySort();

        resultsList.updateContent();
        updateMetadataControlState();
        repaint();
    }

    void ResultsPanel::setSearchQuery(const std::string &query)
    {
        suppressControlCallbacks = true;
        searchBox.setText(query, juce::dontSendNotification);
        suppressControlCallbacks = false;
    }

    void ResultsPanel::setViewMode(ViewMode mode)
    {
        viewMode = mode;
        suppressControlCallbacks = true;
        viewSelector.setSelectedId(selectorIdForViewMode(mode), juce::dontSendNotification);
        suppressControlCallbacks = false;
        updateMetadataControlState();
    }

    ResultsPanel::ViewMode ResultsPanel::getViewMode() const noexcept
    {
        return viewMode;
    }

    void ResultsPanel::setSortMode(SortMode mode)
    {
        sortMode = mode;
        suppressControlCallbacks = true;
        sortSelector.setSelectedId(selectorIdForSortMode(mode), juce::dontSendNotification);
        suppressControlCallbacks = false;
        applySort();
        resultsList.updateContent();
        repaint();
    }

    ResultsPanel::SortMode ResultsPanel::getSortMode() const noexcept
    {
        return sortMode;
    }

    void ResultsPanel::setFacetFilters(FacetFilters filters)
    {
        facetFilters = filters;
        suppressControlCallbacks = true;
        formatSelector.setSelectedId(selectorIdForFormatFilter(facetFilters.format), juce::dontSendNotification);
        channelsSelector.setSelectedId(selectorIdForChannelsFilter(facetFilters.channels), juce::dontSendNotification);
        loopSelector.setSelectedId(selectorIdForLoopFilter(facetFilters.loop), juce::dontSendNotification);
        suppressControlCallbacks = false;
    }

    ResultsPanel::FacetFilters ResultsPanel::getFacetFilters() const noexcept
    {
        return facetFilters;
    }

    void ResultsPanel::setSelectedFileMetadata(std::optional<FileUserDataRecord> userData)
    {
        suppressControlCallbacks = true;
        favoriteToggle.setToggleState(userData.has_value() && userData->isFavorite, juce::dontSendNotification);
        suppressControlCallbacks = false;
        updateMetadataControlState();
    }

    void ResultsPanel::applySort()
    {
        auto compareNameAsc = [](const FileRecord &a, const FileRecord &b)
        {
            const auto nameCmp = juce::String(a.filename).compareIgnoreCase(juce::String(b.filename));
            if (nameCmp != 0)
                return nameCmp < 0;

            const auto relCmp = juce::String(a.relativePath).compareIgnoreCase(juce::String(b.relativePath));
            if (relCmp != 0)
                return relCmp < 0;

            return a.id < b.id;
        };

        auto compareNameDesc = [compareNameAsc](const FileRecord &a, const FileRecord &b)
        {
            if (compareNameAsc(a, b))
                return false;
            if (compareNameAsc(b, a))
                return true;
            return false;
        };

        auto compareNewestFirst = [compareNameAsc](const FileRecord &a, const FileRecord &b)
        {
            if (a.modifiedTime != b.modifiedTime)
                return a.modifiedTime > b.modifiedTime;

            return compareNameAsc(a, b);
        };

        auto compareOldestFirst = [compareNameAsc](const FileRecord &a, const FileRecord &b)
        {
            if (a.modifiedTime != b.modifiedTime)
                return a.modifiedTime < b.modifiedTime;

            return compareNameAsc(a, b);
        };

        auto compareSizeLargestFirst = [compareNameAsc](const FileRecord &a, const FileRecord &b)
        {
            if (a.sizeBytes != b.sizeBytes)
                return a.sizeBytes > b.sizeBytes;

            return compareNameAsc(a, b);
        };

        auto compareSizeSmallestFirst = [compareNameAsc](const FileRecord &a, const FileRecord &b)
        {
            if (a.sizeBytes != b.sizeBytes)
                return a.sizeBytes < b.sizeBytes;

            return compareNameAsc(a, b);
        };

        switch (sortMode)
        {
        case SortMode::NameDesc:
            std::sort(results.begin(), results.end(), compareNameDesc);
            break;
        case SortMode::NewestFirst:
            std::sort(results.begin(), results.end(), compareNewestFirst);
            break;
        case SortMode::OldestFirst:
            std::sort(results.begin(), results.end(), compareOldestFirst);
            break;
        case SortMode::SizeLargestFirst:
            std::sort(results.begin(), results.end(), compareSizeLargestFirst);
            break;
        case SortMode::SizeSmallestFirst:
            std::sort(results.begin(), results.end(), compareSizeSmallestFirst);
            break;
        case SortMode::NameAsc:
        default:
            std::sort(results.begin(), results.end(), compareNameAsc);
            break;
        }
    }

    const std::vector<float> *ResultsPanel::loadWaveformPeaksForFile(const FileRecord &file)
    {
        if (auto existing = waveformPeaksByFileId.find(file.id); existing != waveformPeaksByFileId.end())
            return &existing->second;

        if (waveformCacheMisses.find(file.id) != waveformCacheMisses.end())
            return nullptr;

        if (onResolveWaveformCachePeaksForFile != nullptr)
        {
            if (const auto resolved = onResolveWaveformCachePeaksForFile(file); resolved.has_value() && !resolved->empty())
            {
                waveformPeaksByFileId[file.id] = *resolved;
                return &waveformPeaksByFileId[file.id];
            }
        }

        waveformCacheMisses.insert(file.id);
        return nullptr;
    }

    void ResultsPanel::paintWaveformPreview(juce::Graphics &g, juce::Rectangle<int> bounds, const FileRecord &item)
    {
        const auto backgroundColour = darkModeEnabled ? juce::Colour(0xff1b2434) : juce::Colour(0xffdfe9f9);
        const auto outlineColour = darkModeEnabled ? juce::Colour(0xff3b4a61) : juce::Colour(0xffa8bfdc);
        const auto waveformColour = darkModeEnabled ? juce::Colour(0xff66e0ff) : juce::Colour(0xff1769aa);
        const auto placeholderColour = darkModeEnabled ? juce::Colour(0xff7b8da6) : juce::Colour(0xff7087a1);

        g.setColour(backgroundColour);
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

        g.setColour(outlineColour);
        g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 1.0f);

        const auto *peaks = loadWaveformPeaksForFile(item);
        if (peaks == nullptr || peaks->empty())
        {
            g.setColour(placeholderColour);
            g.setFont(10.0f);
            g.drawFittedText("No cache", bounds.reduced(4), juce::Justification::centred, 1);
            return;
        }

        const auto content = bounds.reduced(3, 3);
        const float centreY = static_cast<float>(content.getCentreY());
        const float halfHeight = static_cast<float>(content.getHeight()) * 0.48f;
        const int width = juce::jmax(1, content.getWidth());
        const int peakCount = static_cast<int>(peaks->size());
        const float widthScale = (width > 1) ? static_cast<float>(width - 1) : 1.0f;

        g.setColour(waveformColour.withAlpha(0.18f));
        g.drawHorizontalLine(static_cast<int>(std::round(centreY)), static_cast<float>(content.getX()), static_cast<float>(content.getRight()));

        g.setColour(waveformColour);
        for (int x = 0; x < width; ++x)
        {
            const int peakIndex = juce::jlimit(0,
                                               peakCount - 1,
                                               static_cast<int>((static_cast<float>(x) / widthScale) * static_cast<float>(peakCount - 1)));
            const float amplitude = juce::jlimit(0.0f, 1.0f, (*peaks)[static_cast<size_t>(peakIndex)]);
            const float drawHalf = juce::jmax(0.75f, amplitude * halfHeight);
            const int drawX = content.getX() + x;

            g.drawVerticalLine(drawX, centreY - drawHalf, centreY + drawHalf);
        }
    }

    void ResultsPanel::selectFirstRowIfNoneSelected()
    {
        if (!results.empty() && resultsList.getSelectedRow() < 0)
            resultsList.selectRow(0);
    }

    bool ResultsPanel::selectFile(int64_t rootId, const std::string &relativePath)
    {
        for (int i = 0; i < static_cast<int>(results.size()); ++i)
        {
            const auto &item = results[static_cast<size_t>(i)];
            if (item.rootId == rootId && item.relativePath == relativePath)
            {
                resultsList.selectRow(i);
                return true;
            }
        }

        return false;
    }

    bool ResultsPanel::selectRow(int row)
    {
        if (row < 0 || row >= static_cast<int>(results.size()))
            return false;

        resultsList.selectRow(row);
        return true;
    }

    int ResultsPanel::getSelectedRow() const noexcept
    {
        return resultsList.getSelectedRow();
    }

    int ResultsPanel::getResultCount() const noexcept
    {
        return static_cast<int>(results.size());
    }

    const FileRecord *ResultsPanel::getResultAt(int row) const noexcept
    {
        if (row < 0 || row >= static_cast<int>(results.size()))
            return nullptr;

        return &results[static_cast<size_t>(row)];
    }

    void ResultsPanel::setDarkMode(bool enabled)
    {
        if (darkModeEnabled == enabled)
            return;

        darkModeEnabled = enabled;

        const auto textColour = darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020);
        const auto editorBg = darkModeEnabled ? juce::Colour(0xff2b2b2b) : juce::Colour(0xffffffff);
        const auto outline = darkModeEnabled ? juce::Colour(0xff4d4d4d) : juce::Colour(0xffb8b8b8);
        const auto placeholder = darkModeEnabled ? juce::Colours::grey : juce::Colour(0xff7a7a7a);
        const auto comboBg = darkModeEnabled ? juce::Colour(0xff2b2b2b) : juce::Colour(0xffffffff);

        searchBox.setColour(juce::TextEditor::textColourId, textColour);
        searchBox.setColour(juce::TextEditor::backgroundColourId, editorBg);
        searchBox.setColour(juce::TextEditor::outlineColourId, outline);
        searchBox.setColour(juce::TextEditor::focusedOutlineColourId, darkModeEnabled ? juce::Colour(0xff6b9bc8) : juce::Colour(0xff2f6fa8));
        searchBox.setTextToShowWhenEmpty("Search samples...", placeholder);

        viewSelector.setColour(juce::ComboBox::textColourId, textColour);
        viewSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        viewSelector.setColour(juce::ComboBox::outlineColourId, outline);
        viewSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        sortSelector.setColour(juce::ComboBox::textColourId, textColour);
        sortSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        sortSelector.setColour(juce::ComboBox::outlineColourId, outline);
        sortSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        formatSelector.setColour(juce::ComboBox::textColourId, textColour);
        formatSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        formatSelector.setColour(juce::ComboBox::outlineColourId, outline);
        formatSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        channelsSelector.setColour(juce::ComboBox::textColourId, textColour);
        channelsSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        channelsSelector.setColour(juce::ComboBox::outlineColourId, outline);
        channelsSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        loopSelector.setColour(juce::ComboBox::textColourId, textColour);
        loopSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        loopSelector.setColour(juce::ComboBox::outlineColourId, outline);
        loopSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        favoriteToggle.setColour(juce::ToggleButton::textColourId, textColour);

        resultsList.setColour(juce::ListBox::backgroundColourId, darkModeEnabled ? juce::Colour(0xff1e1e1e) : juce::Colour(0xfffafafa));

        repaint();
    }

    void ResultsPanel::updateMetadataControlState()
    {
        const bool hasSelectedFile = getSelectedRow() >= 0;
        favoriteToggle.setEnabled(hasSelectedFile);
    }

    int ResultsPanel::getNumRows()
    {
        return static_cast<int>(results.size());
    }

    juce::var ResultsPanel::getDragSourceDescription(const juce::SparseSet<int> &rowsToDescribe)
    {
        if (rowsToDescribe.isEmpty() || onResolveAbsolutePathForFile == nullptr)
            return {};

        const int row = rowsToDescribe[0];
        const auto *file = getResultAt(row);
        if (file == nullptr)
            return {};

        const auto absolutePath = onResolveAbsolutePathForFile(*file);
        if (!absolutePath.has_value() || absolutePath->isEmpty())
            return {};

        const juce::File sourceFile(*absolutePath);
        if (!sourceFile.existsAsFile())
            return {};

        return sourceFile.getFullPathName();
    }

    bool ResultsPanel::shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails &sourceDetails,
                                                            juce::StringArray &files,
                                                            bool &canMoveFiles)
    {
        const auto filePath = sourceDetails.description.toString();
        if (filePath.isEmpty())
            return false;

        const juce::File file(filePath);
        if (!file.existsAsFile())
            return false;

        files.add(file.getFullPathName());
        canMoveFiles = false;
        return true;
    }

    void ResultsPanel::paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected)
    {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(results.size()))
            return;

        if (rowIsSelected)
            g.fillAll(darkModeEnabled ? juce::Colour(0xff2a3d55) : juce::Colour(0xffd8e8f8));

        const auto &item = results[static_cast<size_t>(rowNumber)];

        auto rowArea = juce::Rectangle<int>(0, 0, width, height).reduced(8, 4);
        auto waveformArea = rowArea.removeFromLeft(kWaveformColumnWidth);
        rowArea.removeFromLeft(8);

        paintWaveformPreview(g, waveformArea, item);

        auto titleRow = rowArea.removeFromTop(18);
        auto metadataRow = rowArea.removeFromTop(18);
        auto filenameArea = titleRow.removeFromLeft(titleRow.getWidth() / 2);
        auto pathArea = titleRow;

        g.setColour(darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020));
        g.setFont(13.0f);
        g.drawText(juce::String(item.filename), filenameArea, juce::Justification::centredLeft);

        g.setColour(darkModeEnabled ? juce::Colours::lightgrey : juce::Colour(0xff4a4a4a));
        const juce::String rightText = juce::String(item.relativePath) + (item.indexOnly ? "  [index-only]" : "");
        g.drawText(rightText, pathArea, juce::Justification::centredRight);

        if (isAcidizedLoop(item) || isAppleLoop(item))
        {
            const juce::String badgeText = isAcidizedLoop(item) ? "Acidized" : "Apple Loop";
            const int badgeWidth = isAcidizedLoop(item) ? 64 : 76;
            const int badgeHeight = 14;
            const int badgeX = juce::jmax(filenameArea.getX(), filenameArea.getRight() - badgeWidth - 4);
            const int badgeY = titleRow.getY() + 2;

            const auto badgeColour = isAcidizedLoop(item)
                                         ? (darkModeEnabled ? juce::Colour(0xff2f8f5b) : juce::Colour(0xff2f9f61))
                                         : (darkModeEnabled ? juce::Colour(0xff3465a4) : juce::Colour(0xff3a78bf));
            g.setColour(badgeColour);
            g.fillRoundedRectangle(static_cast<float>(badgeX), static_cast<float>(badgeY),
                                   static_cast<float>(badgeWidth), static_cast<float>(badgeHeight), 4.0f);

            g.setColour(juce::Colours::white);
            g.setFont(10.0f);
            g.drawText(badgeText, badgeX, badgeY, badgeWidth, badgeHeight, juce::Justification::centred);
        }

        g.setFont(11.0f);
        const auto labelColour = darkModeEnabled ? juce::Colours::silver : juce::Colour(0xff6a6a6a);
        const auto valueColour = darkModeEnabled ? juce::Colour(0xffcce9ff) : juce::Colour(0xff1d5c8d);
        drawMetadataTokens(g, metadataRow, buildMetadataTokens(item), labelColour, valueColour);
    }

    void ResultsPanel::selectedRowsChanged(int lastRowSelected)
    {
        const auto *selectedFile = getResultAt(lastRowSelected);
        if (selectedFile == nullptr)
        {
            setSelectedFileMetadata(std::nullopt);
            return;
        }

        updateMetadataControlState();

        if (onFileSelected)
            onFileSelected(*selectedFile);
    }

    void ResultsPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent &)
    {
        const auto *selectedFile = getResultAt(row);
        if (selectedFile == nullptr)
            return;

        if (onFileActivated)
            onFileActivated(*selectedFile);
    }

} // namespace sw
