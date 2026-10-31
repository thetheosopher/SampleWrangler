#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>

namespace sw
{

    enum class LoopRegionRole : int
    {
        Unspecified = 0,
        Sustain = 1,
        Release = 2
    };

    enum class LoopMode : int
    {
        Unknown = 0,
        Forward = 1,
        Alternating = 2,
        Backward = 3
    };

    /// A loop keeps its source endpoint convention; renderers can normalize it
    /// at the boundary instead of losing format-specific endpoint semantics.
    struct LoopRegionRecord
    {
        int index = 0;
        LoopRegionRole role = LoopRegionRole::Unspecified;
        LoopMode mode = LoopMode::Unknown;
        std::optional<uint32_t> rawMode;
        int64_t startSample = 0;
        int64_t endSample = 0;
        bool endInclusive = false;
        std::optional<uint32_t> fractionalPosition;
        std::optional<uint32_t> playCount;
        std::optional<uint32_t> sourceId;
        std::optional<uint16_t> startMarkerId;
        std::optional<uint16_t> endMarkerId;
        std::optional<int> presetBank;
        std::optional<int> presetProgram;
        std::string source;

        std::optional<int64_t> previewEndSampleInclusive() const noexcept
        {
            if (endInclusive)
                return endSample;
            if (endSample <= startSample)
                return std::nullopt;
            return endSample - 1;
        }
    };

    /// One MIDI bank/program entry inside a preset-bank file such as SF2.
    struct PresetProgramRecord
    {
        int ordinal = 0;
        int bank = 0;
        int program = 0;
        std::string name;
        std::optional<int> zoneCount;
        std::optional<int> keyLow;
        std::optional<int> keyHigh;
        std::optional<int> velocityLow;
        std::optional<int> velocityHigh;
        std::optional<int> rootMidiNote;
        std::optional<double> rootTuneCents;
    };

    enum class AnalysisStatus : int
    {
        Complete = 1,
        Failed = 2,
        Unsupported = 3
    };

    inline constexpr int kCurrentAnalysisVersion = 2;
    inline constexpr int kCurrentAnalysisFeatureVectorVersion = 1;
    inline constexpr std::size_t kCurrentAnalysisFeatureCount = 4;

    /// Persisted, versioned worker-thread analysis for one catalog file.
    struct AnalysisResultRecord
    {
        int64_t fileId = 0;
        int analysisVersion = 0;
        int64_t analyzedModifiedTime = 0;
        std::optional<std::string> analyzedContentHash;
        AnalysisStatus status = AnalysisStatus::Complete;
        std::optional<double> detectedBpm;
        std::optional<double> bpmConfidence;
        std::optional<std::string> detectedKey;
        std::optional<double> keyConfidence;
        std::optional<int> transientCount;
        std::optional<double> onsetDensity;
        std::optional<int64_t> suggestedLoopStartSample;
        std::optional<int64_t> suggestedLoopEndSample;
        std::optional<double> loopConfidence;
        int featureVectorVersion = kCurrentAnalysisFeatureVectorVersion;
        // Version 1 is [duration seconds, RMS, peak, zero-crossing rate].
        std::vector<float> featureVector;
        std::optional<std::string> failureReason;
        int64_t analyzedAt = 0;
    };

    /// Represents a configured source folder.
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
        std::string relativePath; // path relative to source
        std::string filename;     // leaf name
        std::string extension;    // lowercase, no dot
        int64_t sizeBytes = 0;
        int64_t modifiedTime = 0; // epoch seconds (filesystem mtime)

        // Analysis results (nullable)
        std::optional<double> durationSec;
        std::optional<int64_t> totalSamples;
        std::optional<int> sampleRate;
        std::optional<int> channels;
        std::optional<int> bitDepth;
        std::optional<int> bitrateKbps;
        std::optional<std::string> codec;
        std::optional<double> bpm;
        std::optional<std::string> key;
        std::optional<int> rootMidiNote;
        std::optional<double> rootTuneCents;
        std::optional<std::string> rootPitchSource;
        std::optional<std::string> metadataSource;
        std::optional<double> inferredBpm;
        std::optional<double> inferredBpmConfidence;
        std::optional<std::string> inferredKey;
        std::optional<double> inferredKeyConfidence;
        std::optional<int> inferredTransientCount;
        std::optional<double> inferredOnsetDensity;
        std::optional<int64_t> suggestedLoopStartSample;
        std::optional<int64_t> suggestedLoopEndSample;
        std::optional<double> suggestedLoopConfidence;
        std::optional<int> acidBeats;
        std::vector<LoopRegionRecord> loopRegions;
        std::optional<int> sliceCount;          // REX/RX2 slice count
        std::optional<std::string> contentHash; // sampled content fingerprint for duplicate detection
        std::optional<std::string> presetName;  // indexed-preset display name when present
        std::optional<int> zoneCount;           // sampler zone/region count for indexed presets
        std::optional<int> keyLow;              // lowest covered MIDI key for indexed presets
        std::optional<int> keyHigh;             // highest covered MIDI key for indexed presets
        std::optional<int> velocityLow;         // lowest covered MIDI velocity for indexed presets
        std::optional<int> velocityHigh;        // highest covered MIDI velocity for indexed presets
        std::vector<PresetProgramRecord> presetPrograms;

        /// True if this format is index-only (for example REX without the SDK, SFZ, or DecentSampler) and cannot be auditioned.
        bool indexOnly = false;

        // Query-only discovery details; these are not persisted in the files table.
        int duplicateClusterSize = 0;
        std::optional<double> featureMatchScore;

        const LoopRegionRecord *previewLoop() const noexcept
        {
            for (const auto &loop : loopRegions)
                if (loop.role == LoopRegionRole::Sustain && loop.mode == LoopMode::Forward)
                    return &loop;

            for (const auto &loop : loopRegions)
                if (loop.role == LoopRegionRole::Unspecified && loop.mode == LoopMode::Forward)
                    return &loop;

            return nullptr;
        }
    };

    /// User-authored metadata that should survive rescans.
    struct FileUserDataRecord
    {
        int64_t fileId = 0;
        bool isFavorite = false;
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
