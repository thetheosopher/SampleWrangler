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

    double sumChannelAbs(const juce::AudioBuffer<float> &buffer,
                         int channel,
                         int samples)
    {
        double totalAbs = 0.0;
        const float *read = buffer.getReadPointer(channel);
        for (int i = 0; i < samples; ++i)
            totalAbs += std::abs(read[i]);

        return totalAbs;
    }

    double sumChannelDifferenceAbs(const juce::AudioBuffer<float> &buffer,
                                   int channelA,
                                   int channelB,
                                   int samples)
    {
        double totalAbs = 0.0;
        const float *readA = buffer.getReadPointer(channelA);
        const float *readB = buffer.getReadPointer(channelB);
        for (int i = 0; i < samples; ++i)
            totalAbs += std::abs(readA[i] - readB[i]);

        return totalAbs;
    }

    BlockStats renderBlock(sw::VoiceManager &manager,
                           juce::AudioBuffer<float> &block,
                           int blockSize)
    {
        BlockStats stats;
        block.clear();

        juce::AudioSourceChannelInfo info(&block, 0, blockSize);
        manager.getNextAudioBlock(info);

        stats.playbackFinishedSeen = manager.consumePlaybackFinishedFlag();
        stats.progress = manager.getPlaybackProgressNormalized();
        stats.anyPlaying = manager.isPlaying();
        stats.primaryPlaying = manager.isPrimaryPlaying();

        for (int ch = 0; ch < block.getNumChannels(); ++ch)
        {
            const auto *read = block.getReadPointer(ch);
            for (int i = 0; i < blockSize; ++i)
                stats.totalAbs += std::abs(read[i]);
        }

        return stats;
    }

    BlockStats renderBlock(sw::VoiceManager &manager,
                           int outputChannels,
                           int blockSize)
    {
        juce::AudioBuffer<float> block(outputChannels, blockSize);
        return renderBlock(manager, block, blockSize);
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

    bool testLoopedPreserveLengthPlaybackRemainsActiveAndBounded()
    {
        constexpr int kBlockSize = 16;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(true);
        manager.setStretchHighQualityEnabled(false);
        manager.setLoopEnabled(true);
        manager.setLoopRegionSamples(64, 127);
        manager.loadBuffer(makeTestBuffer(1, 256), kSampleRate);
        manager.play();
        manager.updateAllVoicePitch(7.0);

        RenderStats loopStats;
        for (int blockIndex = 0; blockIndex < 24; ++blockIndex)
        {
            const auto blockStats = renderBlock(manager, 2, kBlockSize);
            accumulateRenderStats(loopStats, blockStats);

            if (!blockStats.primaryPlaying)
                return false;
        }

        manager.stop();
        const auto stopStats = renderUntilIdle(manager, 2, kBlockSize, 64);

        return loopStats.totalAbs > 1.0 && !loopStats.playbackFinishedSeen && loopStats.lastProgress > 0.20 && loopStats.lastProgress < 0.55 && stopStats.playbackFinishedSeen && !manager.isPlaying();
    }

    bool testLoopedPreserveLengthWrapsAcrossBoundaryCleanly()
    {
        constexpr int kBlockSize = 16;
        constexpr double kStartProgress = 126.0 / 256.0;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(true);
        manager.setStretchHighQualityEnabled(false);
        manager.setLoopEnabled(true);
        manager.setLoopRegionSamples(64, 127);
        manager.loadBuffer(makeTestBuffer(1, 256), kSampleRate);
        manager.play();
        manager.updateAllVoicePitch(7.0);
        manager.setPlaybackProgressNormalized(kStartProgress);

        const auto wrapStats = renderBlock(manager, 2, kBlockSize);

        return wrapStats.totalAbs > 0.1 && wrapStats.primaryPlaying && wrapStats.progress < kStartProgress && wrapStats.progress > 0.24 && wrapStats.progress < 0.50;
    }

    bool testLoopedPreserveLengthSurvivesRepeatedWrapStress()
    {
        constexpr int kBlockSize = 8;
        constexpr double kStartProgress = 126.0 / 256.0;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(true);
        manager.setStretchHighQualityEnabled(false);
        manager.setLoopEnabled(true);
        manager.setLoopRegionSamples(64, 127);
        manager.loadBuffer(makeTestBuffer(1, 256), kSampleRate);
        manager.play();
        manager.updateAllVoicePitch(7.0);
        manager.setPlaybackProgressNormalized(kStartProgress);

        int wrapCount = 0;
        double previousProgress = kStartProgress;
        double totalAbs = 0.0;

        for (int blockIndex = 0; blockIndex < 24; ++blockIndex)
        {
            const auto blockStats = renderBlock(manager, 2, kBlockSize);
            totalAbs += blockStats.totalAbs;

            if (!blockStats.primaryPlaying)
                return false;

            if (blockStats.progress + 0.05 < previousProgress)
                ++wrapCount;

            previousProgress = blockStats.progress;
        }

        manager.stop();
        const auto stopStats = renderUntilIdle(manager, 2, kBlockSize, 64);

        return totalAbs > 1.0 && wrapCount >= 2 && stopStats.playbackFinishedSeen && !manager.isPlaying();
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

    bool testShortClipHighQualityPreserveLengthFallsBackAudibly()
    {
        constexpr int kBlockSize = 16;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(true);
        manager.setStretchHighQualityEnabled(true);
        manager.loadBuffer(makeTestBuffer(1, 48), kSampleRate);
        manager.play();
        manager.updateAllVoicePitch(7.0);

        const auto stats = renderUntilIdle(manager, 2, kBlockSize, 64);

        return stats.totalAbs > 0.1 && stats.renderedBlocks > 0 && stats.playbackFinishedSeen && !manager.isPlaying();
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

    bool testStereoPreserveLengthMaintainsChannelSeparation()
    {
        constexpr int kBlockSize = 32;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(true);
        manager.setStretchHighQualityEnabled(false);
        manager.loadBuffer(makeTestBuffer(2, 256), kSampleRate);
        manager.noteOn(60, 1.5, 1.5);

        juce::AudioBuffer<float> block(2, kBlockSize);
        block.clear();
        juce::AudioSourceChannelInfo info(&block, 0, kBlockSize);
        manager.getNextAudioBlock(info);

        double leftAbs = 0.0;
        double rightAbs = 0.0;
        double channelDifferenceAbs = 0.0;
        const float *leftRead = block.getReadPointer(0);
        const float *rightRead = block.getReadPointer(1);

        for (int i = 0; i < kBlockSize; ++i)
        {
            leftAbs += std::abs(leftRead[i]);
            rightAbs += std::abs(rightRead[i]);
            channelDifferenceAbs += std::abs(leftRead[i] - rightRead[i]);
        }

        return leftAbs > 0.1 && rightAbs > 0.1 && channelDifferenceAbs > 0.01 && manager.isPlaying();
    }

    bool testWiderChannelPreserveLengthFallbackMaintainsRouting()
    {
        constexpr int kBlockSize = 32;
        constexpr int kOutputChannels = 34;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(true);
        manager.setStretchHighQualityEnabled(false);
        manager.loadBuffer(makeTestBuffer(4, 512), kSampleRate);
        manager.noteOn(60, 1.5, 1.5);

        juce::AudioBuffer<float> block(kOutputChannels, kBlockSize);
        const auto stats = renderBlock(manager, block, kBlockSize);

        const double channel0Abs = sumChannelAbs(block, 0, kBlockSize);
        const double channel1Abs = sumChannelAbs(block, 1, kBlockSize);
        const double channel2Abs = sumChannelAbs(block, 2, kBlockSize);
        const double channel3Abs = sumChannelAbs(block, 3, kBlockSize);
        const double channel32Abs = sumChannelAbs(block, 32, kBlockSize);
        const double channel33Abs = sumChannelAbs(block, 33, kBlockSize);

        const double channel01Diff = sumChannelDifferenceAbs(block, 0, 1, kBlockSize);
        const double channel12Diff = sumChannelDifferenceAbs(block, 1, 2, kBlockSize);
        const double channel032Diff = sumChannelDifferenceAbs(block, 0, 32, kBlockSize);
        const double channel033Diff = sumChannelDifferenceAbs(block, 0, 33, kBlockSize);

        return stats.totalAbs > 0.1 &&
               channel0Abs > 0.05 &&
               channel1Abs > 0.05 &&
               channel2Abs > 0.05 &&
               channel3Abs > 0.05 &&
               channel32Abs > 0.05 &&
               channel33Abs > 0.05 &&
               channel01Diff > 0.01 &&
               channel12Diff > 0.01 &&
               channel032Diff < 1.0e-4 &&
               channel033Diff < 1.0e-4 &&
               manager.isPlaying();
    }

    bool testWideSourcePreserveLengthFallbackMaintainsRouting()
    {
        constexpr int kBlockSize = 32;
        constexpr int kSourceChannels = 34;
        constexpr int kOutputChannels = 36;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(true);
        manager.setStretchHighQualityEnabled(false);
        manager.loadBuffer(makeTestBuffer(kSourceChannels, 512), kSampleRate);
        manager.noteOn(60, 1.5, 1.5);

        juce::AudioBuffer<float> block(kOutputChannels, kBlockSize);
        const auto stats = renderBlock(manager, block, kBlockSize);

        const double channel31Abs = sumChannelAbs(block, 31, kBlockSize);
        const double channel32Abs = sumChannelAbs(block, 32, kBlockSize);
        const double channel33Abs = sumChannelAbs(block, 33, kBlockSize);
        const double channel34Abs = sumChannelAbs(block, 34, kBlockSize);
        const double channel35Abs = sumChannelAbs(block, 35, kBlockSize);

        const double channel3132Diff = sumChannelDifferenceAbs(block, 31, 32, kBlockSize);
        const double channel3233Diff = sumChannelDifferenceAbs(block, 32, 33, kBlockSize);
        const double channel034Diff = sumChannelDifferenceAbs(block, 0, 34, kBlockSize);
        const double channel035Diff = sumChannelDifferenceAbs(block, 0, 35, kBlockSize);

        return stats.totalAbs > 0.1 &&
               channel31Abs > 0.02 &&
               channel32Abs > 0.02 &&
               channel33Abs > 0.02 &&
               channel34Abs > 0.02 &&
               channel35Abs > 0.02 &&
               channel3132Diff > 0.005 &&
               channel3233Diff > 0.005 &&
               channel034Diff < 1.0e-4 &&
               channel035Diff < 1.0e-4 &&
               manager.isPlaying();
    }

    bool testStereoDirectFadeInMaintainsChannelSeparation()
    {
        constexpr int kBlockSize = 32;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(false);
        manager.setStretchHighQualityEnabled(false);
        manager.loadBuffer(makeTestBuffer(2, 256), kSampleRate);
        manager.play();

        juce::AudioBuffer<float> block(2, kBlockSize);
        block.clear();
        juce::AudioSourceChannelInfo info(&block, 0, kBlockSize);
        manager.getNextAudioBlock(info);

        double leftAbs = 0.0;
        double rightAbs = 0.0;
        double channelDifferenceAbs = 0.0;
        const float *leftRead = block.getReadPointer(0);
        const float *rightRead = block.getReadPointer(1);

        for (int i = 0; i < kBlockSize; ++i)
        {
            leftAbs += std::abs(leftRead[i]);
            rightAbs += std::abs(rightRead[i]);
            channelDifferenceAbs += std::abs(leftRead[i] - rightRead[i]);
        }

        return leftAbs > 0.05 && rightAbs > 0.05 && channelDifferenceAbs > 0.01 && manager.isPlaying();
    }

    bool testStereoDirectPlaybackMaintainsChannelSeparation()
    {
        constexpr int kBlockSize = 32;

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(false);
        manager.setStretchHighQualityEnabled(false);
        manager.loadBuffer(makeTestBuffer(2, 1024), kSampleRate);
        manager.play();

        for (int blockIndex = 0; blockIndex < 8; ++blockIndex)
        {
            const auto warmupStats = renderBlock(manager, 2, kBlockSize);
            if (!warmupStats.anyPlaying)
                return false;
        }

        juce::AudioBuffer<float> block(2, kBlockSize);
        block.clear();
        juce::AudioSourceChannelInfo info(&block, 0, kBlockSize);
        manager.getNextAudioBlock(info);

        double leftAbs = 0.0;
        double rightAbs = 0.0;
        double channelDifferenceAbs = 0.0;
        const float *leftRead = block.getReadPointer(0);
        const float *rightRead = block.getReadPointer(1);

        for (int i = 0; i < kBlockSize; ++i)
        {
            leftAbs += std::abs(leftRead[i]);
            rightAbs += std::abs(rightRead[i]);
            channelDifferenceAbs += std::abs(leftRead[i] - rightRead[i]);
        }

        return leftAbs > 0.1 && rightAbs > 0.1 && channelDifferenceAbs > 0.01 && manager.isPlaying();
    }

    bool testWiderChannelDirectFallbackMaintainsRouting()
    {
        constexpr int kBlockSize = 32;
        constexpr int kOutputChannels = 34;

        auto routingLooksValid = [](const juce::AudioBuffer<float> &block)
        {
            const double channel0Abs = sumChannelAbs(block, 0, kBlockSize);
            const double channel1Abs = sumChannelAbs(block, 1, kBlockSize);
            const double channel2Abs = sumChannelAbs(block, 2, kBlockSize);
            const double channel3Abs = sumChannelAbs(block, 3, kBlockSize);
            const double channel32Abs = sumChannelAbs(block, 32, kBlockSize);
            const double channel33Abs = sumChannelAbs(block, 33, kBlockSize);

            const double channel01Diff = sumChannelDifferenceAbs(block, 0, 1, kBlockSize);
            const double channel12Diff = sumChannelDifferenceAbs(block, 1, 2, kBlockSize);
            const double channel032Diff = sumChannelDifferenceAbs(block, 0, 32, kBlockSize);
            const double channel033Diff = sumChannelDifferenceAbs(block, 0, 33, kBlockSize);

            return channel0Abs > 0.05 &&
                   channel1Abs > 0.05 &&
                   channel2Abs > 0.05 &&
                   channel3Abs > 0.05 &&
                   channel32Abs > 0.05 &&
                   channel33Abs > 0.05 &&
                   channel01Diff > 0.01 &&
                   channel12Diff > 0.01 &&
                   channel032Diff < 1.0e-4 &&
                   channel033Diff < 1.0e-4;
        };

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(false);
        manager.setStretchHighQualityEnabled(false);
        manager.loadBuffer(makeTestBuffer(4, 1024), kSampleRate);
        manager.play();

        juce::AudioBuffer<float> fadeInBlock(kOutputChannels, kBlockSize);
        const auto fadeInStats = renderBlock(manager, fadeInBlock, kBlockSize);

        for (int blockIndex = 0; blockIndex < 8; ++blockIndex)
        {
            const auto warmupStats = renderBlock(manager, kOutputChannels, kBlockSize);
            if (!warmupStats.anyPlaying)
                return false;
        }

        juce::AudioBuffer<float> steadyStateBlock(kOutputChannels, kBlockSize);
        const auto steadyStateStats = renderBlock(manager, steadyStateBlock, kBlockSize);

        return fadeInStats.totalAbs > 0.1 &&
               steadyStateStats.totalAbs > 0.1 &&
               routingLooksValid(fadeInBlock) &&
               routingLooksValid(steadyStateBlock) &&
               manager.isPlaying();
    }

    bool testWideSourceDirectFallbackMaintainsRouting()
    {
        constexpr int kBlockSize = 32;
        constexpr int kSourceChannels = 34;
        constexpr int kOutputChannels = 36;

        auto routingLooksValid = [](const juce::AudioBuffer<float> &block)
        {
            const double channel31Abs = sumChannelAbs(block, 31, kBlockSize);
            const double channel32Abs = sumChannelAbs(block, 32, kBlockSize);
            const double channel33Abs = sumChannelAbs(block, 33, kBlockSize);
            const double channel34Abs = sumChannelAbs(block, 34, kBlockSize);
            const double channel35Abs = sumChannelAbs(block, 35, kBlockSize);

            const double channel3132Diff = sumChannelDifferenceAbs(block, 31, 32, kBlockSize);
            const double channel3233Diff = sumChannelDifferenceAbs(block, 32, 33, kBlockSize);
            const double channel034Diff = sumChannelDifferenceAbs(block, 0, 34, kBlockSize);
            const double channel035Diff = sumChannelDifferenceAbs(block, 0, 35, kBlockSize);

            return channel31Abs > 0.02 &&
                   channel32Abs > 0.02 &&
                   channel33Abs > 0.02 &&
                   channel34Abs > 0.02 &&
                   channel35Abs > 0.02 &&
                   channel3132Diff > 0.005 &&
                   channel3233Diff > 0.005 &&
                   channel034Diff < 1.0e-4 &&
                   channel035Diff < 1.0e-4;
        };

        sw::VoiceManager manager;
        manager.prepareToPlay(kBlockSize, kSampleRate);
        manager.setPreserveLengthEnabled(false);
        manager.setStretchHighQualityEnabled(false);
        manager.loadBuffer(makeTestBuffer(kSourceChannels, 1024), kSampleRate);
        manager.play();

        juce::AudioBuffer<float> fadeInBlock(kOutputChannels, kBlockSize);
        const auto fadeInStats = renderBlock(manager, fadeInBlock, kBlockSize);

        for (int blockIndex = 0; blockIndex < 8; ++blockIndex)
        {
            const auto warmupStats = renderBlock(manager, kOutputChannels, kBlockSize);
            if (!warmupStats.anyPlaying)
                return false;
        }

        juce::AudioBuffer<float> steadyStateBlock(kOutputChannels, kBlockSize);
        const auto steadyStateStats = renderBlock(manager, steadyStateBlock, kBlockSize);

        return fadeInStats.totalAbs > 0.1 &&
               steadyStateStats.totalAbs > 0.1 &&
               routingLooksValid(fadeInBlock) &&
               routingLooksValid(steadyStateBlock) &&
               manager.isPlaying();
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

    if (!testLoopedPreserveLengthPlaybackRemainsActiveAndBounded())
    {
        std::cerr << "testLoopedPreserveLengthPlaybackRemainsActiveAndBounded failed.\n";
        return 1;
    }

    if (!testLoopedPreserveLengthWrapsAcrossBoundaryCleanly())
    {
        std::cerr << "testLoopedPreserveLengthWrapsAcrossBoundaryCleanly failed.\n";
        return 1;
    }

    if (!testLoopedPreserveLengthSurvivesRepeatedWrapStress())
    {
        std::cerr << "testLoopedPreserveLengthSurvivesRepeatedWrapStress failed.\n";
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

    if (!testShortClipHighQualityPreserveLengthFallsBackAudibly())
    {
        std::cerr << "testShortClipHighQualityPreserveLengthFallsBackAudibly failed.\n";
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

    if (!testStereoPreserveLengthMaintainsChannelSeparation())
    {
        std::cerr << "testStereoPreserveLengthMaintainsChannelSeparation failed.\n";
        return 1;
    }

    if (!testWiderChannelPreserveLengthFallbackMaintainsRouting())
    {
        std::cerr << "testWiderChannelPreserveLengthFallbackMaintainsRouting failed.\n";
        return 1;
    }

    if (!testWideSourcePreserveLengthFallbackMaintainsRouting())
    {
        std::cerr << "testWideSourcePreserveLengthFallbackMaintainsRouting failed.\n";
        return 1;
    }

    if (!testStereoDirectFadeInMaintainsChannelSeparation())
    {
        std::cerr << "testStereoDirectFadeInMaintainsChannelSeparation failed.\n";
        return 1;
    }

    if (!testStereoDirectPlaybackMaintainsChannelSeparation())
    {
        std::cerr << "testStereoDirectPlaybackMaintainsChannelSeparation failed.\n";
        return 1;
    }

    if (!testWiderChannelDirectFallbackMaintainsRouting())
    {
        std::cerr << "testWiderChannelDirectFallbackMaintainsRouting failed.\n";
        return 1;
    }

    if (!testWideSourceDirectFallbackMaintainsRouting())
    {
        std::cerr << "testWideSourceDirectFallbackMaintainsRouting failed.\n";
        return 1;
    }

    return 0;
}