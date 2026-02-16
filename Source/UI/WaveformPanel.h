#pragma once

#include <JuceHeader.h>
#include <functional>

namespace sw
{

    /// Waveform overview display with cached peaks.
    class WaveformPanel final : public juce::Component
    {
    public:
        WaveformPanel();
        ~WaveformPanel() override = default;

        void paint(juce::Graphics &g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent &event) override;
        void mouseDrag(const juce::MouseEvent &event) override;

        /// Load waveform peaks for display. Called from a background thread result.
        void setPeaks(const std::vector<std::vector<float>> &peaksByChannel);
        void setPlayheadNormalized(float playheadPosition);
        void setLoopRegionNormalized(float loopStart, float loopEnd);
        void setDarkMode(bool enabled);

        std::function<void(float normalizedPosition)> onScrubRequested;

    private:
        float normalizedPositionFromX(int x) const;

        std::vector<std::vector<float>> currentPeaksByChannel;
        float playheadNormalized = -1.0f;
        float loopStartNormalized = -1.0f;
        float loopEndNormalized = -1.0f;
        bool darkModeEnabled = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformPanel)
    };

} // namespace sw
