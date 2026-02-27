#include "Pipeline/WaveformPeak.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
    constexpr float kEpsilon = 1.0e-6f;

    bool almostEqual(float a, float b)
    {
        return std::abs(a - b) <= kEpsilon;
    }

    bool testNullAndEmptyInputs()
    {
        const float scalarNull = sw::peakAbsScalar(nullptr, 0);
        const float vectorNull = sw::peakAbsVectorized(nullptr, 0);

        if (!almostEqual(scalarNull, 0.0f) || !almostEqual(vectorNull, 0.0f))
            return false;

        const std::array<float, 4> buffer{0.0f, -1.0f, 1.0f, 0.5f};
        const float scalarEmpty = sw::peakAbsScalar(buffer.data(), 0);
        const float vectorEmpty = sw::peakAbsVectorized(buffer.data(), 0);

        return almostEqual(scalarEmpty, 0.0f) && almostEqual(vectorEmpty, 0.0f);
    }

    bool testKnownValues()
    {
        const std::array<float, 9> values{0.0f, -0.25f, 0.9f, -1.0f, 0.1f, -0.7f, 0.8f, 0.2f, -0.5f};
        const float scalar = sw::peakAbsScalar(values.data(), static_cast<int>(values.size()));
        const float vectorized = sw::peakAbsVectorized(values.data(), static_cast<int>(values.size()));

        return almostEqual(scalar, 1.0f) && almostEqual(vectorized, 1.0f) && almostEqual(scalar, vectorized);
    }

    bool testDeterministicRandomData()
    {
        uint32_t state = 0x12345678u;
        std::vector<float> values;
        values.reserve(4099);

        for (int i = 0; i < 4099; ++i)
        {
            state = state * 1664525u + 1013904223u;
            const float normalized = static_cast<float>(state & 0x00FFFFFFu) / 8388608.0f - 1.0f;
            values.push_back(normalized * 2.0f);
        }

        const float scalar = sw::peakAbsScalar(values.data(), static_cast<int>(values.size()));
        const float vectorized = sw::peakAbsVectorized(values.data(), static_cast<int>(values.size()));
        return almostEqual(scalar, vectorized);
    }
}

int main()
{
    if (!testNullAndEmptyInputs())
    {
        std::cerr << "testNullAndEmptyInputs failed.\n";
        return 1;
    }

    if (!testKnownValues())
    {
        std::cerr << "testKnownValues failed.\n";
        return 1;
    }

    if (!testDeterministicRandomData())
    {
        std::cerr << "testDeterministicRandomData failed.\n";
        return 1;
    }

    return 0;
}
