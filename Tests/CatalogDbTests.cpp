#include "Catalog/CatalogDb.h"

#include <iostream>

namespace
{
    bool testRoots(sw::CatalogDb &db)
    {
        if (!db.addRoot("C:/Samples", "Samples"))
            return false;

        const auto roots = db.allRoots();
        if (roots.size() != 1)
            return false;

        return roots.front().path == "C:/Samples" && roots.front().label == "Samples";
    }

    bool testFiles(sw::CatalogDb &db)
    {
        const auto roots = db.allRoots();
        if (roots.empty())
            return false;

        sw::FileRecord file;
        file.rootId = roots.front().id;
        file.relativePath = "Drums/Kick01.wav";
        file.filename = "Kick01.wav";
        file.extension = "wav";
        file.sizeBytes = 123456;
        file.modifiedTime = 1700000000;
        file.durationSec = 0.42;
        file.sampleRate = 44100;
        file.channels = 2;
        file.bitDepth = 16;

        if (!db.upsertFile(file))
            return false;

        const auto byRel = db.fileByRootAndRelativePath(file.rootId, file.relativePath);
        if (!byRel.has_value())
            return false;

        if (byRel->filename != file.filename)
            return false;

        const auto byId = db.fileById(byRel->id);
        if (!byId.has_value())
            return false;

        return byId->sampleRate.has_value() && *byId->sampleRate == 44100;
    }

    bool testSettings(sw::CatalogDb &db)
    {
        if (!db.setAppSetting("preview.pitchSemitones", "7.0"))
            return false;

        const auto value = db.getAppSetting("preview.pitchSemitones");
        return value.has_value() && *value == "7.0";
    }

    bool testWaveCache(sw::CatalogDb &db)
    {
        const auto roots = db.allRoots();
        if (roots.empty())
            return false;

        const auto file = db.fileByRootAndRelativePath(roots.front().id, "Drums/Kick01.wav");
        if (!file.has_value())
            return false;

        sw::WaveCacheEntry entry;
        entry.fileId = file->id;
        entry.cacheKey = "test-cache-key";
        entry.cachePath = "C:/cache/test-cache-key.peak";

        if (!db.insertCacheEntry(entry))
            return false;

        const auto fetched = db.cacheEntryByKey(entry.cacheKey);
        if (!fetched.has_value())
            return false;

        return fetched->cachePath == entry.cachePath && fetched->fileId == entry.fileId;
    }
}

int main()
{
    sw::CatalogDb db;
    if (!db.open(":memory:"))
    {
        std::cerr << "Failed to open in-memory database.\n";
        return 1;
    }

    if (!testRoots(db))
    {
        std::cerr << "testRoots failed.\n";
        return 1;
    }

    if (!testFiles(db))
    {
        std::cerr << "testFiles failed.\n";
        return 1;
    }

    if (!testSettings(db))
    {
        std::cerr << "testSettings failed.\n";
        return 1;
    }

    if (!testWaveCache(db))
    {
        std::cerr << "testWaveCache failed.\n";
        return 1;
    }

    return 0;
}
