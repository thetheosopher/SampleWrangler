#include "WaveformPanel.h"
#include <cmath>

namespace sw
{

    WaveformPanel::WaveformPanel() = default;

    void WaveformPanel::paint(juce::Graphics &g)
    {
        g.fillAll(darkModeEnabled ? juce::Colour(0xff1a1a2e) : juce::Colour(0xffedf2fb));

        if (currentPeaksByChannel.empty() || currentPeaksByChannel.front().empty())
        {
            g.setColour(darkModeEnabled ? juce::Colours::grey : juce::Colour(0xff6a6a6a));
            g.setFont(12.0f);
            g.drawText("No waveform loaded", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        const int numChannels = static_cast<int>(currentPeaksByChannel.size());
        const int numPeaks = static_cast<int>(currentPeaksByChannel.front().size());
        if (numChannels <= 0 || numPeaks <= 0)
            return;

        const float laneHeight = bounds.getHeight() / static_cast<float>(numChannels);
        const float step = bounds.getWidth() / static_cast<float>(numPeaks);

        g.setColour(darkModeEnabled ? juce::Colour(0xff4fc3f7) : juce::Colour(0xff2b78c6));

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto &channelPeaks = currentPeaksByChannel[static_cast<size_t>(ch)];
            if (channelPeaks.empty())
                continue;

            const float laneTop = bounds.getY() + static_cast<float>(ch) * laneHeight;
            const float laneBottom = laneTop + laneHeight;
            const float laneMid = laneTop + (laneHeight * 0.5f);
            const float laneHalfAmplitude = juce::jmax(1.0f, (laneHeight * 0.5f) - 1.0f);

            for (int i = 0; i < numPeaks && i < static_cast<int>(channelPeaks.size()); ++i)
            {
                const float x = bounds.getX() + static_cast<float>(i) * step;
                const float h = channelPeaks[static_cast<size_t>(i)] * laneHalfAmplitude;
                g.drawVerticalLine(static_cast<int>(x), laneMid - h, laneMid + h);
            }

            if (ch < numChannels - 1)
            {
                g.setColour((darkModeEnabled ? juce::Colours::white : juce::Colour(0xff505050)).withAlpha(0.12f));
                g.drawHorizontalLine(static_cast<int>(laneBottom), bounds.getX(), bounds.getRight());
                g.setColour(darkModeEnabled ? juce::Colour(0xff4fc3f7) : juce::Colour(0xff2b78c6));
            }
        }

        if (loopStartNormalized >= 0.0f && loopEndNormalized > loopStartNormalized)
        {
            const float loopStartX = bounds.getX() + bounds.getWidth() * juce::jlimit(0.0f, 1.0f, loopStartNormalized);
            const float loopEndX = bounds.getX() + bounds.getWidth() * juce::jlimit(0.0f, 1.0f, loopEndNormalized);
            g.setColour((darkModeEnabled ? juce::Colours::yellow : juce::Colour(0xffd8ab00)).withAlpha(0.12f));
            g.fillRect(juce::Rectangle<float>(loopStartX, bounds.getY(), juce::jmax(1.0f, loopEndX - loopStartX), bounds.getHeight()));
        }

        if (playheadNormalized >= 0.0f)
        {
            const float playheadX = bounds.getX() + bounds.getWidth() * juce::jlimit(0.0f, 1.0f, playheadNormalized);
            g.setColour(darkModeEnabled ? juce::Colours::orange : juce::Colour(0xffc86b00));
            g.drawLine(playheadX, bounds.getY(), playheadX, bounds.getBottom(), 1.5f);
        }
    }

    void WaveformPanel::resized()
    {
    }

    void WaveformPanel::mouseDown(const juce::MouseEvent &event)
    {
        if (currentPeaksByChannel.empty() || currentPeaksByChannel.front().empty())
            return;

        const float normalized = normalizedPositionFromX(event.x);
        setPlayheadNormalized(normalized);

        if (onScrubRequested)
            onScrubRequested(normalized);
    }

    void WaveformPanel::mouseDrag(const juce::MouseEvent &event)
    {
        if (currentPeaksByChannel.empty() || currentPeaksByChannel.front().empty())
            return;

        const float normalized = normalizedPositionFromX(event.x);
        setPlayheadNormalized(normalized);

        if (onScrubRequested)
            onScrubRequested(normalized);
    }

    void WaveformPanel::setPeaks(const std::vector<std::vector<float>> &peaksByChannel)
    {
        currentPeaksByChannel = peaksByChannel;
        repaint();
    }

    void WaveformPanel::setPlayheadNormalized(float playheadPosition)
    {
        const float clamped = juce::jlimit(-1.0f, 1.0f, playheadPosition);
        playheadNormalized = clamped;
        repaint();
    }

    void WaveformPanel::setLoopRegionNormalized(float loopStart, float loopEnd)
    {
        loopStartNormalized = juce::jlimit(-1.0f, 1.0f, loopStart);
        loopEndNormalized = juce::jlimit(-1.0f, 1.0f, loopEnd);
        repaint();
    }

    void WaveformPanel::setDarkMode(bool enabled)
    {
        if (darkModeEnabled == enabled)
            return;

        darkModeEnabled = enabled;
        repaint();
    }

    float WaveformPanel::normalizedPositionFromX(int x) const
    {
        const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        if (bounds.getWidth() <= 0.0f)
            return 0.0f;

        const float normalized = (static_cast<float>(x) - bounds.getX()) / bounds.getWidth();
        return juce::jlimit(0.0f, 1.0f, normalized);
    }

} // namespace sw
