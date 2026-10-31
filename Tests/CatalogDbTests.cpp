#include "Catalog/CatalogDb.h"
#include "Catalog/CatalogSchema.h"
#include "Catalog/SearchText.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <sqlite3.h>
#include <utility>
#include <vector>

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
        file.rootMidiNote = 60;
        file.rootTuneCents = 2.5;
        file.rootPitchSource = "test.pitch";
        file.metadataSource = "test.container";
        file.loopRegions.push_back({0,
                                    sw::LoopRegionRole::Sustain,
                                    sw::LoopMode::Alternating,
                                    1u,
                                    10,
                                    20,
                                    true,
                                    0u,
                                    3u,
                                    7u,
                                    std::nullopt,
                                    std::nullopt,
                                    std::nullopt,
                                    std::nullopt,
                                    "test.loop"});
        file.presetPrograms.push_back({0, 1, 2, "Catalogable Program", 2, 24, 96, 1, 127, 60, -3.0});

        if (!db.upsertFile(file))
        {
            std::cerr << "upsertFile failed in testFiles.\n";
            return false;
        }

        const auto byRel = db.fileByRootAndRelativePath(file.rootId, file.relativePath);
        if (!byRel.has_value())
        {
            std::cerr << "fileByRootAndRelativePath failed in testFiles.\n";
            return false;
        }

        if (byRel->filename != file.filename)
            return false;

        const auto byId = db.fileById(byRel->id);
        if (!byId.has_value())
        {
            std::cerr << "fileById failed in testFiles.\n";
            return false;
        }

        const auto programSearch = db.searchFiles("Catalogable");
        const auto rootProgramSearch = db.searchFilesByRoot(file.rootId, "Catalogable");
        const auto programSearchStats = db.fileStatsSearch("Catalogable");
        const auto rootProgramSearchStats = db.fileStatsSearchByRoot(file.rootId, "Catalogable");
        const bool metadataRoundTrips =
            byId->sampleRate.has_value() && *byId->sampleRate == 44100 &&
            byId->contentHash.has_value() && *byId->contentHash == "hash-a" &&
            byId->presetName.has_value() && *byId->presetName == "Warm Pad" &&
            byId->zoneCount.has_value() && *byId->zoneCount == 3 &&
            byId->keyLow.has_value() && *byId->keyLow == 24 &&
            byId->keyHigh.has_value() && *byId->keyHigh == 96 &&
            byId->velocityLow.has_value() && *byId->velocityLow == 1 &&
            byId->velocityHigh.has_value() && *byId->velocityHigh == 127 &&
            byId->rootMidiNote.has_value() && *byId->rootMidiNote == 60 &&
            byId->rootTuneCents.has_value() && *byId->rootTuneCents == 2.5 &&
            byId->rootPitchSource == std::optional<std::string>{"test.pitch"} &&
            byId->metadataSource == std::optional<std::string>{"test.container"} &&
            byId->loopRegions.size() == 1 &&
            byId->loopRegions.front().mode == sw::LoopMode::Alternating &&
            byId->loopRegions.front().fractionalPosition == std::optional<uint32_t>{0} &&
            byId->loopRegions.front().source == "test.loop" &&
            byId->presetPrograms.size() == 1 &&
            byId->presetPrograms.front().bank == 1 &&
            byId->presetPrograms.front().program == 2 &&
            byId->presetPrograms.front().rootMidiNote == std::optional<int>{60} &&
            byId->presetPrograms.front().rootTuneCents == std::optional<double>{-3.0} &&
            programSearch.size() == 1 && programSearch.front().id == byId->id &&
            rootProgramSearch.size() == 1 && rootProgramSearch.front().id == byId->id &&
            programSearchStats.first == 1 && rootProgramSearchStats.first == 1;
        if (!metadataRoundTrips)
            return false;

        file.loopRegions.clear();
        file.presetPrograms.front().name = "Replacement Program";
        if (!db.upsertFile(file))
            return false;

        const auto staleProgramSearch = db.searchFiles("Catalogable");
        const auto replacementSearch = db.searchFiles("Replacement");
        const auto rescannedFile = db.fileById(byId->id);
        return staleProgramSearch.empty() &&
               replacementSearch.size() == 1 && replacementSearch.front().id == byId->id &&
               rescannedFile.has_value() && rescannedFile->loopRegions.empty() &&
               rescannedFile->presetPrograms.size() == 1 &&
               rescannedFile->presetPrograms.front().name == "Replacement Program";
    }

    bool testSearchTextHelpers()
    {
        const auto tokens = sw::tokenizeSearchText("  Kick-808 (Live) \"C#\" ");
        const std::vector<std::string> expectedTokens{"kick", "808", "live", "c"};
        if (tokens != expectedTokens)
        {
            std::cerr << "tokenizeSearchText produced unexpected tokens.\n";
            return false;
        }

        if (sw::buildFtsPrefixQuery(tokens) != "\"kick\"* \"808\"* \"live\"* \"c\"*" ||
            !sw::buildFtsPrefixQuery(sw::tokenizeSearchText("#-()\"")).empty())
        {
            std::cerr << "buildFtsPrefixQuery produced an unexpected expression.\n";
            return false;
        }

        const std::vector<std::string_view> fields{"Kick_808_Live.wav", "Drums/Kick_808_Live.wav"};
        if (!sw::searchTokensMatchFields(sw::tokenizeSearchText("kick 80"), fields) ||
            !sw::searchTokensMatchFields({}, fields) ||
            sw::searchTokensMatchFields(sw::tokenizeSearchText("ick"), fields) ||
            sw::searchTokensMatchFields(sw::tokenizeSearchText("kick snare"), fields))
        {
            std::cerr << "searchTokensMatchFields produced unexpected matches.\n";
            return false;
        }

        if (sw::keyPitchClass("C#4") != std::optional<int>{1} || sw::keyPitchClass("Eb") != std::optional<int>{3} ||
            sw::keyPitchClass("A minor") != std::optional<int>{9} || sw::keyPitchClass("B major") != std::optional<int>{11} ||
            sw::keyPitchClass("Cb") != std::optional<int>{11} || sw::keyPitchClass("H").has_value() ||
            sw::keyPitchClass("").has_value())
        {
            std::cerr << "keyPitchClass parsed an unexpected pitch class.\n";
            return false;
        }

        return true;
    }

    bool testPunctuationSearch()
    {
        sw::CatalogDb db;
        if (!db.open(":memory:") || !db.addRoot("C:/PunctuationSamples", "Punctuation"))
            return false;

        const auto rootId = db.allRoots().front().id;
        const auto addFile = [&db, rootId](const std::string &relativePath, const std::string &filename)
        {
            sw::FileRecord file;
            file.rootId = rootId;
            file.relativePath = relativePath;
            file.filename = filename;
            file.extension = "wav";
            file.sizeBytes = 1000;
            file.modifiedTime = 1700000000;
            return db.upsertFile(file);
        };

        if (!addFile("Drums/Kick-808 (Live).wav", "Kick-808 (Live).wav") ||
            !addFile("Keys/C# Stab.wav", "C# Stab.wav") ||
            !addFile("Drums/Snare.wav", "Snare.wav"))
            return false;

        // Each of these used to reach FTS5 MATCH verbatim and fail with a syntax error.
        for (const auto *query : {"kick-808", "Kick-808 (Live)", "\"kick", "kick*", "808 liv", "(live"})
        {
            const auto results = db.searchFiles(query, -1);
            if (results.size() != 1 || results.front().filename != "Kick-808 (Live).wav")
            {
                std::cerr << "searchFiles(\"" << query << "\") returned " << results.size() << " results.\n";
                return false;
            }
        }

        const auto sharpResults = db.searchFilesByRoot(rootId, "C# stab", -1);
        if (sharpResults.size() != 1 || sharpResults.front().filename != "C# Stab.wav")
        {
            std::cerr << "searchFilesByRoot(\"C# stab\") failed.\n";
            return false;
        }

        if (!db.searchFiles("#()", -1).empty() || db.fileStatsSearch("snare\"").first != 1 ||
            db.fileStatsSearchByRoot(rootId, "kick-").first != 1)
        {
            std::cerr << "Punctuation-only or stats search returned unexpected counts.\n";
            return false;
        }

        return true;
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
               duplicates[1].contentHash.has_value() && *duplicates[1].contentHash == "duplicate-hash" &&
               duplicates[0].duplicateClusterSize == 2 &&
               duplicates[1].duplicateClusterSize == 2;
    }

    bool testNearDuplicateCandidates(sw::CatalogDb &db)
    {
        const auto roots = db.allRoots();
        if (roots.empty() || !db.addRoot("C:/Samples/Other", "Other"))
            return false;

        const auto updatedRoots = db.allRoots();
        const auto otherRoot = std::find_if(updatedRoots.begin(), updatedRoots.end(),
                                            [](const sw::RootRecord &root)
                                            { return root.label == "Other"; });
        if (otherRoot == updatedRoots.end())
            return false;

        auto addAnalyzedFile = [&db](sw::FileRecord file,
                                     std::vector<float> features,
                                     std::optional<int64_t> analyzedModifiedTime = std::nullopt,
                                     std::optional<std::string> analyzedContentHash = std::nullopt)
            -> std::optional<sw::FileRecord>
        {
            if (!db.upsertFile(file))
                return std::nullopt;

            const auto stored = db.fileByRootAndRelativePath(file.rootId, file.relativePath);
            if (!stored)
                return std::nullopt;

            sw::AnalysisResultRecord analysis;
            analysis.fileId = stored->id;
            analysis.analysisVersion = sw::kCurrentAnalysisVersion;
            analysis.analyzedModifiedTime = analyzedModifiedTime.value_or(stored->modifiedTime);
            analysis.analyzedContentHash = analyzedContentHash.value_or(stored->contentHash.value_or(""));
            analysis.status = sw::AnalysisStatus::Complete;
            analysis.featureVectorVersion = sw::kCurrentAnalysisFeatureVectorVersion;
            analysis.featureVector = std::move(features);
            if (!db.upsertAnalysisResult(analysis))
                return std::nullopt;

            return stored;
        };

        const auto makeFile = [](int64_t rootId,
                                 const std::string &relativePath,
                                 const std::string &hash,
                                 double duration,
                                 int sampleRate,
                                 int channels)
        {
            sw::FileRecord file;
            file.rootId = rootId;
            file.relativePath = relativePath;
            file.filename = relativePath.substr(relativePath.find_last_of('/') + 1);
            file.extension = "wav";
            file.sizeBytes = 4096;
            file.modifiedTime = 1700001000;
            file.durationSec = duration;
            file.sampleRate = sampleRate;
            file.channels = channels;
            file.contentHash = hash;
            return file;
        };

        auto reference = addAnalyzedFile(makeFile(roots.front().id, "Similarity/reference.wav", "near-reference",
                                                  1.0, 44100, 2),
                                         {1.0f, 0.20f, 0.40f, 0.30f});
        auto closeCandidate = addAnalyzedFile(makeFile(roots.front().id, "Similarity/near.wav", "near-candidate",
                                                       1.02, 44100, 2),
                                              {1.02f, 0.21f, 0.41f, 0.31f});
        auto distantCandidate = addAnalyzedFile(makeFile(roots.front().id, "Similarity/far.wav", "far-candidate",
                                                         1.0, 44100, 2),
                                                {1.0f, 0.70f, 0.80f, 0.90f});
        auto otherRootCandidate = addAnalyzedFile(makeFile(otherRoot->id, "Similarity/other-root.wav", "other-root",
                                                           1.0, 44100, 2),
                                                  {1.0f, 0.25f, 0.45f, 0.35f});
        auto exactHashCandidate = addAnalyzedFile(makeFile(roots.front().id, "Similarity/exact.wav", "near-reference",
                                                           1.0, 44100, 2),
                                                  {1.0f, 0.20f, 0.40f, 0.30f});
        auto wrongDuration = addAnalyzedFile(makeFile(roots.front().id, "Similarity/long.wav", "too-long",
                                                      1.30, 44100, 2),
                                             {1.30f, 0.20f, 0.40f, 0.30f});
        auto wrongSampleRate = addAnalyzedFile(makeFile(roots.front().id, "Similarity/rate.wav", "wrong-rate",
                                                        1.0, 96000, 2),
                                               {1.0f, 0.20f, 0.40f, 0.30f});
        auto wrongChannels = addAnalyzedFile(makeFile(roots.front().id, "Similarity/mono.wav", "wrong-channels",
                                                      1.0, 44100, 1),
                                             {1.0f, 0.20f, 0.40f, 0.30f});
        auto loopMismatchFile = makeFile(roots.front().id, "Similarity/loop.wav", "loop-mismatch",
                                         1.0, 44100, 2);
        loopMismatchFile.loopRegions.push_back({0, sw::LoopRegionRole::Sustain, sw::LoopMode::Forward,
                                                std::nullopt, 0, 100, false});
        auto loopMismatch = addAnalyzedFile(std::move(loopMismatchFile), {1.0f, 0.20f, 0.40f, 0.30f});
        auto staleMtimeFile = makeFile(roots.front().id, "Similarity/stale-mtime.wav", "stale-mtime",
                                       1.0, 44100, 2);
        auto staleMtime = addAnalyzedFile(std::move(staleMtimeFile), {1.0f, 0.20f, 0.40f, 0.30f},
                                          1700000999);
        auto staleHashFile = makeFile(roots.front().id, "Similarity/stale-hash.wav", "stale-current",
                                      1.0, 44100, 2);
        auto staleHash = addAnalyzedFile(std::move(staleHashFile), {1.0f, 0.20f, 0.40f, 0.30f},
                                         std::nullopt, "stale-old");
        auto indexOnlyFile = makeFile(roots.front().id, "Similarity/index-only.wav", "index-only",
                                      1.0, 44100, 2);
        indexOnlyFile.indexOnly = true;
        auto indexOnly = addAnalyzedFile(std::move(indexOnlyFile), {1.0f, 0.20f, 0.40f, 0.30f});

        if (!reference || !closeCandidate || !distantCandidate || !otherRootCandidate ||
            !exactHashCandidate || !wrongDuration || !wrongSampleRate || !wrongChannels ||
            !loopMismatch || !staleMtime || !staleHash || !indexOnly)
            return false;

        const auto scopedCandidates = db.findNearDuplicateCandidates(reference->id, roots.front().id, 10);
        if (scopedCandidates.size() != 2 ||
            scopedCandidates[0].id != closeCandidate->id ||
            scopedCandidates[1].id != distantCandidate->id ||
            !scopedCandidates[0].featureMatchScore.has_value() ||
            !scopedCandidates[1].featureMatchScore.has_value() ||
            *scopedCandidates[0].featureMatchScore <= *scopedCandidates[1].featureMatchScore)
            return false;

        const auto limitedCandidates = db.findNearDuplicateCandidates(reference->id, roots.front().id, 1);
        if (limitedCandidates.size() != 1 || limitedCandidates.front().id != closeCandidate->id)
            return false;

        const auto otherRootCandidates = db.findNearDuplicateCandidates(reference->id, otherRoot->id, 10);
        return otherRootCandidates.size() == 1 && otherRootCandidates.front().id == otherRootCandidate->id;
    }

    bool testAnalysisPersistence(sw::CatalogDb &db)
    {
        const auto roots = db.allRoots();
        if (roots.empty())
            return false;

        sw::FileRecord file;
        file.rootId = roots.front().id;
        file.relativePath = "Analysis/feature-vector.wav";
        file.filename = "feature-vector.wav";
        file.extension = "wav";
        file.sizeBytes = 32000;
        file.modifiedTime = 1700000400;
        file.contentHash = "analysis-hash-v1";
        file.bpm = 120.0;
        file.key = "Am";
        if (!db.upsertFile(file))
            return false;

        const auto storedFile = db.fileByRootAndRelativePath(file.rootId, file.relativePath);
        if (!storedFile)
            return false;

        const auto containsFileId = [fileId = storedFile->id](const std::vector<int64_t> &ids)
        {
            return std::find(ids.begin(), ids.end(), fileId) != ids.end();
        };
        if (!containsFileId(db.listFileIdsNeedingAnalysisByRoot(
                file.rootId, sw::kCurrentAnalysisVersion, sw::kCurrentAnalysisFeatureVectorVersion)))
            return false;

        sw::AnalysisResultRecord result;
        result.fileId = storedFile->id;
        result.analysisVersion = sw::kCurrentAnalysisVersion;
        result.analyzedModifiedTime = storedFile->modifiedTime;
        result.analyzedContentHash = storedFile->contentHash;
        result.status = sw::AnalysisStatus::Complete;
        result.detectedBpm = 120.5;
        result.bpmConfidence = 0.8;
        result.detectedKey = "A minor";
        result.keyConfidence = 0.7;
        result.transientCount = 4;
        result.onsetDensity = 2.0;
        result.suggestedLoopStartSample = 16;
        result.suggestedLoopEndSample = 8000;
        result.loopConfidence = 0.6;
        result.featureVector = {1.0f, 0.25f, 0.5f, 0.75f};
        result.analyzedAt = 1700000500;
        if (!db.upsertAnalysisResult(result))
            return false;

        const auto fetched = db.analysisResultByFileId(storedFile->id);
        const bool resultRoundTrips = fetched &&
                                      fetched->analysisVersion == result.analysisVersion &&
                                      fetched->analyzedModifiedTime == result.analyzedModifiedTime &&
                                      fetched->analyzedContentHash == result.analyzedContentHash &&
                                      fetched->status == result.status &&
                                      fetched->detectedBpm == result.detectedBpm &&
                                      fetched->bpmConfidence == result.bpmConfidence &&
                                      fetched->detectedKey == result.detectedKey &&
                                      fetched->keyConfidence == result.keyConfidence &&
                                      fetched->transientCount == result.transientCount &&
                                      fetched->onsetDensity == result.onsetDensity &&
                                      fetched->suggestedLoopStartSample == result.suggestedLoopStartSample &&
                                      fetched->suggestedLoopEndSample == result.suggestedLoopEndSample &&
                                      fetched->loopConfidence == result.loopConfidence &&
                                      fetched->featureVectorVersion == result.featureVectorVersion &&
                                      fetched->featureVector == result.featureVector &&
                                      fetched->analyzedAt == result.analyzedAt;
        if (!resultRoundTrips ||
            containsFileId(db.listFileIdsNeedingAnalysisByRoot(
                file.rootId, sw::kCurrentAnalysisVersion, sw::kCurrentAnalysisFeatureVectorVersion)))
            return false;

        if (!containsFileId(db.listFileIdsNeedingAnalysisByRoot(
                file.rootId, sw::kCurrentAnalysisVersion + 1, sw::kCurrentAnalysisFeatureVectorVersion)) ||
            !containsFileId(db.listFileIdsNeedingAnalysisByRoot(
                file.rootId, sw::kCurrentAnalysisVersion, sw::kCurrentAnalysisFeatureVectorVersion + 1)))
            return false;

        file.modifiedTime += 1;
        if (!db.upsertFile(file))
            return false;
        if (!containsFileId(db.listFileIdsNeedingAnalysisByRoot(
                file.rootId, sw::kCurrentAnalysisVersion, sw::kCurrentAnalysisFeatureVectorVersion)))
            return false;

        result.analyzedModifiedTime = file.modifiedTime;
        if (!db.upsertAnalysisResult(result))
            return false;
        file.contentHash = "analysis-hash-v2";
        if (!db.upsertFile(file) ||
            !containsFileId(db.listFileIdsNeedingAnalysisByRoot(
                file.rootId, sw::kCurrentAnalysisVersion, sw::kCurrentAnalysisFeatureVectorVersion)))
            return false;

        result.analyzedContentHash = file.contentHash;
        result.status = sw::AnalysisStatus::Unsupported;
        result.featureVector.clear();
        result.failureReason = "No decoder";
        if (!db.upsertAnalysisResult(result))
            return false;

        const auto unsupported = db.analysisResultByFileId(storedFile->id);
        return unsupported &&
               unsupported->status == sw::AnalysisStatus::Unsupported &&
               unsupported->failureReason == std::optional<std::string>{"No decoder"} &&
               !containsFileId(db.listFileIdsNeedingAnalysisByRoot(
                   file.rootId, sw::kCurrentAnalysisVersion, sw::kCurrentAnalysisFeatureVectorVersion));
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

    bool testFormatMetadataMigration()
    {
        sqlite3 *db = nullptr;
        if (sqlite3_open(":memory:", &db) != SQLITE_OK)
            return false;

        const char *legacySchema = R"SQL(
            CREATE TABLE files (
                id INTEGER PRIMARY KEY,
                extension TEXT,
                loop_type TEXT,
                acid_root_note INTEGER,
                loop_start_sample INTEGER,
                loop_end_sample INTEGER,
                index_only INTEGER NOT NULL DEFAULT 0,
                channels INTEGER,
                sample_rate INTEGER,
                duration_sec REAL
            );
            INSERT INTO files (id, extension, loop_type, acid_root_note, loop_start_sample, loop_end_sample)
                VALUES (1, 'wav', 'acidized', 60, 10, 19);
            PRAGMA user_version = 6;
        )SQL";
        const bool created = sqlite3_exec(db, legacySchema, nullptr, nullptr, nullptr) == SQLITE_OK;
        if (!created || !sw::CatalogSchema::createAll(db))
        {
            sqlite3_close(db);
            return false;
        }

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db,
                               "SELECT root_midi_note, root_tune_cents, root_pitch_source, metadata_source "
                               "FROM files WHERE id = 1;",
                               -1,
                               &stmt,
                               nullptr) != SQLITE_OK)
        {
            sqlite3_close(db);
            return false;
        }

        const bool hasPitchRow = sqlite3_step(stmt) == SQLITE_ROW;
        const auto *rootPitchSource = hasPitchRow ? reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)) : nullptr;
        const auto *metadataSource = hasPitchRow ? reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)) : nullptr;
        const bool pitchMigrated = hasPitchRow &&
                                   sqlite3_column_int(stmt, 0) == 60 &&
                                   sqlite3_column_type(stmt, 1) == SQLITE_NULL &&
                                   rootPitchSource != nullptr && std::string(rootPitchSource) == "wave.acid" &&
                                   metadataSource != nullptr && std::string(metadataSource) == "acidized";
        sqlite3_finalize(stmt);
        if (!pitchMigrated)
        {
            sqlite3_close(db);
            return false;
        }

        if (sqlite3_prepare_v2(db,
                               "SELECT start_sample, end_sample, end_inclusive, source "
                               "FROM file_loop_regions WHERE file_id = 1 AND loop_index = 0;",
                               -1,
                               &stmt,
                               nullptr) != SQLITE_OK)
        {
            sqlite3_close(db);
            return false;
        }

        const bool hasLoopRow = sqlite3_step(stmt) == SQLITE_ROW;
        const auto *loopSource = hasLoopRow ? reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)) : nullptr;
        const bool loopMigrated = hasLoopRow &&
                                  sqlite3_column_int64(stmt, 0) == 10 &&
                                  sqlite3_column_int64(stmt, 1) == 19 &&
                                  sqlite3_column_int(stmt, 2) == 1 &&
                                  loopSource != nullptr && std::string(loopSource) == "acidized";
        sqlite3_finalize(stmt);

        bool analysisTableCreated = false;
        if (sqlite3_prepare_v2(db,
                               "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'analysis_results';",
                               -1,
                               &stmt,
                               nullptr) == SQLITE_OK)
        {
            analysisTableCreated = sqlite3_step(stmt) == SQLITE_ROW;
            sqlite3_finalize(stmt);
        }

        sqlite3_close(db);
        return loopMigrated && analysisTableCreated;
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

    if (!testSearchTextHelpers())
    {
        std::cerr << "testSearchTextHelpers failed.\n";
        return 1;
    }

    if (!testPunctuationSearch())
    {
        std::cerr << "testPunctuationSearch failed.\n";
        return 1;
    }

    if (!testAnalysisPersistence(db))
    {
        std::cerr << "testAnalysisPersistence failed.\n";
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

    if (!testNearDuplicateCandidates(db))
    {
        std::cerr << "testNearDuplicateCandidates failed.\n";
        return 1;
    }

    if (!testFormatMetadataMigration())
    {
        std::cerr << "testFormatMetadataMigration failed.\n";
        return 1;
    }

    return 0;
}
