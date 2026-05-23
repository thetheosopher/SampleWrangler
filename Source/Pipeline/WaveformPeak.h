#pragma once

#include <cstdint>
#include <string>

namespace sw
{

    float peakAbsScalar(const float *samples, int numSamples);
    float peakAbsVectorized(const float *samples, int numSamples);
    std::string buildWaveformCacheKey(int64_t rootId, const std::string &relativePath,
                                      int64_t sizeBytes, int64_t modifiedTime);

} // namespace sw
