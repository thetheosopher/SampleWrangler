#include "WaveformPanel.h"
#include <cmath>

namespace sw
{

    WaveformPanel::WaveformPanel() = default;

    void WaveformPanel::paint(juce::Graphics &g)
    {
        g.fillAll(darkModeEnabled ? juce::Colour(0xff1a1a2e) : juce::Colour(0xffedf2fb));

        const bool isOscilloscope = (displayMode == DisplayMode::compositeOscilloscope);
        const bool isSpectrumAnalyzer = (displayMode == DisplayMode::spectrumAnalyzer);

        if (isOscilloscope)
        {
            const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
            paintCompositeOscilloscope(g, bounds);

            if (loading)
            {
                g.setColour((darkModeEnabled ? juce::Colours::black : juce::Colours::white).withAlpha(0.35f));
                g.fillRect(getLocalBounds());
                g.setColour(darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020));
                g.setFont(14.0f);
                g.drawText("Loading...", getLocalBounds(), juce::Justification::centred);
            }

            return;
        }

        if (isSpectrumAnalyzer)
        {
            const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
            paintSpectrumAnalyzer(g, bounds);

            if (loading)
            {
                g.setColour((darkModeEnabled ? juce::Colours::black : juce::Colours::white).withAlpha(0.35f));
                g.fillRect(getLocalBounds());
                g.setColour(darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020));
                g.setFont(14.0f);
                g.drawText("Loading...", getLocalBounds(), juce::Justification::centred);
            }

            return;
        }

        if (currentPeaksByChannel.empty() || currentPeaksByChannel.front().empty())
        {
            if (loading)
            {
                g.setColour(darkModeEnabled ? juce::Colours::white : juce::Colour(0xff303030));
                g.setFont(14.0f);
                g.drawText("Loading...", getLocalBounds(), juce::Justification::centred);
            }
            else
            {
                g.setColour(darkModeEnabled ? juce::Colours::grey : juce::Colour(0xff6a6a6a));
                g.setFont(12.0f);
                g.drawText("No waveform loaded", getLocalBounds(), juce::Justification::centred);
            }
            return;
        }

        const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        if (displayMode == DisplayMode::spectrogram)
            paintSpectrogram(g, bounds);
        else if (displayMode == DisplayMode::compositeOscilloscope)
            paintCompositeOscilloscope(g, bounds);
        else
            paintWaveform(g, bounds);

        if (loopStartNormalized >= 0.0f && loopEndNormalized > loopStartNormalized)
        {
            const float loopStartX = bounds.getX() + bounds.getWidth() * juce::jlimit(0.0f, 1.0f, loopStartNormalized);
            const float loopEndX = bounds.getX() + bounds.getWidth() * juce::jlimit(0.0f, 1.0f, loopEndNormalized);
            g.setColour(darkModeEnabled ? juce::Colour(0xffff4d4d) : juce::Colour(0xffc62828));
            g.drawLine(loopStartX, bounds.getY(), loopStartX, bounds.getBottom(), 2.0f);
            g.drawLine(loopEndX, bounds.getY(), loopEndX, bounds.getBottom(), 2.0f);
        }

        if (displayMode == DisplayMode::waveform && playheadNormalized >= 0.0f)
        {
            const float playheadX = bounds.getX() + bounds.getWidth() * juce::jlimit(0.0f, 1.0f, playheadNormalized);
            g.setColour(darkModeEnabled ? juce::Colours::orange : juce::Colour(0xffc86b00));
            g.drawLine(playheadX, bounds.getY(), playheadX, bounds.getBottom(), 1.5f);
        }

        if (loading)
        {
            g.setColour((darkModeEnabled ? juce::Colours::black : juce::Colours::white).withAlpha(0.35f));
            g.fillRect(getLocalBounds());
            g.setColour(darkModeEnabled ? juce::Colours::white : juce::Colour(0xff202020));
            g.setFont(14.0f);
            g.drawText("Loading...", getLocalBounds(), juce::Justification::centred);
        }
    }

    void WaveformPanel::resized()
    {
        ensureSpectrogramHistorySize(juce::jmax(1, getLocalBounds().reduced(2).getWidth()));
    }

    void WaveformPanel::mouseDown(const juce::MouseEvent &event)
    {
        if (event.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Waveform", true, displayMode == DisplayMode::waveform);
            menu.addItem(2, "Spectrogram", true, displayMode == DisplayMode::spectrogram);
            menu.addItem(3, "Oscilloscope", true, displayMode == DisplayMode::compositeOscilloscope);
            menu.addItem(4, "Spectrum Analyzer", true, displayMode == DisplayMode::spectrumAnalyzer);

            auto safeThis = juce::Component::SafePointer<WaveformPanel>(this);
            const auto clickArea = juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(clickArea),
                               [safeThis](int selected)
                               {
                                   if (safeThis == nullptr)
                                       return;

                                   if (selected == 1)
                                       safeThis->setDisplayMode(DisplayMode::waveform);
                                   else if (selected == 2)
                                       safeThis->setDisplayMode(DisplayMode::spectrogram);
                                   else if (selected == 3)
                                       safeThis->setDisplayMode(DisplayMode::compositeOscilloscope);
                                   else if (selected == 4)
                                       safeThis->setDisplayMode(DisplayMode::spectrumAnalyzer);
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

    void WaveformPanel::setOscilloscopeSamples(const std::vector<float> &samples)
    {
        if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            auto safeThis = juce::Component::SafePointer<WaveformPanel>(this);
            auto samplesCopy = std::make_shared<std::vector<float>>(samples);
            juce::MessageManager::callAsync([safeThis, samplesCopy]
                                            {
                                                if (safeThis == nullptr)
                                                    return;

                                                safeThis->setOscilloscopeSamples(*samplesCopy); });
            return;
        }

        currentOscilloscopeSamples = samples;
        updateSpectrumAnalyzer();
        updateSpectrogram();

        if (displayMode == DisplayMode::compositeOscilloscope ||
            displayMode == DisplayMode::spectrumAnalyzer ||
            displayMode == DisplayMode::spectrogram)
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

    void WaveformPanel::setLoading(bool isLoading)
    {
        if (loading == isLoading)
            return;

        loading = isLoading;

        if (loading)
        {
            spectrogramWriteIndex = 0;
            spectrogramFilledColumns = 0;
            spectrogramScrollAccumulator = 0.0f;
            for (auto &column : spectrogramColumns)
                column.fill(0.0f);
        }

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
        g.setColour(darkModeEnabled ? juce::Colour(0xff101828) : juce::Colour(0xffdce8fb));
        g.fillRect(bounds);

        const int historySize = static_cast<int>(spectrogramColumns.size());
        if (historySize <= 0 || spectrogramFilledColumns <= 0)
            return;

        const float columnWidth = bounds.getWidth() / static_cast<float>(historySize);
        const float binHeight = bounds.getHeight() / static_cast<float>(kSpectrogramBinCount);
        const int oldestIndex = (spectrogramWriteIndex - spectrogramFilledColumns + historySize) % historySize;
        const float startX = bounds.getRight() - static_cast<float>(spectrogramFilledColumns) * columnWidth;

        for (int column = 0; column < spectrogramFilledColumns; ++column)
        {
            const int sourceIndex = (oldestIndex + column) % historySize;
            const auto &columnBins = spectrogramColumns[static_cast<size_t>(sourceIndex)];
            const float x = startX + static_cast<float>(column) * columnWidth;

            for (int bin = 0; bin < kSpectrogramBinCount; ++bin)
            {
                const float intensity = juce::jlimit(0.0f, 1.0f, columnBins[static_cast<size_t>(bin)]);

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
                g.fillRect(juce::Rectangle<float>(x, y, columnWidth + 0.75f, binHeight + 0.35f));
            }
        }
    }

    void WaveformPanel::paintCompositeOscilloscope(juce::Graphics &g, juce::Rectangle<float> bounds) const
    {
        g.setColour(darkModeEnabled ? juce::Colour(0xff0f1625) : juce::Colour(0xffdfe9f9));
        g.fillRect(bounds);

        if (currentOscilloscopeSamples.size() < 2)
        {
            g.setColour((darkModeEnabled ? juce::Colours::white : juce::Colour(0xff4a4a4a)).withAlpha(0.25f));
            g.drawHorizontalLine(static_cast<int>(std::round(bounds.getCentreY())), bounds.getX(), bounds.getRight());
            g.setColour(darkModeEnabled ? juce::Colours::grey : juce::Colour(0xff6a6a6a));
            g.setFont(12.0f);
            g.drawText("No audio output", bounds.toNearestInt(), juce::Justification::centred);
            return;
        }

        const float midY = bounds.getCentreY();
        const float halfHeight = juce::jmax(1.0f, bounds.getHeight() * 0.5f - 4.0f);
        const int sampleCount = static_cast<int>(currentOscilloscopeSamples.size());
        const float traceWidth = juce::jmax(1.0f, bounds.getWidth());

        juce::Path scopePath;
        bool started = false;

        for (int x = 0; x < static_cast<int>(traceWidth); ++x)
        {
            const int sampleIndex = juce::jlimit(0,
                                                 sampleCount - 1,
                                                 static_cast<int>((static_cast<float>(x) / juce::jmax(1.0f, traceWidth - 1.0f)) * static_cast<float>(sampleCount - 1)));
            const float sample = juce::jlimit(-1.0f, 1.0f, currentOscilloscopeSamples[static_cast<size_t>(sampleIndex)]);
            const float y = midY - sample * halfHeight;
            const float drawX = bounds.getX() + static_cast<float>(x);

            if (!started)
            {
                scopePath.startNewSubPath(drawX, y);
                started = true;
            }
            else
            {
                scopePath.lineTo(drawX, y);
            }
        }

        g.setColour((darkModeEnabled ? juce::Colours::white : juce::Colour(0xff4a4a4a)).withAlpha(0.16f));
        g.drawHorizontalLine(static_cast<int>(std::round(midY)), bounds.getX(), bounds.getRight());

        g.setColour(darkModeEnabled ? juce::Colour(0xff66e0ff) : juce::Colour(0xff1769aa));
        g.strokePath(scopePath, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void WaveformPanel::paintSpectrumAnalyzer(juce::Graphics &g, juce::Rectangle<float> bounds) const
    {
        g.setColour(darkModeEnabled ? juce::Colour(0xff0f1625) : juce::Colour(0xffdfe9f9));
        g.fillRect(bounds);

        const float barGap = 2.0f;
        const float totalGap = barGap * static_cast<float>(kSpectrumBandCount - 1);
        const float barWidth = juce::jmax(1.0f, (bounds.getWidth() - totalGap) / static_cast<float>(kSpectrumBandCount));

        for (int bandIndex = 0; bandIndex < kSpectrumBandCount; ++bandIndex)
        {
            const float normalized = juce::jlimit(0.0f, 1.0f, spectrumBands[static_cast<size_t>(bandIndex)]);
            const float shaped = std::pow(normalized, 0.78f);
            const float barHeight = shaped * bounds.getHeight();
            const float x = bounds.getX() + static_cast<float>(bandIndex) * (barWidth + barGap);
            const float y = bounds.getBottom() - barHeight;

            const auto colour = darkModeEnabled
                                    ? juce::Colour::fromHSV(0.62f - 0.48f * shaped,
                                                            0.78f,
                                                            0.28f + 0.72f * shaped,
                                                            0.92f)
                                    : juce::Colour::fromHSV(0.64f - 0.48f * shaped,
                                                            0.62f,
                                                            0.40f + 0.55f * shaped,
                                                            0.86f);

            g.setColour(colour);
            g.fillRoundedRectangle(x, y, barWidth, juce::jmax(1.0f, barHeight), 1.6f);

            const float peakNormalized = juce::jlimit(0.0f, 1.0f, spectrumPeakIndicators[static_cast<size_t>(bandIndex)]);
            const float peakShaped = std::pow(peakNormalized, 0.78f);
            const float peakY = bounds.getBottom() - peakShaped * bounds.getHeight();

            g.setColour(darkModeEnabled ? juce::Colour(0xffe8f6ff) : juce::Colour(0xff1f4f7a));
            g.drawLine(x,
                       peakY,
                       x + juce::jmax(1.0f, barWidth - 0.25f),
                       peakY,
                       1.5f);
        }
    }

    void WaveformPanel::updateSpectrumAnalyzer()
    {
        constexpr float kDecay = 0.88f;
        constexpr float kPeakGravityPerFrame = 0.00009f;
        constexpr float kPeakMaxFallVelocity = 0.03f;
        constexpr float kNoiseFloorDb = -80.0f;

        for (int bandIndex = 0; bandIndex < kSpectrumBandCount; ++bandIndex)
        {
            auto &band = spectrumBands[static_cast<size_t>(bandIndex)];
            auto &peak = spectrumPeakIndicators[static_cast<size_t>(bandIndex)];
            auto &peakVelocity = spectrumPeakFallVelocity[static_cast<size_t>(bandIndex)];
            band *= kDecay;

            if (peak <= band)
            {
                peak = band;
                peakVelocity = 0.0f;
            }
            else
            {
                peakVelocity = juce::jmin(kPeakMaxFallVelocity, peakVelocity + kPeakGravityPerFrame);
                peak = juce::jmax(band, peak - peakVelocity);

                if (peak <= band)
                    peakVelocity = 0.0f;
            }
        }

        if (currentOscilloscopeSamples.size() < 2)
            return;

        std::fill(spectrumFftBuffer.begin(), spectrumFftBuffer.end(), 0.0f);

        const int available = juce::jmin(static_cast<int>(currentOscilloscopeSamples.size()), kSpectrumFftSize);
        const int start = static_cast<int>(currentOscilloscopeSamples.size()) - available;
        for (int i = 0; i < available; ++i)
            spectrumFftBuffer[static_cast<size_t>(i)] = currentOscilloscopeSamples[static_cast<size_t>(start + i)];

        spectrumWindow.multiplyWithWindowingTable(spectrumFftBuffer.data(), kSpectrumFftSize);
        spectrumFft.performFrequencyOnlyForwardTransform(spectrumFftBuffer.data());

        constexpr int maxBin = (kSpectrumFftSize / 2) - 1;

        constexpr float minBinF = 1.0f;
        const float maxBinF = static_cast<float>(maxBin);
        const float binRatio = maxBinF / minBinF;

        for (int bandIndex = 0; bandIndex < kSpectrumBandCount; ++bandIndex)
        {
            const float startNorm = static_cast<float>(bandIndex) / static_cast<float>(kSpectrumBandCount);
            const float endNorm = static_cast<float>(bandIndex + 1) / static_cast<float>(kSpectrumBandCount);

            const int binStart = juce::jlimit(1,
                                              maxBin,
                                              static_cast<int>(std::floor(minBinF * std::pow(binRatio, startNorm))));
            const int binEnd = juce::jlimit(binStart + 1,
                                            maxBin,
                                            static_cast<int>(std::ceil(minBinF * std::pow(binRatio, endNorm))));

            float maxMagnitude = 0.0f;
            for (int bin = binStart; bin <= binEnd; ++bin)
                maxMagnitude = juce::jmax(maxMagnitude, spectrumFftBuffer[static_cast<size_t>(bin)]);

            const float normalizedMag = maxMagnitude / static_cast<float>(kSpectrumFftSize);
            const float db = juce::Decibels::gainToDecibels(normalizedMag, kNoiseFloorDb);
            const float mapped = juce::jlimit(0.0f, 1.0f, (db - kNoiseFloorDb) / -kNoiseFloorDb);
            spectrumBands[static_cast<size_t>(bandIndex)] = juce::jmax(spectrumBands[static_cast<size_t>(bandIndex)], mapped);
            if (spectrumBands[static_cast<size_t>(bandIndex)] >= spectrumPeakIndicators[static_cast<size_t>(bandIndex)])
            {
                spectrumPeakIndicators[static_cast<size_t>(bandIndex)] = spectrumBands[static_cast<size_t>(bandIndex)];
                spectrumPeakFallVelocity[static_cast<size_t>(bandIndex)] = 0.0f;
            }
        }
    }

    void WaveformPanel::ensureSpectrogramHistorySize(int widthPixels)
    {
        const int clampedWidth = juce::jmax(1, widthPixels);
        if (static_cast<int>(spectrogramColumns.size()) == clampedWidth)
            return;

        spectrogramColumns.clear();
        spectrogramColumns.resize(static_cast<size_t>(clampedWidth));
        spectrogramWriteIndex = 0;
        spectrogramFilledColumns = 0;
        spectrogramScrollAccumulator = 0.0f;
    }

    void WaveformPanel::updateSpectrogram()
    {
        ensureSpectrogramHistorySize(juce::jmax(1, getLocalBounds().reduced(2).getWidth()));
        if (spectrogramColumns.empty())
            return;

        std::array<float, kSpectrogramBinCount> column{};

        if (currentOscilloscopeSamples.size() >= 2)
        {
            std::fill(spectrumFftBuffer.begin(), spectrumFftBuffer.end(), 0.0f);

            const int available = juce::jmin(static_cast<int>(currentOscilloscopeSamples.size()), kSpectrumFftSize);
            const int start = static_cast<int>(currentOscilloscopeSamples.size()) - available;
            for (int i = 0; i < available; ++i)
                spectrumFftBuffer[static_cast<size_t>(i)] = currentOscilloscopeSamples[static_cast<size_t>(start + i)];

            spectrumWindow.multiplyWithWindowingTable(spectrumFftBuffer.data(), kSpectrumFftSize);
            spectrumFft.performFrequencyOnlyForwardTransform(spectrumFftBuffer.data());

            constexpr float kNoiseFloorDb = -80.0f;
            constexpr int maxBin = (kSpectrumFftSize / 2) - 1;
            constexpr float minBinF = 1.0f;
            const float maxBinF = static_cast<float>(maxBin);
            const float binRatio = maxBinF / minBinF;

            for (int binIndex = 0; binIndex < kSpectrogramBinCount; ++binIndex)
            {
                const float startNorm = static_cast<float>(binIndex) / static_cast<float>(kSpectrogramBinCount);
                const float endNorm = static_cast<float>(binIndex + 1) / static_cast<float>(kSpectrogramBinCount);

                const int binStart = juce::jlimit(1,
                                                  maxBin,
                                                  static_cast<int>(std::floor(minBinF * std::pow(binRatio, startNorm))));
                const int binEnd = juce::jlimit(binStart + 1,
                                                maxBin,
                                                static_cast<int>(std::ceil(minBinF * std::pow(binRatio, endNorm))));

                float maxMagnitude = 0.0f;
                for (int bin = binStart; bin <= binEnd; ++bin)
                    maxMagnitude = juce::jmax(maxMagnitude, spectrumFftBuffer[static_cast<size_t>(bin)]);

                const float normalizedMag = maxMagnitude / static_cast<float>(kSpectrumFftSize);
                const float db = juce::Decibels::gainToDecibels(normalizedMag, kNoiseFloorDb);
                const float mapped = juce::jlimit(0.0f, 1.0f, (db - kNoiseFloorDb) / -kNoiseFloorDb);
                column[static_cast<size_t>(binIndex)] = std::pow(mapped, 0.85f);
            }
        }

        spectrogramScrollAccumulator += kSpectrogramColumnsPerUpdate;
        int columnsToAdvance = static_cast<int>(spectrogramScrollAccumulator);
        spectrogramScrollAccumulator -= static_cast<float>(columnsToAdvance);

        if (columnsToAdvance <= 0)
            columnsToAdvance = 1;

        const int historySize = static_cast<int>(spectrogramColumns.size());
        for (int i = 0; i < columnsToAdvance; ++i)
        {
            spectrogramColumns[static_cast<size_t>(spectrogramWriteIndex)] = column;
            spectrogramWriteIndex = (spectrogramWriteIndex + 1) % historySize;
            spectrogramFilledColumns = juce::jmin(spectrogramFilledColumns + 1, historySize);
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
