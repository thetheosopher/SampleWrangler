#pragma once

#include "CatalogModels.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <utility>

struct sqlite3; // forward declare

namespace sw
{

    /// Owns the SQLite connection and provides typed query helpers.
    /// All public methods are meant to be called from a single DB-service thread
    /// (or serialized externally). Do NOT call from the audio thread.
    class CatalogDb
    {
    public:
        CatalogDb();
        ~CatalogDb();

        CatalogDb(const CatalogDb &) = delete;
        CatalogDb &operator=(const CatalogDb &) = delete;

        /// Open (or create) the database at the given path.
        bool open(const std::string &dbPath);

        /// Close the database handle.
        void close();

        bool isOpen() const noexcept;

        // ----- Sources -----
        bool addRoot(const std::string &path, const std::string &label);
        bool updateRootLabel(int64_t rootId, const std::string &newLabel);
        bool updateRootPath(int64_t rootId, const std::string &newPath);
        bool removeRoot(int64_t rootId);
        std::vector<RootRecord> allRoots();

        // ----- Files -----
        bool upsertFile(const FileRecord &rec);
        bool removeFilesByRoot(int64_t rootId);
        std::vector<int64_t> listFileIdsByRoot(int64_t rootId);
        /// Ids whose persisted analysis is missing or stale for the requested version.
        std::vector<int64_t> listFileIdsNeedingAnalysisByRoot(int64_t rootId,
                                                              int analysisVersion,
                                                              int featureVectorVersion);
        bool upsertAnalysisResult(const AnalysisResultRecord &result);
        std::optional<AnalysisResultRecord> analysisResultByFileId(int64_t fileId);
        /// `query` is raw user text; every word is prefix-matched (see SearchText.h).
        std::vector<FileRecord> searchFiles(const std::string &query, int limit = 200);
        std::vector<FileRecord> listRecentFiles(int limit = 200);
        std::vector<FileRecord> searchFilesByRoot(int64_t rootId, const std::string &query, int limit = 200);
        std::vector<FileRecord> listRecentFilesByRoot(int64_t rootId, int limit = 200);
        std::vector<FileRecord> listDuplicateFiles(int limit = 200);
        std::vector<FileRecord> findNearDuplicateCandidates(int64_t referenceFileId,
                                                            std::optional<int64_t> rootId = std::nullopt,
                                                            int limit = 50);
        std::pair<int64_t, int64_t> fileStatsAll();
        std::pair<int64_t, int64_t> fileStatsByRoot(int64_t rootId);
        std::pair<int64_t, int64_t> fileStatsSearch(const std::string &query);
        std::pair<int64_t, int64_t> fileStatsSearchByRoot(int64_t rootId, const std::string &query);
        std::optional<FileRecord> fileById(int64_t fileId);
        std::optional<FileRecord> fileByRootAndRelativePath(int64_t rootId, const std::string &relativePath);
        std::vector<FileRecord> listFavoriteFiles(int limit = 200);
        bool setFileUserData(const FileUserDataRecord &userData);
        std::optional<FileUserDataRecord> fileUserDataByFileId(int64_t fileId);

        // ----- App settings -----
        bool setAppSetting(const std::string &key, const std::string &value);
        std::optional<std::string> getAppSetting(const std::string &key);

        // ----- Wave cache -----
        bool insertCacheEntry(const WaveCacheEntry &entry);
        std::optional<WaveCacheEntry> cacheEntryByKey(const std::string &key);

        // ----- Transactions -----
        bool beginTransaction();
        bool commitTransaction();
        bool rollbackTransaction();

        // ----- Maintenance -----
        bool vacuum();

    private:
        sqlite3 *db = nullptr;
        mutable std::recursive_mutex apiMutex;
    };

} // namespace sw
