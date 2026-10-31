#include "Analyzer.h"
#include "Util/Paths.h"
#include "Util/Logging.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <vector>
#include <JuceHeader.h>

namespace sw
{
    namespace
    {
        constexpr int kAnalysisBlockSize = 8192;
        constexpr int kMaximumAnalysisChannels = 256;
        constexpr double kNormalizedAnalysisSampleRate = 11000.0;
        constexpr double kMaximumSignalAnalysisSeconds = 180.0;
        constexpr int kEnvelopeHopSize = 110;
        constexpr int kKeyWindowSize = 4096;
        constexpr int kKeyHopSize = 8192;
        constexpr int kMinimumTransientSpacingFrames = 8;

        constexpr std::array<const char *, 12> kPitchClassNames{
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        constexpr std::array<double, 12> kMajorProfile{
            6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
        constexpr std::array<double, 12> kMinorProfile{
            6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

        const std::array<float, kKeyWindowSize> &keyWindow()
        {
            static const auto window = []
            {
                std::array<float, kKeyWindowSize> values{};
                for (int i = 0; i < kKeyWindowSize; ++i)
                {
                    const double phase = (2.0 * 3.14159265358979323846 * i) / (kKeyWindowSize - 1);
                    values[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(phase));
                }
                return values;
            }();
            return window;
        }

        double goertzelPower(const std::vector<float> &samples,
                             size_t start,
                             double frequency,
                             double sampleRate)
        {
            const double coefficient = 2.0 * std::cos(2.0 * 3.14159265358979323846 * frequency / sampleRate);
            const auto &window = keyWindow();
            double previous = 0.0;
            double previousPrevious = 0.0;
            for (int i = 0; i < kKeyWindowSize; ++i)
            {
                const double sample = samples[start + static_cast<size_t>(i)] *
                                      window[static_cast<size_t>(i)];
                const double current = sample + coefficient * previous - previousPrevious;
                previousPrevious = previous;
                previous = current;
            }
            return std::max(0.0, previous * previous + previousPrevious * previousPrevious -
                                     coefficient * previous * previousPrevious);
        }

        double profileCorrelation(const std::array<double, 12> &chroma,
                                  const std::array<double, 12> &profile,
                                  int rotation)
        {
            double chromaMean = 0.0;
            double profileMean = 0.0;
            for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            {
                chromaMean += chroma[static_cast<size_t>(pitchClass)];
                profileMean += profile[static_cast<size_t>((pitchClass - rotation + 12) % 12)];
            }
            chromaMean /= 12.0;
            profileMean /= 12.0;

            double covariance = 0.0;
            double chromaVariance = 0.0;
            double profileVariance = 0.0;
            for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            {
                const double centeredChroma = chroma[static_cast<size_t>(pitchClass)] - chromaMean;
                const double centeredProfile =
                    profile[static_cast<size_t>((pitchClass - rotation + 12) % 12)] - profileMean;
                covariance += centeredChroma * centeredProfile;
                chromaVariance += centeredChroma * centeredChroma;
                profileVariance += centeredProfile * centeredProfile;
            }
            const double denominator = std::sqrt(chromaVariance * profileVariance);
            return denominator <= 1.0e-12 ? 0.0 : covariance / denominator;
        }

        void inferKey(const std::vector<float> &samples,
                      AnalysisResultRecord &result,
                      const std::function<bool()> &isCancelled)
        {
            if (samples.size() < static_cast<size_t>(kKeyWindowSize))
                return;

            std::array<double, 12> chroma{};
            constexpr int firstMidiNote = 36;
            constexpr int lastMidiNote = 83;
            constexpr int noteCount = lastMidiNote - firstMidiNote + 1;
            std::array<double, noteCount> frequencies{};
            for (int index = 0; index < noteCount; ++index)
            {
                const int midiNote = firstMidiNote + index;
                frequencies[static_cast<size_t>(index)] =
                    440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
            }

            size_t windowCount = 0;
            for (size_t start = 0; start + static_cast<size_t>(kKeyWindowSize) <= samples.size();
                 start += static_cast<size_t>(kKeyHopSize))
            {
                if (isCancelled())
                    return;

                std::array<double, 12> windowChroma{};
                for (int noteIndex = 0; noteIndex < noteCount; ++noteIndex)
                {
                    const double power = goertzelPower(samples,
                                                       start,
                                                       frequencies[static_cast<size_t>(noteIndex)],
                                                       kNormalizedAnalysisSampleRate);
                    windowChroma[static_cast<size_t>((firstMidiNote + noteIndex) % 12)] += std::sqrt(power);
                }

                const double totalEnergy = std::accumulate(windowChroma.begin(), windowChroma.end(), 0.0);
                double windowMagnitude = 0.0;
                const auto &window = keyWindow();
                for (int i = 0; i < kKeyWindowSize; ++i)
                    windowMagnitude += std::abs(samples[start + static_cast<size_t>(i)] *
                                                 window[static_cast<size_t>(i)]);
                if (totalEnergy > std::max(1.0e-9, windowMagnitude * 0.025))
                {
                    for (size_t pitchClass = 0; pitchClass < chroma.size(); ++pitchClass)
                        chroma[pitchClass] += windowChroma[pitchClass] / totalEnergy;
                    ++windowCount;
                }
            }

            if (windowCount < 2)
                return;

            double chromaTotal = std::accumulate(chroma.begin(), chroma.end(), 0.0);
            if (chromaTotal <= 1.0e-9)
                return;
            for (auto &value : chroma)
                value /= chromaTotal;

            double bestScore = -2.0;
            double secondBestScore = -2.0;
            int bestPitchClass = 0;
            bool bestIsMinor = false;
            for (int rotation = 0; rotation < 12; ++rotation)
            {
                const double majorScore = profileCorrelation(chroma, kMajorProfile, rotation);
                const double minorScore = profileCorrelation(chroma, kMinorProfile, rotation);
                for (const auto [score, isMinor] : {std::pair{majorScore, false}, std::pair{minorScore, true}})
                {
                    if (score > bestScore)
                    {
                        secondBestScore = bestScore;
                        bestScore = score;
                        bestPitchClass = rotation;
                        bestIsMinor = isMinor;
                    }
                    else if (score > secondBestScore)
                    {
                        secondBestScore = score;
                    }
                }
            }

            const double fit = std::clamp(bestScore, 0.0, 1.0);
            const double separation = std::clamp((bestScore - secondBestScore) * 5.0, 0.0, 1.0);
            const double confidence = 0.7 * fit + 0.3 * separation;
            if (confidence < 0.25)
                return;

            result.detectedKey = std::string(kPitchClassNames[static_cast<size_t>(bestPitchClass)]) +
                                 (bestIsMinor ? " minor" : " major");
            result.keyConfidence = confidence;
        }

        double normalizedCorrelation(const std::vector<double> &values, size_t lag)
        {
            if (lag == 0 || lag >= values.size())
                return 0.0;

            double sumFirst = 0.0;
            double sumSecond = 0.0;
            double sumFirstSquared = 0.0;
            double sumSecondSquared = 0.0;
            double crossProduct = 0.0;
            const size_t pairCount = values.size() - lag;
            for (size_t i = 0; i < pairCount; ++i)
            {
                const double first = values[i];
                const double second = values[i + lag];
                sumFirst += first;
                sumSecond += second;
                sumFirstSquared += first * first;
                sumSecondSquared += second * second;
                crossProduct += first * second;
            }

            const double covariance = crossProduct - (sumFirst * sumSecond / pairCount);
            const double firstVariance = sumFirstSquared - (sumFirst * sumFirst / pairCount);
            const double secondVariance = sumSecondSquared - (sumSecond * sumSecond / pairCount);
            if (firstVariance <= 1.0e-12 || secondVariance <= 1.0e-12)
                return 0.0;
            return std::clamp(covariance / std::sqrt(firstVariance * secondVariance), -1.0, 1.0);
        }

        void inferRhythmAndLoop(const std::vector<float> &samples,
                                double sourceSampleRate,
                                int64_t sourceSampleCount,
                                AnalysisResultRecord &result,
                                const std::function<bool()> &isCancelled)
        {
            if (samples.size() < static_cast<size_t>(kEnvelopeHopSize * 3))
                return;

            std::vector<double> frameRms;
            std::vector<double> transientFrameRms;
            frameRms.reserve(samples.size() / static_cast<size_t>(kEnvelopeHopSize));
            transientFrameRms.reserve(samples.size() / static_cast<size_t>(kEnvelopeHopSize));
            const double highPassAlpha =
                1.0 - std::exp(-2.0 * 3.14159265358979323846 * 1800.0 / kNormalizedAnalysisSampleRate);
            double lowPassState = 0.0;
            for (size_t start = 0; start + static_cast<size_t>(kEnvelopeHopSize) <= samples.size();
                 start += static_cast<size_t>(kEnvelopeHopSize))
            {
                double sumSquares = 0.0;
                double transientSumSquares = 0.0;
                for (int offset = 0; offset < kEnvelopeHopSize; ++offset)
                {
                    const double sample = samples[start + static_cast<size_t>(offset)];
                    sumSquares += sample * sample;
                    lowPassState += highPassAlpha * (sample - lowPassState);
                    const double highPassSample = sample - lowPassState;
                    transientSumSquares += highPassSample * highPassSample;
                }
                frameRms.push_back(std::sqrt(sumSquares / kEnvelopeHopSize));
                transientFrameRms.push_back(std::sqrt(transientSumSquares / kEnvelopeHopSize));
            }

            if (frameRms.size() < 3)
                return;

            const auto rawFrameRms = frameRms;
            for (size_t i = 0; i < frameRms.size(); ++i)
            {
                const size_t begin = i > 2 ? i - 2 : 0;
                const size_t end = std::min(frameRms.size(), i + 3);
                const double sum = std::accumulate(rawFrameRms.begin() + static_cast<std::ptrdiff_t>(begin),
                                                   rawFrameRms.begin() + static_cast<std::ptrdiff_t>(end),
                                                   0.0);
                frameRms[i] = sum / static_cast<double>(end - begin);
            }

            std::vector<double> onsetStrength(frameRms.size(), 0.0);
            double maximumOnsetStrength = 0.0;
            for (size_t i = 1; i < frameRms.size(); ++i)
            {
                onsetStrength[i] = std::max(0.0, transientFrameRms[i] - transientFrameRms[i - 1]);
                maximumOnsetStrength = std::max(maximumOnsetStrength, onsetStrength[i]);
            }

            const double onsetThreshold = maximumOnsetStrength * 0.45;
            int transientCount = 0;
            int lastTransientFrame = -kMinimumTransientSpacingFrames;
            for (size_t i = 1; i + 1 < onsetStrength.size(); ++i)
            {
                if (onsetStrength[i] >= onsetThreshold &&
                    onsetStrength[i] >= onsetStrength[i - 1] &&
                    onsetStrength[i] > onsetStrength[i + 1] &&
                    static_cast<int>(i) - lastTransientFrame >= kMinimumTransientSpacingFrames)
                {
                    ++transientCount;
                    lastTransientFrame = static_cast<int>(i);
                }
            }

            const double analyzedDurationSeconds =
                static_cast<double>(samples.size()) / kNormalizedAnalysisSampleRate;
            result.transientCount = transientCount;
            result.onsetDensity = analyzedDurationSeconds > 0.0
                                      ? static_cast<double>(transientCount) / analyzedDurationSeconds
                                      : 0.0;

            auto tempoOnsetStrength = onsetStrength;
            for (auto &strength : tempoOnsetStrength)
                if (strength < onsetThreshold)
                    strength = 0.0;

            constexpr size_t minimumBeatLag = 30;
            constexpr size_t maximumBeatLag = 100;
            double bestTempoCorrelation = 0.0;
            double secondTempoCorrelation = 0.0;
            size_t bestTempoLag = 0;
            for (size_t lag = minimumBeatLag;
                 lag <= maximumBeatLag && lag < tempoOnsetStrength.size() / 2;
                 ++lag)
            {
                if (isCancelled())
                    return;
                const double score = normalizedCorrelation(tempoOnsetStrength, lag);
                if (score > bestTempoCorrelation + 1.0e-6)
                {
                    secondTempoCorrelation = bestTempoCorrelation;
                    bestTempoCorrelation = score;
                    bestTempoLag = lag;
                }
                else if (score > secondTempoCorrelation)
                {
                    secondTempoCorrelation = score;
                }
            }

            if (bestTempoLag > 0 && bestTempoCorrelation >= 0.35 && transientCount >= 4)
            {
                const double bpm = (60.0 * (kNormalizedAnalysisSampleRate / kEnvelopeHopSize)) /
                                   static_cast<double>(bestTempoLag);
                if (std::isfinite(bpm) && bpm >= 60.0 && bpm <= 200.0)
                {
                    result.detectedBpm = bpm;
                    const double eventSupport = std::min(1.0, static_cast<double>(transientCount) / 8.0);
                    const double ambiguityFactor =
                        secondTempoCorrelation >= bestTempoCorrelation * 0.98 ? 0.75 : 1.0;
                    result.bpmConfidence =
                        std::clamp(bestTempoCorrelation * eventSupport * ambiguityFactor, 0.0, 1.0);
                }
            }

            const size_t minimumLoopLag = static_cast<size_t>(
                std::ceil(kNormalizedAnalysisSampleRate / kEnvelopeHopSize));
            const size_t maximumLoopLag = std::min<size_t>(
                static_cast<size_t>(10.0 * kNormalizedAnalysisSampleRate / kEnvelopeHopSize),
                frameRms.size() / 3);
            double bestLoopCorrelation = 0.0;
            size_t bestLoopLag = 0;
            for (size_t lag = minimumLoopLag; lag <= maximumLoopLag; ++lag)
            {
                if (isCancelled())
                    return;
                const double score = normalizedCorrelation(frameRms, lag);
                if (score > bestLoopCorrelation)
                {
                    bestLoopCorrelation = score;
                    bestLoopLag = lag;
                }
            }

            if (bestLoopLag > 0 && bestLoopCorrelation >= 0.92)
            {
                const double sampleOffset =
                    static_cast<double>(bestLoopLag * static_cast<size_t>(kEnvelopeHopSize)) *
                    sourceSampleRate / kNormalizedAnalysisSampleRate;
                if (std::isfinite(sampleOffset) && sampleOffset > 0.0 &&
                    sampleOffset < static_cast<double>(sourceSampleCount))
                {
                    result.suggestedLoopStartSample = 0;
                    result.suggestedLoopEndSample = static_cast<int64_t>(std::llround(sampleOffset));
                    result.loopConfidence = std::clamp(bestLoopCorrelation, 0.0, 1.0);
                }
            }
        }

        void inferAudioMetadata(const std::vector<float> &samples,
                                double sourceSampleRate,
                                int64_t sourceSampleCount,
                                AnalysisResultRecord &result,
                                const std::function<bool()> &isCancelled)
        {
            inferRhythmAndLoop(samples, sourceSampleRate, sourceSampleCount, result, isCancelled);
            if (!isCancelled())
                inferKey(samples, result, isCancelled);
        }

        int64_t currentEpochSeconds()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }
    }

    Analyzer::Analyzer(CatalogDb &db, JobQueue &queue)
        : catalogDb(db), jobQueue(queue)
    {
    }

    void Analyzer::analyzeFile(int64_t fileId)
    {
        jobQueue.enqueue(Job{
            [this, fileId](const std::atomic<uint64_t> &cancelGeneration, uint64_t jobGeneration)
            {
                const auto isCancelled = [&cancelGeneration, jobGeneration]()
                {
                    return cancelGeneration.load(std::memory_order_relaxed) != jobGeneration;
                };
                if (isCancelled())
                    return;

                const auto maybeFile = catalogDb.fileById(fileId);
                if (!maybeFile || maybeFile->indexOnly)
                    return;

                const auto roots = catalogDb.allRoots();
                const auto rootIt = std::find_if(roots.begin(), roots.end(), [rootId = maybeFile->rootId](const RootRecord &root)
                                                 { return root.id == rootId; });
                if (rootIt == roots.end())
                {
                    SW_LOG_WARN("Analyzer: source root missing for file " << fileId);
                    return;
                }

                AnalysisResultRecord result;
                result.fileId = maybeFile->id;
                result.analysisVersion = kCurrentAnalysisVersion;
                result.analyzedModifiedTime = maybeFile->modifiedTime;
                result.analyzedContentHash = maybeFile->contentHash;
                result.featureVectorVersion = kCurrentAnalysisFeatureVectorVersion;

                const auto persistResult = [this, &result]()
                {
                    result.analyzedAt = currentEpochSeconds();
                    if (!catalogDb.upsertAnalysisResult(result))
                        SW_LOG_ERR("Analyzer: failed to persist analysis for file " << result.fileId);
                };
                const auto fail = [&result, &persistResult](AnalysisStatus status, std::string reason)
                {
                    result.status = status;
                    result.failureReason = std::move(reason);
                    persistResult();
                };

                if (isCancelled())
                    return;

                const auto absolutePath = resolveAbsolutePath(rootIt->path, maybeFile->relativePath);
                const juce::File sourceFile(absolutePath);
                if (!sourceFile.existsAsFile())
                {
                    fail(AnalysisStatus::Failed, "Source file is unavailable");
                    return;
                }

                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();
                const auto *knownFormat = formatManager.findFormatForFileExtension(sourceFile.getFileExtension());
                auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(sourceFile));
                if (!reader)
                {
                    fail(knownFormat == nullptr ? AnalysisStatus::Unsupported : AnalysisStatus::Failed,
                         knownFormat == nullptr ? "No registered audio decoder for this format"
                                                : "Audio decoder could not read the source file");
                    return;
                }

                if (reader->sampleRate <= 0.0 || !std::isfinite(reader->sampleRate) ||
                    reader->lengthInSamples < 0 || reader->numChannels == 0)
                {
                    fail(AnalysisStatus::Failed, "Audio reader returned invalid stream metadata");
                    return;
                }

                if (reader->numChannels > static_cast<unsigned int>(kMaximumAnalysisChannels))
                {
                    fail(AnalysisStatus::Unsupported, "Channel count exceeds the analysis limit");
                    return;
                }

                const int channelCount = static_cast<int>(reader->numChannels);
                juce::AudioBuffer<float> block(channelCount, kAnalysisBlockSize);
                double sumSquares = 0.0;
                double peak = 0.0;
                uint64_t finiteSampleCount = 0;
                uint64_t monoFrameCount = 0;
                uint64_t zeroCrossingCount = 0;
                double previousMonoSample = 0.0;
                bool hasPreviousMonoSample = false;
                constexpr size_t maximumSignalAnalysisSamples =
                    static_cast<size_t>(kNormalizedAnalysisSampleRate * kMaximumSignalAnalysisSeconds);
                const double estimatedAnalysisSamples =
                    static_cast<double>(reader->lengthInSamples) *
                    (kNormalizedAnalysisSampleRate / reader->sampleRate);
                const size_t expectedAnalysisSamples = static_cast<size_t>(
                    std::clamp(std::ceil(estimatedAnalysisSamples),
                               0.0,
                               static_cast<double>(maximumSignalAnalysisSamples)));
                std::vector<float> analysisMonoSamples;
                analysisMonoSamples.reserve(expectedAnalysisSamples);
                const double sourceSamplesPerAnalysisSample =
                    reader->sampleRate / kNormalizedAnalysisSampleRate;
                double nextSourceSamplePosition = 0.0;
                int64_t sourceFrameIndex = 0;
                double previousFilteredMonoSample = 0.0;
                bool hasPreviousResampleSample = false;
                double filteredMonoSample = 0.0;
                const double lowPassAlpha = reader->sampleRate > kNormalizedAnalysisSampleRate
                                                ? 1.0 - std::exp(
                                                            -2.0 * 3.14159265358979323846 *
                                                            (0.45 * kNormalizedAnalysisSampleRate) /
                                                            reader->sampleRate)
                                                : 1.0;

                for (int64_t startSample = 0; startSample < reader->lengthInSamples;)
                {
                    if (isCancelled())
                        return;

                    const int samplesToRead = static_cast<int>(std::min<int64_t>(
                        kAnalysisBlockSize, reader->lengthInSamples - startSample));
                    if (!reader->read(&block, 0, samplesToRead, startSample, true, true))
                    {
                        fail(AnalysisStatus::Failed, "Audio decoder failed while reading samples");
                        return;
                    }

                    for (int frame = 0; frame < samplesToRead; ++frame)
                    {
                        double monoSum = 0.0;
                        int finiteChannels = 0;
                        for (int channel = 0; channel < channelCount; ++channel)
                        {
                            const double sample = block.getSample(channel, frame);
                            if (!std::isfinite(sample))
                                continue;

                            sumSquares += sample * sample;
                            peak = std::max(peak, std::abs(sample));
                            ++finiteSampleCount;
                            monoSum += sample;
                            ++finiteChannels;
                        }

                        const double monoSample = finiteChannels == 0 ? 0.0 : monoSum / finiteChannels;
                        filteredMonoSample += lowPassAlpha * (monoSample - filteredMonoSample);
                        if (!hasPreviousResampleSample)
                        {
                            previousFilteredMonoSample = filteredMonoSample;
                            hasPreviousResampleSample = true;
                        }
                        while (nextSourceSamplePosition <= static_cast<double>(sourceFrameIndex) &&
                               analysisMonoSamples.size() < maximumSignalAnalysisSamples)
                        {
                            double interpolatedSample = filteredMonoSample;
                            if (sourceFrameIndex > 0)
                            {
                                const double fraction = std::clamp(
                                    nextSourceSamplePosition - static_cast<double>(sourceFrameIndex - 1),
                                    0.0,
                                    1.0);
                                interpolatedSample = previousFilteredMonoSample +
                                                     fraction * (filteredMonoSample - previousFilteredMonoSample);
                            }
                            analysisMonoSamples.push_back(static_cast<float>(interpolatedSample));
                            nextSourceSamplePosition += sourceSamplesPerAnalysisSample;
                        }
                        previousFilteredMonoSample = filteredMonoSample;
                        ++sourceFrameIndex;

                        if (finiteChannels == 0)
                        {
                            hasPreviousMonoSample = false;
                            continue;
                        }

                        if (hasPreviousMonoSample &&
                            ((previousMonoSample < 0.0 && monoSample >= 0.0) ||
                             (previousMonoSample >= 0.0 && monoSample < 0.0)))
                        {
                            ++zeroCrossingCount;
                        }

                        previousMonoSample = monoSample;
                        hasPreviousMonoSample = true;
                        ++monoFrameCount;
                    }

                    startSample += samplesToRead;
                }

                const double durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
                const double rms = finiteSampleCount == 0
                                       ? 0.0
                                       : std::sqrt(sumSquares / static_cast<double>(finiteSampleCount));
                const double zeroCrossingRate = monoFrameCount < 2
                                                    ? 0.0
                                                    : static_cast<double>(zeroCrossingCount) /
                                                          static_cast<double>(monoFrameCount - 1);
                const double floatLimit = static_cast<double>(std::numeric_limits<float>::max());
                if (!std::isfinite(durationSeconds) || !std::isfinite(rms) || !std::isfinite(peak) ||
                    !std::isfinite(zeroCrossingRate) || durationSeconds > floatLimit ||
                    rms > floatLimit || peak > floatLimit)
                {
                    fail(AnalysisStatus::Failed, "Audio analysis produced non-finite or out-of-range features");
                    return;
                }

                if (isCancelled())
                    return;

                result.status = AnalysisStatus::Complete;
                result.featureVector = {
                    static_cast<float>(durationSeconds),
                    static_cast<float>(rms),
                    static_cast<float>(peak),
                    static_cast<float>(zeroCrossingRate)};
                inferAudioMetadata(analysisMonoSamples,
                                   reader->sampleRate,
                                   reader->lengthInSamples,
                                   result,
                                   isCancelled);
                if (isCancelled())
                    return;
                persistResult();
            },
            JobPriority::Low});
    }

    void Analyzer::analyzeRoot(int64_t rootId)
    {
        jobQueue.enqueue(Job{
            [this, rootId](const std::atomic<uint64_t> &cancelGeneration, uint64_t jobGeneration)
            {
                if (cancelGeneration.load(std::memory_order_relaxed) != jobGeneration)
                    return;

                const auto fileIds = catalogDb.listFileIdsNeedingAnalysisByRoot(
                    rootId, kCurrentAnalysisVersion, kCurrentAnalysisFeatureVectorVersion);
                for (const auto fileId : fileIds)
                {
                    if (cancelGeneration.load(std::memory_order_relaxed) != jobGeneration)
                        return;
                    analyzeFile(fileId);
                }
            },
            JobPriority::Low});
    }

} // namespace sw
