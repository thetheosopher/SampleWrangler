#include "ResultsPanel.h"
#include <algorithm>

namespace
{
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
        searchBox.setTextToShowWhenEmpty("Search samples...", juce::Colours::grey);
        searchBox.onTextChange = [this]
        {
            if (onSearchQueryChanged)
                onSearchQueryChanged(searchBox.getText().toStdString());
        };
        addAndMakeVisible(searchBox);

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
        searchBox.setBounds(area.removeFromTop(28));
        area.removeFromTop(4);
        resultsList.setBounds(area);
    }

    void ResultsPanel::setResults(std::vector<FileRecord> newResults)
    {
        // Deselect before updating to avoid stale selection indices
        resultsList.deselectAllRows();

        results = std::move(newResults);

        // Sort results alphabetically by filename (case-insensitive)
        std::sort(results.begin(), results.end(), [](const FileRecord &a, const FileRecord &b)
                  { return juce::String(a.filename).compareIgnoreCase(juce::String(b.filename)) < 0; });

        resultsList.updateContent();
        repaint();
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

        searchBox.setColour(juce::TextEditor::textColourId, textColour);
        searchBox.setColour(juce::TextEditor::backgroundColourId, editorBg);
        searchBox.setColour(juce::TextEditor::outlineColourId, outline);
        searchBox.setColour(juce::TextEditor::focusedOutlineColourId, darkModeEnabled ? juce::Colour(0xff6b9bc8) : juce::Colour(0xff2f6fa8));
        searchBox.setTextToShowWhenEmpty("Search samples...", placeholder);

        resultsList.setColour(juce::ListBox::backgroundColourId, darkModeEnabled ? juce::Colour(0xff1e1e1e) : juce::Colour(0xfffafafa));

        repaint();
    }

    int ResultsPanel::getNumRows()
    {
        return static_cast<int>(results.size());
    }

    void ResultsPanel::paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int /*height*/, bool rowIsSelected)
    {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(results.size()))
            return;

        if (rowIsSelected)
            g.fillAll(darkModeEnabled ? juce::Colour(0xff2a3d55) : juce::Colour(0xffd8e8f8));

        const auto &item = results[static_cast<size_t>(rowNumber)];

        g.setColour(darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020));
        g.setFont(13.0f);
        g.drawText(juce::String(item.filename), 8, 0, width / 2, 18, juce::Justification::centredLeft);

        g.setColour(darkModeEnabled ? juce::Colours::lightgrey : juce::Colour(0xff4a4a4a));
        const juce::String rightText = juce::String(item.relativePath) + (item.indexOnly ? "  [index-only]" : "");
        g.drawText(rightText, width / 2, 0, width / 2 - 8, 18, juce::Justification::centredRight);

        if (isAcidizedLoop(item) || isAppleLoop(item))
        {
            const juce::String badgeText = isAcidizedLoop(item) ? "Acidized" : "Apple Loop";
            const int badgeWidth = isAcidizedLoop(item) ? 64 : 76;
            const int badgeHeight = 14;
            const int badgeX = juce::jmax(8, (width / 2) - badgeWidth - 4);
            const int badgeY = 2;

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
        g.drawFittedText(formatMetadataLine(item), 8, 19, width - 16, 18, juce::Justification::centredLeft, 1);
    }

    void ResultsPanel::selectedRowsChanged(int lastRowSelected)
    {
        if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(results.size()))
            return;

        if (onFileSelected)
            onFileSelected(results[static_cast<size_t>(lastRowSelected)]);
    }

    void ResultsPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent &)
    {
        if (row < 0 || row >= static_cast<int>(results.size()))
            return;

        if (onFileActivated)
            onFileActivated(results[static_cast<size_t>(row)]);
    }

} // namespace sw
