#include "ResultsPanel.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kSortByNameId = 1;
    constexpr int kSortByPathId = 2;
    constexpr int kViewRecentId = 1;
    constexpr int kViewFavoritesId = 2;
    constexpr int kViewDuplicatesId = 3;
    constexpr int kWaveformColumnWidth = 120;

    int selectorIdForViewMode(sw::ResultsPanel::ViewMode mode)
    {
        switch (mode)
        {
        case sw::ResultsPanel::ViewMode::Favorites:
            return kViewFavoritesId;
        case sw::ResultsPanel::ViewMode::Duplicates:
            return kViewDuplicatesId;
        case sw::ResultsPanel::ViewMode::Recent:
        default:
            return kViewRecentId;
        }
    }

    sw::ResultsPanel::ViewMode viewModeForSelectorId(int id)
    {
        switch (id)
        {
        case kViewFavoritesId:
            return sw::ResultsPanel::ViewMode::Favorites;
        case kViewDuplicatesId:
            return sw::ResultsPanel::ViewMode::Duplicates;
        case kViewRecentId:
        default:
            return sw::ResultsPanel::ViewMode::Recent;
        }
    }

    std::vector<std::string> splitTagsFromEditor(const juce::String &text)
    {
        juce::StringArray tokens;
        tokens.addTokens(text, ",", {});
        tokens.trim();
        tokens.removeEmptyStrings();

        std::vector<std::string> result;
        result.reserve(static_cast<size_t>(tokens.size()));
        for (const auto &token : tokens)
            result.push_back(token.toStdString());

        return result;
    }

    juce::String joinTagsForEditor(const std::vector<std::string> &tags)
    {
        juce::StringArray tokens;
        for (const auto &tag : tags)
            tokens.add(tag);

        return tokens.joinIntoString(", ");
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

        viewSelector.addItem("Recent", kViewRecentId);
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

        sortSelector.addItem("Name", kSortByNameId);
        sortSelector.addItem("Path", kSortByPathId);
        sortSelector.setSelectedId(kSortByNameId, juce::dontSendNotification);
        sortSelector.onChange = [this]
        {
            const int selectedRow = resultsList.getSelectedRow();
            std::optional<int64_t> selectedRootId;
            std::optional<std::string> selectedRelativePath;
            if (const auto *selectedFile = getResultAt(selectedRow); selectedFile != nullptr)
            {
                selectedRootId = selectedFile->rootId;
                selectedRelativePath = selectedFile->relativePath;
            }

            const auto selectedId = sortSelector.getSelectedId();
            sortMode = (selectedId == kSortByPathId) ? SortMode::Path : SortMode::Name;
            applySort();
            resultsList.updateContent();

            if (selectedRootId.has_value() && selectedRelativePath.has_value())
                selectFile(*selectedRootId, *selectedRelativePath);

            repaint();
        };
        addAndMakeVisible(sortSelector);

        savedSearchSelector.setTextWhenNothingSelected("Saved searches");
        savedSearchSelector.onChange = [this]
        {
            if (suppressControlCallbacks)
                return;

            updateMetadataControlState();

            const int index = savedSearchSelector.getSelectedItemIndex();
            if (index < 0 || index >= static_cast<int>(savedSearches.size()))
            {
                if (onSavedSearchSelected)
                    onSavedSearchSelected(std::nullopt);
                return;
            }

            if (onSavedSearchSelected)
                onSavedSearchSelected(savedSearches[static_cast<size_t>(index)].id);
        };
        addAndMakeVisible(savedSearchSelector);

        saveSearchButton.onClick = [this]
        {
            if (onSaveSearchRequested)
                onSaveSearchRequested();
        };
        addAndMakeVisible(saveSearchButton);

        deleteSavedSearchButton.onClick = [this]
        {
            const int index = savedSearchSelector.getSelectedItemIndex();
            if (index < 0 || index >= static_cast<int>(savedSearches.size()))
                return;

            if (onDeleteSavedSearchRequested)
                onDeleteSavedSearchRequested(savedSearches[static_cast<size_t>(index)].id);
        };
        addAndMakeVisible(deleteSavedSearchButton);

        favoriteToggle.onClick = [this]
        {
            if (suppressControlCallbacks)
                return;

            if (onSelectedFileFavoriteChanged)
                onSelectedFileFavoriteChanged(favoriteToggle.getToggleState());
        };
        addAndMakeVisible(favoriteToggle);

        ratingSelector.addItem("No rating", 1);
        ratingSelector.addItem("1 star", 2);
        ratingSelector.addItem("2 stars", 3);
        ratingSelector.addItem("3 stars", 4);
        ratingSelector.addItem("4 stars", 5);
        ratingSelector.addItem("5 stars", 6);
        ratingSelector.setSelectedId(1, juce::dontSendNotification);
        ratingSelector.onChange = [this]
        {
            if (suppressControlCallbacks)
                return;

            const int selectedId = ratingSelector.getSelectedId();
            const std::optional<int> rating = (selectedId <= 1) ? std::nullopt : std::optional<int>(selectedId - 1);
            if (onSelectedFileRatingChanged)
                onSelectedFileRatingChanged(rating);
        };
        addAndMakeVisible(ratingSelector);

        tagsEditor.setMultiLine(false);
        tagsEditor.setReturnKeyStartsNewLine(false);
        tagsEditor.setTextToShowWhenEmpty("Tags (comma-separated)", juce::Colours::grey);
        tagsEditor.onReturnKey = [this]
        {
            commitTagsFromEditor();
        };
        tagsEditor.onFocusLost = [this]
        {
            commitTagsFromEditor();
        };
        addAndMakeVisible(tagsEditor);

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
        constexpr int selectorWidth = 96;
        constexpr int savedSearchWidth = 170;
        constexpr int buttonWidth = 104;
        constexpr int favoriteWidth = 88;
        constexpr int ratingWidth = 96;
        constexpr int controlGap = 6;

        deleteSavedSearchButton.setBounds(topRow.removeFromRight(buttonWidth));
        topRow.removeFromRight(controlGap);
        saveSearchButton.setBounds(topRow.removeFromRight(buttonWidth));
        topRow.removeFromRight(controlGap);
        savedSearchSelector.setBounds(topRow.removeFromRight(savedSearchWidth));
        topRow.removeFromRight(controlGap);
        sortSelector.setBounds(topRow.removeFromRight(selectorWidth));
        topRow.removeFromRight(controlGap);
        viewSelector.setBounds(topRow.removeFromRight(viewWidth));
        topRow.removeFromRight(controlGap);
        searchBox.setBounds(topRow);

        ratingSelector.setBounds(metadataRow.removeFromRight(ratingWidth));
        metadataRow.removeFromRight(controlGap);
        favoriteToggle.setBounds(metadataRow.removeFromRight(favoriteWidth));
        metadataRow.removeFromRight(controlGap);
        tagsEditor.setBounds(metadataRow);

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

    void ResultsPanel::setSavedSearches(std::vector<SavedSearchRecord> newSavedSearches,
                                        std::optional<int64_t> selectedSavedSearchId)
    {
        savedSearches = std::move(newSavedSearches);

        suppressControlCallbacks = true;
        savedSearchSelector.clear(juce::dontSendNotification);
        for (size_t i = 0; i < savedSearches.size(); ++i)
            savedSearchSelector.addItem(savedSearches[i].name, static_cast<int>(i) + 1);

        int selectedIndex = -1;
        if (selectedSavedSearchId.has_value())
        {
            for (int i = 0; i < static_cast<int>(savedSearches.size()); ++i)
            {
                if (savedSearches[static_cast<size_t>(i)].id == *selectedSavedSearchId)
                {
                    selectedIndex = i;
                    break;
                }
            }
        }

        savedSearchSelector.setSelectedItemIndex(selectedIndex, juce::dontSendNotification);
        suppressControlCallbacks = false;
        updateMetadataControlState();
    }

    void ResultsPanel::setSelectedFileMetadata(std::optional<FileUserDataRecord> userData,
                                               std::vector<std::string> tags)
    {
        suppressControlCallbacks = true;
        favoriteToggle.setToggleState(userData.has_value() && userData->isFavorite, juce::dontSendNotification);
        ratingSelector.setSelectedId(userData.has_value() && userData->rating.has_value() ? (*userData->rating + 1) : 1,
                                     juce::dontSendNotification);
        tagsEditor.setText(joinTagsForEditor(tags), juce::dontSendNotification);
        suppressControlCallbacks = false;
        updateMetadataControlState();
    }

    void ResultsPanel::applySort()
    {
        std::sort(results.begin(), results.end(), [this](const FileRecord &a, const FileRecord &b)
                  {
            if (sortMode == SortMode::Path)
            {
                if (a.rootId != b.rootId)
                    return a.rootId < b.rootId;

                const auto relCmp = juce::String(a.relativePath).compareIgnoreCase(juce::String(b.relativePath));
                if (relCmp != 0)
                    return relCmp < 0;
            }

            const auto nameCmp = juce::String(a.filename).compareIgnoreCase(juce::String(b.filename));
            if (nameCmp != 0)
                return nameCmp < 0;

            const auto relCmp = juce::String(a.relativePath).compareIgnoreCase(juce::String(b.relativePath));
            if (relCmp != 0)
                return relCmp < 0;

            return a.id < b.id; });
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

        sortSelector.setColour(juce::ComboBox::textColourId, textColour);
        sortSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        sortSelector.setColour(juce::ComboBox::outlineColourId, outline);
        sortSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        viewSelector.setColour(juce::ComboBox::textColourId, textColour);
        viewSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        viewSelector.setColour(juce::ComboBox::outlineColourId, outline);
        viewSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        savedSearchSelector.setColour(juce::ComboBox::textColourId, textColour);
        savedSearchSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        savedSearchSelector.setColour(juce::ComboBox::outlineColourId, outline);
        savedSearchSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        tagsEditor.setColour(juce::TextEditor::textColourId, textColour);
        tagsEditor.setColour(juce::TextEditor::backgroundColourId, editorBg);
        tagsEditor.setColour(juce::TextEditor::outlineColourId, outline);
        tagsEditor.setColour(juce::TextEditor::focusedOutlineColourId, darkModeEnabled ? juce::Colour(0xff6b9bc8) : juce::Colour(0xff2f6fa8));
        tagsEditor.setTextToShowWhenEmpty("Tags (comma-separated)", placeholder);

        ratingSelector.setColour(juce::ComboBox::textColourId, textColour);
        ratingSelector.setColour(juce::ComboBox::backgroundColourId, comboBg);
        ratingSelector.setColour(juce::ComboBox::outlineColourId, outline);
        ratingSelector.setColour(juce::ComboBox::arrowColourId, textColour);

        saveSearchButton.setColour(juce::TextButton::buttonColourId, darkModeEnabled ? juce::Colour(0xff314154) : juce::Colour(0xffd7e3f0));
        saveSearchButton.setColour(juce::TextButton::textColourOffId, textColour);
        deleteSavedSearchButton.setColour(juce::TextButton::buttonColourId, darkModeEnabled ? juce::Colour(0xff4d2b2b) : juce::Colour(0xfff2d7d7));
        deleteSavedSearchButton.setColour(juce::TextButton::textColourOffId, textColour);

        favoriteToggle.setColour(juce::ToggleButton::textColourId, textColour);

        resultsList.setColour(juce::ListBox::backgroundColourId, darkModeEnabled ? juce::Colour(0xff1e1e1e) : juce::Colour(0xfffafafa));

        repaint();
    }

    void ResultsPanel::updateMetadataControlState()
    {
        const bool hasSelectedFile = getSelectedRow() >= 0;
        favoriteToggle.setEnabled(hasSelectedFile);
        ratingSelector.setEnabled(hasSelectedFile);
        tagsEditor.setEnabled(hasSelectedFile);
        saveSearchButton.setEnabled(viewMode != ViewMode::Duplicates);
        deleteSavedSearchButton.setEnabled(savedSearchSelector.getSelectedItemIndex() >= 0);
    }

    void ResultsPanel::commitTagsFromEditor()
    {
        if (suppressControlCallbacks || !tagsEditor.isEnabled())
            return;

        if (onSelectedFileTagsChanged)
            onSelectedFileTagsChanged(splitTagsFromEditor(tagsEditor.getText()));
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
            setSelectedFileMetadata(std::nullopt, {});
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
