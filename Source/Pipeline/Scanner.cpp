#include "Scanner.h"
#include "RexManager.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <optional>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <cstdio>

namespace fs = std::filesystem;

namespace sw
{

    namespace
    {
        std::string buildWaveCacheKey(int64_t rootId,
                                      const std::string &relativePath,
                                      int64_t sizeBytes,
                                      int64_t modifiedTime)
        {
            const std::string input = std::to_string(rootId) + "|" +
                                      relativePath + "|" +
                                      std::to_string(sizeBytes) + "|" +
                                      std::to_string(modifiedTime);

            constexpr uint64_t fnvOffset = 14695981039346656037ULL;
            constexpr uint64_t fnvPrime = 1099511628211ULL;

            uint64_t hash = fnvOffset;
            for (char c : input)
            {
                hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
                hash *= fnvPrime;
            }

            char out[17]{};
            std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
            return std::string(out);
        }

        std::vector<float> buildOverviewPeaks(juce::AudioFormatReader &reader, int targetPeakCount)
        {
            const int64_t totalSamples = reader.lengthInSamples;
            if (totalSamples <= 0)
                return {};

            const int numChannels = std::max(1, static_cast<int>(reader.numChannels));
            const int peakCount = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(targetPeakCount, totalSamples)));
            const int64_t samplesPerPeak = std::max<int64_t>(1, totalSamples / peakCount);

            std::vector<float> peaks(static_cast<size_t>(peakCount), 0.0f);
            juce::AudioBuffer<float> tempBuffer(numChannels, static_cast<int>(samplesPerPeak));

            for (int i = 0; i < peakCount; ++i)
            {
                const int64_t startSample = static_cast<int64_t>(i) * samplesPerPeak;
                const int64_t remaining = totalSamples - startSample;
                if (remaining <= 0)
                    break;

                const int blockSamples = static_cast<int>(std::min<int64_t>(samplesPerPeak, remaining));
                if (blockSamples <= 0)
                    break;

                tempBuffer.clear();
                if (!reader.read(&tempBuffer, 0, blockSamples, startSample, true, true))
                    break;

                float maxAbs = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float *channelData = tempBuffer.getReadPointer(ch);
                    for (int s = 0; s < blockSamples; ++s)
                        maxAbs = std::max(maxAbs, std::abs(channelData[s]));
                }

                peaks[static_cast<size_t>(i)] = maxAbs;
            }

            return peaks;
        }

        std::vector<float> buildOverviewPeaks(const juce::AudioBuffer<float> &buffer, int targetPeakCount)
        {
            const int totalSamples = buffer.getNumSamples();
            if (totalSamples <= 0)
                return {};

            const int numChannels = std::max(1, buffer.getNumChannels());
            const int peakCount = std::max(1, std::min(targetPeakCount, totalSamples));
            const int samplesPerPeak = std::max(1, totalSamples / peakCount);

            std::vector<float> peaks(static_cast<size_t>(peakCount), 0.0f);

            for (int i = 0; i < peakCount; ++i)
            {
                const int startSample = i * samplesPerPeak;
                const int remaining = totalSamples - startSample;
                if (remaining <= 0)
                    break;

                const int blockSamples = std::min(samplesPerPeak, remaining);
                float maxAbs = 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float *channelData = buffer.getReadPointer(ch);
                    for (int s = 0; s < blockSamples; ++s)
                        maxAbs = std::max(maxAbs, std::abs(channelData[startSample + s]));
                }

                peaks[static_cast<size_t>(i)] = maxAbs;
            }

            return peaks;
        }

        std::optional<uint32_t> readU32LE(const uint8_t *data, size_t size, size_t offset)
        {
            if (offset + 4 > size)
                return std::nullopt;

            return static_cast<uint32_t>(data[offset]) |
                   (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) |
                   (static_cast<uint32_t>(data[offset + 3]) << 24);
        }

        std::optional<uint16_t> readU16LE(const uint8_t *data, size_t size, size_t offset)
        {
            if (offset + 2 > size)
                return std::nullopt;

            return static_cast<uint16_t>(
                static_cast<uint16_t>(data[offset]) |
                static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8));
        }

        std::optional<uint32_t> readU32BE(const uint8_t *data, size_t size, size_t offset)
        {
            if (offset + 4 > size)
                return std::nullopt;

            return (static_cast<uint32_t>(data[offset]) << 24) |
                   (static_cast<uint32_t>(data[offset + 1]) << 16) |
                   (static_cast<uint32_t>(data[offset + 2]) << 8) |
                   static_cast<uint32_t>(data[offset + 3]);
        }

        std::optional<uint16_t> readU16BE(const uint8_t *data, size_t size, size_t offset)
        {
            if (offset + 2 > size)
                return std::nullopt;

            return static_cast<uint16_t>(
                (static_cast<uint16_t>(data[offset]) << 8) |
                static_cast<uint16_t>(data[offset + 1]));
        }

        std::optional<float> readF32LE(const uint8_t *data, size_t size, size_t offset)
        {
            const auto raw = readU32LE(data, size, offset);
            if (!raw.has_value())
                return std::nullopt;

            float value = 0.0f;
            static_assert(sizeof(value) == sizeof(uint32_t));
            std::memcpy(&value, &raw.value(), sizeof(value));
            return value;
        }

        juce::String midiNoteName(int midiNote)
        {
            static constexpr const char *kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            if (midiNote < 0 || midiNote > 127)
                return {};

            const int octave = (midiNote / 12) - 1;
            return juce::String(kNames[midiNote % 12]) + juce::String(octave);
        }

        void parseWavAcidAndLoopMetadata(const std::filesystem::path &path, FileRecord &rec)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return;

            std::array<char, 12> riffHeader{};
            in.read(riffHeader.data(), static_cast<std::streamsize>(riffHeader.size()));
            if (in.gcount() != static_cast<std::streamsize>(riffHeader.size()))
                return;

            if (std::memcmp(riffHeader.data(), "RIFF", 4) != 0 || std::memcmp(riffHeader.data() + 8, "WAVE", 4) != 0)
                return;

            std::vector<int64_t> cueSampleOffsets;

            while (in)
            {
                std::array<char, 8> chunkHeader{};
                in.read(chunkHeader.data(), static_cast<std::streamsize>(chunkHeader.size()));
                if (in.gcount() != static_cast<std::streamsize>(chunkHeader.size()))
                    break;

                const char *chunkId = chunkHeader.data();
                const uint32_t chunkSize = static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[4])) |
                                           (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[5])) << 8) |
                                           (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[6])) << 16) |
                                           (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[7])) << 24);

                std::vector<uint8_t> chunkData(chunkSize);
                if (chunkSize > 0)
                {
                    in.read(reinterpret_cast<char *>(chunkData.data()), static_cast<std::streamsize>(chunkSize));
                    if (in.gcount() != static_cast<std::streamsize>(chunkSize))
                        break;
                }

                if ((chunkSize & 1u) != 0u)
                    in.seekg(1, std::ios::cur);

                if (std::memcmp(chunkId, "acid", 4) == 0)
                {
                    rec.loopType = std::string("acidized");

                    if (const auto rootNote = readU16LE(chunkData.data(), chunkData.size(), 4))
                    {
                        rec.acidRootNote = static_cast<int>(*rootNote);
                        if (!rec.key.has_value())
                        {
                            const auto noteName = midiNoteName(static_cast<int>(*rootNote));
                            if (noteName.isNotEmpty())
                                rec.key = noteName.toStdString();
                        }
                    }

                    if (const auto beats = readU32LE(chunkData.data(), chunkData.size(), 8))
                        rec.acidBeats = static_cast<int>(*beats);

                    std::optional<float> tempo;
                    if (chunkData.size() >= 16)
                        tempo = readF32LE(chunkData.data(), chunkData.size(), 12);
                    if ((!tempo.has_value() || *tempo <= 0.0f || *tempo > 400.0f) && chunkData.size() >= 24)
                        tempo = readF32LE(chunkData.data(), chunkData.size(), 20);

                    if (tempo.has_value())
                    {
                        const double bpm = static_cast<double>(*tempo);
                        if (bpm > 0.0 && bpm < 400.0)
                            rec.bpm = bpm;
                    }
                }
                else if (std::memcmp(chunkId, "smpl", 4) == 0)
                {
                    const auto numLoops = readU32LE(chunkData.data(), chunkData.size(), 28);
                    if (!numLoops.has_value() || *numLoops == 0)
                        continue;

                    const size_t firstLoopOffset = 36;
                    if (firstLoopOffset + 24 > chunkData.size())
                        continue;

                    const auto loopStart = readU32LE(chunkData.data(), chunkData.size(), firstLoopOffset + 8);
                    const auto loopEnd = readU32LE(chunkData.data(), chunkData.size(), firstLoopOffset + 12);
                    if (loopStart.has_value() && loopEnd.has_value() && *loopEnd >= *loopStart)
                    {
                        rec.loopStartSample = static_cast<int64_t>(*loopStart);
                        rec.loopEndSample = static_cast<int64_t>(*loopEnd);
                    }
                }
                else if (std::memcmp(chunkId, "cue ", 4) == 0)
                {
                    const auto numCuePoints = readU32LE(chunkData.data(), chunkData.size(), 0);
                    if (!numCuePoints.has_value() || *numCuePoints == 0)
                        continue;

                    constexpr size_t cueHeaderSize = 4;
                    constexpr size_t cueEntrySize = 24;
                    const size_t availableEntries = (chunkData.size() > cueHeaderSize)
                                                        ? ((chunkData.size() - cueHeaderSize) / cueEntrySize)
                                                        : 0;
                    const size_t entriesToRead = std::min<size_t>(*numCuePoints, availableEntries);

                    for (size_t i = 0; i < entriesToRead; ++i)
                    {
                        const size_t entryOffset = cueHeaderSize + i * cueEntrySize;
                        const auto sampleOffset = readU32LE(chunkData.data(), chunkData.size(), entryOffset + 20);
                        if (sampleOffset.has_value())
                            cueSampleOffsets.push_back(static_cast<int64_t>(*sampleOffset));
                    }
                }
            }

            if ((!rec.loopStartSample.has_value() || !rec.loopEndSample.has_value()) && cueSampleOffsets.size() >= 2)
            {
                std::sort(cueSampleOffsets.begin(), cueSampleOffsets.end());
                rec.loopStartSample = cueSampleOffsets.front();
                rec.loopEndSample = cueSampleOffsets.back();
            }

            if (rec.loopType.has_value() && *rec.loopType == "acidized" &&
                !rec.loopStartSample.has_value() && rec.acidBeats.has_value() &&
                rec.bpm.has_value() && rec.sampleRate.has_value())
            {
                const double estimatedSeconds = (60.0 * static_cast<double>(*rec.acidBeats)) / *rec.bpm;
                const auto estimatedSamples = static_cast<int64_t>(std::round(estimatedSeconds * static_cast<double>(*rec.sampleRate)));
                if (estimatedSamples > 1)
                {
                    rec.loopStartSample = 0;
                    rec.loopEndSample = estimatedSamples - 1;
                }
            }
        }

        void parseAiffAppleLoopMetadata(const std::filesystem::path &path, FileRecord &rec)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return;

            std::array<char, 12> formHeader{};
            in.read(formHeader.data(), static_cast<std::streamsize>(formHeader.size()));
            if (in.gcount() != static_cast<std::streamsize>(formHeader.size()))
                return;

            if (std::memcmp(formHeader.data(), "FORM", 4) != 0)
                return;

            const bool isAiff = std::memcmp(formHeader.data() + 8, "AIFF", 4) == 0 ||
                                std::memcmp(formHeader.data() + 8, "AIFC", 4) == 0;
            if (!isAiff)
                return;

            std::unordered_map<uint16_t, int64_t> markerIdToSample;
            std::optional<uint16_t> sustainLoopBeginMarkerId;
            std::optional<uint16_t> sustainLoopEndMarkerId;
            std::optional<int> rootMidiNote;
            bool hasAppleApplicationChunk = false;

            while (in)
            {
                std::array<char, 8> chunkHeader{};
                in.read(chunkHeader.data(), static_cast<std::streamsize>(chunkHeader.size()));
                if (in.gcount() != static_cast<std::streamsize>(chunkHeader.size()))
                    break;

                const char *chunkId = chunkHeader.data();
                const uint32_t chunkSize = (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[4])) << 24) |
                                           (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[5])) << 16) |
                                           (static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[6])) << 8) |
                                           static_cast<uint32_t>(static_cast<uint8_t>(chunkHeader[7]));

                std::vector<uint8_t> chunkData(chunkSize);
                if (chunkSize > 0)
                {
                    in.read(reinterpret_cast<char *>(chunkData.data()), static_cast<std::streamsize>(chunkSize));
                    if (in.gcount() != static_cast<std::streamsize>(chunkSize))
                        break;
                }

                if ((chunkSize & 1u) != 0u)
                    in.seekg(1, std::ios::cur);

                if (std::memcmp(chunkId, "INST", 4) == 0)
                {
                    if (chunkData.size() >= 20)
                    {
                        rootMidiNote = static_cast<int>(chunkData[0]);

                        const auto sustainPlayMode = readU16BE(chunkData.data(), chunkData.size(), 8);
                        const auto beginMarkerId = readU16BE(chunkData.data(), chunkData.size(), 10);
                        const auto endMarkerId = readU16BE(chunkData.data(), chunkData.size(), 12);

                        if (sustainPlayMode.has_value() && *sustainPlayMode != 0 &&
                            beginMarkerId.has_value() && endMarkerId.has_value() &&
                            *beginMarkerId > 0 && *endMarkerId > 0)
                        {
                            sustainLoopBeginMarkerId = beginMarkerId;
                            sustainLoopEndMarkerId = endMarkerId;
                        }
                    }
                }
                else if (std::memcmp(chunkId, "MARK", 4) == 0)
                {
                    const auto markerCount = readU16BE(chunkData.data(), chunkData.size(), 0);
                    if (!markerCount.has_value() || *markerCount == 0)
                        continue;

                    size_t offset = 2;
                    for (uint16_t markerIndex = 0; markerIndex < *markerCount; ++markerIndex)
                    {
                        if (offset + 7 > chunkData.size())
                            break;

                        const auto markerId = readU16BE(chunkData.data(), chunkData.size(), offset);
                        const auto markerPosition = readU32BE(chunkData.data(), chunkData.size(), offset + 2);
                        if (!markerId.has_value() || !markerPosition.has_value())
                            break;

                        markerIdToSample[*markerId] = static_cast<int64_t>(*markerPosition);

                        const size_t markerNameLengthOffset = offset + 6;
                        const uint8_t markerNameLength = chunkData[markerNameLengthOffset];
                        size_t markerRecordSize = static_cast<size_t>(2 + 4 + 1 + markerNameLength);
                        if ((markerRecordSize & 1u) != 0u)
                            ++markerRecordSize;

                        offset += markerRecordSize;
                    }
                }
                else if (std::memcmp(chunkId, "APPL", 4) == 0)
                {
                    if (chunkData.size() >= 4)
                    {
                        if (std::memcmp(chunkData.data(), "stoc", 4) == 0 ||
                            std::memcmp(chunkData.data(), "AAPL", 4) == 0)
                        {
                            hasAppleApplicationChunk = true;
                        }
                    }
                }
            }

            if (rootMidiNote.has_value() && *rootMidiNote >= 0 && *rootMidiNote <= 127)
            {
                rec.acidRootNote = *rootMidiNote;
                if (!rec.key.has_value())
                {
                    const auto noteName = midiNoteName(*rootMidiNote);
                    if (noteName.isNotEmpty())
                        rec.key = noteName.toStdString();
                }
            }

            if (sustainLoopBeginMarkerId.has_value() && sustainLoopEndMarkerId.has_value())
            {
                const auto beginIt = markerIdToSample.find(*sustainLoopBeginMarkerId);
                const auto endIt = markerIdToSample.find(*sustainLoopEndMarkerId);
                if (beginIt != markerIdToSample.end() && endIt != markerIdToSample.end())
                {
                    const int64_t loopStart = std::min(beginIt->second, endIt->second);
                    const int64_t loopEnd = std::max(beginIt->second, endIt->second);
                    if (loopEnd > loopStart)
                    {
                        rec.loopStartSample = loopStart;
                        rec.loopEndSample = loopEnd;
                    }
                }
            }

            if (hasAppleApplicationChunk || rec.acidRootNote.has_value() || rec.loopStartSample.has_value())
                rec.loopType = std::string("apple-loop");
        }
    }

    Scanner::Scanner(CatalogDb &db, JobQueue &queue)
        : catalogDb(db), jobQueue(queue)
    {
    }

    void Scanner::setWaveCacheBlobDb(WaveCacheBlobDb *blobDb) noexcept
    {
        waveCacheBlobDb = blobDb;
    }

    bool Scanner::isPlayableExtension(const std::string &ext)
    {
        if (ext == "wav" || ext == "aif" || ext == "aiff" || ext == "flac" || ext == "mp3")
            return true;

        // REX/RX2 files are playable when the REX SDK is available;
        // otherwise they fall back to index-only.
        if (isRexExtension(ext))
            return RexManager::isAvailable();

        return false;
    }

    bool Scanner::isIndexOnlyExtension(const std::string &ext)
    {
        // REX files without the SDK loaded are index-only.
        if (isRexExtension(ext))
            return !RexManager::isAvailable();

        return false;
    }

    void Scanner::scanRoot(int64_t rootId,
                           const std::string &rootPath,
                           ProgressCallback onProgress,
                           CompletionCallback onCompleted)
    {
        struct ScanCandidate
        {
            fs::path absolutePath;
            std::string relativePath;
            std::string extension;
            bool playable = false;
            bool indexOnly = false;
        };

        struct ScanState
        {
            std::atomic<int> pendingJobs{0};
            std::atomic<bool> completionSignalled{false};
        };

        auto state = std::make_shared<ScanState>();

        const auto notifyCompletedOnce = [state, onCompleted]()
        {
            if (state->completionSignalled.exchange(true, std::memory_order_acq_rel))
                return;

            if (onCompleted)
                onCompleted();
        };

        const auto markJobFinished = [state, notifyCompletedOnce]()
        {
            const int previous = state->pendingJobs.fetch_sub(1, std::memory_order_acq_rel);
            if (previous == 1)
                notifyCompletedOnce();
        };

        state->pendingJobs.store(1, std::memory_order_release);

        jobQueue.enqueue(Job{
            [this, rootId, rootPath, onProgress, state, notifyCompletedOnce, markJobFinished](const std::atomic<uint64_t> &cancelGeneration, uint64_t jobGeneration)
            {
                const auto isCancelled = [&cancelGeneration, jobGeneration]()
                {
                    return cancelGeneration.load(std::memory_order_relaxed) != jobGeneration;
                };

                if (isCancelled())
                {
                    markJobFinished();
                    return;
                }

                std::vector<ScanCandidate> candidates;
                candidates.reserve(1024);

                std::error_code ec;
                for (auto it = fs::recursive_directory_iterator(rootPath,
                                                                fs::directory_options::skip_permission_denied, ec);
                     it != fs::recursive_directory_iterator(); ++it)
                {
                    if (isCancelled())
                    {
                        markJobFinished();
                        return;
                    }

                    if (!it->is_regular_file(ec))
                        continue;

                    const auto &path = it->path();
                    std::string ext = path.extension().string();
                    if (!ext.empty() && ext[0] == '.')
                        ext = ext.substr(1);
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c)
                                   { return static_cast<char>(std::tolower(c)); });

                    const bool playable = isPlayableExtension(ext);
                    const bool indexOnly = isIndexOnlyExtension(ext);
                    if (!playable && !indexOnly)
                        continue;

                    // Build relative path from source
                    auto relPath = fs::relative(path, rootPath, ec).generic_string();
                    if (ec)
                        continue;

                    candidates.push_back(ScanCandidate{
                        path,
                        std::move(relPath),
                        std::move(ext),
                        playable,
                        indexOnly});
                }

                if (candidates.empty())
                {
                    markJobFinished();
                    return;
                }

                thread_local juce::AudioFormatManager formatManager;
                thread_local bool formatsRegistered = false;
                if (!formatsRegistered)
                {
                    formatManager.registerBasicFormats();
                    formatsRegistered = true;
                }

                constexpr size_t kChunkSize = 32;
                for (size_t begin = 0; begin < candidates.size(); begin += kChunkSize)
                {
                    if (isCancelled())
                    {
                        markJobFinished();
                        return;
                    }

                    const size_t end = std::min(begin + kChunkSize, candidates.size());
                    if (!catalogDb.beginTransaction())
                    {
                        markJobFinished();
                        return;
                    }

                    bool transactionOpen = true;
                    for (size_t i = begin; i < end; ++i)
                    {
                        const auto &candidate = candidates[i];

                        if (isCancelled())
                        {
                            if (transactionOpen)
                                catalogDb.rollbackTransaction();
                            markJobFinished();
                            return;
                        }

                        FileRecord rec;
                        rec.rootId = rootId;
                        rec.relativePath = candidate.relativePath;
                        rec.filename = candidate.absolutePath.filename().string();
                        rec.extension = candidate.extension;
                        rec.indexOnly = candidate.indexOnly;

                        std::error_code fileEc;
                        rec.sizeBytes = static_cast<int64_t>(fs::file_size(candidate.absolutePath, fileEc));
                        auto ftime = fs::last_write_time(candidate.absolutePath, fileEc);
                        rec.modifiedTime = static_cast<int64_t>(
                            std::chrono::duration_cast<std::chrono::seconds>(
                                ftime.time_since_epoch())
                                .count());

                        std::vector<float> overviewPeaks;

                        if (candidate.playable && isRexExtension(candidate.extension))
                        {
                            // --- REX / RX2: use the REX SDK for metadata ---
                            const auto absStr = candidate.absolutePath.string();
                            if (auto rexInfo = RexManager::readInfo(absStr))
                            {
                                rec.sampleRate = rexInfo->sampleRate;
                                rec.channels = rexInfo->channels;
                                rec.bitDepth = rexInfo->bitDepth;
                                rec.totalSamples = rexInfo->totalSamples;
                                rec.durationSec = rexInfo->durationSec;
                                rec.bpm = rexInfo->bpm;
                                rec.sliceCount = rexInfo->sliceCount;
                                rec.loopType = "rex";

                                rec.codec = (candidate.extension == "rx2") ? "REX2" : "REX";

                                if (rec.durationSec.has_value() && *rec.durationSec > 0.0 && rec.sizeBytes > 0)
                                {
                                    const auto kbps = static_cast<int>((static_cast<double>(rec.sizeBytes) * 8.0) / (*rec.durationSec * 1000.0));
                                    rec.bitrateKbps = kbps;
                                }

                                if (waveCacheBlobDb != nullptr && waveCacheBlobDb->isOpen())
                                {
                                    double decodedSampleRate = 0.0;
                                    if (auto decoded = RexManager::decodeToBuffer(absStr, decodedSampleRate))
                                        overviewPeaks = buildOverviewPeaks(*decoded, 256);
                                }
                            }
                        }
                        else if (candidate.playable)
                        {
                            if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(juce::File(candidate.absolutePath.string()))))
                            {
                                rec.totalSamples = static_cast<int64_t>(reader->lengthInSamples);
                                rec.sampleRate = static_cast<int>(reader->sampleRate);
                                rec.channels = static_cast<int>(reader->numChannels);
                                rec.bitDepth = reader->bitsPerSample;
                                rec.codec = reader->getFormatName().toStdString();

                                if (reader->sampleRate > 0.0)
                                    rec.durationSec = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;

                                if (rec.durationSec.has_value() && *rec.durationSec > 0.0 && rec.sizeBytes > 0)
                                {
                                    const auto kbps = static_cast<int>((static_cast<double>(rec.sizeBytes) * 8.0) / (*rec.durationSec * 1000.0));
                                    rec.bitrateKbps = kbps;
                                }

                                if (candidate.extension == "wav")
                                    parseWavAcidAndLoopMetadata(candidate.absolutePath, rec);
                                else if (candidate.extension == "aif" || candidate.extension == "aiff")
                                    parseAiffAppleLoopMetadata(candidate.absolutePath, rec);

                                if (waveCacheBlobDb != nullptr && waveCacheBlobDb->isOpen())
                                    overviewPeaks = buildOverviewPeaks(*reader, 256);
                            }
                        }

                        if (!catalogDb.upsertFile(rec))
                        {
                            if (transactionOpen)
                                catalogDb.rollbackTransaction();
                            markJobFinished();
                            return;
                        }

                        if (waveCacheBlobDb != nullptr && waveCacheBlobDb->isOpen() && !overviewPeaks.empty())
                        {
                            const auto cacheKey = buildWaveCacheKey(rec.rootId,
                                                                    rec.relativePath,
                                                                    rec.sizeBytes,
                                                                    rec.modifiedTime);

                            if (const auto persisted = catalogDb.fileByRootAndRelativePath(rec.rootId, rec.relativePath); persisted.has_value())
                                waveCacheBlobDb->upsertPeaksByKey(cacheKey, persisted->id, overviewPeaks);
                        }

                        if (onProgress)
                            onProgress(candidate.relativePath);
                    }

                    if (!catalogDb.commitTransaction())
                    {
                        markJobFinished();
                        return;
                    }
                    transactionOpen = false;
                }

                markJobFinished();
            },
            JobPriority::Low});
    }

} // namespace sw
