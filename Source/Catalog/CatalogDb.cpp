#include "CatalogDb.h"
#include "CatalogSchema.h"
#include <sqlite3.h>
#include <cassert>

namespace sw
{

    CatalogDb::CatalogDb() = default;

    CatalogDb::~CatalogDb()
    {
        close();
    }

    bool CatalogDb::open(const std::string &dbPath)
    {
        if (db)
            close();

        int rc = sqlite3_open(dbPath.c_str(), &db);
        if (rc != SQLITE_OK)
        {
            db = nullptr;
            return false;
        }

        // Enable WAL mode for better concurrent read performance
        sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

        if (!CatalogSchema::createAll(db))
        {
            close();
            return false;
        }

        return true;
    }

    void CatalogDb::close()
    {
        if (db)
        {
            sqlite3_close(db);
            db = nullptr;
        }
    }

    // ---------------------------------------------------------------------------
    // Roots
    // ---------------------------------------------------------------------------

    bool CatalogDb::addRoot(const std::string &path, const std::string &label)
    {
        const char *sql = "INSERT OR IGNORE INTO roots (path, label) VALUES (?, ?)";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, label.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool CatalogDb::removeRoot(int64_t rootId)
    {
        const char *sql = "DELETE FROM roots WHERE id = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, rootId);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    std::vector<RootRecord> CatalogDb::allRoots()
    {
        std::vector<RootRecord> results;
        const char *sql = "SELECT id, path, label, enabled FROM roots ORDER BY label";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return results;

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            RootRecord r;
            r.id = sqlite3_column_int64(stmt, 0);
            r.path = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            r.label = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            r.enabled = sqlite3_column_int(stmt, 3) != 0;
            results.push_back(std::move(r));
        }
        sqlite3_finalize(stmt);
        return results;
    }

    // ---------------------------------------------------------------------------
    // Files
    // ---------------------------------------------------------------------------

    bool CatalogDb::upsertFile(const FileRecord &rec)
    {
        const char *sql = R"SQL(
        INSERT INTO files (root_id, relative_path, filename, extension,
                           size_bytes, modified_time, duration_sec, sample_rate,
                           channels, bit_depth, bpm, key, loop_type, index_only)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(root_id, relative_path) DO UPDATE SET
            filename      = excluded.filename,
            extension     = excluded.extension,
            size_bytes    = excluded.size_bytes,
            modified_time = excluded.modified_time,
            duration_sec  = excluded.duration_sec,
            sample_rate   = excluded.sample_rate,
            channels      = excluded.channels,
            bit_depth     = excluded.bit_depth,
            bpm           = excluded.bpm,
            key           = excluded.key,
            loop_type     = excluded.loop_type,
            index_only    = excluded.index_only
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, rec.rootId);
        sqlite3_bind_text(stmt, 2, rec.relativePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, rec.filename.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, rec.extension.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 5, rec.sizeBytes);
        sqlite3_bind_int64(stmt, 6, rec.modifiedTime);

        if (rec.durationSec)
            sqlite3_bind_double(stmt, 7, *rec.durationSec);
        else
            sqlite3_bind_null(stmt, 7);

        if (rec.sampleRate)
            sqlite3_bind_int(stmt, 8, *rec.sampleRate);
        else
            sqlite3_bind_null(stmt, 8);

        if (rec.channels)
            sqlite3_bind_int(stmt, 9, *rec.channels);
        else
            sqlite3_bind_null(stmt, 9);

        if (rec.bitDepth)
            sqlite3_bind_int(stmt, 10, *rec.bitDepth);
        else
            sqlite3_bind_null(stmt, 10);

        if (rec.bpm)
            sqlite3_bind_double(stmt, 11, *rec.bpm);
        else
            sqlite3_bind_null(stmt, 11);

        if (rec.key)
            sqlite3_bind_text(stmt, 12, rec.key->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 12);

        if (rec.loopType)
            sqlite3_bind_text(stmt, 13, rec.loopType->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 13);

        sqlite3_bind_int(stmt, 14, rec.indexOnly ? 1 : 0);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool CatalogDb::removeFilesByRoot(int64_t rootId)
    {
        const char *sql = "DELETE FROM files WHERE root_id = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, rootId);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    std::vector<FileRecord> CatalogDb::searchFiles(const std::string &query, int limit)
    {
        std::vector<FileRecord> results;

        // Use FTS5 match for searching
        const char *sql = R"SQL(
        SELECT f.id, f.root_id, f.relative_path, f.filename, f.extension,
               f.size_bytes, f.modified_time, f.duration_sec, f.sample_rate,
               f.channels, f.bit_depth, f.bpm, f.key, f.loop_type, f.index_only
        FROM files f
        JOIN files_fts fts ON fts.rowid = f.id
        WHERE files_fts MATCH ?
        LIMIT ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return results;

        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            FileRecord r;
            r.id = sqlite3_column_int64(stmt, 0);
            r.rootId = sqlite3_column_int64(stmt, 1);
            r.relativePath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            r.filename = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
            r.extension = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
            r.sizeBytes = sqlite3_column_int64(stmt, 5);
            r.modifiedTime = sqlite3_column_int64(stmt, 6);

            if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
                r.durationSec = sqlite3_column_double(stmt, 7);
            if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
                r.sampleRate = sqlite3_column_int(stmt, 8);
            if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
                r.channels = sqlite3_column_int(stmt, 9);
            if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
                r.bitDepth = sqlite3_column_int(stmt, 10);
            if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
                r.bpm = sqlite3_column_double(stmt, 11);
            if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
                r.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 12));
            if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
                r.loopType = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
            r.indexOnly = sqlite3_column_int(stmt, 14) != 0;

            results.push_back(std::move(r));
        }
        sqlite3_finalize(stmt);
        return results;
    }

    std::vector<FileRecord> CatalogDb::listRecentFiles(int limit)
    {
        std::vector<FileRecord> results;

        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
               size_bytes, modified_time, duration_sec, sample_rate,
               channels, bit_depth, bpm, key, loop_type, index_only
        FROM files
        ORDER BY id DESC
        LIMIT ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return results;

        sqlite3_bind_int(stmt, 1, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            FileRecord r;
            r.id = sqlite3_column_int64(stmt, 0);
            r.rootId = sqlite3_column_int64(stmt, 1);
            r.relativePath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            r.filename = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
            r.extension = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
            r.sizeBytes = sqlite3_column_int64(stmt, 5);
            r.modifiedTime = sqlite3_column_int64(stmt, 6);

            if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
                r.durationSec = sqlite3_column_double(stmt, 7);
            if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
                r.sampleRate = sqlite3_column_int(stmt, 8);
            if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
                r.channels = sqlite3_column_int(stmt, 9);
            if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
                r.bitDepth = sqlite3_column_int(stmt, 10);
            if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
                r.bpm = sqlite3_column_double(stmt, 11);
            if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
                r.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 12));
            if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
                r.loopType = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
            r.indexOnly = sqlite3_column_int(stmt, 14) != 0;

            results.push_back(std::move(r));
        }
        sqlite3_finalize(stmt);
        return results;
    }

    std::optional<FileRecord> CatalogDb::fileById(int64_t fileId)
    {
        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
               size_bytes, modified_time, duration_sec, sample_rate,
               channels, bit_depth, bpm, key, loop_type, index_only
        FROM files WHERE id = ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_int64(stmt, 1, fileId);

        if (sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        FileRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.rootId = sqlite3_column_int64(stmt, 1);
        r.relativePath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        r.filename = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        r.extension = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        r.sizeBytes = sqlite3_column_int64(stmt, 5);
        r.modifiedTime = sqlite3_column_int64(stmt, 6);

        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
            r.durationSec = sqlite3_column_double(stmt, 7);
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            r.sampleRate = sqlite3_column_int(stmt, 8);
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
            r.channels = sqlite3_column_int(stmt, 9);
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
            r.bitDepth = sqlite3_column_int(stmt, 10);
        if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
            r.bpm = sqlite3_column_double(stmt, 11);
        if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
            r.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 12));
        if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
            r.loopType = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
        r.indexOnly = sqlite3_column_int(stmt, 14) != 0;

        sqlite3_finalize(stmt);
        return r;
    }

    std::optional<FileRecord> CatalogDb::fileByRootAndRelativePath(int64_t rootId, const std::string &relativePath)
    {
        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
               size_bytes, modified_time, duration_sec, sample_rate,
               channels, bit_depth, bpm, key, loop_type, index_only
        FROM files
        WHERE root_id = ? AND relative_path = ?
        LIMIT 1
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_int64(stmt, 1, rootId);
        sqlite3_bind_text(stmt, 2, relativePath.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        FileRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.rootId = sqlite3_column_int64(stmt, 1);
        r.relativePath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        r.filename = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        r.extension = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        r.sizeBytes = sqlite3_column_int64(stmt, 5);
        r.modifiedTime = sqlite3_column_int64(stmt, 6);

        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
            r.durationSec = sqlite3_column_double(stmt, 7);
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            r.sampleRate = sqlite3_column_int(stmt, 8);
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
            r.channels = sqlite3_column_int(stmt, 9);
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
            r.bitDepth = sqlite3_column_int(stmt, 10);
        if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
            r.bpm = sqlite3_column_double(stmt, 11);
        if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
            r.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 12));
        if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
            r.loopType = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
        r.indexOnly = sqlite3_column_int(stmt, 14) != 0;

        sqlite3_finalize(stmt);
        return r;
    }

    bool CatalogDb::setAppSetting(const std::string &key, const std::string &value)
    {
        if (db == nullptr)
            return false;

        const char *sql = "INSERT INTO app_settings(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);

        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    std::optional<std::string> CatalogDb::getAppSetting(const std::string &key)
    {
        if (db == nullptr)
            return std::nullopt;

        const char *sql = "SELECT value FROM app_settings WHERE key = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        const auto *valueText = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        std::optional<std::string> value;
        if (valueText != nullptr)
            value = std::string(valueText);

        sqlite3_finalize(stmt);
        return value;
    }

    // ---------------------------------------------------------------------------
    // Wave cache
    // ---------------------------------------------------------------------------

    bool CatalogDb::insertCacheEntry(const WaveCacheEntry &entry)
    {
        const char *sql = "INSERT OR REPLACE INTO wave_cache (file_id, cache_key, cache_path) VALUES (?, ?, ?)";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, entry.fileId);
        sqlite3_bind_text(stmt, 2, entry.cacheKey.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, entry.cachePath.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    std::optional<WaveCacheEntry> CatalogDb::cacheEntryByKey(const std::string &key)
    {
        const char *sql = "SELECT id, file_id, cache_key, cache_path FROM wave_cache WHERE cache_key = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        WaveCacheEntry e;
        e.id = sqlite3_column_int64(stmt, 0);
        e.fileId = sqlite3_column_int64(stmt, 1);
        e.cacheKey = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        e.cachePath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));

        sqlite3_finalize(stmt);
        return e;
    }

} // namespace sw
