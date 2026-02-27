#include "WaveformCache.h"
#include "WaveformPeak.h"
#include "Util/Hashing.h"
#include "Util/Logging.h"
#include "Util/Paths.h"
#include <JuceHeader.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace sw
{

    WaveformCache::WaveformCache(CatalogDb &db, JobQueue &queue, const std::string &cacheDir)
        : catalogDb(db), jobQueue(queue), cacheDirectory(cacheDir)
    {
        // Ensure cache directory exists
        juce::File(cacheDirectory).createDirectory();
    }

    std::string WaveformCache::buildCacheKey(int64_t rootId, const std::string &relPath,
                                             int64_t sizeBytes, int64_t mtime)
    {
        std::string input = std::to_string(rootId) + "|" + relPath + "|" + std::to_string(sizeBytes) + "|" + std::to_string(mtime);
        return hashString(input);
    }

    void WaveformCache::requestPeaks(int64_t fileId, PeaksReadyCallback onReady)
    {
        jobQueue.enqueue(Job{
            [this, fileId, onReady](const std::atomic<uint64_t> &cancelGeneration, uint64_t jobGeneration)
            {
                if (cancelGeneration.load(std::memory_order_relaxed) != jobGeneration)
                    return;

                auto maybeFile = catalogDb.fileById(fileId);
                if (!maybeFile.has_value())
                    return;

                const auto &rec = *maybeFile;
                auto key = buildCacheKey(rec.rootId, rec.relativePath,
                                         rec.sizeBytes, rec.modifiedTime);

                // Check if already cached in DB
                auto cached = catalogDb.cacheEntryByKey(key);
                if (cached.has_value())
                {
                    std::ifstream input(cached->cachePath, std::ios::binary);
                    if (input)
                    {
                        uint32_t peakCount = 0;
                        input.read(reinterpret_cast<char *>(&peakCount), sizeof(peakCount));

                        if (input && peakCount > 0 && peakCount <= 100000)
                        {
                            std::vector<float> peaks(static_cast<size_t>(peakCount));
                            input.read(reinterpret_cast<char *>(peaks.data()), static_cast<std::streamsize>(peakCount * sizeof(float)));

                            if (input.good() || input.eof())
                            {
                                if (onReady)
                                    onReady(fileId, std::move(peaks));
                                return;
                            }
                        }
                    }
                }

                const auto roots = catalogDb.allRoots();
                const auto rootIt = std::find_if(roots.begin(), roots.end(), [rootId = rec.rootId](const RootRecord &root)
                                                 { return root.id == rootId; });

                if (rootIt == roots.end())
                    return;

                const auto absolutePath = resolveAbsolutePath(rootIt->path, rec.relativePath);

                juce::AudioFormatManager formatManager;
                formatManager.registerBasicFormats();

                const juce::File sourceFile(absolutePath);
                auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(sourceFile));
                if (!reader)
                    return;

                const auto totalSamples = reader->lengthInSamples;
                const int numChannels = std::max(1, static_cast<int>(reader->numChannels));

                if (totalSamples <= 0)
                {
                    if (onReady)
                        onReady(fileId, {});
                    return;
                }

                constexpr int targetPeakCount = 1600;
                const int peakCount = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(static_cast<int64_t>(targetPeakCount), totalSamples)));
                std::vector<float> peaks(static_cast<size_t>(peakCount), 0.0f);
                std::vector<int64_t> peakBoundaries(static_cast<size_t>(peakCount + 1), 0);
                for (int p = 0; p <= peakCount; ++p)
                    peakBoundaries[static_cast<size_t>(p)] = (static_cast<int64_t>(p) * totalSamples) / peakCount;
                peakBoundaries[static_cast<size_t>(peakCount)] = totalSamples;

                constexpr int readChunkSamples = 65536;
                juce::AudioBuffer<float> tempBuffer(numChannels, readChunkSamples);
                int currentPeak = 0;

                for (int64_t chunkStartSample = 0; chunkStartSample < totalSamples; chunkStartSample += readChunkSamples)
                {
                    if (cancelGeneration.load(std::memory_order_relaxed) != jobGeneration)
                        return;

                    const int64_t remaining = totalSamples - chunkStartSample;
                    if (remaining <= 0)
                        break;

                    const int blockSamples = static_cast<int>(std::min<int64_t>(readChunkSamples, remaining));
                    if (blockSamples <= 0)
                        break;

                    tempBuffer.clear();
                    if (!reader->read(&tempBuffer, 0, blockSamples, chunkStartSample, true, true))
                        break;

                    const int64_t chunkEndSample = chunkStartSample + blockSamples;
                    while ((currentPeak + 1) <= peakCount &&
                           peakBoundaries[static_cast<size_t>(currentPeak + 1)] <= chunkStartSample)
                    {
                        ++currentPeak;
                    }

                    int localOffset = 0;
                    while (localOffset < blockSamples && currentPeak < peakCount)
                    {
                        const int64_t segmentStartSample = chunkStartSample + localOffset;
                        const int64_t peakEndSample = peakBoundaries[static_cast<size_t>(currentPeak + 1)];
                        const int64_t segmentEndSample = std::min<int64_t>(chunkEndSample, peakEndSample);
                        const int segmentSamples = static_cast<int>(segmentEndSample - segmentStartSample);

                        if (segmentSamples <= 0)
                        {
                            ++currentPeak;
                            continue;
                        }

                        float maxAbs = peaks[static_cast<size_t>(currentPeak)];
                        for (int ch = 0; ch < numChannels; ++ch)
                        {
                            const float *channelData = tempBuffer.getReadPointer(ch) + localOffset;
                            maxAbs = std::max(maxAbs, peakAbsVectorized(channelData, segmentSamples));
                        }
                        peaks[static_cast<size_t>(currentPeak)] = maxAbs;

                        localOffset += segmentSamples;
                        if (segmentEndSample >= peakEndSample)
                            ++currentPeak;
                    }
                }

                const auto cachePath = (std::filesystem::path(cacheDirectory) / (key + ".peak")).string();
                {
                    std::ofstream output(cachePath, std::ios::binary | std::ios::trunc);
                    if (output)
                    {
                        const uint32_t count = static_cast<uint32_t>(peaks.size());
                        output.write(reinterpret_cast<const char *>(&count), sizeof(count));
                        output.write(reinterpret_cast<const char *>(peaks.data()), static_cast<std::streamsize>(peaks.size() * sizeof(float)));

                        if (output.good())
                        {
                            WaveCacheEntry entry;
                            entry.fileId = fileId;
                            entry.cacheKey = key;
                            entry.cachePath = cachePath;
                            catalogDb.insertCacheEntry(entry);
                        }
                    }
                    else
                    {
                        SW_LOG_WARN("Failed to write wave cache file: " << cachePath);
                    }
                }

                if (onReady)
                    onReady(fileId, std::move(peaks));
            },
            JobPriority::High});
    }

} // namespace sw
