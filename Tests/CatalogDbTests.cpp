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
        file.contentHash = "hash-a";
        file.presetName = "Warm Pad";
        file.zoneCount = 3;
        file.keyLow = 24;
        file.keyHigh = 96;
        file.velocityLow = 1;
        file.velocityHigh = 127;

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

        return byId->sampleRate.has_value() && *byId->sampleRate == 44100 &&
               byId->contentHash.has_value() && *byId->contentHash == "hash-a" &&
               byId->presetName.has_value() && *byId->presetName == "Warm Pad" &&
               byId->zoneCount.has_value() && *byId->zoneCount == 3 &&
               byId->keyLow.has_value() && *byId->keyLow == 24 &&
               byId->keyHigh.has_value() && *byId->keyHigh == 96 &&
               byId->velocityLow.has_value() && *byId->velocityLow == 1 &&
               byId->velocityHigh.has_value() && *byId->velocityHigh == 127;
    }

    bool testDuplicateFileLookup(sw::CatalogDb &db)
    {
        const auto roots = db.allRoots();
        if (roots.empty())
            return false;

        sw::FileRecord duplicateA;
        duplicateA.rootId = roots.front().id;
        duplicateA.relativePath = "Drums/Snare01.wav";
        duplicateA.filename = "Snare01.wav";
        duplicateA.extension = "wav";
        duplicateA.sizeBytes = 2048;
        duplicateA.modifiedTime = 1700000100;
        duplicateA.contentHash = "duplicate-hash";

        sw::FileRecord duplicateB = duplicateA;
        duplicateB.relativePath = "Drums/Snare01-copy.wav";
        duplicateB.filename = "Snare01-copy.wav";
        duplicateB.modifiedTime = 1700000200;

        sw::FileRecord unique = duplicateA;
        unique.relativePath = "Drums/Unique.wav";
        unique.filename = "Unique.wav";
        unique.modifiedTime = 1700000300;
        unique.contentHash = "unique-hash";

        if (!db.upsertFile(duplicateA) || !db.upsertFile(duplicateB) || !db.upsertFile(unique))
            return false;

        const auto duplicates = db.listDuplicateFiles();
        if (duplicates.size() != 2)
            return false;

        return duplicates[0].contentHash.has_value() && *duplicates[0].contentHash == "duplicate-hash" &&
               duplicates[1].contentHash.has_value() && *duplicates[1].contentHash == "duplicate-hash";
    }

    bool testFileUserFavorites(sw::CatalogDb &db)
    {
        const auto roots = db.allRoots();
        if (roots.empty())
            return false;

        const auto file = db.fileByRootAndRelativePath(roots.front().id, "Drums/Kick01.wav");
        if (!file.has_value())
            return false;

        sw::FileUserDataRecord userData;
        userData.fileId = file->id;
        userData.isFavorite = true;

        if (!db.setFileUserData(userData))
            return false;

        const auto fetchedUserData = db.fileUserDataByFileId(file->id);
        if (!fetchedUserData.has_value() || !fetchedUserData->isFavorite)
            return false;

        const auto favoriteFiles = db.listFavoriteFiles();
        if (favoriteFiles.size() != 1 || favoriteFiles.front().id != file->id)
            return false;

        userData.isFavorite = false;
        if (!db.setFileUserData(userData))
            return false;

        const auto clearedUserData = db.fileUserDataByFileId(file->id);
        if (!clearedUserData.has_value() || clearedUserData->isFavorite)
            return false;

        return db.listFavoriteFiles().empty();
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

    if (!testDuplicateFileLookup(db))
    {
        std::cerr << "testDuplicateFileLookup failed.\n";
        return 1;
    }

    if (!testFileUserFavorites(db))
    {
        std::cerr << "testFileUserFavorites failed.\n";
        return 1;
    }

    if (!testWaveCache(db))
    {
        std::cerr << "testWaveCache failed.\n";
        return 1;
    }

    return 0;
}
