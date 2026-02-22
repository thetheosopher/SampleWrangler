#include "ResultsPanel.h"
#include "Pipeline/WaveformCache.h"
#include "Util/Paths.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace
{
    constexpr int kSortByNameId = 1;
    constexpr int kSortByPathId = 2;
    constexpr int kWaveformColumnWidth = 120;

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

    juce::String formatMetadataLine(const sw::FileRecord &item)
    {
        auto valueOrDash = [](const auto &opt, const juce::String &suffix = juce::String())
        {
            if (!opt.has_value())
                return juce::String("--");
            return juce::String(*opt) + suffix;
        };

        const juce::String sampleRate = valueOrDash(item.sampleRate, " Hz");
        const juce::String channels = valueOrDash(item.channels);
        const juce::String bitDepth = valueOrDash(item.bitDepth);
        const juce::String bitrate = valueOrDash(item.bitrateKbps, " kbps");
        const juce::String totalSamples = item.totalSamples.has_value() ? juce::String(*item.totalSamples) : juce::String("--");
        const juce::String codec = item.codec.has_value() ? juce::String(*item.codec) : juce::String("--");

        juce::String metadata = "SR: " + sampleRate +
                                "  Ch: " + channels +
                                "  Bit Depth: " + bitDepth +
                                "  Bitrate: " + bitrate +
                                "  Duration: " + formatDuration(item.durationSec) +
                                "  Samples: " + totalSamples +
                                "  Codec/Enc: " + codec;

        if (isAcidizedLoop(item) || isAppleLoop(item))
        {
            const juce::String acidRoot = item.acidRootNote.has_value() ? midiNoteToName(*item.acidRootNote) : juce::String("--");
            const juce::String acidBeats = item.acidBeats.has_value() ? juce::String(*item.acidBeats) : juce::String("--");
            const juce::String acidBpm = item.bpm.has_value() ? juce::String(*item.bpm, 2) : juce::String("--");
            const juce::String loopStart = item.loopStartSample.has_value() ? juce::String(*item.loopStartSample) : juce::String("--");
            const juce::String loopEnd = item.loopEndSample.has_value() ? juce::String(*item.loopEndSample) : juce::String("--");

            metadata += isAcidizedLoop(item) ? "  Acid: yes" : "  Apple Loop: yes";
            metadata += "  Root: " + acidRoot;
            if (isAcidizedLoop(item))
                metadata += "  Beats: " + acidBeats;
            if (item.bpm.has_value())
                metadata += "  Tempo: " + acidBpm;
            metadata += "  Loop: " + loopStart + "-" + loopEnd;
        }

        return metadata;
    }
}

namespace sw
{

    ResultsPanel::ResultsPanel()
    {
        waveformCacheDirectory = defaultCacheDirectory();

        searchBox.setTextToShowWhenEmpty("Search samples...", juce::Colours::grey);
        searchBox.onTextChange = [this]
        {
            if (onSearchQueryChanged)
                onSearchQueryChanged(searchBox.getText().toStdString());
        };
        addAndMakeVisible(searchBox);

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

        resultsList.setModel(this);
        resultsList.setRowHeight(40);
        addAndMakeVisible(resultsList);

        setDarkMode(true);
    }

    void ResultsPanel::paint(juce::Graphics &g)
    {
        g.fillAll(darkModeEnabled ? juce::Colour(0xff1e1e1e) : juce::Colour(0xfffafafa));
    }

    void ResultsPanel::resized()
    {
        auto area = getLocalBounds().reduced(4);
        auto topRow = area.removeFromTop(28);
        constexpr int selectorWidth = 110;
        constexpr int controlGap = 6;
        sortSelector.setBounds(topRow.removeFromRight(selectorWidth));
        topRow.removeFromRight(controlGap);
        searchBox.setBounds(topRow);
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
        repaint();
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

        std::string cachePath;
        if (onResolveWaveformCachePathForFile != nullptr)
        {
            if (const auto resolved = onResolveWaveformCachePathForFile(file); resolved.has_value() && !resolved->empty())
                cachePath = *resolved;
        }

        if (cachePath.empty())
        {
            const auto cacheKey = WaveformCache::buildCacheKey(file.rootId, file.relativePath, file.sizeBytes, file.modifiedTime);
            cachePath = (std::filesystem::path(waveformCacheDirectory) / (cacheKey + ".peak")).string();
        }

        std::ifstream input(cachePath, std::ios::binary);

        if (!input)
        {
            waveformCacheMisses.insert(file.id);
            return nullptr;
        }

        uint32_t peakCount = 0;
        input.read(reinterpret_cast<char *>(&peakCount), sizeof(peakCount));

        if (!input || peakCount == 0 || peakCount > 100000)
        {
            waveformCacheMisses.insert(file.id);
            return nullptr;
        }

        auto &peaks = waveformPeaksByFileId[file.id];
        peaks.resize(static_cast<size_t>(peakCount));
        input.read(reinterpret_cast<char *>(peaks.data()), static_cast<std::streamsize>(peaks.size() * sizeof(float)));

        if (!input.good() && !input.eof())
        {
            waveformPeaksByFileId.erase(file.id);
            waveformCacheMisses.insert(file.id);
            return nullptr;
        }

        return &waveformPeaksByFileId[file.id];
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

        resultsList.setColour(juce::ListBox::backgroundColourId, darkModeEnabled ? juce::Colour(0xff1e1e1e) : juce::Colour(0xfffafafa));

        repaint();
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

        g.setColour(darkModeEnabled ? juce::Colours::silver : juce::Colour(0xff6a6a6a));
        g.setFont(11.0f);
        g.drawFittedText(formatMetadataLine(item), metadataRow, juce::Justification::centredLeft, 1);
    }

    void ResultsPanel::selectedRowsChanged(int lastRowSelected)
    {
        const auto *selectedFile = getResultAt(lastRowSelected);
        if (selectedFile == nullptr)
            return;

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
