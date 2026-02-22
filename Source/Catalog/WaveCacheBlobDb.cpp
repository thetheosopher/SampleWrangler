#include "WaveCacheBlobDb.h"

#include <sqlite3.h>
#include <cstring>

namespace sw
{

    namespace
    {
        bool execSql(sqlite3 *db, const char *sql)
        {
            return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
        }
    }

    WaveCacheBlobDb::~WaveCacheBlobDb()
    {
        close();
    }

    bool WaveCacheBlobDb::open(const std::string &dbPath)
    {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);

        close();

        if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK)
        {
            close();
            return false;
        }

        if (!execSql(db, "PRAGMA journal_mode=WAL;"))
        {
            close();
            return false;
        }

        if (!execSql(db, "PRAGMA synchronous=NORMAL;"))
        {
            close();
            return false;
        }

        if (!execSql(db, R"SQL(
            CREATE TABLE IF NOT EXISTS wave_cache_blob (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                file_id INTEGER NOT NULL,
                cache_key TEXT NOT NULL UNIQUE,
                peaks_blob BLOB NOT NULL,
                updated_time INTEGER NOT NULL DEFAULT (strftime('%s','now'))
            );
        )SQL"))
        {
            close();
            return false;
        }

        if (!execSql(db, "CREATE INDEX IF NOT EXISTS idx_wave_cache_blob_file_id ON wave_cache_blob(file_id);"))
        {
            close();
            return false;
        }

        return true;
    }

    void WaveCacheBlobDb::close()
    {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);

        if (db != nullptr)
        {
            sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
            sqlite3_close(db);
            db = nullptr;
        }
    }

    bool WaveCacheBlobDb::isOpen() const noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);
        return db != nullptr;
    }

    bool WaveCacheBlobDb::upsertPeaksByKey(const std::string &cacheKey,
                                           int64_t fileId,
                                           const std::vector<float> &peaks)
    {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);

        if (db == nullptr)
            return false;

        const char *sql = R"SQL(
            INSERT INTO wave_cache_blob (file_id, cache_key, peaks_blob, updated_time)
            VALUES (?, ?, ?, strftime('%s','now'))
            ON CONFLICT(cache_key) DO UPDATE SET
                file_id = excluded.file_id,
                peaks_blob = excluded.peaks_blob,
                updated_time = excluded.updated_time;
        )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, fileId);
        sqlite3_bind_text(stmt, 2, cacheKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt,
                          3,
                          peaks.data(),
                          static_cast<int>(peaks.size() * sizeof(float)),
                          SQLITE_TRANSIENT);

        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    std::optional<std::vector<float>> WaveCacheBlobDb::peaksByKey(const std::string &cacheKey)
    {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);

        if (db == nullptr)
            return std::nullopt;

        const char *sql = "SELECT peaks_blob FROM wave_cache_blob WHERE cache_key = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_text(stmt, 1, cacheKey.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        const void *blobData = sqlite3_column_blob(stmt, 0);
        const int blobBytes = sqlite3_column_bytes(stmt, 0);
        if (blobData == nullptr || blobBytes <= 0 || (blobBytes % static_cast<int>(sizeof(float))) != 0)
        {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        const int floatCount = blobBytes / static_cast<int>(sizeof(float));
        std::vector<float> peaks(static_cast<size_t>(floatCount));
        std::memcpy(peaks.data(), blobData, static_cast<size_t>(blobBytes));

        sqlite3_finalize(stmt);
        return peaks;
    }

    bool WaveCacheBlobDb::removeByFileIds(const std::vector<int64_t> &fileIds)
    {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);

        if (db == nullptr)
            return false;

        if (fileIds.empty())
            return true;

        if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK)
            return false;

        const char *sql = "DELETE FROM wave_cache_blob WHERE file_id = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        bool ok = true;
        for (const auto fileId : fileIds)
        {
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            sqlite3_bind_int64(stmt, 1, fileId);
            if (sqlite3_step(stmt) != SQLITE_DONE)
            {
                ok = false;
                break;
            }
        }

        sqlite3_finalize(stmt);

        if (ok)
            ok = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
        else
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);

        return ok;
    }

    bool WaveCacheBlobDb::vacuum()
    {
        std::lock_guard<std::recursive_mutex> lock(apiMutex);

        if (db == nullptr)
            return false;

        return sqlite3_exec(db, "VACUUM;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }

} // namespace sw
