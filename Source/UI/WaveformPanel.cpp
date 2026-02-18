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
        if (displayMode == DisplayMode::spectrogram)
            paintSpectrogram(g, bounds);
        else
            paintWaveform(g, bounds);

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
        if (event.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Waveform", true, displayMode == DisplayMode::waveform);
            menu.addItem(2, "Spectrogram", true, displayMode == DisplayMode::spectrogram);

            auto safeThis = juce::Component::SafePointer<WaveformPanel>(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                               [safeThis](int selected)
                               {
                                   if (safeThis == nullptr)
                                       return;

                                   if (selected == 1)
                                       safeThis->setDisplayMode(DisplayMode::waveform);
                                   else if (selected == 2)
                                       safeThis->setDisplayMode(DisplayMode::spectrogram);
                               });
            return;
        }

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
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            auto safeThis = juce::Component::SafePointer<WaveformPanel>(this);
            auto peaksCopy = std::make_shared<std::vector<std::vector<float>>>(peaksByChannel);
            juce::MessageManager::callAsync([safeThis, peaksCopy]
                                            {
                                                if (safeThis == nullptr)
                                                    return;

                                                safeThis->setPeaks(*peaksCopy); });
            return;
        }

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

    void WaveformPanel::setDisplayMode(DisplayMode mode)
    {
        if (displayMode == mode)
            return;

        displayMode = mode;
        repaint();
    }

    WaveformPanel::DisplayMode WaveformPanel::getDisplayMode() const noexcept
    {
        return displayMode;
    }

    void WaveformPanel::paintWaveform(juce::Graphics &g, juce::Rectangle<float> bounds) const
    {
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
    }

    void WaveformPanel::paintSpectrogram(juce::Graphics &g, juce::Rectangle<float> bounds) const
    {
        const int numChannels = static_cast<int>(currentPeaksByChannel.size());
        const int numPeaks = static_cast<int>(currentPeaksByChannel.front().size());
        if (numChannels <= 0 || numPeaks <= 0)
            return;

        constexpr int kFrequencyBins = 48;
        const float columnWidth = juce::jmax(1.0f, bounds.getWidth() / static_cast<float>(numPeaks));
        const float binHeight = juce::jmax(1.0f, bounds.getHeight() / static_cast<float>(kFrequencyBins));

        g.setColour(darkModeEnabled ? juce::Colour(0xff101828) : juce::Colour(0xffdce8fb));
        g.fillRect(bounds);

        for (int i = 0; i < numPeaks; ++i)
        {
            float peakEnergy = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto &channelPeaks = currentPeaksByChannel[static_cast<size_t>(ch)];
                if (i < static_cast<int>(channelPeaks.size()))
                    peakEnergy = juce::jmax(peakEnergy, channelPeaks[static_cast<size_t>(i)]);
            }

            peakEnergy = juce::jlimit(0.0f, 1.0f, peakEnergy);
            const float energyBase = std::pow(peakEnergy, 1.15f);
            const float x = bounds.getX() + static_cast<float>(i) * columnWidth;

            for (int bin = 0; bin < kFrequencyBins; ++bin)
            {
                const float freqNorm = static_cast<float>(bin) / static_cast<float>(kFrequencyBins - 1);
                const float bandWeight = std::pow(1.0f - freqNorm, 0.55f);
                const float intensity = juce::jlimit(0.0f, 1.0f, energyBase * bandWeight);

                if (intensity < 0.04f)
                    continue;

                const auto colour = darkModeEnabled
                                        ? juce::Colour::fromHSV(0.60f - 0.48f * intensity,
                                                                0.80f,
                                                                0.20f + 0.80f * intensity,
                                                                0.75f)
                                        : juce::Colour::fromHSV(0.64f - 0.50f * intensity,
                                                                0.62f,
                                                                0.45f + 0.50f * intensity,
                                                                0.65f);

                const float y = bounds.getBottom() - static_cast<float>(bin + 1) * binHeight;
                g.setColour(colour);
                g.fillRect(juce::Rectangle<float>(x, y, columnWidth + 0.5f, binHeight + 0.25f));
            }
        }
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
