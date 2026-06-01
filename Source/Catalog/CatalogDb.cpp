#include "CatalogDb.h"
#include "CatalogSchema.h"
#include <sqlite3.h>
#include <algorithm>
#include <cassert>

namespace sw
{

    namespace
    {
        std::string readTextColumn(sqlite3_stmt *stmt, int index)
        {
            if (const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, index)); text != nullptr)
                return text;

            return {};
        }

        std::optional<std::string> readOptionalTextColumn(sqlite3_stmt *stmt, int index)
        {
            if (sqlite3_column_type(stmt, index) == SQLITE_NULL)
                return std::nullopt;

            return readTextColumn(stmt, index);
        }

        FileRecord readFileRecord(sqlite3_stmt *stmt)
        {
            FileRecord r;
            r.id = sqlite3_column_int64(stmt, 0);
            r.rootId = sqlite3_column_int64(stmt, 1);
            r.relativePath = readTextColumn(stmt, 2);
            r.filename = readTextColumn(stmt, 3);
            r.extension = readTextColumn(stmt, 4);
            r.sizeBytes = sqlite3_column_int64(stmt, 5);
            r.modifiedTime = sqlite3_column_int64(stmt, 6);

            if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
                r.durationSec = sqlite3_column_double(stmt, 7);
            if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
                r.totalSamples = sqlite3_column_int64(stmt, 8);
            if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
                r.sampleRate = sqlite3_column_int(stmt, 9);
            if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
                r.channels = sqlite3_column_int(stmt, 10);
            if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
                r.bitDepth = sqlite3_column_int(stmt, 11);
            if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
                r.bitrateKbps = sqlite3_column_int(stmt, 12);
            r.codec = readOptionalTextColumn(stmt, 13);
            if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
                r.bpm = sqlite3_column_double(stmt, 14);
            r.key = readOptionalTextColumn(stmt, 15);
            r.loopType = readOptionalTextColumn(stmt, 16);
            if (sqlite3_column_type(stmt, 17) != SQLITE_NULL)
                r.acidRootNote = sqlite3_column_int(stmt, 17);
            if (sqlite3_column_type(stmt, 18) != SQLITE_NULL)
                r.acidBeats = sqlite3_column_int(stmt, 18);
            if (sqlite3_column_type(stmt, 19) != SQLITE_NULL)
                r.loopStartSample = sqlite3_column_int64(stmt, 19);
            if (sqlite3_column_type(stmt, 20) != SQLITE_NULL)
                r.loopEndSample = sqlite3_column_int64(stmt, 20);
            r.indexOnly = sqlite3_column_int(stmt, 21) != 0;
            if (sqlite3_column_type(stmt, 22) != SQLITE_NULL)
                r.sliceCount = sqlite3_column_int(stmt, 22);
            r.contentHash = readOptionalTextColumn(stmt, 23);
            r.presetName = readOptionalTextColumn(stmt, 24);
            if (sqlite3_column_type(stmt, 25) != SQLITE_NULL)
                r.zoneCount = sqlite3_column_int(stmt, 25);
            if (sqlite3_column_type(stmt, 26) != SQLITE_NULL)
                r.keyLow = sqlite3_column_int(stmt, 26);
            if (sqlite3_column_type(stmt, 27) != SQLITE_NULL)
                r.keyHigh = sqlite3_column_int(stmt, 27);
            if (sqlite3_column_type(stmt, 28) != SQLITE_NULL)
                r.velocityLow = sqlite3_column_int(stmt, 28);
            if (sqlite3_column_type(stmt, 29) != SQLITE_NULL)
                r.velocityHigh = sqlite3_column_int(stmt, 29);
            return r;
        }

    }

#define SW_DB_GUARD std::lock_guard<std::recursive_mutex> dbLock(apiMutex)

    CatalogDb::CatalogDb() = default;

    CatalogDb::~CatalogDb()
    {
        close();
    }

    bool CatalogDb::open(const std::string &dbPath)
    {
        SW_DB_GUARD;

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
        SW_DB_GUARD;

        if (db)
        {
            sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
            sqlite3_close(db);
            db = nullptr;
        }
    }

    bool CatalogDb::isOpen() const noexcept
    {
        SW_DB_GUARD;
        return db != nullptr;
    }

    // ---------------------------------------------------------------------------
    // Sources
    // ---------------------------------------------------------------------------

    bool CatalogDb::addRoot(const std::string &path, const std::string &label)
    {
        SW_DB_GUARD;

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

    bool CatalogDb::updateRootPath(int64_t rootId, const std::string &newPath)
    {
        SW_DB_GUARD;

        const char *sql = "UPDATE roots SET path = ? WHERE id = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_text(stmt, 1, newPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, rootId);

        const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) > 0;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool CatalogDb::updateRootLabel(int64_t rootId, const std::string &newLabel)
    {
        SW_DB_GUARD;

        const char *sql = "UPDATE roots SET label = ? WHERE id = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_text(stmt, 1, newLabel.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, rootId);

        const bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) > 0;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool CatalogDb::removeRoot(int64_t rootId)
    {
        SW_DB_GUARD;

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
        SW_DB_GUARD;

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
        SW_DB_GUARD;

        const char *sql = R"SQL(
        INSERT INTO files (root_id, relative_path, filename, extension,
                           size_bytes, modified_time, duration_sec, total_samples,
                           sample_rate, channels, bit_depth, bitrate_kbps, codec,
                           bpm, key, loop_type, acid_root_note, acid_beats,
                           loop_start_sample, loop_end_sample, index_only, slice_count, content_hash,
                           preset_name, zone_count, key_low, key_high, velocity_low, velocity_high)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(root_id, relative_path) DO UPDATE SET
            filename      = excluded.filename,
            extension     = excluded.extension,
            size_bytes    = excluded.size_bytes,
            modified_time = excluded.modified_time,
            duration_sec  = excluded.duration_sec,
            total_samples = excluded.total_samples,
            sample_rate   = excluded.sample_rate,
            channels      = excluded.channels,
            bit_depth     = excluded.bit_depth,
            bitrate_kbps  = excluded.bitrate_kbps,
            codec         = excluded.codec,
            bpm           = excluded.bpm,
            key           = excluded.key,
            loop_type     = excluded.loop_type,
                acid_root_note = excluded.acid_root_note,
                acid_beats     = excluded.acid_beats,
                loop_start_sample = excluded.loop_start_sample,
                loop_end_sample   = excluded.loop_end_sample,
            index_only    = excluded.index_only,
            slice_count   = excluded.slice_count,
            content_hash  = excluded.content_hash,
            preset_name   = excluded.preset_name,
            zone_count    = excluded.zone_count,
            key_low       = excluded.key_low,
            key_high      = excluded.key_high,
            velocity_low  = excluded.velocity_low,
            velocity_high = excluded.velocity_high
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

        if (rec.totalSamples)
            sqlite3_bind_int64(stmt, 8, *rec.totalSamples);
        else
            sqlite3_bind_null(stmt, 8);

        if (rec.sampleRate)
            sqlite3_bind_int(stmt, 9, *rec.sampleRate);
        else
            sqlite3_bind_null(stmt, 9);

        if (rec.channels)
            sqlite3_bind_int(stmt, 10, *rec.channels);
        else
            sqlite3_bind_null(stmt, 10);

        if (rec.bitDepth)
            sqlite3_bind_int(stmt, 11, *rec.bitDepth);
        else
            sqlite3_bind_null(stmt, 11);

        if (rec.bitrateKbps)
            sqlite3_bind_int(stmt, 12, *rec.bitrateKbps);
        else
            sqlite3_bind_null(stmt, 12);

        if (rec.codec)
            sqlite3_bind_text(stmt, 13, rec.codec->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 13);

        if (rec.bpm)
            sqlite3_bind_double(stmt, 14, *rec.bpm);
        else
            sqlite3_bind_null(stmt, 14);

        if (rec.key)
            sqlite3_bind_text(stmt, 15, rec.key->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 15);

        if (rec.loopType)
            sqlite3_bind_text(stmt, 16, rec.loopType->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 16);

        if (rec.acidRootNote)
            sqlite3_bind_int(stmt, 17, *rec.acidRootNote);
        else
            sqlite3_bind_null(stmt, 17);

        if (rec.acidBeats)
            sqlite3_bind_int(stmt, 18, *rec.acidBeats);
        else
            sqlite3_bind_null(stmt, 18);

        if (rec.loopStartSample)
            sqlite3_bind_int64(stmt, 19, *rec.loopStartSample);
        else
            sqlite3_bind_null(stmt, 19);

        if (rec.loopEndSample)
            sqlite3_bind_int64(stmt, 20, *rec.loopEndSample);
        else
            sqlite3_bind_null(stmt, 20);

        sqlite3_bind_int(stmt, 21, rec.indexOnly ? 1 : 0);

        if (rec.sliceCount)
            sqlite3_bind_int(stmt, 22, *rec.sliceCount);
        else
            sqlite3_bind_null(stmt, 22);

        if (rec.contentHash)
            sqlite3_bind_text(stmt, 23, rec.contentHash->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 23);

        if (rec.presetName)
            sqlite3_bind_text(stmt, 24, rec.presetName->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 24);

        if (rec.zoneCount)
            sqlite3_bind_int(stmt, 25, *rec.zoneCount);
        else
            sqlite3_bind_null(stmt, 25);

        if (rec.keyLow)
            sqlite3_bind_int(stmt, 26, *rec.keyLow);
        else
            sqlite3_bind_null(stmt, 26);

        if (rec.keyHigh)
            sqlite3_bind_int(stmt, 27, *rec.keyHigh);
        else
            sqlite3_bind_null(stmt, 27);

        if (rec.velocityLow)
            sqlite3_bind_int(stmt, 28, *rec.velocityLow);
        else
            sqlite3_bind_null(stmt, 28);

        if (rec.velocityHigh)
            sqlite3_bind_int(stmt, 29, *rec.velocityHigh);
        else
            sqlite3_bind_null(stmt, 29);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool CatalogDb::removeFilesByRoot(int64_t rootId)
    {
        SW_DB_GUARD;

        const char *sql = "DELETE FROM files WHERE root_id = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, rootId);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    std::vector<int64_t> CatalogDb::listFileIdsByRoot(int64_t rootId)
    {
        SW_DB_GUARD;

        std::vector<int64_t> ids;

        const char *sql = "SELECT id FROM files WHERE root_id = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return ids;

        sqlite3_bind_int64(stmt, 1, rootId);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            ids.push_back(sqlite3_column_int64(stmt, 0));

        sqlite3_finalize(stmt);
        return ids;
    }

    std::vector<int64_t> CatalogDb::listFileIdsNeedingAnalysisByRoot(int64_t rootId)
    {
        SW_DB_GUARD;

        std::vector<int64_t> ids;

        const char *sql =
            "SELECT id FROM files "
            "WHERE root_id = ? AND index_only = 0 AND duration_sec IS NULL";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return ids;

        sqlite3_bind_int64(stmt, 1, rootId);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            ids.push_back(sqlite3_column_int64(stmt, 0));

        sqlite3_finalize(stmt);
        return ids;
    }

    std::vector<FileRecord> CatalogDb::searchFiles(const std::string &query, int limit)
    {
        SW_DB_GUARD;

        std::vector<FileRecord> results;

        // Use FTS5 match for searching
        const char *sql = R"SQL(
        SELECT f.id, f.root_id, f.relative_path, f.filename, f.extension,
             f.size_bytes, f.modified_time, f.duration_sec, f.total_samples,
             f.sample_rate, f.channels, f.bit_depth, f.bitrate_kbps, f.codec,
               f.bpm, f.key, f.loop_type, f.acid_root_note, f.acid_beats,
               f.loop_start_sample, f.loop_end_sample, f.index_only, f.slice_count, f.content_hash,
               f.preset_name, f.zone_count, f.key_low, f.key_high, f.velocity_low, f.velocity_high
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
            results.push_back(readFileRecord(stmt));
        sqlite3_finalize(stmt);
        return results;
    }

    std::vector<FileRecord> CatalogDb::listRecentFiles(int limit)
    {
        SW_DB_GUARD;

        std::vector<FileRecord> results;

        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
             size_bytes, modified_time, duration_sec, total_samples,
             sample_rate, channels, bit_depth, bitrate_kbps, codec,
               bpm, key, loop_type, acid_root_note, acid_beats,
               loop_start_sample, loop_end_sample, index_only, slice_count, content_hash,
               preset_name, zone_count, key_low, key_high, velocity_low, velocity_high
        FROM files
        ORDER BY id DESC
        LIMIT ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return results;

        sqlite3_bind_int(stmt, 1, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            results.push_back(readFileRecord(stmt));
        sqlite3_finalize(stmt);
        return results;
    }

    std::vector<FileRecord> CatalogDb::searchFilesByRoot(int64_t rootId, const std::string &query, int limit)
    {
        SW_DB_GUARD;

        std::vector<FileRecord> results;

        const char *sql = R"SQL(
        SELECT f.id, f.root_id, f.relative_path, f.filename, f.extension,
             f.size_bytes, f.modified_time, f.duration_sec, f.total_samples,
             f.sample_rate, f.channels, f.bit_depth, f.bitrate_kbps, f.codec,
               f.bpm, f.key, f.loop_type, f.acid_root_note, f.acid_beats,
               f.loop_start_sample, f.loop_end_sample, f.index_only, f.slice_count, f.content_hash,
               f.preset_name, f.zone_count, f.key_low, f.key_high, f.velocity_low, f.velocity_high
        FROM files f
        JOIN files_fts fts ON fts.rowid = f.id
        WHERE f.root_id = ? AND files_fts MATCH ?
        LIMIT ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return results;

        sqlite3_bind_int64(stmt, 1, rootId);
        sqlite3_bind_text(stmt, 2, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            results.push_back(readFileRecord(stmt));

        sqlite3_finalize(stmt);
        return results;
    }

    std::vector<FileRecord> CatalogDb::listRecentFilesByRoot(int64_t rootId, int limit)
    {
        SW_DB_GUARD;

        std::vector<FileRecord> results;

        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
             size_bytes, modified_time, duration_sec, total_samples,
             sample_rate, channels, bit_depth, bitrate_kbps, codec,
               bpm, key, loop_type, acid_root_note, acid_beats,
               loop_start_sample, loop_end_sample, index_only, slice_count, content_hash,
               preset_name, zone_count, key_low, key_high, velocity_low, velocity_high
        FROM files
        WHERE root_id = ?
        ORDER BY id DESC
        LIMIT ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return results;

        sqlite3_bind_int64(stmt, 1, rootId);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            results.push_back(readFileRecord(stmt));

        sqlite3_finalize(stmt);
        return results;
    }

    std::vector<FileRecord> CatalogDb::listDuplicateFiles(int limit)
    {
        SW_DB_GUARD;

        std::vector<FileRecord> results;

        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
             size_bytes, modified_time, duration_sec, total_samples,
             sample_rate, channels, bit_depth, bitrate_kbps, codec,
               bpm, key, loop_type, acid_root_note, acid_beats,
             loop_start_sample, loop_end_sample, index_only, slice_count, content_hash,
             preset_name, zone_count, key_low, key_high, velocity_low, velocity_high
        FROM files
        WHERE content_hash IN (
            SELECT content_hash
            FROM files
            WHERE content_hash IS NOT NULL AND content_hash <> ''
            GROUP BY content_hash
            HAVING COUNT(*) > 1
        )
        ORDER BY content_hash, filename, id
        LIMIT ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return results;

        sqlite3_bind_int(stmt, 1, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            results.push_back(readFileRecord(stmt));

        sqlite3_finalize(stmt);
        return results;
    }

    std::pair<int64_t, int64_t> CatalogDb::fileStatsAll()
    {
        SW_DB_GUARD;

        const char *sql = "SELECT COUNT(*), COALESCE(SUM(size_bytes), 0) FROM files";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return {0, 0};

        std::pair<int64_t, int64_t> stats{0, 0};
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            stats.first = sqlite3_column_int64(stmt, 0);
            stats.second = sqlite3_column_int64(stmt, 1);
        }

        sqlite3_finalize(stmt);
        return stats;
    }

    std::pair<int64_t, int64_t> CatalogDb::fileStatsByRoot(int64_t rootId)
    {
        SW_DB_GUARD;

        const char *sql = "SELECT COUNT(*), COALESCE(SUM(size_bytes), 0) FROM files WHERE root_id = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return {0, 0};

        sqlite3_bind_int64(stmt, 1, rootId);

        std::pair<int64_t, int64_t> stats{0, 0};
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            stats.first = sqlite3_column_int64(stmt, 0);
            stats.second = sqlite3_column_int64(stmt, 1);
        }

        sqlite3_finalize(stmt);
        return stats;
    }

    std::pair<int64_t, int64_t> CatalogDb::fileStatsSearch(const std::string &query)
    {
        SW_DB_GUARD;

        const char *sql = R"SQL(
        SELECT COUNT(*), COALESCE(SUM(f.size_bytes), 0)
        FROM files f
        JOIN files_fts fts ON fts.rowid = f.id
        WHERE files_fts MATCH ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return {0, 0};

        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);

        std::pair<int64_t, int64_t> stats{0, 0};
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            stats.first = sqlite3_column_int64(stmt, 0);
            stats.second = sqlite3_column_int64(stmt, 1);
        }

        sqlite3_finalize(stmt);
        return stats;
    }

    std::pair<int64_t, int64_t> CatalogDb::fileStatsSearchByRoot(int64_t rootId, const std::string &query)
    {
        SW_DB_GUARD;

        const char *sql = R"SQL(
        SELECT COUNT(*), COALESCE(SUM(f.size_bytes), 0)
        FROM files f
        JOIN files_fts fts ON fts.rowid = f.id
        WHERE f.root_id = ? AND files_fts MATCH ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return {0, 0};

        sqlite3_bind_int64(stmt, 1, rootId);
        sqlite3_bind_text(stmt, 2, query.c_str(), -1, SQLITE_TRANSIENT);

        std::pair<int64_t, int64_t> stats{0, 0};
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            stats.first = sqlite3_column_int64(stmt, 0);
            stats.second = sqlite3_column_int64(stmt, 1);
        }

        sqlite3_finalize(stmt);
        return stats;
    }

    std::optional<FileRecord> CatalogDb::fileById(int64_t fileId)
    {
        SW_DB_GUARD;

        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
             size_bytes, modified_time, duration_sec, total_samples,
             sample_rate, channels, bit_depth, bitrate_kbps, codec,
               bpm, key, loop_type, acid_root_note, acid_beats,
               loop_start_sample, loop_end_sample, index_only, slice_count, content_hash,
               preset_name, zone_count, key_low, key_high, velocity_low, velocity_high
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

        FileRecord r = readFileRecord(stmt);

        sqlite3_finalize(stmt);
        return r;
    }

    std::optional<FileRecord> CatalogDb::fileByRootAndRelativePath(int64_t rootId, const std::string &relativePath)
    {
        SW_DB_GUARD;

        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
             size_bytes, modified_time, duration_sec, total_samples,
             sample_rate, channels, bit_depth, bitrate_kbps, codec,
               bpm, key, loop_type, acid_root_note, acid_beats,
               loop_start_sample, loop_end_sample, index_only, slice_count, content_hash,
               preset_name, zone_count, key_low, key_high, velocity_low, velocity_high
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

        FileRecord r = readFileRecord(stmt);

        sqlite3_finalize(stmt);
        return r;
    }

    std::vector<FileRecord> CatalogDb::listFavoriteFiles(int limit)
    {
        SW_DB_GUARD;

        std::vector<FileRecord> results;

        const char *sql = R"SQL(
        SELECT f.id, f.root_id, f.relative_path, f.filename, f.extension,
             f.size_bytes, f.modified_time, f.duration_sec, f.total_samples,
             f.sample_rate, f.channels, f.bit_depth, f.bitrate_kbps, f.codec,
               f.bpm, f.key, f.loop_type, f.acid_root_note, f.acid_beats,
             f.loop_start_sample, f.loop_end_sample, f.index_only, f.slice_count, f.content_hash,
             f.preset_name, f.zone_count, f.key_low, f.key_high, f.velocity_low, f.velocity_high
        FROM files f
        JOIN file_user_data d ON d.file_id = f.id
        WHERE d.is_favorite <> 0
        ORDER BY f.filename, f.id
        LIMIT ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return results;

        sqlite3_bind_int(stmt, 1, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            results.push_back(readFileRecord(stmt));

        sqlite3_finalize(stmt);
        return results;
    }

    bool CatalogDb::setFileUserData(const FileUserDataRecord &userData)
    {
        SW_DB_GUARD;

        const char *sql = R"SQL(
        INSERT INTO file_user_data (file_id, is_favorite)
        VALUES (?, ?)
        ON CONFLICT(file_id) DO UPDATE SET
            is_favorite = excluded.is_favorite
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, userData.fileId);
        sqlite3_bind_int(stmt, 2, userData.isFavorite ? 1 : 0);

        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    std::optional<FileUserDataRecord> CatalogDb::fileUserDataByFileId(int64_t fileId)
    {
        SW_DB_GUARD;

        const char *sql = "SELECT file_id, is_favorite FROM file_user_data WHERE file_id = ?";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_int64(stmt, 1, fileId);

        if (sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        FileUserDataRecord result;
        result.fileId = sqlite3_column_int64(stmt, 0);
        result.isFavorite = sqlite3_column_int(stmt, 1) != 0;

        sqlite3_finalize(stmt);
        return result;
    }

    bool CatalogDb::setAppSetting(const std::string &key, const std::string &value)
    {
        SW_DB_GUARD;

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
        SW_DB_GUARD;

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
        SW_DB_GUARD;

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
        SW_DB_GUARD;

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

    bool CatalogDb::vacuum()
    {
        SW_DB_GUARD;

        if (db == nullptr)
            return false;

        return sqlite3_exec(db, "VACUUM;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    bool CatalogDb::beginTransaction()
    {
        SW_DB_GUARD;

        if (db == nullptr)
            return false;

        return sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    bool CatalogDb::commitTransaction()
    {
        SW_DB_GUARD;

        if (db == nullptr)
            return false;

        return sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    bool CatalogDb::rollbackTransaction()
    {
        SW_DB_GUARD;

        if (db == nullptr)
            return false;

        return sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK;
    }

#undef SW_DB_GUARD

} // namespace sw
