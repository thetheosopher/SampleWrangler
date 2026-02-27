#pragma once

namespace sw
{

    float peakAbsScalar(const float *samples, int numSamples);
    float peakAbsVectorized(const float *samples, int numSamples);

} // namespace sw
