#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <vector>

namespace sw
{

    /// Represents a configured root folder.
    struct RootRecord
    {
        int64_t id = 0;
        std::string path;  // absolute path on disk
        std::string label; // user-visible label
        bool enabled = true;
    };

    /// Represents a file entry in the catalog.
    struct FileRecord
    {
        int64_t id = 0;
        int64_t rootId = 0;
        std::string relativePath; // path relative to root
        std::string filename;     // leaf name
        std::string extension;    // lowercase, no dot
        int64_t sizeBytes = 0;
        int64_t modifiedTime = 0; // epoch seconds (filesystem mtime)

        // Analysis results (nullable)
        std::optional<double> durationSec;
        std::optional<int> sampleRate;
        std::optional<int> channels;
        std::optional<int> bitDepth;
        std::optional<double> bpm;
        std::optional<std::string> key;
        std::optional<std::string> loopType; // e.g. "acidized", "apple-loop"

        /// True if this format is index-only (REX, NKI, SFZ) and cannot be auditioned.
        bool indexOnly = false;
    };

    /// Preview-settings snapshot stored per preview session or globally.
    struct PreviewSettings
    {
        double pitchSemitones = 0.0;
        bool loopEnabled = false;
        double loopStartSec = 0.0;
        double loopEndSec = 0.0;
        float volume = 1.0f;
    };

    /// Waveform-cache metadata row.
    struct WaveCacheEntry
    {
        int64_t id = 0;
        int64_t fileId = 0;
        std::string cacheKey;  // e.g. hash of (rootId, relPath, size, mtime)
        std::string cachePath; // path to the .peak file on disk
    };

} // namespace sw
