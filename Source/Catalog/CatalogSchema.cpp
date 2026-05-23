#include "CatalogSchema.h"
#include "Util/Logging.h"
#include <sqlite3.h>
#include <string>

namespace sw
{

    namespace
    {
        constexpr int kCurrentSchemaVersion = 4;

        bool execSql(sqlite3 *db, const char *sql)
        {
            char *errMsg = nullptr;
            const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK)
            {
                if (errMsg)
                    SW_LOG_ERR("Catalog schema SQL error: " << errMsg);
                else
                    SW_LOG_ERR("Catalog schema SQL error with unknown message.");

                if (errMsg)
                    sqlite3_free(errMsg);
                return false;
            }

            return true;
        }

        int readUserVersion(sqlite3 *db)
        {
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr) != SQLITE_OK)
                return 0;

            int version = 0;
            if (sqlite3_step(stmt) == SQLITE_ROW)
                version = sqlite3_column_int(stmt, 0);

            sqlite3_finalize(stmt);
            return version;
        }

        bool setUserVersion(sqlite3 *db, int version)
        {
            return execSql(db, ("PRAGMA user_version = " + std::to_string(version) + ";").c_str());
        }

        bool addColumnIfMissing(sqlite3 *db, const char *sql)
        {
            char *errMsg = nullptr;
            const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
            if (rc == SQLITE_OK)
                return true;

            if (errMsg != nullptr)
            {
                const std::string error(errMsg);
                sqlite3_free(errMsg);
                if (error.find("duplicate column name") != std::string::npos)
                    return true;
            }

            return false;
        }

        bool applyMigration1(sqlite3 *db)
        {
            if (!execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS roots (
            id      INTEGER PRIMARY KEY AUTOINCREMENT,
            path    TEXT NOT NULL UNIQUE,
            label   TEXT NOT NULL DEFAULT '',
            enabled INTEGER NOT NULL DEFAULT 1
        );
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS files (
            id                INTEGER PRIMARY KEY AUTOINCREMENT,
            root_id           INTEGER NOT NULL REFERENCES roots(id) ON DELETE CASCADE,
            relative_path     TEXT NOT NULL,
            filename          TEXT NOT NULL,
            extension         TEXT NOT NULL DEFAULT '',
            size_bytes        INTEGER NOT NULL DEFAULT 0,
            modified_time     INTEGER NOT NULL DEFAULT 0,
            duration_sec      REAL,
            total_samples     INTEGER,
            sample_rate       INTEGER,
            channels          INTEGER,
            bit_depth         INTEGER,
            bitrate_kbps      INTEGER,
            codec             TEXT,
            bpm               REAL,
            key               TEXT,
            loop_type         TEXT,
            acid_root_note    INTEGER,
            acid_beats        INTEGER,
            loop_start_sample INTEGER,
            loop_end_sample   INTEGER,
            index_only        INTEGER NOT NULL DEFAULT 0,
            slice_count       INTEGER,
            content_hash      TEXT,
            UNIQUE(root_id, relative_path)
        );
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE INDEX IF NOT EXISTS idx_files_root ON files(root_id);
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5(
            filename,
            relative_path,
            content='files',
            content_rowid='id'
        );
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TRIGGER IF NOT EXISTS files_ai AFTER INSERT ON files BEGIN
            INSERT INTO files_fts(rowid, filename, relative_path)
                VALUES (new.id, new.filename, new.relative_path);
        END;
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TRIGGER IF NOT EXISTS files_ad AFTER DELETE ON files BEGIN
            INSERT INTO files_fts(files_fts, rowid, filename, relative_path)
                VALUES ('delete', old.id, old.filename, old.relative_path);
        END;
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TRIGGER IF NOT EXISTS files_au AFTER UPDATE ON files BEGIN
            INSERT INTO files_fts(files_fts, rowid, filename, relative_path)
                VALUES ('delete', old.id, old.filename, old.relative_path);
            INSERT INTO files_fts(rowid, filename, relative_path)
                VALUES (new.id, new.filename, new.relative_path);
        END;
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS preview_settings (
            id                INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id           INTEGER REFERENCES files(id) ON DELETE CASCADE,
            pitch_semitones   REAL NOT NULL DEFAULT 0.0,
            loop_enabled      INTEGER NOT NULL DEFAULT 0,
            loop_start_sec    REAL NOT NULL DEFAULT 0.0,
            loop_end_sec      REAL NOT NULL DEFAULT 0.0,
            volume            REAL NOT NULL DEFAULT 1.0
        );
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS wave_cache (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            file_id     INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
            cache_key   TEXT NOT NULL UNIQUE,
            cache_path  TEXT NOT NULL
        );
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS app_settings (
            key     TEXT PRIMARY KEY,
            value   TEXT NOT NULL
        );
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS file_user_data (
            file_id      INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
            is_favorite  INTEGER NOT NULL DEFAULT 0,
            rating       INTEGER CHECK (rating IS NULL OR (rating >= 1 AND rating <= 5))
        );
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE INDEX IF NOT EXISTS idx_file_user_data_favorite ON file_user_data(is_favorite);
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS file_tags (
            file_id  INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
            tag      TEXT NOT NULL,
            PRIMARY KEY (file_id, tag)
        );
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE INDEX IF NOT EXISTS idx_file_tags_tag ON file_tags(tag);
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS saved_searches (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            name            TEXT NOT NULL UNIQUE,
            query_text      TEXT NOT NULL DEFAULT '',
            root_id         INTEGER REFERENCES roots(id) ON DELETE SET NULL,
            favorites_only  INTEGER NOT NULL DEFAULT 0
        );
    )SQL"))
                return false;

            return true;
        }

        bool applyMigration2(sqlite3 *db)
        {
            if (!addColumnIfMissing(db, "ALTER TABLE files ADD COLUMN total_samples INTEGER;"))
                return false;
            if (!addColumnIfMissing(db, "ALTER TABLE files ADD COLUMN bitrate_kbps INTEGER;"))
                return false;
            if (!addColumnIfMissing(db, "ALTER TABLE files ADD COLUMN codec TEXT;"))
                return false;
            if (!addColumnIfMissing(db, "ALTER TABLE files ADD COLUMN acid_root_note INTEGER;"))
                return false;
            if (!addColumnIfMissing(db, "ALTER TABLE files ADD COLUMN acid_beats INTEGER;"))
                return false;
            if (!addColumnIfMissing(db, "ALTER TABLE files ADD COLUMN loop_start_sample INTEGER;"))
                return false;
            if (!addColumnIfMissing(db, "ALTER TABLE files ADD COLUMN loop_end_sample INTEGER;"))
                return false;
            if (!addColumnIfMissing(db, "ALTER TABLE files ADD COLUMN slice_count INTEGER;"))
                return false;

            return true;
        }

        bool applyMigration3(sqlite3 *db)
        {
            if (!addColumnIfMissing(db, "ALTER TABLE files ADD COLUMN content_hash TEXT;"))
                return false;

            return execSql(db, R"SQL(
        CREATE INDEX IF NOT EXISTS idx_files_content_hash ON files(content_hash);
    )SQL");
        }

        bool applyMigration4(sqlite3 *db)
        {
            if (!execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS file_user_data (
            file_id      INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
            is_favorite  INTEGER NOT NULL DEFAULT 0,
            rating       INTEGER CHECK (rating IS NULL OR (rating >= 1 AND rating <= 5))
        );
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE INDEX IF NOT EXISTS idx_file_user_data_favorite ON file_user_data(is_favorite);
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS file_tags (
            file_id  INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
            tag      TEXT NOT NULL,
            PRIMARY KEY (file_id, tag)
        );
    )SQL"))
                return false;

            if (!execSql(db, R"SQL(
        CREATE INDEX IF NOT EXISTS idx_file_tags_tag ON file_tags(tag);
    )SQL"))
                return false;

            return execSql(db, R"SQL(
        CREATE TABLE IF NOT EXISTS saved_searches (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            name            TEXT NOT NULL UNIQUE,
            query_text      TEXT NOT NULL DEFAULT '',
            root_id         INTEGER REFERENCES roots(id) ON DELETE SET NULL,
            favorites_only  INTEGER NOT NULL DEFAULT 0
        );
    )SQL");
        }
    }

    bool CatalogSchema::exec(sqlite3 *db, const char *sql)
    {
        return execSql(db, sql);
    }

    bool CatalogSchema::createAll(sqlite3 *db)
    {
        const int startingVersion = readUserVersion(db);
        for (int version = startingVersion + 1; version <= kCurrentSchemaVersion; ++version)
        {
            bool ok = false;
            switch (version)
            {
            case 1:
                ok = applyMigration1(db);
                break;
            case 2:
                ok = applyMigration2(db);
                break;
            case 3:
                ok = applyMigration3(db);
                break;
            case 4:
                ok = applyMigration4(db);
                break;
            default:
                ok = false;
                break;
            }

            if (!ok || !setUserVersion(db, version))
                return false;
        }

        return true;
    }

} // namespace sw
