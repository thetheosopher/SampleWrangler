#include "WaveformPeak.h"
#include "Util/Hashing.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>

namespace sw
{

    float peakAbsScalar(const float *samples, int numSamples)
    {
        if (samples == nullptr || numSamples <= 0)
            return 0.0f;

        float maxAbs = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            maxAbs = std::max(maxAbs, std::abs(samples[i]));

        return maxAbs;
    }

    float peakAbsVectorized(const float *samples, int numSamples)
    {
        if (samples == nullptr || numSamples <= 0)
            return 0.0f;

        const auto range = juce::FloatVectorOperations::findMinAndMax(samples, numSamples);
        return std::max(std::abs(range.getStart()), std::abs(range.getEnd()));
    }

    std::string buildWaveformCacheKey(int64_t rootId, const std::string &relativePath,
                                      int64_t sizeBytes, int64_t modifiedTime)
    {
        return hashString(std::to_string(rootId) + "|" +
                          relativePath + "|" +
                          std::to_string(sizeBytes) + "|" +
                          std::to_string(modifiedTime));
    }

} // namespace sw
