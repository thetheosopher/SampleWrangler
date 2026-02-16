#include "WaveformPanel.h"

namespace sw
{

    WaveformPanel::WaveformPanel() = default;

    void WaveformPanel::paint(juce::Graphics &g)
    {
        g.fillAll(juce::Colour(0xff1a1a2e));

        if (currentPeaks.empty())
        {
            g.setColour(juce::Colours::grey);
            g.setFont(12.0f);
            g.drawText("No waveform loaded", getLocalBounds(), juce::Justification::centred);
            return;
        }

        // Draw simple peak waveform
        const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        const float midY = bounds.getCentreY();
        const float halfH = bounds.getHeight() * 0.5f;

        g.setColour(juce::Colour(0xff4fc3f7));

        const int numPeaks = static_cast<int>(currentPeaks.size());
        const float step = bounds.getWidth() / static_cast<float>(numPeaks);

        for (int i = 0; i < numPeaks; ++i)
        {
            float x = bounds.getX() + static_cast<float>(i) * step;
            float h = currentPeaks[static_cast<size_t>(i)] * halfH;
            g.drawVerticalLine(static_cast<int>(x), midY - h, midY + h);
        }

        if (loopStartNormalized >= 0.0f && loopEndNormalized > loopStartNormalized)
        {
            const float loopStartX = bounds.getX() + bounds.getWidth() * juce::jlimit(0.0f, 1.0f, loopStartNormalized);
            const float loopEndX = bounds.getX() + bounds.getWidth() * juce::jlimit(0.0f, 1.0f, loopEndNormalized);
            g.setColour(juce::Colours::yellow.withAlpha(0.12f));
            g.fillRect(juce::Rectangle<float>(loopStartX, bounds.getY(), juce::jmax(1.0f, loopEndX - loopStartX), bounds.getHeight()));
        }

        if (playheadNormalized >= 0.0f)
        {
            const float playheadX = bounds.getX() + bounds.getWidth() * juce::jlimit(0.0f, 1.0f, playheadNormalized);
            g.setColour(juce::Colours::orange);
            g.drawLine(playheadX, bounds.getY(), playheadX, bounds.getBottom(), 1.5f);
        }
    }

    void WaveformPanel::resized()
    {
    }

    void WaveformPanel::setPeaks(const std::vector<float> &peaks)
    {
        currentPeaks = peaks;
        repaint();
    }

    void WaveformPanel::setPlayheadNormalized(float playheadPosition)
    {
        playheadNormalized = juce::jlimit(-1.0f, 1.0f, playheadPosition);
        repaint();
    }

    void WaveformPanel::setLoopRegionNormalized(float loopStart, float loopEnd)
    {
        loopStartNormalized = juce::jlimit(-1.0f, 1.0f, loopStart);
        loopEndNormalized = juce::jlimit(-1.0f, 1.0f, loopEnd);
        repaint();
    }

} // namespace sw
