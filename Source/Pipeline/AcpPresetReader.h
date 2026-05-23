#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <cstdint>
#include <memory>
#include <optional>

namespace sw
{

    struct AcpPresetData
    {
        juce::String presetName;
        std::shared_ptr<juce::AudioBuffer<float>> embeddedSampleBuffer;
        juce::String externalSamplePath;
        double sampleRate = 0.0;
        int channels = 0;
        int rootMidiNote = 60;
        std::optional<int64_t> totalSamples;
        std::optional<double> durationSec;
        std::optional<int64_t> loopStartSample;
        std::optional<int64_t> loopEndSample;
        std::optional<double> bpm;
        bool loopPlayback = false;

        bool hasPreviewAudio() const noexcept
        {
            return embeddedSampleBuffer != nullptr && embeddedSampleBuffer->getNumSamples() > 0;
        }
    };

    class AcpPresetReader final
    {
    public:
        static std::optional<AcpPresetData> readPreset(const juce::File &presetFile);
        static juce::File resolveReferencedSampleFile(const juce::File &presetFile,
                                                      const juce::String &referencedPath);
    };

} // namespace sw