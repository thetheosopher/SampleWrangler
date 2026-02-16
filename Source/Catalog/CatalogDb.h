#pragma once

#include "CatalogModels.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

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

        bool isOpen() const noexcept { return db != nullptr; }

        // ----- Sources -----
        bool addRoot(const std::string &path, const std::string &label);
        bool removeRoot(int64_t rootId);
        std::vector<RootRecord> allRoots();

        // ----- Files -----
        bool upsertFile(const FileRecord &rec);
        bool removeFilesByRoot(int64_t rootId);
        std::vector<FileRecord> searchFiles(const std::string &query, int limit = 200);
        std::vector<FileRecord> listRecentFiles(int limit = 200);
        std::vector<FileRecord> searchFilesByRoot(int64_t rootId, const std::string &query, int limit = 200);
        std::vector<FileRecord> listRecentFilesByRoot(int64_t rootId, int limit = 200);
        std::optional<FileRecord> fileById(int64_t fileId);
        std::optional<FileRecord> fileByRootAndRelativePath(int64_t rootId, const std::string &relativePath);

        // ----- App settings -----
        bool setAppSetting(const std::string &key, const std::string &value);
        std::optional<std::string> getAppSetting(const std::string &key);

        // ----- Wave cache -----
        bool insertCacheEntry(const WaveCacheEntry &entry);
        std::optional<WaveCacheEntry> cacheEntryByKey(const std::string &key);

        // ----- Maintenance -----
        bool vacuum();

    private:
        sqlite3 *db = nullptr;
    };

} // namespace sw
