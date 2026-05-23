#include "Voice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#if SW_HAVE_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

namespace sw
{

    namespace
    {
#if SW_HAVE_RUBBERBAND
        constexpr int kRubberBandMaxBlockSize = 4096;
        constexpr int kRubberBandOutputFifoSize = 32768;
        constexpr int kRubberBandOnsetBlendSamples = 256;

        struct RubberBandBuffers
        {
            std::array<std::array<float, kRubberBandMaxBlockSize>, Voice::kMaxChannels> input{};
            std::array<std::array<float, kRubberBandMaxBlockSize>, Voice::kMaxChannels> processOutput{};
            std::array<std::array<float, kRubberBandOutputFifoSize>, Voice::kMaxChannels> outputFifo{};
            std::array<const float *, Voice::kMaxChannels> inputPtrs{};
            std::array<float *, Voice::kMaxChannels> processOutputPtrs{};
        };

        auto makeRubberBandOptions()
        {
            return RubberBand::RubberBandStretcher::OptionProcessRealTime |
                   RubberBand::RubberBandStretcher::OptionThreadingNever |
                   RubberBand::RubberBandStretcher::OptionChannelsTogether |
                   RubberBand::RubberBandStretcher::OptionEngineFaster |
                   RubberBand::RubberBandStretcher::OptionWindowStandard;
        }

        std::unique_ptr<RubberBand::RubberBandStretcher> createRubberBandStretcher(double sampleRate,
                                                                                   double initialPitchRatio)
        {
            return std::make_unique<RubberBand::RubberBandStretcher>(
                static_cast<size_t>(juce::jlimit(8000, 192000, static_cast<int>(std::round(sampleRate)))),
                static_cast<size_t>(Voice::kMaxChannels),
                makeRubberBandOptions(),
                1.0,
                initialPitchRatio);
        }
#endif
    }

    struct Voice::RubberBandState
    {
#if SW_HAVE_RUBBERBAND
        std::unique_ptr<RubberBand::RubberBandStretcher> stretcherOwner;
        RubberBand::RubberBandStretcher *stretcher = nullptr;
        std::unique_ptr<RubberBandBuffers> buffers;
        double configuredSampleRate = 0.0;
        int processBlockSize = 1024;
        int inputFill = 0;
        int startDelayRemaining = 0;
        int preferredStartPadRemaining = 0;
        int outputFifoRead = 0;
        int outputFifoWrite = 0;
        int outputFifoCount = 0;
        int onsetBlendRemaining = 0;
        bool initialized = false;
        double lastPitchScale = 1.0;
        float lastOutputLeft = 0.0f;
        float lastOutputRight = 0.0f;

        void initialise(double sampleRate, double initialPitchRatio)
        {
            if (initialized && std::abs(configuredSampleRate - sampleRate) < 1.0e-6)
                return;

            configuredSampleRate = sampleRate;
            stretcherOwner = createRubberBandStretcher(sampleRate, initialPitchRatio);
            stretcher = stretcherOwner.get();

            if (stretcher == nullptr)
            {
                initialized = false;
                return;
            }

            if (buffers == nullptr)
                buffers = std::make_unique<RubberBandBuffers>();

            if (buffers == nullptr)
            {
                initialized = false;
                stretcherOwner.reset();
                stretcher = nullptr;
                return;
            }

            stretcher->setDebugLevel(0);
            stretcher->setMaxProcessSize(static_cast<size_t>(kRubberBandMaxBlockSize));

            processBlockSize = static_cast<int>(stretcher->getSamplesRequired());
            if (processBlockSize <= 0 || processBlockSize > kRubberBandMaxBlockSize)
                processBlockSize = 1024;

            initialized = true;
        }

        void reset(double pitchRatio)
        {
            if (stretcher == nullptr || buffers == nullptr)
                return;

            stretcher->reset();
            stretcher->setTimeRatio(1.0);
            lastPitchScale = pitchRatio;
            stretcher->setPitchScale(lastPitchScale);

            inputFill = 0;
            outputFifoRead = 0;
            outputFifoWrite = 0;
            outputFifoCount = 0;
            onsetBlendRemaining = kRubberBandOnsetBlendSamples;
            lastOutputLeft = 0.0f;
            lastOutputRight = 0.0f;
            preferredStartPadRemaining = static_cast<int>(stretcher->getPreferredStartPad());
            startDelayRemaining = static_cast<int>(stretcher->getStartDelay());

            const int required = static_cast<int>(stretcher->getSamplesRequired());
            if (required > 0 && required <= kRubberBandMaxBlockSize)
                processBlockSize = required;

            for (int ch = 0; ch < Voice::kMaxChannels; ++ch)
            {
                std::fill(buffers->input[static_cast<size_t>(ch)].begin(),
                          buffers->input[static_cast<size_t>(ch)].end(),
                          0.0f);
                std::fill(buffers->processOutput[static_cast<size_t>(ch)].begin(),
                          buffers->processOutput[static_cast<size_t>(ch)].end(),
                          0.0f);
                std::fill(buffers->outputFifo[static_cast<size_t>(ch)].begin(),
                          buffers->outputFifo[static_cast<size_t>(ch)].end(),
                          0.0f);
            }
        }

        void processIfReady()
        {
            if (stretcher == nullptr || buffers == nullptr)
                return;

            while (true)
            {
                int required = static_cast<int>(stretcher->getSamplesRequired());
                if (required <= 0 || required > kRubberBandMaxBlockSize)
                    required = processBlockSize;

                required = juce::jlimit(1, kRubberBandMaxBlockSize, required);
                if (inputFill < required)
                    break;

                for (int ch = 0; ch < Voice::kMaxChannels; ++ch)
                {
                    buffers->inputPtrs[static_cast<size_t>(ch)] = buffers->input[static_cast<size_t>(ch)].data();
                    buffers->processOutputPtrs[static_cast<size_t>(ch)] = buffers->processOutput[static_cast<size_t>(ch)].data();
                }

                stretcher->process(buffers->inputPtrs.data(), static_cast<size_t>(required), false);

                const int remaining = inputFill - required;
                if (remaining > 0)
                {
                    for (int ch = 0; ch < Voice::kMaxChannels; ++ch)
                    {
                        std::memmove(buffers->input[static_cast<size_t>(ch)].data(),
                                     buffers->input[static_cast<size_t>(ch)].data() + required,
                                     static_cast<size_t>(remaining) * sizeof(float));
                    }
                }
                inputFill = remaining;

                int available = stretcher->available();
                while (available > 0)
                {
                    const size_t toRetrieve = static_cast<size_t>(juce::jmin(available, kRubberBandMaxBlockSize));
                    const size_t retrieved = stretcher->retrieve(buffers->processOutputPtrs.data(), toRetrieve);
                    if (retrieved == 0)
                        break;

                    for (size_t frame = 0; frame < retrieved; ++frame)
                    {
                        if (outputFifoCount >= kRubberBandOutputFifoSize)
                        {
                            outputFifoRead = (outputFifoRead + 1) % kRubberBandOutputFifoSize;
                            --outputFifoCount;
                        }

                        for (int ch = 0; ch < Voice::kMaxChannels; ++ch)
                        {
                            buffers->outputFifo[static_cast<size_t>(ch)][static_cast<size_t>(outputFifoWrite)] =
                                buffers->processOutput[static_cast<size_t>(ch)][frame];
                        }

                        outputFifoWrite = (outputFifoWrite + 1) % kRubberBandOutputFifoSize;
                        ++outputFifoCount;
                    }

                    available = stretcher->available();
                }
            }
        }
#else
        bool initialized = false;

        void initialise(double, double) {}
        void reset(double) {}
        void processIfReady() {}
#endif
    };

    Voice::Voice() = default;
    Voice::~Voice() = default;

    void Voice::initialiseRubberBand(double sampleRate, double initialPitchRatio)
    {
#if SW_HAVE_RUBBERBAND
        if (rubberBandState == nullptr)
            rubberBandState = std::make_unique<RubberBandState>();

        rubberBandState->initialise(sampleRate, initialPitchRatio);
        if (rubberBandState->initialized)
            resetRubberBand();
#else
        juce::ignoreUnused(sampleRate, initialPitchRatio);
#endif
    }

    void Voice::resetRubberBand()
    {
        if (rubberBandState != nullptr)
            rubberBandState->reset(pitchRatio);
    }

    bool Voice::isRubberBandReady() const noexcept
    {
        return rubberBandState != nullptr && rubberBandState->initialized;
    }

    int Voice::getRubberBandInputDeficit() const noexcept
    {
#if SW_HAVE_RUBBERBAND
        if (!isRubberBandReady())
            return 1;

        return juce::jmax(1, rubberBandState->processBlockSize - rubberBandState->inputFill);
#else
        return 1;
#endif
    }

    bool Voice::shouldProvideRubberBandStartPadSample() const noexcept
    {
#if SW_HAVE_RUBBERBAND
        return isRubberBandReady() && rubberBandState->preferredStartPadRemaining > 0;
#else
        return false;
#endif
    }

    void Voice::consumeRubberBandStartPadSample() noexcept
    {
#if SW_HAVE_RUBBERBAND
        if (isRubberBandReady() && rubberBandState->preferredStartPadRemaining > 0)
            --rubberBandState->preferredStartPadRemaining;
#endif
    }

    void Voice::pushRubberBandInput(float leftSample, float rightSample) noexcept
    {
#if SW_HAVE_RUBBERBAND
        if (!isRubberBandReady() || rubberBandState->buffers == nullptr)
            return;

        const int writeIndex = rubberBandState->inputFill;
        if (writeIndex < 0 || writeIndex >= kRubberBandMaxBlockSize)
            return;

        rubberBandState->buffers->input[0][static_cast<size_t>(writeIndex)] = leftSample;
        rubberBandState->buffers->input[1][static_cast<size_t>(writeIndex)] = rightSample;
        ++rubberBandState->inputFill;
#else
        juce::ignoreUnused(leftSample, rightSample);
#endif
    }

    void Voice::setRubberBandPitchScale(double pitchScale)
    {
#if SW_HAVE_RUBBERBAND
        if (!isRubberBandReady() || rubberBandState->stretcher == nullptr)
            return;

        if (std::abs(pitchScale - rubberBandState->lastPitchScale) <= 1.0e-6)
            return;

        rubberBandState->stretcher->setPitchScale(pitchScale);
        rubberBandState->lastPitchScale = pitchScale;
#else
        juce::ignoreUnused(pitchScale);
#endif
    }

    void Voice::processRubberBandIfReady()
    {
        if (rubberBandState != nullptr)
            rubberBandState->processIfReady();
    }

    void Voice::skipRubberBandStartDelay() noexcept
    {
#if SW_HAVE_RUBBERBAND
        if (!isRubberBandReady())
            return;

        while (rubberBandState->outputFifoCount > 0 && rubberBandState->startDelayRemaining > 0)
        {
            rubberBandState->outputFifoRead = (rubberBandState->outputFifoRead + 1) % kRubberBandOutputFifoSize;
            --rubberBandState->outputFifoCount;
            --rubberBandState->startDelayRemaining;
        }
#endif
    }

    bool Voice::hasRubberBandOutput() const noexcept
    {
#if SW_HAVE_RUBBERBAND
        return isRubberBandReady() && rubberBandState->outputFifoCount > 0;
#else
        return false;
#endif
    }

    void Voice::popRubberBandOutputOrReuseLast(float &leftSample, float &rightSample) noexcept
    {
        leftSample = 0.0f;
        rightSample = 0.0f;

#if SW_HAVE_RUBBERBAND
        if (!isRubberBandReady())
            return;

        leftSample = rubberBandState->lastOutputLeft;
        rightSample = rubberBandState->lastOutputRight;

        if (rubberBandState->outputFifoCount <= 0 || rubberBandState->buffers == nullptr)
            return;

        leftSample = rubberBandState->buffers->outputFifo[0][static_cast<size_t>(rubberBandState->outputFifoRead)];
        rightSample = rubberBandState->buffers->outputFifo[1][static_cast<size_t>(rubberBandState->outputFifoRead)];
        rubberBandState->outputFifoRead = (rubberBandState->outputFifoRead + 1) % kRubberBandOutputFifoSize;
        --rubberBandState->outputFifoCount;
        rubberBandState->lastOutputLeft = leftSample;
        rubberBandState->lastOutputRight = rightSample;
#endif
    }

    float Voice::consumeRubberBandAttackGain() noexcept
    {
#if SW_HAVE_RUBBERBAND
        if (!isRubberBandReady())
            return 1.0f;

        float attackGain = 1.0f;
        if (rubberBandState->onsetBlendRemaining > 0)
        {
            const int clampedRemaining = juce::jlimit(0,
                                                      kRubberBandOnsetBlendSamples,
                                                      rubberBandState->onsetBlendRemaining);
            attackGain = 1.0f - (static_cast<float>(clampedRemaining) /
                                 static_cast<float>(kRubberBandOnsetBlendSamples));
            --rubberBandState->onsetBlendRemaining;
        }

        return attackGain;
#else
        return 1.0f;
#endif
    }

} // namespace sw