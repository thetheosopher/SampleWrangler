#include "ResultsPanel.h"

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
        addAndMakeVisible(resultsList);
    }

    void ResultsPanel::paint(juce::Graphics &g)
    {
        g.fillAll(juce::Colour(0xff1e1e1e));
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
        results = std::move(newResults);
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

    int ResultsPanel::getNumRows()
    {
        return static_cast<int>(results.size());
    }

    void ResultsPanel::paintListBoxItem(int rowNumber, juce::Graphics &g, int width, int height, bool rowIsSelected)
    {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(results.size()))
            return;

        if (rowIsSelected)
            g.fillAll(juce::Colour(0xff2a3d55));

        const auto &item = results[static_cast<size_t>(rowNumber)];

        g.setColour(juce::Colours::white);
        g.setFont(13.0f);
        g.drawText(juce::String(item.filename), 8, 0, width / 2, height, juce::Justification::centredLeft);

        g.setColour(juce::Colours::lightgrey);
        const juce::String rightText = juce::String(item.relativePath) + (item.indexOnly ? "  [index-only]" : "");
        g.drawText(rightText, width / 2, 0, width / 2 - 8, height, juce::Justification::centredRight);
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
