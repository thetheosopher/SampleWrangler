#include "Catalog/CatalogDb.h"
#include "Pipeline/Analyzer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    class TemporaryDirectory
    {
    public:
        explicit TemporaryDirectory(std::filesystem::path directory)
            : path(std::move(directory))
        {
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        std::filesystem::path path;
    };

    void writeUint16(std::ostream &stream, uint16_t value)
    {
        stream.put(static_cast<char>(value & 0xffu));
        stream.put(static_cast<char>((value >> 8) & 0xffu));
    }

    void writeUint32(std::ostream &stream, uint32_t value)
    {
        for (unsigned int shift = 0; shift < 32; shift += 8)
            stream.put(static_cast<char>((value >> shift) & 0xffu));
    }

    bool writeWaveSamples(const std::filesystem::path &path,
                          uint32_t sampleRate,
                          const std::vector<float> &samples)
    {
        if (samples.size() > (std::numeric_limits<uint32_t>::max() - 36u) / sizeof(int16_t))
            return false;

        const auto sampleCount = static_cast<uint32_t>(samples.size());
        const uint32_t actualDataSize = sampleCount * sizeof(int16_t);

        std::ofstream stream(path, std::ios::binary);
        if (!stream)
            return false;

        stream.write("RIFF", 4);
        writeUint32(stream, 36 + actualDataSize);
        stream.write("WAVEfmt ", 8);
        writeUint32(stream, 16);
        writeUint16(stream, 1);
        writeUint16(stream, 1);
        writeUint32(stream, sampleRate);
        writeUint32(stream, sampleRate * sizeof(int16_t));
        writeUint16(stream, sizeof(int16_t));
        writeUint16(stream, 16);
        stream.write("data", 4);
        writeUint32(stream, actualDataSize);

        for (const auto sample : samples)
        {
            const auto quantized = static_cast<int16_t>(std::lround(
                std::clamp(static_cast<double>(sample), -1.0, 1.0) * 32767.0));
            writeUint16(stream, static_cast<uint16_t>(quantized));
        }
        return stream.good();
    }

    bool writeAnalysisFixture(const std::filesystem::path &path)
    {
        constexpr uint32_t sampleRate = 8000;
        constexpr uint32_t sampleCount = 20000;
        std::vector<float> samples(sampleCount);
        for (uint32_t i = 0; i < sampleCount; ++i)
            samples[i] = (i % 2 == 0) ? 0.5f : -0.5f;
        return writeWaveSamples(path, sampleRate, samples);
    }

    bool writeTempoAndKeyFixture(const std::filesystem::path &path)
    {
        constexpr uint32_t sampleRate = 11000;
        constexpr uint32_t durationSeconds = 8;
        constexpr uint32_t sampleCount = sampleRate * durationSeconds;
        constexpr uint32_t beatPeriod = sampleRate / 2;
        std::vector<float> samples(sampleCount);
        for (uint32_t i = 0; i < sampleCount; ++i)
        {
            const double time = static_cast<double>(i) / sampleRate;
            double sample = 0.14 * (std::sin(2.0 * 3.14159265358979323846 * 261.63 * time) +
                                     std::sin(2.0 * 3.14159265358979323846 * 329.63 * time) +
                                     std::sin(2.0 * 3.14159265358979323846 * 392.00 * time));
            const uint32_t beatOffset = i % beatPeriod;
            if (beatOffset < sampleRate / 200)
                sample += 0.75 * std::exp(-700.0 * static_cast<double>(beatOffset) / sampleRate);
            samples[i] = static_cast<float>(sample);
        }
        return writeWaveSamples(path, sampleRate, samples);
    }

    bool writeMinorKeyFixture(const std::filesystem::path &path)
    {
        constexpr uint32_t sampleRate = 11000;
        constexpr uint32_t durationSeconds = 8;
        constexpr uint32_t sampleCount = sampleRate * durationSeconds;
        std::vector<float> samples(sampleCount);
        for (uint32_t i = 0; i < sampleCount; ++i)
        {
            const double time = static_cast<double>(i) / sampleRate;
            samples[i] = static_cast<float>(0.2 * (std::sin(2.0 * 3.14159265358979323846 * 220.0 * time) +
                                                    std::sin(2.0 * 3.14159265358979323846 * 261.63 * time) +
                                                    std::sin(2.0 * 3.14159265358979323846 * 329.63 * time)));
        }
        return writeWaveSamples(path, sampleRate, samples);
    }

    bool writeRepeatedLoopFixture(const std::filesystem::path &path)
    {
        constexpr uint32_t sampleRate = 11000;
        constexpr uint32_t loopLengthSamples = sampleRate * 2;
        constexpr uint32_t repetitions = 4;
        std::vector<float> loop(loopLengthSamples, 0.0f);
        constexpr std::array<double, 4> eventTimes{0.17, 0.33, 1.09, 1.43};
        for (uint32_t i = 0; i < loopLengthSamples; ++i)
        {
            const double time = static_cast<double>(i) / sampleRate;
            double sample = 0.0;
            for (const auto eventTime : eventTimes)
            {
                const double elapsed = time - eventTime;
                if (elapsed >= 0.0 && elapsed < 0.025)
                    sample += 0.8 * std::exp(-150.0 * elapsed) *
                              std::sin(2.0 * 3.14159265358979323846 * 330.0 * elapsed);
            }
            loop[i] = static_cast<float>(sample);
        }

        std::vector<float> samples;
        samples.reserve(static_cast<size_t>(loopLengthSamples) * repetitions);
        for (uint32_t repetition = 0; repetition < repetitions; ++repetition)
            samples.insert(samples.end(), loop.begin(), loop.end());
        return writeWaveSamples(path, sampleRate, samples);
    }

    std::optional<sw::AnalysisResultRecord> waitForAnalysis(sw::CatalogDb &db,
                                                            int64_t fileId,
                                                            const std::string &expectedHash)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto result = db.analysisResultByFileId(fileId);
            if (result && result->analyzedContentHash == std::optional<std::string>{expectedHash})
                return result;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return std::nullopt;
    }

    bool testStreamingAnalysisAndInvalidation()
    {
        const auto uniqueSuffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        TemporaryDirectory directory(
            std::filesystem::temp_directory_path() / ("SampleWranglerAnalyzer-" + uniqueSuffix));
        std::error_code filesystemError;
        if (!std::filesystem::create_directories(directory.path, filesystemError) || filesystemError)
            return false;

        const auto audioPath = directory.path / "feature.wav";
        if (!writeAnalysisFixture(audioPath))
            return false;

        sw::CatalogDb db;
        if (!db.open(":memory:") || !db.addRoot(directory.path.string(), "Analyzer test"))
            return false;

        const auto roots = db.allRoots();
        if (roots.size() != 1)
            return false;

        const auto modifiedTime = std::chrono::duration_cast<std::chrono::seconds>(
                                      std::filesystem::last_write_time(audioPath).time_since_epoch())
                                      .count();
        sw::FileRecord file;
        file.rootId = roots.front().id;
        file.relativePath = "feature.wav";
        file.filename = "feature.wav";
        file.extension = "wav";
        file.sizeBytes = static_cast<int64_t>(std::filesystem::file_size(audioPath));
        file.modifiedTime = modifiedTime;
        file.contentHash = "fixture-hash-v1";
        file.bpm = 128.0;
        file.key = "F#m";
        if (!db.upsertFile(file))
            return false;

        const auto storedFile = db.fileByRootAndRelativePath(file.rootId, file.relativePath);
        if (!storedFile)
            return false;

        {
            sw::JobQueue queue{1};
            sw::Analyzer analyzer{db, queue};
            analyzer.analyzeRoot(file.rootId);

            const auto result = waitForAnalysis(db, storedFile->id, "fixture-hash-v1");
            if (!result || result->status != sw::AnalysisStatus::Complete ||
                result->analysisVersion != sw::kCurrentAnalysisVersion ||
                result->featureVectorVersion != sw::kCurrentAnalysisFeatureVectorVersion ||
                result->featureVector.size() != sw::kCurrentAnalysisFeatureCount ||
                std::abs(result->featureVector[0] - 2.5f) > 0.0001f ||
                std::abs(result->featureVector[1] - 0.5f) > 0.0001f ||
                std::abs(result->featureVector[2] - 0.5f) > 0.0001f ||
                std::abs(result->featureVector[3] - 1.0f) > 0.0001f ||
                result->detectedBpm || result->detectedKey ||
                !db.listFileIdsNeedingAnalysisByRoot(
                    file.rootId, sw::kCurrentAnalysisVersion, sw::kCurrentAnalysisFeatureVectorVersion)
                     .empty())
            {
                std::cerr << "Streaming analysis feature extraction failed.\n";
                if (result)
                    std::cerr << "Feature vector: "
                              << (result->featureVector.size() > 0 ? result->featureVector[0] : -1.0f) << ", "
                              << (result->featureVector.size() > 1 ? result->featureVector[1] : -1.0f) << ", "
                              << (result->featureVector.size() > 2 ? result->featureVector[2] : -1.0f) << ", "
                              << (result->featureVector.size() > 3 ? result->featureVector[3] : -1.0f)
                              << "; inferred BPM: " << result->detectedBpm.value_or(-1.0)
                              << "; inferred key: " << result->detectedKey.value_or("none")
                              << " (" << result->keyConfidence.value_or(-1.0) << ")"
                              << "; stale queue count: "
                              << db.listFileIdsNeedingAnalysisByRoot(
                                     file.rootId,
                                     sw::kCurrentAnalysisVersion,
                                     sw::kCurrentAnalysisFeatureVectorVersion)
                                     .size()
                              << '\n';
                return false;
            }

            const auto fileAfterAnalysis = db.fileById(storedFile->id);
            if (!fileAfterAnalysis ||
                fileAfterAnalysis->bpm != std::optional<double>{128.0} ||
                fileAfterAnalysis->key != std::optional<std::string>{"F#m"})
            {
                std::cerr << "Analysis overwrote embedded BPM/key metadata.\n";
                return false;
            }

            const auto tempoAndKeyPath = directory.path / "tempo-key.wav";
            const auto minorKeyPath = directory.path / "minor-key.wav";
            const auto loopPath = directory.path / "repeating-loop.wav";
            if (!writeTempoAndKeyFixture(tempoAndKeyPath) ||
                !writeMinorKeyFixture(minorKeyPath) ||
                !writeRepeatedLoopFixture(loopPath))
                return false;

            const auto makeCatalogFile = [&db, rootId = file.rootId](const std::filesystem::path &path,
                                                                     const std::string &relativePath,
                                                                     const std::string &hash)
                -> std::optional<sw::FileRecord>
            {
                sw::FileRecord audioFile;
                audioFile.rootId = rootId;
                audioFile.relativePath = relativePath;
                audioFile.filename = path.filename().string();
                audioFile.extension = "wav";
                audioFile.sizeBytes = static_cast<int64_t>(std::filesystem::file_size(path));
                audioFile.modifiedTime = std::chrono::duration_cast<std::chrono::seconds>(
                                             std::filesystem::last_write_time(path).time_since_epoch())
                                             .count();
                audioFile.durationSec = 8.0;
                audioFile.sampleRate = 11000;
                audioFile.channels = 1;
                audioFile.contentHash = hash;
                if (!db.upsertFile(audioFile))
                    return std::nullopt;
                return db.fileByRootAndRelativePath(rootId, relativePath);
            };

            auto tempoAndKeyFile = makeCatalogFile(tempoAndKeyPath, "tempo-key.wav", "tempo-key-hash-v1");
            auto minorKeyFile = makeCatalogFile(minorKeyPath, "minor-key.wav", "minor-key-hash-v1");
            auto loopFile = makeCatalogFile(loopPath, "repeating-loop.wav", "loop-hash-v1");
            if (!tempoAndKeyFile || !minorKeyFile || !loopFile)
                return false;

            analyzer.analyzeFile(tempoAndKeyFile->id);
            const auto tempoAndKeyResult = waitForAnalysis(db, tempoAndKeyFile->id, "tempo-key-hash-v1");
            const auto inferredTempoAndKeyFile = db.fileById(tempoAndKeyFile->id);
            if (!tempoAndKeyResult || !inferredTempoAndKeyFile ||
                !tempoAndKeyResult->detectedBpm ||
                std::abs(*tempoAndKeyResult->detectedBpm - 120.0) > 3.0 ||
                !tempoAndKeyResult->bpmConfidence || *tempoAndKeyResult->bpmConfidence < 0.2 ||
                !tempoAndKeyResult->detectedKey ||
                *tempoAndKeyResult->detectedKey != "C major" ||
                !tempoAndKeyResult->keyConfidence || *tempoAndKeyResult->keyConfidence < 0.25 ||
                !tempoAndKeyResult->transientCount || *tempoAndKeyResult->transientCount < 12 ||
                *tempoAndKeyResult->transientCount > 24 ||
                !inferredTempoAndKeyFile->inferredBpm ||
                !inferredTempoAndKeyFile->inferredKey ||
                inferredTempoAndKeyFile->bpm.has_value() ||
                inferredTempoAndKeyFile->key.has_value())
            {
                std::cerr << "Tempo, key, or transient inference failed.\n";
                if (tempoAndKeyResult)
                {
                    std::cerr << "BPM: " << tempoAndKeyResult->detectedBpm.value_or(-1.0)
                              << ", key: " << tempoAndKeyResult->detectedKey.value_or("none")
                              << ", transients: " << tempoAndKeyResult->transientCount.value_or(-1) << '\n';
                }
                return false;
            }

            analyzer.analyzeFile(minorKeyFile->id);
            const auto minorKeyResult = waitForAnalysis(db, minorKeyFile->id, "minor-key-hash-v1");
            if (!minorKeyResult || !minorKeyResult->detectedKey ||
                *minorKeyResult->detectedKey != "A minor" ||
                !minorKeyResult->keyConfidence || *minorKeyResult->keyConfidence < 0.25)
            {
                std::cerr << "Minor-key template inference failed.\n";
                if (minorKeyResult)
                    std::cerr << "Detected key: " << minorKeyResult->detectedKey.value_or("none")
                              << ", confidence: " << minorKeyResult->keyConfidence.value_or(-1.0) << '\n';
                return false;
            }

            tempoAndKeyFile->contentHash = "tempo-key-hash-v2";
            if (!db.upsertFile(*tempoAndKeyFile))
                return false;
            const auto staleTempoAndKeyFile = db.fileById(tempoAndKeyFile->id);
            if (!staleTempoAndKeyFile || staleTempoAndKeyFile->inferredBpm || staleTempoAndKeyFile->inferredKey)
            {
                std::cerr << "Stale analysis metadata was surfaced after the fingerprint changed.\n";
                return false;
            }
            analyzer.analyzeFile(tempoAndKeyFile->id);
            const auto refreshedInference = waitForAnalysis(db, tempoAndKeyFile->id, "tempo-key-hash-v2");
            if (!refreshedInference || !refreshedInference->detectedBpm ||
                !refreshedInference->detectedKey)
            {
                std::cerr << "Analysis did not refresh inferred metadata after fingerprint change.\n";
                return false;
            }

            analyzer.analyzeFile(loopFile->id);
            const auto loopResult = waitForAnalysis(db, loopFile->id, "loop-hash-v1");
            if (!loopResult || !loopResult->suggestedLoopStartSample ||
                *loopResult->suggestedLoopStartSample != 0 ||
                !loopResult->suggestedLoopEndSample ||
                std::abs(*loopResult->suggestedLoopEndSample - 22000) > 1000 ||
                !loopResult->loopConfidence || *loopResult->loopConfidence < 0.92)
            {
                std::cerr << "Repeated-loop boundary inference failed.\n";
                if (loopResult)
                    std::cerr << "Loop samples: " << loopResult->suggestedLoopEndSample.value_or(-1)
                              << ", confidence: " << loopResult->loopConfidence.value_or(-1.0) << '\n';
                return false;
            }
            const auto inferredLoopFile = db.fileById(loopFile->id);
            if (!inferredLoopFile ||
                inferredLoopFile->suggestedLoopStartSample != loopResult->suggestedLoopStartSample ||
                inferredLoopFile->suggestedLoopEndSample != loopResult->suggestedLoopEndSample ||
                inferredLoopFile->suggestedLoopConfidence != loopResult->loopConfidence)
            {
                std::cerr << "Current loop analysis metadata was not surfaced through the catalog.\n";
                return false;
            }

            file.contentHash = "fixture-hash-v2";
            if (!db.upsertFile(file))
                return false;
            analyzer.analyzeRoot(file.rootId);

            const auto refreshed = waitForAnalysis(db, storedFile->id, "fixture-hash-v2");
            if (!refreshed || refreshed->status != sw::AnalysisStatus::Complete)
            {
                std::cerr << "Analysis did not refresh after the content fingerprint changed.\n";
                return false;
            }
        }

        return true;
    }
}

int main()
{
    if (!testStreamingAnalysisAndInvalidation())
    {
        std::cerr << "testStreamingAnalysisAndInvalidation failed.\n";
        return 1;
    }
    return 0;
}
