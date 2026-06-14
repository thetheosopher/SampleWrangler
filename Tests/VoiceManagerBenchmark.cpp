#include "Audio/VoiceManager.h"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;

    struct Scenario
    {
        std::string_view name;
        int sourceChannels = 2;
        int outputChannels = 2;
        int sourceSamples = 2048;
        bool preserveLength = false;
        double pitchSemitones = 0.0;
        bool useLoop = false;
        int loopStartSample = 0;
        int loopEndSample = 0;
        int blockSize = 64;
    };

    struct Result
    {
        double microsecondsPerBlock = 0.0;
        double nanosecondsPerFrame = 0.0;
        double realtimeFactor = 0.0;
        double checksum = 0.0;
    };

    std::unique_ptr<juce::AudioBuffer<float>> makeTestBuffer(int channels, int samples)
    {
        auto buffer = std::make_unique<juce::AudioBuffer<float>>(channels, samples);

        for (int ch = 0; ch < channels; ++ch)
        {
            auto *write = buffer->getWritePointer(ch);
            for (int i = 0; i < samples; ++i)
            {
                const float phase = static_cast<float>(i) * 0.173f;
                const float channelScale = 1.0f - (0.15f * static_cast<float>(ch));
                write[i] = std::sin(phase) * channelScale;
            }
        }

        return buffer;
    }

    double renderBlock(sw::VoiceManager &manager,
                       juce::AudioBuffer<float> &block,
                       int blockSize)
    {
        block.clear();
        juce::AudioSourceChannelInfo info(&block, 0, blockSize);
        manager.getNextAudioBlock(info);

        double checksum = 0.0;
        for (int ch = 0; ch < block.getNumChannels(); ++ch)
        {
            const float *read = block.getReadPointer(ch);
            for (int i = 0; i < blockSize; ++i)
                checksum += std::abs(read[i]);
        }

        return checksum;
    }

    void prepareScenario(sw::VoiceManager &manager,
                         const Scenario &scenario)
    {
        manager.prepareToPlay(scenario.blockSize, kSampleRate);
        manager.setPreserveLengthEnabled(scenario.preserveLength);
        manager.setStretchHighQualityEnabled(false);
        manager.setLoopEnabled(scenario.useLoop);
        if (scenario.useLoop)
            manager.setLoopRegionSamples(scenario.loopStartSample, scenario.loopEndSample);

        manager.loadBuffer(makeTestBuffer(scenario.sourceChannels, scenario.sourceSamples), kSampleRate);
        manager.play();

        if (scenario.preserveLength && std::abs(scenario.pitchSemitones) > 1.0e-6)
            manager.updateAllVoicePitch(scenario.pitchSemitones);
    }

    std::optional<Result> runScenario(const Scenario &scenario,
                                      int warmupBlocks,
                                      int measuredBlocks)
    {
        sw::VoiceManager manager;
        prepareScenario(manager, scenario);
        juce::AudioBuffer<float> block(scenario.outputChannels, scenario.blockSize);

        for (int i = 0; i < warmupBlocks; ++i)
        {
            const double checksum = renderBlock(manager, block, scenario.blockSize);
            if (!manager.isPlaying() || checksum <= 0.0)
                return std::nullopt;
        }

        double checksum = 0.0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < measuredBlocks; ++i)
        {
            checksum += renderBlock(manager, block, scenario.blockSize);
            if (!manager.isPlaying())
                return std::nullopt;
        }
        const auto end = std::chrono::steady_clock::now();

        const auto elapsed = std::chrono::duration<double>(end - start).count();
        const double framesRendered = static_cast<double>(measuredBlocks * scenario.blockSize);

        Result result;
        result.microsecondsPerBlock = (elapsed * 1.0e6) / static_cast<double>(measuredBlocks);
        result.nanosecondsPerFrame = (elapsed * 1.0e9) / framesRendered;
        result.realtimeFactor = (framesRendered / kSampleRate) / elapsed;
        result.checksum = checksum;
        return result;
    }

    int parseIntArg(const std::vector<std::string> &args,
                    std::string_view flag,
                    int defaultValue)
    {
        for (size_t i = 0; i + 1 < args.size(); ++i)
        {
            if (args[i] == flag)
                return std::max(1, std::stoi(args[i + 1]));
        }

        return defaultValue;
    }
}

int main(int argc, char **argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    const int warmupBlocks = parseIntArg(args, "--warmup-blocks", 128);
    const int measuredBlocks = parseIntArg(args, "--measured-blocks", 4096);

    const std::vector<Scenario> scenarios{
        {"direct-mono-to-stereo", 1, 2, 4096, false, 0.0, true, 256, 4095, 64},
        {"direct-stereo-looped", 2, 2, 4096, false, 0.0, true, 256, 4095, 64},
        {"direct-wide-output-4src", 4, 34, 4096, false, 0.0, true, 256, 4095, 64},
        {"direct-wide-source-34src", 34, 36, 4096, false, 0.0, true, 256, 4095, 64},
        {"preserve-mono-to-stereo", 1, 2, 4096, true, 7.0, true, 256, 4095, 64},
        {"preserve-stereo-looped", 2, 2, 4096, true, 7.0, true, 256, 4095, 64},
        {"preserve-wide-output-4src", 4, 34, 4096, true, 7.0, true, 256, 4095, 64},
        {"preserve-wide-source-34src", 34, 36, 4096, true, 7.0, true, 256, 4095, 64},
        {"preserve-wide-output-4src-b256", 4, 34, 4096, true, 7.0, true, 256, 4095, 256},
        {"preserve-wide-source-34src-b256", 34, 36, 4096, true, 7.0, true, 256, 4095, 256},
        {"preserve-wide-output-4src-b512", 4, 34, 4096, true, 7.0, true, 256, 4095, 512},
        {"preserve-wide-source-34src-b512", 34, 36, 4096, true, 7.0, true, 256, 4095, 512},
    };

    std::cout << "SampleWrangler VoiceManager benchmark\n";
    std::cout << "warmupBlocks=" << warmupBlocks << ", measuredBlocks=" << measuredBlocks << "\n\n";
    std::cout << std::left << std::setw(28) << "scenario"
              << std::right << std::setw(8) << "src"
              << std::setw(8) << "out"
              << std::setw(16) << "us/block"
              << std::setw(16) << "ns/frame"
              << std::setw(16) << "x realtime"
              << std::setw(16) << "checksum"
              << "\n";

    std::cout << std::string(108, '-') << "\n";
    std::cout << std::fixed << std::setprecision(2);

    for (const auto &scenario : scenarios)
    {
        const auto result = runScenario(scenario, warmupBlocks, measuredBlocks);
        if (!result.has_value())
        {
            std::cerr << "Benchmark scenario failed to stay active: " << scenario.name << "\n";
            return 1;
        }

        std::cout << std::left << std::setw(28) << scenario.name
                  << std::right << std::setw(8) << scenario.sourceChannels
                  << std::setw(8) << scenario.outputChannels
                  << std::setw(16) << result->microsecondsPerBlock
                  << std::setw(16) << result->nanosecondsPerFrame
                  << std::setw(16) << result->realtimeFactor
                  << std::setw(16) << result->checksum
                  << "\n";
    }

    return 0;
}