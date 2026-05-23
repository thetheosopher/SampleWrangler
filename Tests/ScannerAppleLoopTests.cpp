#include "Catalog/CatalogDb.h"
#include "Catalog/WaveCacheBlobDb.h"
#include "Pipeline/JobQueue.h"
#include "Pipeline/Scanner.h"
#include "Pipeline/WaveformPeak.h"

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void appendU16BE(std::vector<uint8_t> &out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void appendU32BE(std::vector<uint8_t> &out, uint32_t value)
    {
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void appendU16LE(std::vector<uint8_t> &out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    void appendU32LE(std::vector<uint8_t> &out, uint32_t value)
    {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    void appendFixedString(std::vector<uint8_t> &out, const std::string &text, size_t width)
    {
        const auto bytesToCopy = std::min(width, text.size());
        out.insert(out.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(bytesToCopy));
        out.insert(out.end(), width - bytesToCopy, 0);
    }

    void appendChunk(std::vector<uint8_t> &out, const char id[4], const std::vector<uint8_t> &chunkData)
    {
        out.insert(out.end(), id, id + 4);
        appendU32BE(out, static_cast<uint32_t>(chunkData.size()));
        out.insert(out.end(), chunkData.begin(), chunkData.end());
        if ((chunkData.size() & 1u) != 0u)
            out.push_back(0);
    }

    void appendChunkLE(std::vector<uint8_t> &out, const char id[4], const std::vector<uint8_t> &chunkData)
    {
        out.insert(out.end(), id, id + 4);
        appendU32LE(out, static_cast<uint32_t>(chunkData.size()));
        out.insert(out.end(), chunkData.begin(), chunkData.end());
        if ((chunkData.size() & 1u) != 0u)
            out.push_back(0);
    }

    void appendListChunkLE(std::vector<uint8_t> &out, const char listType[4], const std::vector<uint8_t> &chunkData)
    {
        std::vector<uint8_t> listBody;
        listBody.insert(listBody.end(), listType, listType + 4);
        listBody.insert(listBody.end(), chunkData.begin(), chunkData.end());
        appendChunkLE(out, "LIST", listBody);
    }

    bool writeMinimalAppleLoopAiff(const std::filesystem::path &filePath)
    {
        std::vector<uint8_t> comm;
        appendU16BE(comm, 1);
        appendU32BE(comm, 2);
        appendU16BE(comm, 16);
        const uint8_t sampleRate80[10] = {0x40, 0x0E, 0xAC, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        comm.insert(comm.end(), std::begin(sampleRate80), std::end(sampleRate80));

        std::vector<uint8_t> ssnd;
        appendU32BE(ssnd, 0);
        appendU32BE(ssnd, 0);
        appendU16BE(ssnd, 0);
        appendU16BE(ssnd, 0);

        std::vector<uint8_t> inst;
        inst.push_back(60);
        inst.push_back(0);
        inst.push_back(0);
        inst.push_back(127);
        inst.push_back(1);
        inst.push_back(127);
        appendU16BE(inst, 0);
        appendU16BE(inst, 1);
        appendU16BE(inst, 1);
        appendU16BE(inst, 2);
        appendU16BE(inst, 0);
        appendU16BE(inst, 0);
        appendU16BE(inst, 0);

        std::vector<uint8_t> mark;
        appendU16BE(mark, 2);
        appendU16BE(mark, 1);
        appendU32BE(mark, 0);
        mark.push_back(5);
        mark.insert(mark.end(), {'s', 't', 'a', 'r', 't'});
        appendU16BE(mark, 2);
        appendU32BE(mark, 1);
        mark.push_back(3);
        mark.insert(mark.end(), {'e', 'n', 'd'});

        std::vector<uint8_t> appl;
        appl.insert(appl.end(), {'s', 't', 'o', 'c'});
        appl.insert(appl.end(), {'t', 'e', 's', 't'});

        std::vector<uint8_t> formBody;
        formBody.insert(formBody.end(), {'A', 'I', 'F', 'F'});
        appendChunk(formBody, "COMM", comm);
        appendChunk(formBody, "SSND", ssnd);
        appendChunk(formBody, "INST", inst);
        appendChunk(formBody, "MARK", mark);
        appendChunk(formBody, "APPL", appl);

        std::vector<uint8_t> fileData;
        fileData.insert(fileData.end(), {'F', 'O', 'R', 'M'});
        appendU32BE(fileData, static_cast<uint32_t>(formBody.size()));
        fileData.insert(fileData.end(), formBody.begin(), formBody.end());

        std::ofstream out(filePath, std::ios::binary);
        if (!out)
            return false;

        out.write(reinterpret_cast<const char *>(fileData.data()), static_cast<std::streamsize>(fileData.size()));
        return out.good();
    }

    bool writeMinimalWav(const std::filesystem::path &filePath)
    {
        constexpr uint16_t numChannels = 1;
        constexpr uint32_t sampleRate = 44100;
        constexpr uint16_t bitsPerSample = 16;
        constexpr uint16_t blockAlign = numChannels * (bitsPerSample / 8);
        constexpr uint32_t byteRate = sampleRate * blockAlign;
        const std::array<int16_t, 8> samples = {0, 4096, -4096, 2048, -2048, 1024, -1024, 0};
        const uint32_t dataChunkSize = static_cast<uint32_t>(samples.size() * sizeof(int16_t));

        std::vector<uint8_t> fileData;
        fileData.reserve(44 + dataChunkSize);
        fileData.insert(fileData.end(), {'R', 'I', 'F', 'F'});
        appendU32LE(fileData, 36u + dataChunkSize);
        fileData.insert(fileData.end(), {'W', 'A', 'V', 'E'});

        fileData.insert(fileData.end(), {'f', 'm', 't', ' '});
        appendU32LE(fileData, 16);
        appendU16LE(fileData, 1);
        appendU16LE(fileData, numChannels);
        appendU32LE(fileData, sampleRate);
        appendU32LE(fileData, byteRate);
        appendU16LE(fileData, blockAlign);
        appendU16LE(fileData, bitsPerSample);

        fileData.insert(fileData.end(), {'d', 'a', 't', 'a'});
        appendU32LE(fileData, dataChunkSize);
        for (const auto sample : samples)
            appendU16LE(fileData, static_cast<uint16_t>(sample));

        std::ofstream out(filePath, std::ios::binary);
        if (!out)
            return false;

        out.write(reinterpret_cast<const char *>(fileData.data()), static_cast<std::streamsize>(fileData.size()));
        return out.good();
    }

    bool writeTextFile(const std::filesystem::path &filePath, const std::string &contents)
    {
        std::ofstream out(filePath, std::ios::binary);
        if (!out)
            return false;

        out << contents;
        return out.good();
    }

    bool writeMinimalSf2(const std::filesystem::path &filePath)
    {
        const std::array<int16_t, 8> samples = {0, 4096, -4096, 2048, -2048, 1024, -1024, 0};

        std::vector<uint8_t> infoChunks;
        const std::string infoName = "Mini SF2";
        appendChunkLE(infoChunks,
                      "INAM",
                      std::vector<uint8_t>(infoName.begin(), infoName.end()));

        std::vector<uint8_t> smplData;
        smplData.reserve(samples.size() * sizeof(int16_t));
        for (const auto sample : samples)
            appendU16LE(smplData, static_cast<uint16_t>(sample));

        std::vector<uint8_t> sdtaChunks;
        appendChunkLE(sdtaChunks, "smpl", smplData);

        std::vector<uint8_t> phdr;
        appendFixedString(phdr, "Warm Pad", 20);
        appendU16LE(phdr, 0);
        appendU16LE(phdr, 0);
        appendU16LE(phdr, 0);
        appendU32LE(phdr, 0);
        appendU32LE(phdr, 0);
        appendU32LE(phdr, 0);

        appendFixedString(phdr, "EOP", 20);
        appendU16LE(phdr, 0);
        appendU16LE(phdr, 0);
        appendU16LE(phdr, 1);
        appendU32LE(phdr, 0);
        appendU32LE(phdr, 0);
        appendU32LE(phdr, 0);

        std::vector<uint8_t> pbag;
        appendU16LE(pbag, 0);
        appendU16LE(pbag, 0);
        appendU16LE(pbag, 1);
        appendU16LE(pbag, 0);

        std::vector<uint8_t> pgen;
        appendU16LE(pgen, 41);
        appendU16LE(pgen, 0);

        std::vector<uint8_t> inst;
        appendFixedString(inst, "Warm Layer", 20);
        appendU16LE(inst, 0);
        appendFixedString(inst, "EOI", 20);
        appendU16LE(inst, 1);

        std::vector<uint8_t> ibag;
        appendU16LE(ibag, 0);
        appendU16LE(ibag, 0);
        appendU16LE(ibag, 3);
        appendU16LE(ibag, 0);

        std::vector<uint8_t> igen;
        appendU16LE(igen, 43);
        appendU16LE(igen, static_cast<uint16_t>((72 << 8) | 48));
        appendU16LE(igen, 44);
        appendU16LE(igen, static_cast<uint16_t>((120 << 8) | 10));
        appendU16LE(igen, 53);
        appendU16LE(igen, 0);

        std::vector<uint8_t> shdr;
        appendFixedString(shdr, "WarmSample", 20);
        appendU32LE(shdr, 0);
        appendU32LE(shdr, static_cast<uint32_t>(samples.size()));
        appendU32LE(shdr, 1);
        appendU32LE(shdr, static_cast<uint32_t>(samples.size() - 1));
        appendU32LE(shdr, 44100);
        shdr.push_back(60);
        shdr.push_back(0);
        appendU16LE(shdr, 0);
        appendU16LE(shdr, 1);

        appendFixedString(shdr, "EOS", 20);
        appendU32LE(shdr, static_cast<uint32_t>(samples.size()));
        appendU32LE(shdr, static_cast<uint32_t>(samples.size()));
        appendU32LE(shdr, static_cast<uint32_t>(samples.size()));
        appendU32LE(shdr, static_cast<uint32_t>(samples.size()));
        appendU32LE(shdr, 44100);
        shdr.push_back(0);
        shdr.push_back(0);
        appendU16LE(shdr, 0);
        appendU16LE(shdr, 1);

        std::vector<uint8_t> pdtaChunks;
        appendChunkLE(pdtaChunks, "phdr", phdr);
        appendChunkLE(pdtaChunks, "pbag", pbag);
        appendChunkLE(pdtaChunks, "pgen", pgen);
        appendChunkLE(pdtaChunks, "inst", inst);
        appendChunkLE(pdtaChunks, "ibag", ibag);
        appendChunkLE(pdtaChunks, "igen", igen);
        appendChunkLE(pdtaChunks, "shdr", shdr);

        std::vector<uint8_t> riffBody;
        riffBody.insert(riffBody.end(), {'s', 'f', 'b', 'k'});
        appendListChunkLE(riffBody, "INFO", infoChunks);
        appendListChunkLE(riffBody, "sdta", sdtaChunks);
        appendListChunkLE(riffBody, "pdta", pdtaChunks);

        std::vector<uint8_t> fileData;
        fileData.insert(fileData.end(), {'R', 'I', 'F', 'F'});
        appendU32LE(fileData, static_cast<uint32_t>(riffBody.size()));
        fileData.insert(fileData.end(), riffBody.begin(), riffBody.end());

        std::ofstream out(filePath, std::ios::binary);
        if (!out)
            return false;

        out.write(reinterpret_cast<const char *>(fileData.data()), static_cast<std::streamsize>(fileData.size()));
        return out.good();
    }

    bool writeZipArchive(const std::filesystem::path &archivePath,
                         const std::vector<std::pair<std::filesystem::path, juce::String>> &entries)
    {
        juce::ZipFile::Builder builder;
        for (const auto &[sourcePath, storedPath] : entries)
            builder.addFile(juce::File(sourcePath.string()), 9, storedPath);

        juce::FileOutputStream output(juce::File(archivePath.string()));
        if (!output.openedOk())
            return false;

        const auto wroteArchive = builder.writeToStream(output, nullptr);
        output.flush();
        return wroteArchive && output.getStatus().wasOk();
    }

    bool waitForScanCompletion(sw::Scanner &scanner, int64_t rootId, const std::string &rootPath)
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;

        scanner.scanRoot(rootId, rootPath, {}, [&]
                         {
                             std::lock_guard<std::mutex> lock(mutex);
                             done = true;
                             cv.notify_one(); });

        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(5), [&done]
                           { return done; });
    }

    bool testAppleLoopAiffMetadataExtraction()
    {
        const auto uniqueSuffix = std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto testRoot = std::filesystem::temp_directory_path() / ("SampleWranglerAppleLoopTest_" + uniqueSuffix);

        std::error_code ec;
        std::filesystem::create_directories(testRoot, ec);
        if (ec)
            return false;

        const auto testFilePath = testRoot / "apple_loop_test.aif";
        if (!writeMinimalAppleLoopAiff(testFilePath))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::CatalogDb db;
        if (!db.open(":memory:"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        if (!db.addRoot(testRoot.string(), "AppleLoopRoot"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto roots = db.allRoots();
        if (roots.empty())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::JobQueue queue(1);
        sw::Scanner scanner(db, queue);

        if (!waitForScanCompletion(scanner, roots.front().id, testRoot.string()))
        {
            queue.shutdown();
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        queue.shutdown();

        const auto file = db.fileByRootAndRelativePath(roots.front().id, "apple_loop_test.aif");
        std::filesystem::remove_all(testRoot, ec);

        if (!file.has_value())
            return false;

        if (!file->loopType.has_value() || *file->loopType != "apple-loop")
            return false;

        if (!file->acidRootNote.has_value() || *file->acidRootNote != 60)
            return false;

        if (!file->loopStartSample.has_value() || *file->loopStartSample != 0)
            return false;

        if (!file->loopEndSample.has_value() || *file->loopEndSample != 1)
            return false;

        return true;
    }

    bool testIndexedSfzMetadataExtraction()
    {
        const auto uniqueSuffix = std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto testRoot = std::filesystem::temp_directory_path() / ("SampleWranglerSfzTest_" + uniqueSuffix);

        std::error_code ec;
        std::filesystem::create_directories(testRoot / "samples", ec);
        if (ec)
            return false;

        if (!writeMinimalWav(testRoot / "samples" / "one_shot.wav") ||
            !writeTextFile(testRoot / "kit.sfz",
                           "default_path=samples/\n"
                           "<group>\n"
                           "<region> sample=\"one_shot.wav\" pitch_keycenter=60 lokey=24 hikey=72 lovel=1 hivel=80\n"
                           "<region> sample=\"one_shot.wav\" pitch_keycenter=67 lokey=73 hikey=96 lovel=81 hivel=127\n"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::CatalogDb db;
        sw::WaveCacheBlobDb blobDb;
        if (!db.open(":memory:") || !blobDb.open((testRoot / "wave_cache.db").string()))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        if (!db.addRoot(testRoot.string(), "SfzRoot"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto roots = db.allRoots();
        if (roots.empty())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::JobQueue queue(1);
        sw::Scanner scanner(db, queue);
        scanner.setWaveCacheBlobDb(&blobDb);

        if (!waitForScanCompletion(scanner, roots.front().id, testRoot.string()))
        {
            queue.shutdown();
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        queue.shutdown();

        const auto file = db.fileByRootAndRelativePath(roots.front().id, "kit.sfz");
        if (!file.has_value())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto peaks = blobDb.peaksByKey(sw::buildWaveformCacheKey(file->rootId,
                                                                       file->relativePath,
                                                                       file->sizeBytes,
                                                                       file->modifiedTime));
        std::filesystem::remove_all(testRoot, ec);

        if (!file->indexOnly)
            return false;

        if (!file->codec.has_value() || file->codec->find("SFZ") == std::string::npos)
            return false;

        if (!file->sampleRate.has_value() || *file->sampleRate != 44100)
            return false;

        if (!file->channels.has_value() || *file->channels != 1)
            return false;

        if (!file->key.has_value() || *file->key != "C4")
            return false;

        if (!file->zoneCount.has_value() || *file->zoneCount != 2)
            return false;

        if (!file->keyLow.has_value() || *file->keyLow != 24)
            return false;

        if (!file->keyHigh.has_value() || *file->keyHigh != 96)
            return false;

        if (!file->velocityLow.has_value() || *file->velocityLow != 1)
            return false;

        if (!file->velocityHigh.has_value() || *file->velocityHigh != 127)
            return false;

        if (!peaks.has_value() || peaks->empty())
            return false;

        return true;
    }

    bool testIndexedBitwigMultisampleMetadataExtraction()
    {
        const auto uniqueSuffix = std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto testRoot = std::filesystem::temp_directory_path() / ("SampleWranglerBitwigMultisampleTest_" + uniqueSuffix);

        std::error_code ec;
        std::filesystem::create_directories(testRoot / "archive", ec);
        if (ec)
            return false;

        const auto lowSamplePath = testRoot / "archive" / "zip_low.wav";
        const auto highSamplePath = testRoot / "archive" / "zip_high.wav";
        const auto manifestPath = testRoot / "archive" / "multisample.xml";
        if (!writeMinimalWav(lowSamplePath) ||
            !writeMinimalWav(highSamplePath) ||
            !writeTextFile(manifestPath,
                           "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<multisample name=\"Zip Pad\">\n"
                           "  <layer>\n"
                           "    <sample file=\"Samples/zip_low.wav\">\n"
                           "      <key root=\"48\" low=\"48\" high=\"60\" lowVelocity=\"1\" highVelocity=\"90\" />\n"
                           "    </sample>\n"
                           "    <sample file=\"Samples/zip_high.wav\">\n"
                           "      <key root=\"72\" low=\"61\" high=\"72\" lowVelocity=\"91\" highVelocity=\"127\" />\n"
                           "    </sample>\n"
                           "  </layer>\n"
                           "</multisample>\n") ||
            !writeZipArchive(testRoot / "zip_pad.multisample",
                             {{manifestPath, "multisample.xml"},
                              {lowSamplePath, "Samples/zip_low.wav"},
                              {highSamplePath, "Samples/zip_high.wav"}}))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::CatalogDb db;
        sw::WaveCacheBlobDb blobDb;
        if (!db.open(":memory:") || !blobDb.open((testRoot / "wave_cache.db").string()))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        if (!db.addRoot(testRoot.string(), "BitwigRoot"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto roots = db.allRoots();
        if (roots.empty())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::JobQueue queue(1);
        sw::Scanner scanner(db, queue);
        scanner.setWaveCacheBlobDb(&blobDb);

        if (!waitForScanCompletion(scanner, roots.front().id, testRoot.string()))
        {
            queue.shutdown();
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        queue.shutdown();

        const auto file = db.fileByRootAndRelativePath(roots.front().id, "zip_pad.multisample");
        if (!file.has_value())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto peaks = blobDb.peaksByKey(sw::buildWaveformCacheKey(file->rootId,
                                                                       file->relativePath,
                                                                       file->sizeBytes,
                                                                       file->modifiedTime));
        std::filesystem::remove_all(testRoot, ec);

        if (!file->indexOnly)
            return false;

        if (!file->codec.has_value() || file->codec->find("Bitwig Multisample") == std::string::npos)
            return false;

        if (!file->sampleRate.has_value() || *file->sampleRate != 44100)
            return false;

        if (!file->channels.has_value() || *file->channels != 1)
            return false;

        if (!file->key.has_value() || *file->key != "C3")
            return false;

        if (!file->presetName.has_value() || *file->presetName != "Zip Pad")
            return false;

        if (!file->zoneCount.has_value() || *file->zoneCount != 2)
            return false;

        if (!file->keyLow.has_value() || *file->keyLow != 48)
            return false;

        if (!file->keyHigh.has_value() || *file->keyHigh != 72)
            return false;

        if (!file->velocityLow.has_value() || *file->velocityLow != 1)
            return false;

        if (!file->velocityHigh.has_value() || *file->velocityHigh != 127)
            return false;

        if (!peaks.has_value() || peaks->empty())
            return false;

        return true;
    }

    bool testIndexedDecentSamplerMetadataExtraction()
    {
        const auto uniqueSuffix = std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto testRoot = std::filesystem::temp_directory_path() / ("SampleWranglerDecentSamplerTest_" + uniqueSuffix);

        std::error_code ec;
        std::filesystem::create_directories(testRoot / "Samples", ec);
        if (ec)
            return false;

        if (!writeMinimalWav(testRoot / "Samples" / "pad.wav") ||
            !writeTextFile(testRoot / "pad.dspreset",
                           "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<DecentSampler>\n"
                           "  <groups>\n"
                           "    <group>\n"
                           "      <sample path=\"Samples/pad.wav\" rootNote=\"48\" />\n"
                           "    </group>\n"
                           "  </groups>\n"
                           "</DecentSampler>\n"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::CatalogDb db;
        sw::WaveCacheBlobDb blobDb;
        if (!db.open(":memory:") || !blobDb.open((testRoot / "wave_cache.db").string()))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        if (!db.addRoot(testRoot.string(), "DecentSamplerRoot"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto roots = db.allRoots();
        if (roots.empty())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::JobQueue queue(1);
        sw::Scanner scanner(db, queue);
        scanner.setWaveCacheBlobDb(&blobDb);

        if (!waitForScanCompletion(scanner, roots.front().id, testRoot.string()))
        {
            queue.shutdown();
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        queue.shutdown();

        const auto file = db.fileByRootAndRelativePath(roots.front().id, "pad.dspreset");
        if (!file.has_value())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto peaks = blobDb.peaksByKey(sw::buildWaveformCacheKey(file->rootId,
                                                                       file->relativePath,
                                                                       file->sizeBytes,
                                                                       file->modifiedTime));
        std::filesystem::remove_all(testRoot, ec);

        if (!file->indexOnly)
            return false;

        if (!file->codec.has_value() || file->codec->find("DecentSampler") == std::string::npos)
            return false;

        if (!file->sampleRate.has_value() || *file->sampleRate != 44100)
            return false;

        if (!file->channels.has_value() || *file->channels != 1)
            return false;

        if (!file->key.has_value() || *file->key != "C3")
            return false;

        if (!file->zoneCount.has_value() || *file->zoneCount != 1)
            return false;

        if (!peaks.has_value() || peaks->empty())
            return false;

        return true;
    }

    bool testIndexedKorgMultisampleMetadataExtraction()
    {
        const auto uniqueSuffix = std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto testRoot = std::filesystem::temp_directory_path() / ("SampleWranglerKorgMultisampleTest_" + uniqueSuffix);

        std::error_code ec;
        std::filesystem::create_directories(testRoot / "archive", ec);
        if (ec)
            return false;

        const auto lowSamplePath = testRoot / "archive" / "korg_low.wav";
        const auto highSamplePath = testRoot / "archive" / "korg_high.wav";
        const auto manifestPath = testRoot / "archive" / "multisample.xml";
        if (!writeMinimalWav(lowSamplePath) ||
            !writeMinimalWav(highSamplePath) ||
            !writeTextFile(manifestPath,
                           "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<multisample name=\"Korg Stack\">\n"
                           "  <sample file=\"Samples/korg_low.wav\" rootkey=\"48\" lokey=\"36\" hikey=\"60\" lovel=\"1\" hivel=\"96\" />\n"
                           "  <sample file=\"Samples/korg_high.wav\" rootkey=\"72\" lokey=\"61\" hikey=\"96\" lovel=\"97\" hivel=\"127\" />\n"
                           "</multisample>\n") ||
            !writeZipArchive(testRoot / "korg_stack.korgmultisample",
                             {{manifestPath, "multisample.xml"},
                              {lowSamplePath, "Samples/korg_low.wav"},
                              {highSamplePath, "Samples/korg_high.wav"}}))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::CatalogDb db;
        sw::WaveCacheBlobDb blobDb;
        if (!db.open(":memory:") || !blobDb.open((testRoot / "wave_cache.db").string()))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        if (!db.addRoot(testRoot.string(), "KorgRoot"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto roots = db.allRoots();
        if (roots.empty())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::JobQueue queue(1);
        sw::Scanner scanner(db, queue);
        scanner.setWaveCacheBlobDb(&blobDb);

        if (!waitForScanCompletion(scanner, roots.front().id, testRoot.string()))
        {
            queue.shutdown();
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        queue.shutdown();

        const auto file = db.fileByRootAndRelativePath(roots.front().id, "korg_stack.korgmultisample");
        if (!file.has_value())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto peaks = blobDb.peaksByKey(sw::buildWaveformCacheKey(file->rootId,
                                                                       file->relativePath,
                                                                       file->sizeBytes,
                                                                       file->modifiedTime));
        std::filesystem::remove_all(testRoot, ec);

        if (!file->indexOnly)
            return false;

        if (!file->codec.has_value() || file->codec->find("Korg Multisample") == std::string::npos)
            return false;

        if (!file->presetName.has_value() || *file->presetName != "Korg Stack")
            return false;

        if (!file->sampleRate.has_value() || *file->sampleRate != 44100)
            return false;

        if (!file->channels.has_value() || *file->channels != 1)
            return false;

        if (!file->key.has_value() || *file->key != "C3")
            return false;

        if (!file->zoneCount.has_value() || *file->zoneCount != 2)
            return false;

        if (!file->keyLow.has_value() || *file->keyLow != 36)
            return false;

        if (!file->keyHigh.has_value() || *file->keyHigh != 96)
            return false;

        if (!file->velocityLow.has_value() || *file->velocityLow != 1)
            return false;

        if (!file->velocityHigh.has_value() || *file->velocityHigh != 127)
            return false;

        if (!peaks.has_value() || peaks->empty())
            return false;

        return true;
    }

    bool testIndexedSf2MetadataExtraction()
    {
        const auto uniqueSuffix = std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto testRoot = std::filesystem::temp_directory_path() / ("SampleWranglerSf2Test_" + uniqueSuffix);

        std::error_code ec;
        std::filesystem::create_directories(testRoot, ec);
        if (ec)
            return false;

        if (!writeMinimalSf2(testRoot / "warm_pad.sf2"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::CatalogDb db;
        sw::WaveCacheBlobDb blobDb;
        if (!db.open(":memory:") || !blobDb.open((testRoot / "wave_cache.db").string()))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        if (!db.addRoot(testRoot.string(), "Sf2Root"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto roots = db.allRoots();
        if (roots.empty())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::JobQueue queue(1);
        sw::Scanner scanner(db, queue);
        scanner.setWaveCacheBlobDb(&blobDb);

        if (!waitForScanCompletion(scanner, roots.front().id, testRoot.string()))
        {
            queue.shutdown();
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        queue.shutdown();

        const auto file = db.fileByRootAndRelativePath(roots.front().id, "warm_pad.sf2");
        if (!file.has_value())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto peaks = blobDb.peaksByKey(sw::buildWaveformCacheKey(file->rootId,
                                                                       file->relativePath,
                                                                       file->sizeBytes,
                                                                       file->modifiedTime));
        std::filesystem::remove_all(testRoot, ec);

        if (!file->indexOnly)
            return false;

        if (!file->codec.has_value() || file->codec->find("SoundFont 2") == std::string::npos)
            return false;

        if (!file->presetName.has_value() || *file->presetName != "Warm Pad")
            return false;

        if (!file->sampleRate.has_value() || *file->sampleRate != 44100)
            return false;

        if (!file->channels.has_value() || *file->channels != 1)
            return false;

        if (!file->bitDepth.has_value() || *file->bitDepth != 16)
            return false;

        if (!file->key.has_value() || *file->key != "C4")
            return false;

        if (!file->zoneCount.has_value() || *file->zoneCount != 1)
            return false;

        if (!file->keyLow.has_value() || *file->keyLow != 48)
            return false;

        if (!file->keyHigh.has_value() || *file->keyHigh != 72)
            return false;

        if (!file->velocityLow.has_value() || *file->velocityLow != 10)
            return false;

        if (!file->velocityHigh.has_value() || *file->velocityHigh != 120)
            return false;

        if (!file->loopType.has_value() || *file->loopType != "sf2")
            return false;

        if (!peaks.has_value() || peaks->empty())
            return false;

        return true;
    }

    bool testIndexedTalSamplerMetadataExtraction()
    {
        const auto uniqueSuffix = std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto testRoot = std::filesystem::temp_directory_path() / ("SampleWranglerTalSamplerTest_" + uniqueSuffix);

        std::error_code ec;
        std::filesystem::create_directories(testRoot / "Samples", ec);
        if (ec)
            return false;

        if (!writeMinimalWav(testRoot / "Samples" / "tal_pad.wav") ||
            !writeTextFile(testRoot / "pad.talsmpl",
                           "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<tal>\n"
                           "  <program name=\"Warm Pad\">\n"
                           "    <sample url=\"Samples/tal_pad.wav\" rootkey=\"60\" />\n"
                           "  </program>\n"
                           "</tal>\n"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::CatalogDb db;
        sw::WaveCacheBlobDb blobDb;
        if (!db.open(":memory:") || !blobDb.open((testRoot / "wave_cache.db").string()))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        if (!db.addRoot(testRoot.string(), "TalSamplerRoot"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto roots = db.allRoots();
        if (roots.empty())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::JobQueue queue(1);
        sw::Scanner scanner(db, queue);
        scanner.setWaveCacheBlobDb(&blobDb);

        if (!waitForScanCompletion(scanner, roots.front().id, testRoot.string()))
        {
            queue.shutdown();
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        queue.shutdown();

        const auto file = db.fileByRootAndRelativePath(roots.front().id, "pad.talsmpl");
        if (!file.has_value())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto peaks = blobDb.peaksByKey(sw::buildWaveformCacheKey(file->rootId,
                                                                       file->relativePath,
                                                                       file->sizeBytes,
                                                                       file->modifiedTime));
        std::filesystem::remove_all(testRoot, ec);

        if (!file->indexOnly)
            return false;

        if (!file->codec.has_value() || file->codec->find("TAL Sampler") == std::string::npos)
            return false;

        if (!file->sampleRate.has_value() || *file->sampleRate != 44100)
            return false;

        if (!file->channels.has_value() || *file->channels != 1)
            return false;

        if (!file->key.has_value() || *file->key != "C4")
            return false;

        if (!peaks.has_value() || peaks->empty())
            return false;

        return true;
    }

    bool testIndexedTx16WxMetadataExtraction()
    {
        const auto uniqueSuffix = std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto testRoot = std::filesystem::temp_directory_path() / ("SampleWranglerTx16WxTest_" + uniqueSuffix);

        std::error_code ec;
        std::filesystem::create_directories(testRoot / "Samples", ec);
        if (ec)
            return false;

        if (!writeMinimalWav(testRoot / "Samples" / "tx_pad.wav") ||
            !writeTextFile(testRoot / "pad.txprog",
                           "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<program name=\"TX Pad\">\n"
                           "  <group>\n"
                           "    <region sample=\"Samples/tx_pad.wav\" rootkey=\"60\" />\n"
                           "  </group>\n"
                           "</program>\n"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::CatalogDb db;
        sw::WaveCacheBlobDb blobDb;
        if (!db.open(":memory:") || !blobDb.open((testRoot / "wave_cache.db").string()))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        if (!db.addRoot(testRoot.string(), "Tx16WxRoot"))
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto roots = db.allRoots();
        if (roots.empty())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        sw::JobQueue queue(1);
        sw::Scanner scanner(db, queue);
        scanner.setWaveCacheBlobDb(&blobDb);

        if (!waitForScanCompletion(scanner, roots.front().id, testRoot.string()))
        {
            queue.shutdown();
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        queue.shutdown();

        const auto file = db.fileByRootAndRelativePath(roots.front().id, "pad.txprog");
        if (!file.has_value())
        {
            std::filesystem::remove_all(testRoot, ec);
            return false;
        }

        const auto peaks = blobDb.peaksByKey(sw::buildWaveformCacheKey(file->rootId,
                                                                       file->relativePath,
                                                                       file->sizeBytes,
                                                                       file->modifiedTime));
        std::filesystem::remove_all(testRoot, ec);

        if (!file->indexOnly)
            return false;

        if (!file->codec.has_value() || file->codec->find("TX16Wx") == std::string::npos)
            return false;

        if (!file->sampleRate.has_value() || *file->sampleRate != 44100)
            return false;

        if (!file->channels.has_value() || *file->channels != 1)
            return false;

        if (!file->key.has_value() || *file->key != "C4")
            return false;

        if (!peaks.has_value() || peaks->empty())
            return false;

        return true;
    }
}

int main()
{
    if (!testAppleLoopAiffMetadataExtraction())
    {
        std::cerr << "testAppleLoopAiffMetadataExtraction failed.\n";
        return 1;
    }

    if (!testIndexedSfzMetadataExtraction())
    {
        std::cerr << "testIndexedSfzMetadataExtraction failed.\n";
        return 1;
    }

    if (!testIndexedBitwigMultisampleMetadataExtraction())
    {
        std::cerr << "testIndexedBitwigMultisampleMetadataExtraction failed.\n";
        return 1;
    }

    if (!testIndexedKorgMultisampleMetadataExtraction())
    {
        std::cerr << "testIndexedKorgMultisampleMetadataExtraction failed.\n";
        return 1;
    }

    if (!testIndexedDecentSamplerMetadataExtraction())
    {
        std::cerr << "testIndexedDecentSamplerMetadataExtraction failed.\n";
        return 1;
    }

    if (!testIndexedTalSamplerMetadataExtraction())
    {
        std::cerr << "testIndexedTalSamplerMetadataExtraction failed.\n";
        return 1;
    }

    if (!testIndexedTx16WxMetadataExtraction())
    {
        std::cerr << "testIndexedTx16WxMetadataExtraction failed.\n";
        return 1;
    }

    if (!testIndexedSf2MetadataExtraction())
    {
        std::cerr << "testIndexedSf2MetadataExtraction failed.\n";
        return 1;
    }

    return 0;
}
