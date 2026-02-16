#include "Catalog/CatalogDb.h"
#include "Pipeline/JobQueue.h"
#include "Pipeline/Scanner.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
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

    void appendChunk(std::vector<uint8_t> &out, const char id[4], const std::vector<uint8_t> &chunkData)
    {
        out.insert(out.end(), id, id + 4);
        appendU32BE(out, static_cast<uint32_t>(chunkData.size()));
        out.insert(out.end(), chunkData.begin(), chunkData.end());
        if ((chunkData.size() & 1u) != 0u)
            out.push_back(0);
    }

    bool writeMinimalAppleLoopAiff(const std::filesystem::path &filePath)
    {
        std::vector<uint8_t> comm;
        appendU16BE(comm, 1);  // numChannels
        appendU32BE(comm, 2);  // numSampleFrames
        appendU16BE(comm, 16); // sampleSize
        // 80-bit extended float: 44100.0
        const uint8_t sampleRate80[10] = {0x40, 0x0E, 0xAC, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        comm.insert(comm.end(), std::begin(sampleRate80), std::end(sampleRate80));

        std::vector<uint8_t> ssnd;
        appendU32BE(ssnd, 0); // offset
        appendU32BE(ssnd, 0); // block size
        appendU16BE(ssnd, 0); // sample 0
        appendU16BE(ssnd, 0); // sample 1

        std::vector<uint8_t> inst;
        inst.push_back(60);   // baseNote C4
        inst.push_back(0);    // detune
        inst.push_back(0);    // lowNote
        inst.push_back(127);  // highNote
        inst.push_back(1);    // lowVelocity
        inst.push_back(127);  // highVelocity
        appendU16BE(inst, 0); // gain

        appendU16BE(inst, 1); // sustainLoop playMode (forward)
        appendU16BE(inst, 1); // sustainLoop begin marker id
        appendU16BE(inst, 2); // sustainLoop end marker id

        appendU16BE(inst, 0); // releaseLoop playMode
        appendU16BE(inst, 0); // releaseLoop begin marker id
        appendU16BE(inst, 0); // releaseLoop end marker id

        std::vector<uint8_t> mark;
        appendU16BE(mark, 2); // marker count

        appendU16BE(mark, 1); // marker 1 id
        appendU32BE(mark, 0); // marker 1 position
        mark.push_back(5);    // pstring length
        mark.insert(mark.end(), {'s', 't', 'a', 'r', 't'});

        appendU16BE(mark, 2); // marker 2 id
        appendU32BE(mark, 1); // marker 2 position
        mark.push_back(3);    // pstring length
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
}

int main()
{
    if (!testAppleLoopAiffMetadataExtraction())
    {
        std::cerr << "testAppleLoopAiffMetadataExtraction failed.\n";
        return 1;
    }

    return 0;
}
