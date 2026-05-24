#include "Audio/VoiceManager.h"

#include <cmath>
#include <iostream>
#include <memory>

namespace
{
    constexpr double kSampleRate = 48000.0;

    struct BlockStats
    {
        double totalAbs = 0.0;
        bool playbackFinishedSeen = false;
        double progress = 0.0;
        bool anyPlaying = false;
        bool primaryPlaying = false;
    };

    struct RenderStats
    {
        double totalAbs = 0.0;
        int renderedBlocks = 0;
        bool playbackFinishedSeen = false;
        double lastProgress = 0.0;
        bool anyPlaying = false;
        bool primaryPlaying = false;
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

    BlockStats renderBlock(sw::VoiceManager &manager,
                           int outputChannels,
                           int blockSize)
    {
        BlockStats stats;
        juce::AudioBuffer<float> block(outputChannels, blockSize);
        block.clear();

        juce::AudioSourceChannelInfo info(&block, 0, blockSize);
        manager.getNextAudioBlock(info);

        stats.playbackFinishedSeen = manager.consumePlaybackFinishedFlag();
        stats.progress = manager.getPlaybackProgressNormalized();
        stats.anyPlaying = manager.isPlaying();
        stats.primaryPlaying = manager.isPrimaryPlaying();

        for (int ch = 0; ch < outputChannels; ++ch)
        {
            const auto *read = block.getReadPointer(ch);
            for (int i = 0; i < blockSize; ++i)
                stats.totalAbs += std::abs(read[i]);
        }

        return stats;
    }

    void accumulateRenderStats(RenderStats &aggregate,
                               const BlockStats &blockStats)
    {
        aggregate.totalAbs += blockStats.totalAbs;
        ++aggregate.renderedBlocks;
        aggregate.playbackFinishedSeen = aggregate.playbackFinishedSeen || blockStats.playbackFinishedSeen;
        aggregate.lastProgress = blockStats.progress;
        aggregate.anyPlaying = blockStats.anyPlaying;
        aggregate.primaryPlaying = blockStats.primaryPlaying;
    }

    RenderStats renderUntilIdle(sw::VoiceManager &manager,
                                int outputChannels,
                                int blockSize,
                                int maxBlocks)
    {
        RenderStats stats;

        for (int blockIndex = 0; blockIndex < maxBlocks; ++blockIndex)
        {
            const auto blockStats = renderBlock(manager, outputChannels, blockSize);
            accumulateRenderStats(stats, blockStats);

            if (!blockStats.anyPlaying)
                break;
        }

        return stats;
    }

    bool testPrimaryPlaybackCompletesAndSignalsFinished()
    {
        constexpr int kBlockSize = 32;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.loadBuffer(makeTestBuffer(1, 96), kSampleRate);
        manager.play();

        const auto stats = renderUntilIdle(manager, 2, kBlockSize, 32);
        return stats.totalAbs > 1.0 && stats.renderedBlocks > 0 && !manager.isPlaying() && stats.playbackFinishedSeen && stats.lastProgress > 0.95;
    }

    bool testPreserveLengthPlaybackOutlastsDirectResampling()
    {
        constexpr int kBlockSize = 16;

        sw::VoiceManager directManager;
        directManager.prepareToPlay(kBlockSize, kSampleRate);
        directManager.setPreserveLengthEnabled(false);
        directManager.setStretchHighQualityEnabled(false);
        directManager.loadBuffer(makeTestBuffer(1, 128), kSampleRate);
        directManager.noteOn(60, 2.0, 2.0);
        const auto directStats = renderUntilIdle(directManager, 2, kBlockSize, 32);

        sw::VoiceManager preserveManager;
        preserveManager.prepareToPlay(kBlockSize, kSampleRate);
        preserveManager.setPreserveLengthEnabled(true);
        preserveManager.setStretchHighQualityEnabled(false);
        preserveManager.loadBuffer(makeTestBuffer(1, 128), kSampleRate);
        preserveManager.noteOn(60, 2.0, 2.0);
        const auto preserveStats = renderUntilIdle(preserveManager, 2, kBlockSize, 32);

        return directStats.totalAbs > 1.0 && preserveStats.totalAbs > 1.0 && preserveStats.renderedBlocks > directStats.renderedBlocks && !directManager.isPlaying() && !preserveManager.isPlaying();
    }

    bool testLoopedPrimaryPlaybackRemainsActiveAndBounded()
    {
        constexpr int kBlockSize = 8;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setLoopEnabled(true);
        manager.setLoopRegionSamples(8, 15);
        manager.loadBuffer(makeTestBuffer(1, 64), kSampleRate);
        manager.play();

        RenderStats loopStats;
        for (int blockIndex = 0; blockIndex < 20; ++blockIndex)
        {
            const auto blockStats = renderBlock(manager, 2, kBlockSize);
            accumulateRenderStats(loopStats, blockStats);

            if (!blockStats.primaryPlaying)
                return false;
        }

        manager.stop();
        const auto stopStats = renderUntilIdle(manager, 2, kBlockSize, 64);

        return loopStats.totalAbs > 1.0 && !loopStats.playbackFinishedSeen && loopStats.lastProgress > 0.10 && loopStats.lastProgress < 0.35 && stopStats.playbackFinishedSeen && !manager.isPlaying();
    }

    bool testScrubResetRepositionsPrimaryPreserveLengthPlayback()
    {
        constexpr int kBlockSize = 8;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(true);
        manager.setStretchHighQualityEnabled(false);
        manager.loadBuffer(makeTestBuffer(1, 256), kSampleRate);
        manager.play();
        manager.updateAllVoicePitch(7.0);

        auto initialStats = renderBlock(manager, 2, kBlockSize);
        initialStats = renderBlock(manager, 2, kBlockSize);

        manager.setPlaybackProgressNormalized(0.25);
        const auto scrubbedStats = renderBlock(manager, 2, kBlockSize);

        return initialStats.totalAbs > 0.1 && scrubbedStats.totalAbs > 0.1 && scrubbedStats.primaryPlaying && scrubbedStats.progress > 0.24 && scrubbedStats.progress < 0.35 && scrubbedStats.progress > initialStats.progress + 0.10;
    }

    bool testHighQualityPreserveLengthPlaybackCompletes()
    {
        constexpr int kBlockSize = 32;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(true);
        manager.setStretchHighQualityEnabled(true);
        manager.loadBuffer(makeTestBuffer(1, 2048), kSampleRate);
        manager.play();
        manager.updateAllVoicePitch(5.0);

        const auto highQualityCompletion = renderUntilIdle(manager, 2, kBlockSize, 128);

        return highQualityCompletion.totalAbs > 0.1 && highQualityCompletion.playbackFinishedSeen && !manager.isPlaying();
    }

    bool testLiveHighQualityToggleDefersUntilNextPreserveLengthPlayback()
    {
        constexpr int kBlockSize = 32;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(true);
        manager.setStretchHighQualityEnabled(false);
        manager.loadBuffer(makeTestBuffer(1, 2048), kSampleRate);
        manager.play();
        manager.updateAllVoicePitch(5.0);

        const auto warmupStats = renderBlock(manager, 2, kBlockSize);

        manager.setStretchHighQualityEnabled(true);
        RenderStats inFlightStats;
        for (int blockIndex = 0; blockIndex < 6; ++blockIndex)
        {
            const auto blockStats = renderBlock(manager, 2, kBlockSize);
            accumulateRenderStats(inFlightStats, blockStats);
        }

        const auto deferredCompletion = renderUntilIdle(manager, 2, kBlockSize, 128);

        manager.setPlaybackProgressNormalized(0.0);
        manager.play();
        manager.updateAllVoicePitch(5.0);
        const auto nextNoteHighQualityCompletion = renderUntilIdle(manager, 2, kBlockSize, 128);

        return warmupStats.totalAbs > 0.1 && warmupStats.primaryPlaying && inFlightStats.totalAbs > 0.1 && inFlightStats.anyPlaying && !inFlightStats.playbackFinishedSeen && deferredCompletion.totalAbs > 0.1 && deferredCompletion.playbackFinishedSeen && nextNoteHighQualityCompletion.totalAbs > 0.1 && nextNoteHighQualityCompletion.playbackFinishedSeen && !manager.isPlaying();
    }

    bool testPreserveLengthRespectsSourceSampleRate()
    {
        constexpr int kBlockSize = 16;
        constexpr double kSourceSampleRate = 24000.0;

        sw::VoiceManager directManager;
        directManager.prepareToPlay(kBlockSize, kSampleRate);
        directManager.setPreserveLengthEnabled(false);
        directManager.setStretchHighQualityEnabled(false);
        directManager.loadBuffer(makeTestBuffer(1, 128), kSourceSampleRate);
        directManager.noteOn(60, 1.5, 1.5);
        const auto directStats = renderUntilIdle(directManager, 2, kBlockSize, 32);

        sw::VoiceManager preserveManager;
        preserveManager.prepareToPlay(kBlockSize, kSampleRate);
        preserveManager.setPreserveLengthEnabled(true);
        preserveManager.setStretchHighQualityEnabled(false);
        preserveManager.loadBuffer(makeTestBuffer(1, 128), kSourceSampleRate);
        preserveManager.noteOn(60, 1.5, 1.5);
        const auto preserveStats = renderUntilIdle(preserveManager, 2, kBlockSize, 32);

        return directStats.totalAbs > 1.0 && preserveStats.totalAbs > 1.0 && preserveStats.renderedBlocks > directStats.renderedBlocks && !directManager.isPlaying() && !preserveManager.isPlaying();
    }
}

int main()
{
    if (!testPrimaryPlaybackCompletesAndSignalsFinished())
    {
        std::cerr << "testPrimaryPlaybackCompletesAndSignalsFinished failed.\n";
        return 1;
    }

    if (!testPreserveLengthPlaybackOutlastsDirectResampling())
    {
        std::cerr << "testPreserveLengthPlaybackOutlastsDirectResampling failed.\n";
        return 1;
    }

    if (!testLoopedPrimaryPlaybackRemainsActiveAndBounded())
    {
        std::cerr << "testLoopedPrimaryPlaybackRemainsActiveAndBounded failed.\n";
        return 1;
    }

    if (!testScrubResetRepositionsPrimaryPreserveLengthPlayback())
    {
        std::cerr << "testScrubResetRepositionsPrimaryPreserveLengthPlayback failed.\n";
        return 1;
    }

    if (!testHighQualityPreserveLengthPlaybackCompletes())
    {
        std::cerr << "testHighQualityPreserveLengthPlaybackCompletes failed.\n";
        return 1;
    }

    if (!testLiveHighQualityToggleDefersUntilNextPreserveLengthPlayback())
    {
        std::cerr << "testLiveHighQualityToggleDefersUntilNextPreserveLengthPlayback failed.\n";
        return 1;
    }

    if (!testPreserveLengthRespectsSourceSampleRate())
    {
        std::cerr << "testPreserveLengthRespectsSourceSampleRate failed.\n";
        return 1;
    }

    return 0;
}