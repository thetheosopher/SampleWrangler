#include "CatalogDb.h"
#include "CatalogSchema.h"
#include "Util/Logging.h"
#include <sqlite3.h>
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>

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

        bool encodeFeatureVector(const std::vector<float> &features, std::vector<unsigned char> &bytes)
        {
            static_assert(sizeof(float) == sizeof(uint32_t));
            if (features.size() > static_cast<size_t>(std::numeric_limits<int>::max()) / sizeof(float))
                return false;

            bytes.clear();
            bytes.reserve(features.size() * sizeof(float));
            for (const auto feature : features)
            {
                if (!std::isfinite(feature))
                    return false;

                const auto bits = std::bit_cast<uint32_t>(feature);
                for (unsigned int shift = 0; shift < 32; shift += 8)
                    bytes.push_back(static_cast<unsigned char>((bits >> shift) & 0xffu));
            }
            return true;
        }

        bool readFeatureVectorColumn(sqlite3_stmt *stmt, int index, std::vector<float> &features)
        {
            features.clear();
            if (sqlite3_column_type(stmt, index) == SQLITE_NULL)
                return true;
            if (sqlite3_column_type(stmt, index) != SQLITE_BLOB)
                return false;

            const int byteCount = sqlite3_column_bytes(stmt, index);
            if (byteCount < 0 || byteCount % static_cast<int>(sizeof(float)) != 0)
                return false;

            const auto *bytes = static_cast<const unsigned char *>(sqlite3_column_blob(stmt, index));
            if (byteCount > 0 && bytes == nullptr)
                return false;

            features.reserve(static_cast<size_t>(byteCount) / sizeof(float));
            for (int offset = 0; offset < byteCount; offset += static_cast<int>(sizeof(float)))
            {
                const auto bits = static_cast<uint32_t>(bytes[offset]) |
                                  (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
                                  (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
                                  (static_cast<uint32_t>(bytes[offset + 3]) << 24);
                const auto feature = std::bit_cast<float>(bits);
                if (!std::isfinite(feature))
                    return false;
                features.push_back(feature);
            }
            return true;
        }

        double featureMatchScore(const std::vector<float> &reference,
                                 const std::vector<float> &candidate)
        {
            const auto relativeDifference = [](double a, double b, double floor)
            {
                const double scale = std::max({std::abs(a), std::abs(b), floor});
                return std::min(1.0, std::abs(a - b) / scale);
            };

            const double durationDistance = relativeDifference(reference[0], candidate[0], 0.25);
            const double rmsDistance = relativeDifference(reference[1], candidate[1], 0.05);
            const double peakDistance = relativeDifference(reference[2], candidate[2], 0.05);
            const double zeroCrossingDistance =
                std::min(1.0, std::abs(static_cast<double>(reference[3]) - candidate[3]));
            const double distance = 0.40 * durationDistance +
                                    0.20 * rmsDistance +
                                    0.15 * peakDistance +
                                    0.25 * zeroCrossingDistance;
            return std::clamp((1.0 - distance) * 100.0, 0.0, 100.0);
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
            if (sqlite3_column_type(stmt, 18) != SQLITE_NULL)
                r.acidBeats = sqlite3_column_int(stmt, 18);
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

        std::string makePlaceholders(size_t count)
        {
            std::string placeholders;
            placeholders.reserve(count * 2);
            for (size_t i = 0; i < count; ++i)
            {
                if (i > 0)
                    placeholders.push_back(',');
                placeholders.push_back('?');
            }
            return placeholders;
        }

        bool bindFileIds(sqlite3_stmt *stmt, const std::vector<int64_t> &fileIds, size_t startIndex)
        {
            for (size_t i = 0; i < fileIds.size(); ++i)
            {
                if (sqlite3_bind_int64(stmt, static_cast<int>(startIndex + i), fileIds[i]) != SQLITE_OK)
                    return false;
            }
            return true;
        }

        bool logMetadataLoadError(sqlite3 *db, const char *operation)
        {
            SW_LOG_ERR("CatalogDb: failed to " << operation << ": " << sqlite3_errmsg(db));
            return false;
        }

        template <typename Callback>
        bool queryRelatedRows(sqlite3 *db,
                              const std::vector<int64_t> &fileIds,
                              const std::string &selectPrefix,
                              const char *operation,
                              Callback &&onRow)
        {
            constexpr size_t kMaxIdsPerQuery = 900;
            for (size_t begin = 0; begin < fileIds.size(); begin += kMaxIdsPerQuery)
            {
                const auto count = std::min(kMaxIdsPerQuery, fileIds.size() - begin);
                const auto placeholders = makePlaceholders(count);
                const auto sql = selectPrefix + placeholders + ")";

                sqlite3_stmt *stmt = nullptr;
                if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                    return logMetadataLoadError(db, operation);

                bool ok = bindFileIds(stmt,
                                      std::vector<int64_t>(fileIds.begin() + static_cast<std::ptrdiff_t>(begin),
                                                           fileIds.begin() + static_cast<std::ptrdiff_t>(begin + count)),
                                      1);
                int rc = SQLITE_OK;
                while (ok && (rc = sqlite3_step(stmt)) == SQLITE_ROW)
                    onRow(stmt);

                if (rc != SQLITE_DONE)
                    ok = false;

                sqlite3_finalize(stmt);
                if (!ok)
                    return logMetadataLoadError(db, operation);
            }

            return true;
        }

        bool hydrateRelatedMetadata(sqlite3 *db, std::vector<FileRecord> &files)
        {
            if (files.empty())
                return true;

            std::unordered_map<int64_t, FileRecord *> filesById;
            std::vector<int64_t> fileIds;
            fileIds.reserve(files.size());
            for (auto &file : files)
            {
                filesById.emplace(file.id, &file);
                fileIds.push_back(file.id);
            }

            if (!queryRelatedRows(db,
                                  fileIds,
                                  "SELECT id, root_midi_note, root_tune_cents, root_pitch_source, metadata_source "
                                  "FROM files WHERE id IN (",
                                  "load root-pitch metadata",
                                  [&filesById](sqlite3_stmt *stmt)
                                  {
                                      const auto it = filesById.find(sqlite3_column_int64(stmt, 0));
                                      if (it == filesById.end())
                                          return;

                                      auto &file = *it->second;
                                      if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
                                          file.rootMidiNote = sqlite3_column_int(stmt, 1);
                                      if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
                                          file.rootTuneCents = sqlite3_column_double(stmt, 2);
                                      file.rootPitchSource = readOptionalTextColumn(stmt, 3);
                                      file.metadataSource = readOptionalTextColumn(stmt, 4);
                                  }))
                return false;

            if (!queryRelatedRows(db,
                                  fileIds,
                                  "SELECT file_id, loop_index, role, mode, raw_mode, start_sample, end_sample, "
                                  "end_inclusive, fractional_position, play_count, source_id, start_marker_id, "
                                  "end_marker_id, preset_bank, preset_program, source "
                                  "FROM file_loop_regions WHERE file_id IN (",
                                  "load loop regions",
                                  [&filesById](sqlite3_stmt *stmt)
                                  {
                                      const auto it = filesById.find(sqlite3_column_int64(stmt, 0));
                                      if (it == filesById.end())
                                          return;

                                      LoopRegionRecord loop;
                                      loop.index = sqlite3_column_int(stmt, 1);
                                      const auto role = sqlite3_column_int(stmt, 2);
                                      if (role >= static_cast<int>(LoopRegionRole::Unspecified) &&
                                          role <= static_cast<int>(LoopRegionRole::Release))
                                          loop.role = static_cast<LoopRegionRole>(role);
                                      const auto mode = sqlite3_column_int(stmt, 3);
                                      if (mode >= static_cast<int>(LoopMode::Unknown) &&
                                          mode <= static_cast<int>(LoopMode::Backward))
                                          loop.mode = static_cast<LoopMode>(mode);
                                      if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
                                          loop.rawMode = static_cast<uint32_t>(sqlite3_column_int64(stmt, 4));
                                      loop.startSample = sqlite3_column_int64(stmt, 5);
                                      loop.endSample = sqlite3_column_int64(stmt, 6);
                                      loop.endInclusive = sqlite3_column_int(stmt, 7) != 0;
                                      if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
                                          loop.fractionalPosition = static_cast<uint32_t>(sqlite3_column_int64(stmt, 8));
                                      if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
                                          loop.playCount = static_cast<uint32_t>(sqlite3_column_int64(stmt, 9));
                                      if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
                                          loop.sourceId = static_cast<uint32_t>(sqlite3_column_int64(stmt, 10));
                                      if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
                                          loop.startMarkerId = static_cast<uint16_t>(sqlite3_column_int(stmt, 11));
                                      if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
                                          loop.endMarkerId = static_cast<uint16_t>(sqlite3_column_int(stmt, 12));
                                      if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
                                          loop.presetBank = sqlite3_column_int(stmt, 13);
                                      if (sqlite3_column_type(stmt, 14) != SQLITE_NULL)
                                          loop.presetProgram = sqlite3_column_int(stmt, 14);
                                      loop.source = readTextColumn(stmt, 15);
                                      it->second->loopRegions.push_back(std::move(loop));
                                  }))
                return false;

            if (!queryRelatedRows(db,
                                  fileIds,
                                  "SELECT file_id, ordinal, bank, program, preset_name, zone_count, key_low, "
                                  "key_high, velocity_low, velocity_high, root_midi_note, root_tune_cents "
                                  "FROM file_preset_programs WHERE file_id IN (",
                                  "load preset programs",
                                  [&filesById](sqlite3_stmt *stmt)
                                  {
                                      const auto it = filesById.find(sqlite3_column_int64(stmt, 0));
                                      if (it == filesById.end())
                                          return;

                                      PresetProgramRecord program;
                                      program.ordinal = sqlite3_column_int(stmt, 1);
                                      program.bank = sqlite3_column_int(stmt, 2);
                                      program.program = sqlite3_column_int(stmt, 3);
                                      program.name = readTextColumn(stmt, 4);
                                      if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
                                          program.zoneCount = sqlite3_column_int(stmt, 5);
                                      if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
                                          program.keyLow = sqlite3_column_int(stmt, 6);
                                      if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
                                          program.keyHigh = sqlite3_column_int(stmt, 7);
                                      if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
                                          program.velocityLow = sqlite3_column_int(stmt, 8);
                                      if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
                                          program.velocityHigh = sqlite3_column_int(stmt, 9);
                                      if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
                                          program.rootMidiNote = sqlite3_column_int(stmt, 10);
                                      if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
                                          program.rootTuneCents = sqlite3_column_double(stmt, 11);
                                      it->second->presetPrograms.push_back(std::move(program));
                                  }))
                return false;

            const auto analysisVersion = std::to_string(kCurrentAnalysisVersion);
            const auto featureVersion = std::to_string(kCurrentAnalysisFeatureVectorVersion);
            const auto analysisStatus = std::to_string(static_cast<int>(AnalysisStatus::Complete));
            return queryRelatedRows(
                db,
                fileIds,
                "SELECT a.file_id, a.detected_bpm, a.bpm_confidence, a.detected_key, a.key_confidence, "
                "a.transient_count, a.onset_density, a.suggested_loop_start_sample, "
                "a.suggested_loop_end_sample, a.loop_confidence "
                "FROM analysis_results a JOIN files f ON f.id = a.file_id "
                "WHERE a.analysis_version = " +
                    analysisVersion + " AND a.feature_vector_version = " + featureVersion +
                    " AND a.status = " + analysisStatus +
                    " AND a.analyzed_modified_time = f.modified_time "
                    "AND a.analyzed_content_hash IS f.content_hash AND a.file_id IN (",
                "load current analysis metadata",
                [&filesById](sqlite3_stmt *stmt)
                {
                    const auto it = filesById.find(sqlite3_column_int64(stmt, 0));
                    if (it == filesById.end())
                        return;

                    auto &file = *it->second;
                    if (sqlite3_column_type(stmt, 1) != SQLITE_NULL)
                        file.inferredBpm = sqlite3_column_double(stmt, 1);
                    if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
                        file.inferredBpmConfidence = sqlite3_column_double(stmt, 2);
                    file.inferredKey = readOptionalTextColumn(stmt, 3);
                    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
                        file.inferredKeyConfidence = sqlite3_column_double(stmt, 4);
                    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
                        file.inferredTransientCount = sqlite3_column_int(stmt, 5);
                    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
                        file.inferredOnsetDensity = sqlite3_column_double(stmt, 6);
                    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
                        file.suggestedLoopStartSample = sqlite3_column_int64(stmt, 7);
                    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
                        file.suggestedLoopEndSample = sqlite3_column_int64(stmt, 8);
                    if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
                        file.suggestedLoopConfidence = sqlite3_column_double(stmt, 9);
                });
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
        {
            SW_LOG_ERR("CatalogDb: allRoots prepare failed: " << sqlite3_errmsg(db));
            return results;
        }

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

        if (db == nullptr)
            return false;

        if (sqlite3_exec(db, "SAVEPOINT sw_catalog_file_upsert;", nullptr, nullptr, nullptr) != SQLITE_OK)
            return false;

        const auto rollback = [this]
        {
            sqlite3_exec(db, "ROLLBACK TO sw_catalog_file_upsert;", nullptr, nullptr, nullptr);
            sqlite3_exec(db, "RELEASE sw_catalog_file_upsert;", nullptr, nullptr, nullptr);
        };

        const char *sql = R"SQL(
        INSERT INTO files (root_id, relative_path, filename, extension,
                           size_bytes, modified_time, duration_sec, total_samples,
                           sample_rate, channels, bit_depth, bitrate_kbps, codec,
                           bpm, key, loop_type, acid_root_note, acid_beats,
                           loop_start_sample, loop_end_sample, index_only, slice_count, content_hash,
                           preset_name, zone_count, key_low, key_high, velocity_low, velocity_high,
                           root_midi_note, root_tune_cents, root_pitch_source, metadata_source)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                ?, ?, ?, ?)
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
            velocity_high = excluded.velocity_high,
            root_midi_note = excluded.root_midi_note,
            root_tune_cents = excluded.root_tune_cents,
            root_pitch_source = excluded.root_pitch_source,
            metadata_source = excluded.metadata_source
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            rollback();
            return false;
        }

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

        if (rec.metadataSource)
            sqlite3_bind_text(stmt, 16, rec.metadataSource->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 16);

        if (rec.rootMidiNote)
            sqlite3_bind_int(stmt, 17, *rec.rootMidiNote);
        else
            sqlite3_bind_null(stmt, 17);

        if (rec.acidBeats)
            sqlite3_bind_int(stmt, 18, *rec.acidBeats);
        else
            sqlite3_bind_null(stmt, 18);

        const auto *legacyLoop = rec.loopRegions.empty() ? nullptr : &rec.loopRegions.front();
        if (legacyLoop != nullptr)
            sqlite3_bind_int64(stmt, 19, legacyLoop->startSample);
        else
            sqlite3_bind_null(stmt, 19);

        if (legacyLoop != nullptr)
            sqlite3_bind_int64(stmt, 20, legacyLoop->endSample);
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

        if (rec.rootMidiNote)
            sqlite3_bind_int(stmt, 30, *rec.rootMidiNote);
        else
            sqlite3_bind_null(stmt, 30);

        if (rec.rootTuneCents)
            sqlite3_bind_double(stmt, 31, *rec.rootTuneCents);
        else
            sqlite3_bind_null(stmt, 31);

        if (rec.rootPitchSource)
            sqlite3_bind_text(stmt, 32, rec.rootPitchSource->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 32);

        if (rec.metadataSource)
            sqlite3_bind_text(stmt, 33, rec.metadataSource->c_str(), -1, SQLITE_TRANSIENT);
        else
            sqlite3_bind_null(stmt, 33);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!ok)
        {
            rollback();
            return false;
        }

        sqlite3_stmt *fileIdStmt = nullptr;
        if (sqlite3_prepare_v2(db,
                               "SELECT id FROM files WHERE root_id = ? AND relative_path = ?;",
                               -1,
                               &fileIdStmt,
                               nullptr) != SQLITE_OK)
        {
            rollback();
            return false;
        }
        sqlite3_bind_int64(fileIdStmt, 1, rec.rootId);
        sqlite3_bind_text(fileIdStmt, 2, rec.relativePath.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(fileIdStmt) != SQLITE_ROW)
        {
            sqlite3_finalize(fileIdStmt);
            rollback();
            return false;
        }
        const int64_t fileId = sqlite3_column_int64(fileIdStmt, 0);
        sqlite3_finalize(fileIdStmt);

        const auto deleteRelatedRows = [this, fileId](const char *tableName)
        {
            const auto deleteSql = std::string("DELETE FROM ") + tableName + " WHERE file_id = ?;";
            sqlite3_stmt *deleteStmt = nullptr;
            if (sqlite3_prepare_v2(db, deleteSql.c_str(), -1, &deleteStmt, nullptr) != SQLITE_OK)
                return false;
            sqlite3_bind_int64(deleteStmt, 1, fileId);
            const bool deleteOk = sqlite3_step(deleteStmt) == SQLITE_DONE;
            sqlite3_finalize(deleteStmt);
            return deleteOk;
        };

        if (!deleteRelatedRows("file_loop_regions") || !deleteRelatedRows("file_preset_programs"))
        {
            rollback();
            return false;
        }

        const char *loopSql = R"SQL(
        INSERT INTO file_loop_regions (
            file_id, loop_index, role, mode, raw_mode, start_sample, end_sample,
            end_inclusive, fractional_position, play_count, source_id,
            start_marker_id, end_marker_id, preset_bank, preset_program, source
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )SQL";
        sqlite3_stmt *loopStmt = nullptr;
        if (!rec.loopRegions.empty() && sqlite3_prepare_v2(db, loopSql, -1, &loopStmt, nullptr) != SQLITE_OK)
        {
            rollback();
            return false;
        }

        for (size_t i = 0; i < rec.loopRegions.size(); ++i)
        {
            const auto &loop = rec.loopRegions[i];
            sqlite3_bind_int64(loopStmt, 1, fileId);
            sqlite3_bind_int(loopStmt, 2, static_cast<int>(i));
            sqlite3_bind_int(loopStmt, 3, static_cast<int>(loop.role));
            sqlite3_bind_int(loopStmt, 4, static_cast<int>(loop.mode));
            if (loop.rawMode)
                sqlite3_bind_int64(loopStmt, 5, *loop.rawMode);
            else
                sqlite3_bind_null(loopStmt, 5);
            sqlite3_bind_int64(loopStmt, 6, loop.startSample);
            sqlite3_bind_int64(loopStmt, 7, loop.endSample);
            sqlite3_bind_int(loopStmt, 8, loop.endInclusive ? 1 : 0);
            if (loop.fractionalPosition)
                sqlite3_bind_int64(loopStmt, 9, *loop.fractionalPosition);
            else
                sqlite3_bind_null(loopStmt, 9);
            if (loop.playCount)
                sqlite3_bind_int64(loopStmt, 10, *loop.playCount);
            else
                sqlite3_bind_null(loopStmt, 10);
            if (loop.sourceId)
                sqlite3_bind_int64(loopStmt, 11, *loop.sourceId);
            else
                sqlite3_bind_null(loopStmt, 11);
            if (loop.startMarkerId)
                sqlite3_bind_int(loopStmt, 12, *loop.startMarkerId);
            else
                sqlite3_bind_null(loopStmt, 12);
            if (loop.endMarkerId)
                sqlite3_bind_int(loopStmt, 13, *loop.endMarkerId);
            else
                sqlite3_bind_null(loopStmt, 13);
            if (loop.presetBank)
                sqlite3_bind_int(loopStmt, 14, *loop.presetBank);
            else
                sqlite3_bind_null(loopStmt, 14);
            if (loop.presetProgram)
                sqlite3_bind_int(loopStmt, 15, *loop.presetProgram);
            else
                sqlite3_bind_null(loopStmt, 15);
            sqlite3_bind_text(loopStmt, 16, loop.source.c_str(), -1, SQLITE_TRANSIENT);

            if (sqlite3_step(loopStmt) != SQLITE_DONE)
            {
                sqlite3_finalize(loopStmt);
                rollback();
                return false;
            }
            sqlite3_reset(loopStmt);
            sqlite3_clear_bindings(loopStmt);
        }
        if (loopStmt != nullptr)
            sqlite3_finalize(loopStmt);

        const char *programSql = R"SQL(
        INSERT INTO file_preset_programs (
            file_id, ordinal, bank, program, preset_name, zone_count,
            key_low, key_high, velocity_low, velocity_high, root_midi_note, root_tune_cents
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )SQL";
        sqlite3_stmt *programStmt = nullptr;
        if (!rec.presetPrograms.empty() && sqlite3_prepare_v2(db, programSql, -1, &programStmt, nullptr) != SQLITE_OK)
        {
            rollback();
            return false;
        }

        for (size_t i = 0; i < rec.presetPrograms.size(); ++i)
        {
            const auto &program = rec.presetPrograms[i];
            sqlite3_bind_int64(programStmt, 1, fileId);
            sqlite3_bind_int(programStmt, 2, static_cast<int>(i));
            sqlite3_bind_int(programStmt, 3, program.bank);
            sqlite3_bind_int(programStmt, 4, program.program);
            sqlite3_bind_text(programStmt, 5, program.name.c_str(), -1, SQLITE_TRANSIENT);
            if (program.zoneCount)
                sqlite3_bind_int(programStmt, 6, *program.zoneCount);
            else
                sqlite3_bind_null(programStmt, 6);
            if (program.keyLow)
                sqlite3_bind_int(programStmt, 7, *program.keyLow);
            else
                sqlite3_bind_null(programStmt, 7);
            if (program.keyHigh)
                sqlite3_bind_int(programStmt, 8, *program.keyHigh);
            else
                sqlite3_bind_null(programStmt, 8);
            if (program.velocityLow)
                sqlite3_bind_int(programStmt, 9, *program.velocityLow);
            else
                sqlite3_bind_null(programStmt, 9);
            if (program.velocityHigh)
                sqlite3_bind_int(programStmt, 10, *program.velocityHigh);
            else
                sqlite3_bind_null(programStmt, 10);
            if (program.rootMidiNote)
                sqlite3_bind_int(programStmt, 11, *program.rootMidiNote);
            else
                sqlite3_bind_null(programStmt, 11);
            if (program.rootTuneCents)
                sqlite3_bind_double(programStmt, 12, *program.rootTuneCents);
            else
                sqlite3_bind_null(programStmt, 12);

            if (sqlite3_step(programStmt) != SQLITE_DONE)
            {
                sqlite3_finalize(programStmt);
                rollback();
                return false;
            }
            sqlite3_reset(programStmt);
            sqlite3_clear_bindings(programStmt);
        }
        if (programStmt != nullptr)
            sqlite3_finalize(programStmt);

        if (sqlite3_exec(db, "RELEASE sw_catalog_file_upsert;", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            rollback();
            return false;
        }

        return true;
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

    std::vector<int64_t> CatalogDb::listFileIdsNeedingAnalysisByRoot(int64_t rootId,
                                                                      int analysisVersion,
                                                                      int featureVectorVersion)
    {
        SW_DB_GUARD;

        std::vector<int64_t> ids;
        if (db == nullptr)
            return ids;

        const char *sql = R"SQL(
        SELECT f.id
        FROM files f
        LEFT JOIN analysis_results a ON a.file_id = f.id
        WHERE f.root_id = ?
          AND f.index_only = 0
          AND (
              a.file_id IS NULL
              OR a.analysis_version <> ?
              OR a.feature_vector_version <> ?
              OR a.analyzed_modified_time <> f.modified_time
              OR a.analyzed_content_hash IS NOT f.content_hash
              OR (a.status = ? AND a.feature_vector IS NULL)
          )
        ORDER BY f.id
    )SQL";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            logMetadataLoadError(db, "prepare stale-analysis query");
            return ids;
        }

        const bool bindingsOk =
            sqlite3_bind_int64(stmt, 1, rootId) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 2, analysisVersion) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 3, featureVectorVersion) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 4, static_cast<int>(AnalysisStatus::Complete)) == SQLITE_OK;
        if (!bindingsOk)
        {
            logMetadataLoadError(db, "bind stale-analysis query");
            sqlite3_finalize(stmt);
            return ids;
        }

        int rc = SQLITE_OK;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
            ids.push_back(sqlite3_column_int64(stmt, 0));

        if (rc != SQLITE_DONE)
            logMetadataLoadError(db, "read stale-analysis query");
        sqlite3_finalize(stmt);
        return ids;
    }

    bool CatalogDb::upsertAnalysisResult(const AnalysisResultRecord &result)
    {
        SW_DB_GUARD;

        if (db == nullptr)
            return false;

        const auto statusValue = static_cast<int>(result.status);
        if (result.fileId <= 0 || result.analysisVersion <= 0 || result.featureVectorVersion <= 0 ||
            statusValue < static_cast<int>(AnalysisStatus::Complete) ||
            statusValue > static_cast<int>(AnalysisStatus::Unsupported) ||
            (result.status == AnalysisStatus::Complete && result.featureVector.empty()))
        {
            SW_LOG_ERR("CatalogDb: refusing invalid analysis result for file " << result.fileId);
            return false;
        }

        std::vector<unsigned char> featureBytes;
        if (!encodeFeatureVector(result.featureVector, featureBytes))
        {
            SW_LOG_ERR("CatalogDb: invalid feature vector for file " << result.fileId);
            return false;
        }

        const char *sql = R"SQL(
        INSERT INTO analysis_results (
            file_id, analysis_version, analyzed_modified_time, analyzed_content_hash, status,
            detected_bpm, bpm_confidence, detected_key, key_confidence, transient_count,
            onset_density, suggested_loop_start_sample, suggested_loop_end_sample, loop_confidence,
            feature_vector_version, feature_vector, failure_reason, analyzed_at
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(file_id) DO UPDATE SET
            analysis_version = excluded.analysis_version,
            analyzed_modified_time = excluded.analyzed_modified_time,
            analyzed_content_hash = excluded.analyzed_content_hash,
            status = excluded.status,
            detected_bpm = excluded.detected_bpm,
            bpm_confidence = excluded.bpm_confidence,
            detected_key = excluded.detected_key,
            key_confidence = excluded.key_confidence,
            transient_count = excluded.transient_count,
            onset_density = excluded.onset_density,
            suggested_loop_start_sample = excluded.suggested_loop_start_sample,
            suggested_loop_end_sample = excluded.suggested_loop_end_sample,
            loop_confidence = excluded.loop_confidence,
            feature_vector_version = excluded.feature_vector_version,
            feature_vector = excluded.feature_vector,
            failure_reason = excluded.failure_reason,
            analyzed_at = excluded.analyzed_at
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return logMetadataLoadError(db, "prepare analysis-result upsert");

        const auto bindOptionalDouble = [stmt](int index, const std::optional<double> &value)
        {
            return value ? sqlite3_bind_double(stmt, index, *value) == SQLITE_OK
                         : sqlite3_bind_null(stmt, index) == SQLITE_OK;
        };
        const auto bindOptionalInt = [stmt](int index, const std::optional<int> &value)
        {
            return value ? sqlite3_bind_int(stmt, index, *value) == SQLITE_OK
                         : sqlite3_bind_null(stmt, index) == SQLITE_OK;
        };
        const auto bindOptionalInt64 = [stmt](int index, const std::optional<int64_t> &value)
        {
            return value ? sqlite3_bind_int64(stmt, index, *value) == SQLITE_OK
                         : sqlite3_bind_null(stmt, index) == SQLITE_OK;
        };
        const auto bindOptionalText = [stmt](int index, const std::optional<std::string> &value)
        {
            return value ? sqlite3_bind_text(stmt, index, value->c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK
                         : sqlite3_bind_null(stmt, index) == SQLITE_OK;
        };

        const bool bindingsOk =
            sqlite3_bind_int64(stmt, 1, result.fileId) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 2, result.analysisVersion) == SQLITE_OK &&
            sqlite3_bind_int64(stmt, 3, result.analyzedModifiedTime) == SQLITE_OK &&
            bindOptionalText(4, result.analyzedContentHash) &&
            sqlite3_bind_int(stmt, 5, statusValue) == SQLITE_OK &&
            bindOptionalDouble(6, result.detectedBpm) &&
            bindOptionalDouble(7, result.bpmConfidence) &&
            bindOptionalText(8, result.detectedKey) &&
            bindOptionalDouble(9, result.keyConfidence) &&
            bindOptionalInt(10, result.transientCount) &&
            bindOptionalDouble(11, result.onsetDensity) &&
            bindOptionalInt64(12, result.suggestedLoopStartSample) &&
            bindOptionalInt64(13, result.suggestedLoopEndSample) &&
            bindOptionalDouble(14, result.loopConfidence) &&
            sqlite3_bind_int(stmt, 15, result.featureVectorVersion) == SQLITE_OK &&
            (featureBytes.empty()
                 ? sqlite3_bind_null(stmt, 16) == SQLITE_OK
                 : sqlite3_bind_blob(stmt,
                                     16,
                                     featureBytes.data(),
                                     static_cast<int>(featureBytes.size()),
                                     SQLITE_TRANSIENT) == SQLITE_OK) &&
            bindOptionalText(17, result.failureReason) &&
            sqlite3_bind_int64(stmt, 18, result.analyzedAt) == SQLITE_OK;

        if (!bindingsOk)
        {
            logMetadataLoadError(db, "bind analysis-result upsert");
            sqlite3_finalize(stmt);
            return false;
        }

        const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        if (!ok)
            logMetadataLoadError(db, "upsert analysis result");
        sqlite3_finalize(stmt);
        return ok;
    }

    std::optional<AnalysisResultRecord> CatalogDb::analysisResultByFileId(int64_t fileId)
    {
        SW_DB_GUARD;

        if (db == nullptr)
            return std::nullopt;

        const char *sql = R"SQL(
        SELECT file_id, analysis_version, analyzed_modified_time, analyzed_content_hash, status,
               detected_bpm, bpm_confidence, detected_key, key_confidence, transient_count,
               onset_density, suggested_loop_start_sample, suggested_loop_end_sample, loop_confidence,
               feature_vector_version, feature_vector, failure_reason, analyzed_at
        FROM analysis_results
        WHERE file_id = ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            logMetadataLoadError(db, "prepare analysis-result lookup");
            return std::nullopt;
        }

        if (sqlite3_bind_int64(stmt, 1, fileId) != SQLITE_OK)
        {
            logMetadataLoadError(db, "bind analysis-result lookup");
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        const int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW)
        {
            if (rc != SQLITE_DONE)
                logMetadataLoadError(db, "read analysis-result lookup");
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        AnalysisResultRecord result;
        result.fileId = sqlite3_column_int64(stmt, 0);
        result.analysisVersion = sqlite3_column_int(stmt, 1);
        result.analyzedModifiedTime = sqlite3_column_int64(stmt, 2);
        result.analyzedContentHash = readOptionalTextColumn(stmt, 3);

        const int statusValue = sqlite3_column_int(stmt, 4);
        if (statusValue < static_cast<int>(AnalysisStatus::Complete) ||
            statusValue > static_cast<int>(AnalysisStatus::Unsupported))
        {
            SW_LOG_ERR("CatalogDb: invalid analysis status for file " << fileId);
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
        result.status = static_cast<AnalysisStatus>(statusValue);

        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
            result.detectedBpm = sqlite3_column_double(stmt, 5);
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
            result.bpmConfidence = sqlite3_column_double(stmt, 6);
        result.detectedKey = readOptionalTextColumn(stmt, 7);
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
            result.keyConfidence = sqlite3_column_double(stmt, 8);
        if (sqlite3_column_type(stmt, 9) != SQLITE_NULL)
            result.transientCount = sqlite3_column_int(stmt, 9);
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL)
            result.onsetDensity = sqlite3_column_double(stmt, 10);
        if (sqlite3_column_type(stmt, 11) != SQLITE_NULL)
            result.suggestedLoopStartSample = sqlite3_column_int64(stmt, 11);
        if (sqlite3_column_type(stmt, 12) != SQLITE_NULL)
            result.suggestedLoopEndSample = sqlite3_column_int64(stmt, 12);
        if (sqlite3_column_type(stmt, 13) != SQLITE_NULL)
            result.loopConfidence = sqlite3_column_double(stmt, 13);
        result.featureVectorVersion = sqlite3_column_int(stmt, 14);
        if (!readFeatureVectorColumn(stmt, 15, result.featureVector))
        {
            SW_LOG_ERR("CatalogDb: malformed analysis feature vector for file " << fileId);
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
        result.failureReason = readOptionalTextColumn(stmt, 16);
        result.analyzedAt = sqlite3_column_int64(stmt, 17);

        sqlite3_finalize(stmt);
        return result;
    }

    std::vector<FileRecord> CatalogDb::searchFiles(const std::string &query, int limit)
    {
        SW_DB_GUARD;

        std::vector<FileRecord> results;

        // Use FTS5 match for searching
        const char *sql = R"SQL(
        WITH matching_files(file_id) AS (
            SELECT rowid FROM files_fts WHERE files_fts MATCH ?
            UNION
            SELECT p.file_id
            FROM file_preset_programs_fts
            JOIN file_preset_programs p ON p.id = file_preset_programs_fts.rowid
            WHERE file_preset_programs_fts MATCH ?
        )
        SELECT f.id, f.root_id, f.relative_path, f.filename, f.extension,
             f.size_bytes, f.modified_time, f.duration_sec, f.total_samples,
             f.sample_rate, f.channels, f.bit_depth, f.bitrate_kbps, f.codec,
               f.bpm, f.key, f.loop_type, f.acid_root_note, f.acid_beats,
               f.loop_start_sample, f.loop_end_sample, f.index_only, f.slice_count, f.content_hash,
               f.preset_name, f.zone_count, f.key_low, f.key_high, f.velocity_low, f.velocity_high
        FROM files f
        JOIN matching_files m ON m.file_id = f.id
        LIMIT ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            SW_LOG_ERR("CatalogDb: searchFiles prepare failed: " << sqlite3_errmsg(db));
            return results;
        }

        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, limit);

        int rc = SQLITE_OK;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
            results.push_back(readFileRecord(stmt));
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE)
        {
            SW_LOG_ERR("CatalogDb: searchFiles query failed: " << sqlite3_errmsg(db));
            return {};
        }
        if (!hydrateRelatedMetadata(db, results))
            return {};
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
        if (!hydrateRelatedMetadata(db, results))
            return {};
        return results;
    }

    std::vector<FileRecord> CatalogDb::searchFilesByRoot(int64_t rootId, const std::string &query, int limit)
    {
        SW_DB_GUARD;

        std::vector<FileRecord> results;

        const char *sql = R"SQL(
        WITH matching_files(file_id) AS (
            SELECT rowid FROM files_fts WHERE files_fts MATCH ?
            UNION
            SELECT p.file_id
            FROM file_preset_programs_fts
            JOIN file_preset_programs p ON p.id = file_preset_programs_fts.rowid
            WHERE file_preset_programs_fts MATCH ?
        )
        SELECT f.id, f.root_id, f.relative_path, f.filename, f.extension,
             f.size_bytes, f.modified_time, f.duration_sec, f.total_samples,
             f.sample_rate, f.channels, f.bit_depth, f.bitrate_kbps, f.codec,
               f.bpm, f.key, f.loop_type, f.acid_root_note, f.acid_beats,
               f.loop_start_sample, f.loop_end_sample, f.index_only, f.slice_count, f.content_hash,
               f.preset_name, f.zone_count, f.key_low, f.key_high, f.velocity_low, f.velocity_high
        FROM files f
        JOIN matching_files m ON m.file_id = f.id
        WHERE f.root_id = ?
        LIMIT ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return results;

        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, rootId);
        sqlite3_bind_int(stmt, 4, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            results.push_back(readFileRecord(stmt));

        sqlite3_finalize(stmt);
        if (!hydrateRelatedMetadata(db, results))
            return {};
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
        if (!hydrateRelatedMetadata(db, results))
            return {};
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
             preset_name, zone_count, key_low, key_high, velocity_low, velocity_high,
             (SELECT COUNT(*)
              FROM files duplicate_members
              WHERE duplicate_members.content_hash = files.content_hash) AS duplicate_cluster_size
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
        {
            auto file = readFileRecord(stmt);
            file.duplicateClusterSize = sqlite3_column_int(stmt, 30);
            results.push_back(std::move(file));
        }

        sqlite3_finalize(stmt);
        if (!hydrateRelatedMetadata(db, results))
            return {};
        return results;
    }

    std::vector<FileRecord> CatalogDb::findNearDuplicateCandidates(int64_t referenceFileId,
                                                                    std::optional<int64_t> rootId,
                                                                    int limit)
    {
        SW_DB_GUARD;

        std::vector<FileRecord> results;
        if (db == nullptr || referenceFileId <= 0 || limit <= 0)
            return results;

        const auto reference = fileById(referenceFileId);
        if (!reference || reference->indexOnly || !reference->durationSec || !reference->sampleRate ||
            !reference->channels || !std::isfinite(*reference->durationSec) || *reference->durationSec < 0.0 ||
            *reference->sampleRate <= 0 || *reference->channels <= 0)
            return results;

        const double durationTolerance = std::max(0.25, *reference->durationSec * 0.02);
        const int sampleRateMinimum = std::max(1, static_cast<int>(std::floor(*reference->sampleRate * 0.9)));
        const int sampleRateMaximum = static_cast<int>(std::ceil(*reference->sampleRate * 1.1));
        const int resultLimit = std::min(limit, 200);
        constexpr int prefilterLimit = 500;
        const int featureVectorByteCount = static_cast<int>(kCurrentAnalysisFeatureCount * sizeof(float));

        const char *sql = R"SQL(
        WITH anchor AS (
            SELECT f.id, f.root_id, f.modified_time, f.content_hash,
                   f.duration_sec, f.sample_rate, f.channels,
                   EXISTS (
                       SELECT 1
                       FROM file_loop_regions loops
                       WHERE loops.file_id = f.id AND loops.role IN (0, 1)
                   ) AS has_loop
            FROM files f
            WHERE f.id = ? AND f.index_only = 0
        ),
        anchor_analysis AS (
            SELECT a.feature_vector
            FROM analysis_results a
            JOIN anchor ON anchor.id = a.file_id
            WHERE a.analysis_version = ?
              AND a.feature_vector_version = ?
              AND a.status = ?
              AND a.analyzed_modified_time = anchor.modified_time
              AND a.analyzed_content_hash IS anchor.content_hash
              AND length(a.feature_vector) = ?
        )
        SELECT f.id, f.root_id, f.relative_path, f.filename, f.extension,
               f.size_bytes, f.modified_time, f.duration_sec, f.total_samples,
               f.sample_rate, f.channels, f.bit_depth, f.bitrate_kbps, f.codec,
               f.bpm, f.key, f.loop_type, f.acid_root_note, f.acid_beats,
               f.loop_start_sample, f.loop_end_sample, f.index_only, f.slice_count, f.content_hash,
               f.preset_name, f.zone_count, f.key_low, f.key_high, f.velocity_low, f.velocity_high,
               candidate_analysis.feature_vector
        FROM anchor
        JOIN anchor_analysis ON 1 = 1
        JOIN files f ON f.id <> anchor.id
        JOIN analysis_results candidate_analysis ON candidate_analysis.file_id = f.id
        WHERE f.index_only = 0
          AND f.channels = anchor.channels
          AND f.sample_rate BETWEEN ? AND ?
          AND f.duration_sec BETWEEN ? AND ?
          AND (
              anchor.content_hash IS NULL OR anchor.content_hash = ''
              OR f.content_hash IS NULL OR f.content_hash = ''
              OR f.content_hash <> anchor.content_hash
          )
          AND (
              EXISTS (
                  SELECT 1
                  FROM file_loop_regions loops
                  WHERE loops.file_id = f.id AND loops.role IN (0, 1)
              ) = anchor.has_loop
          )
          AND (? = 0 OR f.root_id = ?)
          AND candidate_analysis.analysis_version = ?
          AND candidate_analysis.feature_vector_version = ?
          AND candidate_analysis.status = ?
          AND candidate_analysis.analyzed_modified_time = f.modified_time
          AND candidate_analysis.analyzed_content_hash IS f.content_hash
          AND length(candidate_analysis.feature_vector) = ?
        ORDER BY ABS(f.duration_sec - anchor.duration_sec), f.id
        LIMIT ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            logMetadataLoadError(db, "prepare near-duplicate candidate query");
            return results;
        }

        const auto scopeRootId = rootId.value_or(0);
        const bool bindingsOk =
            sqlite3_bind_int64(stmt, 1, referenceFileId) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 2, kCurrentAnalysisVersion) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 3, kCurrentAnalysisFeatureVectorVersion) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 4, static_cast<int>(AnalysisStatus::Complete)) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 5, featureVectorByteCount) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 6, sampleRateMinimum) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 7, sampleRateMaximum) == SQLITE_OK &&
            sqlite3_bind_double(stmt, 8, std::max(0.0, *reference->durationSec - durationTolerance)) == SQLITE_OK &&
            sqlite3_bind_double(stmt, 9, *reference->durationSec + durationTolerance) == SQLITE_OK &&
            sqlite3_bind_int64(stmt, 10, scopeRootId) == SQLITE_OK &&
            sqlite3_bind_int64(stmt, 11, scopeRootId) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 12, kCurrentAnalysisVersion) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 13, kCurrentAnalysisFeatureVectorVersion) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 14, static_cast<int>(AnalysisStatus::Complete)) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 15, featureVectorByteCount) == SQLITE_OK &&
            sqlite3_bind_int(stmt, 16, prefilterLimit) == SQLITE_OK;
        if (!bindingsOk)
        {
            logMetadataLoadError(db, "bind near-duplicate candidate query");
            sqlite3_finalize(stmt);
            return results;
        }

        std::vector<float> referenceFeatures;
        const auto referenceAnalysis = analysisResultByFileId(referenceFileId);
        if (!referenceAnalysis || referenceAnalysis->status != AnalysisStatus::Complete ||
            referenceAnalysis->analysisVersion != kCurrentAnalysisVersion ||
            referenceAnalysis->featureVectorVersion != kCurrentAnalysisFeatureVectorVersion ||
            referenceAnalysis->analyzedModifiedTime != reference->modifiedTime ||
            referenceAnalysis->analyzedContentHash != reference->contentHash ||
            referenceAnalysis->featureVector.size() != kCurrentAnalysisFeatureCount)
        {
            sqlite3_finalize(stmt);
            return results;
        }
        referenceFeatures = referenceAnalysis->featureVector;
        results.reserve(static_cast<size_t>(std::min(resultLimit, prefilterLimit)));

        int rc = SQLITE_OK;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            std::vector<float> candidateFeatures;
            if (!readFeatureVectorColumn(stmt, 30, candidateFeatures) ||
                candidateFeatures.size() != kCurrentAnalysisFeatureCount)
            {
                SW_LOG_ERR("CatalogDb: malformed feature vector in near-duplicate candidate query");
                continue;
            }

            auto candidate = readFileRecord(stmt);
            candidate.featureMatchScore = featureMatchScore(referenceFeatures, candidateFeatures);
            results.push_back(std::move(candidate));
        }
        if (rc != SQLITE_DONE)
            logMetadataLoadError(db, "read near-duplicate candidate query");
        sqlite3_finalize(stmt);

        std::stable_sort(results.begin(), results.end(), [](const FileRecord &left, const FileRecord &right)
                         {
                             if (left.featureMatchScore != right.featureMatchScore)
                                 return left.featureMatchScore.value_or(0.0) > right.featureMatchScore.value_or(0.0);
                             if (left.filename != right.filename)
                                 return left.filename < right.filename;
                             return left.id < right.id;
                         });
        if (results.size() > static_cast<size_t>(resultLimit))
            results.resize(static_cast<size_t>(resultLimit));

        if (!hydrateRelatedMetadata(db, results))
            return {};
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
        WITH matching_files(file_id) AS (
            SELECT rowid FROM files_fts WHERE files_fts MATCH ?
            UNION
            SELECT p.file_id
            FROM file_preset_programs_fts
            JOIN file_preset_programs p ON p.id = file_preset_programs_fts.rowid
            WHERE file_preset_programs_fts MATCH ?
        )
        SELECT COUNT(*), COALESCE(SUM(f.size_bytes), 0)
        FROM files f
        JOIN matching_files m ON m.file_id = f.id
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return {0, 0};

        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
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

    std::pair<int64_t, int64_t> CatalogDb::fileStatsSearchByRoot(int64_t rootId, const std::string &query)
    {
        SW_DB_GUARD;

        const char *sql = R"SQL(
        WITH matching_files(file_id) AS (
            SELECT rowid FROM files_fts WHERE files_fts MATCH ?
            UNION
            SELECT p.file_id
            FROM file_preset_programs_fts
            JOIN file_preset_programs p ON p.id = file_preset_programs_fts.rowid
            WHERE file_preset_programs_fts MATCH ?
        )
        SELECT COUNT(*), COALESCE(SUM(f.size_bytes), 0)
        FROM files f
        JOIN matching_files m ON m.file_id = f.id
        WHERE f.root_id = ?
    )SQL";

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return {0, 0};

        sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, query.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, rootId);

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
        std::vector<FileRecord> relatedRecords;
        relatedRecords.push_back(std::move(r));
        if (!hydrateRelatedMetadata(db, relatedRecords))
            return std::nullopt;
        return std::move(relatedRecords.front());
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
        std::vector<FileRecord> relatedRecords;
        relatedRecords.push_back(std::move(r));
        if (!hydrateRelatedMetadata(db, relatedRecords))
            return std::nullopt;
        return std::move(relatedRecords.front());
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
        if (!hydrateRelatedMetadata(db, results))
            return {};
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
