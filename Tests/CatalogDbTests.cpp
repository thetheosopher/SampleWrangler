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
               byId->contentHash.has_value() && *byId->contentHash == "hash-a";
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

    bool testFileUserMetadataAndSavedSearches(sw::CatalogDb &db)
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
        userData.rating = 5;

        if (!db.setFileUserData(userData))
            return false;

        const auto fetchedUserData = db.fileUserDataByFileId(file->id);
        if (!fetchedUserData.has_value() || !fetchedUserData->isFavorite || !fetchedUserData->rating.has_value() || *fetchedUserData->rating != 5)
            return false;

        if (!db.replaceFileTags(file->id, {" Drum ", "One Shot", "drum"}))
            return false;

        const auto fileTags = db.tagsForFile(file->id);
        if (fileTags.size() != 2 || fileTags[0] != "drum" || fileTags[1] != "one shot")
            return false;

        const auto allTags = db.allTags();
        if (allTags.size() != 2)
            return false;

        const auto favoriteFiles = db.listFavoriteFiles();
        if (favoriteFiles.size() != 1 || favoriteFiles.front().id != file->id)
            return false;

        sw::SavedSearchRecord savedSearch;
        savedSearch.name = "Favorite Kicks";
        savedSearch.queryText = "Kick";
        savedSearch.rootId = roots.front().id;
        savedSearch.favoritesOnly = true;

        if (!db.upsertSavedSearch(savedSearch))
            return false;

        auto savedSearches = db.listSavedSearches();
        if (savedSearches.size() != 1)
            return false;

        if (savedSearches.front().name != "Favorite Kicks" || !savedSearches.front().favoritesOnly)
            return false;

        if (!db.removeSavedSearch(savedSearches.front().id))
            return false;

        savedSearches = db.listSavedSearches();
        return savedSearches.empty();
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

    if (!testFileUserMetadataAndSavedSearches(db))
    {
        std::cerr << "testFileUserMetadataAndSavedSearches failed.\n";
        return 1;
    }

    if (!testWaveCache(db))
    {
        std::cerr << "testWaveCache failed.\n";
        return 1;
    }

    return 0;
}
