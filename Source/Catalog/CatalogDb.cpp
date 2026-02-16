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
    // Sources
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
                           size_bytes, modified_time, duration_sec, total_samples,
                           sample_rate, channels, bit_depth, bitrate_kbps, codec,
                           bpm, key, loop_type, acid_root_note, acid_beats,
                           loop_start_sample, loop_end_sample, index_only)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
             f.size_bytes, f.modified_time, f.duration_sec, f.total_samples,
             f.sample_rate, f.channels, f.bit_depth, f.bitrate_kbps, f.codec,
               f.bpm, f.key, f.loop_type, f.acid_root_note, f.acid_beats,
               f.loop_start_sample, f.loop_end_sample, f.index_only
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
                r.totalSamples = sqlite3_column_int64(stmt, 8);
            if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
                r.sampleRate = sqlite3_column_int(stmt, 9);
            if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
                r.channels = sqlite3_column_int(stmt, 10);
            if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
                r.bitDepth = sqlite3_column_int(stmt, 11);
            if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
                r.bitrateKbps = sqlite3_column_int(stmt, 12);
            if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
                r.codec = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
            if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
                r.bpm = sqlite3_column_double(stmt, 14);
            if (sqlite3_column_type(stmt, 15) != SQLITE_NULL)
                r.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));
            if (sqlite3_column_type(stmt, 16) != SQLITE_NULL)
                r.loopType = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 16));
            if (sqlite3_column_type(stmt, 17) != SQLITE_NULL)
                r.acidRootNote = sqlite3_column_int(stmt, 17);
            if (sqlite3_column_type(stmt, 18) != SQLITE_NULL)
                r.acidBeats = sqlite3_column_int(stmt, 18);
            if (sqlite3_column_type(stmt, 19) != SQLITE_NULL)
                r.loopStartSample = sqlite3_column_int64(stmt, 19);
            if (sqlite3_column_type(stmt, 20) != SQLITE_NULL)
                r.loopEndSample = sqlite3_column_int64(stmt, 20);
            r.indexOnly = sqlite3_column_int(stmt, 21) != 0;

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
             size_bytes, modified_time, duration_sec, total_samples,
             sample_rate, channels, bit_depth, bitrate_kbps, codec,
               bpm, key, loop_type, acid_root_note, acid_beats,
               loop_start_sample, loop_end_sample, index_only
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
                r.totalSamples = sqlite3_column_int64(stmt, 8);
            if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
                r.sampleRate = sqlite3_column_int(stmt, 9);
            if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
                r.channels = sqlite3_column_int(stmt, 10);
            if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
                r.bitDepth = sqlite3_column_int(stmt, 11);
            if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
                r.bitrateKbps = sqlite3_column_int(stmt, 12);
            if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
                r.codec = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
            if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
                r.bpm = sqlite3_column_double(stmt, 14);
            if (sqlite3_column_type(stmt, 15) != SQLITE_NULL)
                r.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));
            if (sqlite3_column_type(stmt, 16) != SQLITE_NULL)
                r.loopType = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 16));
            if (sqlite3_column_type(stmt, 17) != SQLITE_NULL)
                r.acidRootNote = sqlite3_column_int(stmt, 17);
            if (sqlite3_column_type(stmt, 18) != SQLITE_NULL)
                r.acidBeats = sqlite3_column_int(stmt, 18);
            if (sqlite3_column_type(stmt, 19) != SQLITE_NULL)
                r.loopStartSample = sqlite3_column_int64(stmt, 19);
            if (sqlite3_column_type(stmt, 20) != SQLITE_NULL)
                r.loopEndSample = sqlite3_column_int64(stmt, 20);
            r.indexOnly = sqlite3_column_int(stmt, 21) != 0;

            results.push_back(std::move(r));
        }
        sqlite3_finalize(stmt);
        return results;
    }

    std::vector<FileRecord> CatalogDb::searchFilesByRoot(int64_t rootId, const std::string &query, int limit)
    {
        std::vector<FileRecord> results;

        const char *sql = R"SQL(
        SELECT f.id, f.root_id, f.relative_path, f.filename, f.extension,
             f.size_bytes, f.modified_time, f.duration_sec, f.total_samples,
             f.sample_rate, f.channels, f.bit_depth, f.bitrate_kbps, f.codec,
               f.bpm, f.key, f.loop_type, f.acid_root_note, f.acid_beats,
               f.loop_start_sample, f.loop_end_sample, f.index_only
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
                r.totalSamples = sqlite3_column_int64(stmt, 8);
            if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
                r.sampleRate = sqlite3_column_int(stmt, 9);
            if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
                r.channels = sqlite3_column_int(stmt, 10);
            if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
                r.bitDepth = sqlite3_column_int(stmt, 11);
            if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
                r.bitrateKbps = sqlite3_column_int(stmt, 12);
            if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
                r.codec = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
            if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
                r.bpm = sqlite3_column_double(stmt, 14);
            if (sqlite3_column_type(stmt, 15) != SQLITE_NULL)
                r.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));
            if (sqlite3_column_type(stmt, 16) != SQLITE_NULL)
                r.loopType = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 16));
            if (sqlite3_column_type(stmt, 17) != SQLITE_NULL)
                r.acidRootNote = sqlite3_column_int(stmt, 17);
            if (sqlite3_column_type(stmt, 18) != SQLITE_NULL)
                r.acidBeats = sqlite3_column_int(stmt, 18);
            if (sqlite3_column_type(stmt, 19) != SQLITE_NULL)
                r.loopStartSample = sqlite3_column_int64(stmt, 19);
            if (sqlite3_column_type(stmt, 20) != SQLITE_NULL)
                r.loopEndSample = sqlite3_column_int64(stmt, 20);
            r.indexOnly = sqlite3_column_int(stmt, 21) != 0;

            results.push_back(std::move(r));
        }

        sqlite3_finalize(stmt);
        return results;
    }

    std::vector<FileRecord> CatalogDb::listRecentFilesByRoot(int64_t rootId, int limit)
    {
        std::vector<FileRecord> results;

        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
             size_bytes, modified_time, duration_sec, total_samples,
             sample_rate, channels, bit_depth, bitrate_kbps, codec,
               bpm, key, loop_type, acid_root_note, acid_beats,
               loop_start_sample, loop_end_sample, index_only
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
                r.totalSamples = sqlite3_column_int64(stmt, 8);
            if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
                r.sampleRate = sqlite3_column_int(stmt, 9);
            if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
                r.channels = sqlite3_column_int(stmt, 10);
            if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
                r.bitDepth = sqlite3_column_int(stmt, 11);
            if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
                r.bitrateKbps = sqlite3_column_int(stmt, 12);
            if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
                r.codec = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
            if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
                r.bpm = sqlite3_column_double(stmt, 14);
            if (sqlite3_column_type(stmt, 15) != SQLITE_NULL)
                r.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));
            if (sqlite3_column_type(stmt, 16) != SQLITE_NULL)
                r.loopType = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 16));
            if (sqlite3_column_type(stmt, 17) != SQLITE_NULL)
                r.acidRootNote = sqlite3_column_int(stmt, 17);
            if (sqlite3_column_type(stmt, 18) != SQLITE_NULL)
                r.acidBeats = sqlite3_column_int(stmt, 18);
            if (sqlite3_column_type(stmt, 19) != SQLITE_NULL)
                r.loopStartSample = sqlite3_column_int64(stmt, 19);
            if (sqlite3_column_type(stmt, 20) != SQLITE_NULL)
                r.loopEndSample = sqlite3_column_int64(stmt, 20);
            r.indexOnly = sqlite3_column_int(stmt, 21) != 0;

            results.push_back(std::move(r));
        }

        sqlite3_finalize(stmt);
        return results;
    }

    std::optional<FileRecord> CatalogDb::fileById(int64_t fileId)
    {
        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
             size_bytes, modified_time, duration_sec, total_samples,
             sample_rate, channels, bit_depth, bitrate_kbps, codec,
               bpm, key, loop_type, acid_root_note, acid_beats,
               loop_start_sample, loop_end_sample, index_only
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
            r.totalSamples = sqlite3_column_int64(stmt, 8);
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
            r.sampleRate = sqlite3_column_int(stmt, 9);
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
            r.channels = sqlite3_column_int(stmt, 10);
        if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
            r.bitDepth = sqlite3_column_int(stmt, 11);
        if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
            r.bitrateKbps = sqlite3_column_int(stmt, 12);
        if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
            r.codec = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
        if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
            r.bpm = sqlite3_column_double(stmt, 14);
        if (sqlite3_column_type(stmt, 15) != SQLITE_NULL)
            r.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));
        if (sqlite3_column_type(stmt, 16) != SQLITE_NULL)
            r.loopType = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 16));
        if (sqlite3_column_type(stmt, 17) != SQLITE_NULL)
            r.acidRootNote = sqlite3_column_int(stmt, 17);
        if (sqlite3_column_type(stmt, 18) != SQLITE_NULL)
            r.acidBeats = sqlite3_column_int(stmt, 18);
        if (sqlite3_column_type(stmt, 19) != SQLITE_NULL)
            r.loopStartSample = sqlite3_column_int64(stmt, 19);
        if (sqlite3_column_type(stmt, 20) != SQLITE_NULL)
            r.loopEndSample = sqlite3_column_int64(stmt, 20);
        r.indexOnly = sqlite3_column_int(stmt, 21) != 0;

        sqlite3_finalize(stmt);
        return r;
    }

    std::optional<FileRecord> CatalogDb::fileByRootAndRelativePath(int64_t rootId, const std::string &relativePath)
    {
        const char *sql = R"SQL(
        SELECT id, root_id, relative_path, filename, extension,
             size_bytes, modified_time, duration_sec, total_samples,
             sample_rate, channels, bit_depth, bitrate_kbps, codec,
               bpm, key, loop_type, acid_root_note, acid_beats,
               loop_start_sample, loop_end_sample, index_only
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
            r.totalSamples = sqlite3_column_int64(stmt, 8);
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
            r.sampleRate = sqlite3_column_int(stmt, 9);
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
            r.channels = sqlite3_column_int(stmt, 10);
        if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
            r.bitDepth = sqlite3_column_int(stmt, 11);
        if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
            r.bitrateKbps = sqlite3_column_int(stmt, 12);
        if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
            r.codec = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 13));
        if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
            r.bpm = sqlite3_column_double(stmt, 14);
        if (sqlite3_column_type(stmt, 15) != SQLITE_NULL)
            r.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 15));
        if (sqlite3_column_type(stmt, 16) != SQLITE_NULL)
            r.loopType = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 16));
        if (sqlite3_column_type(stmt, 17) != SQLITE_NULL)
            r.acidRootNote = sqlite3_column_int(stmt, 17);
        if (sqlite3_column_type(stmt, 18) != SQLITE_NULL)
            r.acidBeats = sqlite3_column_int(stmt, 18);
        if (sqlite3_column_type(stmt, 19) != SQLITE_NULL)
            r.loopStartSample = sqlite3_column_int64(stmt, 19);
        if (sqlite3_column_type(stmt, 20) != SQLITE_NULL)
            r.loopEndSample = sqlite3_column_int64(stmt, 20);
        r.indexOnly = sqlite3_column_int(stmt, 21) != 0;

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
