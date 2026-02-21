#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>

namespace sw
{

    /// Waveform overview display with cached peaks.
    class WaveformPanel final : public juce::Component
    {
    public:
        enum class DisplayMode
        {
            waveform,
            spectrogram,
            compositeOscilloscope,
            spectrumAnalyzer
        };

        WaveformPanel();
        ~WaveformPanel() override = default;

        void paint(juce::Graphics &g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent &event) override;
        void mouseDrag(const juce::MouseEvent &event) override;

        /// Load waveform peaks for display. Called from a background thread result.
        void setPeaks(const std::vector<std::vector<float>> &peaksByChannel);
        void setOscilloscopeSamples(const std::vector<float> &samples);
        void setPlayheadNormalized(float playheadPosition);
        void setLoopRegionNormalized(float loopStart, float loopEnd);
        void setLoading(bool loading);
        void setDarkMode(bool enabled);
        void setDisplayMode(DisplayMode mode);
        DisplayMode getDisplayMode() const noexcept;

        std::function<void(float normalizedPosition)> onScrubRequested;

    private:
        void paintWaveform(juce::Graphics &g, juce::Rectangle<float> bounds) const;
        void paintSpectrogram(juce::Graphics &g, juce::Rectangle<float> bounds) const;
        void paintCompositeOscilloscope(juce::Graphics &g, juce::Rectangle<float> bounds) const;
        void paintSpectrumAnalyzer(juce::Graphics &g, juce::Rectangle<float> bounds) const;
        void updateSpectrumAnalyzer();
        void updateSpectrogram();
        void ensureSpectrogramHistorySize(int widthPixels);
        float normalizedPositionFromX(int x) const;

        static constexpr int kSpectrumBandCount = 64;
        static constexpr int kSpectrumFftOrder = 12;
        static constexpr int kSpectrumFftSize = 1 << kSpectrumFftOrder;
        static constexpr int kSpectrogramBinCount = 128;
        static constexpr float kSpectrogramColumnsPerUpdate = 1.35f;

        std::vector<std::vector<float>> currentPeaksByChannel;
        std::vector<float> currentOscilloscopeSamples;
        std::array<float, kSpectrumBandCount> spectrumBands{};
        std::array<float, kSpectrumBandCount> spectrumPeakIndicators{};
        std::array<float, kSpectrumBandCount> spectrumPeakFallVelocity{};
        std::vector<std::array<float, kSpectrogramBinCount>> spectrogramColumns;
        int spectrogramWriteIndex = 0;
        int spectrogramFilledColumns = 0;
        float spectrogramScrollAccumulator = 0.0f;
        std::array<float, kSpectrumFftSize * 2> spectrumFftBuffer{};
        juce::dsp::FFT spectrumFft{kSpectrumFftOrder};
        juce::dsp::WindowingFunction<float> spectrumWindow{kSpectrumFftSize, juce::dsp::WindowingFunction<float>::hann, true};
        float playheadNormalized = -1.0f;
        float loopStartNormalized = -1.0f;
        float loopEndNormalized = -1.0f;
        bool loading = false;
        bool darkModeEnabled = true;
        DisplayMode displayMode = DisplayMode::waveform;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformPanel)
    };

} // namespace sw
