#pragma once

#include <JuceHeader.h>
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
            compositeOscilloscope
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
        float normalizedPositionFromX(int x) const;

        std::vector<std::vector<float>> currentPeaksByChannel;
        std::vector<float> currentOscilloscopeSamples;
        float playheadNormalized = -1.0f;
        float loopStartNormalized = -1.0f;
        float loopEndNormalized = -1.0f;
        bool loading = false;
        bool darkModeEnabled = true;
        DisplayMode displayMode = DisplayMode::waveform;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformPanel)
    };

} // namespace sw
