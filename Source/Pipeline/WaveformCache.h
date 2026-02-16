#pragma once

#include "JobQueue.h"
#include "Catalog/CatalogDb.h"
#include <string>
#include <vector>

namespace sw
{

    /// Generates and caches peak-overview waveforms for audio files.
    /// Cache key: hash of (root_id, relative_path, size_bytes, modified_time).
    class WaveformCache
    {
    public:
        WaveformCache(CatalogDb &db, JobQueue &queue, const std::string &cacheDir);
        ~WaveformCache() = default;

        /// Request peaks for a file. Returns cached data synchronously if available,
        /// otherwise enqueues generation and calls onReady when done.
        /// onReady is called on a background thread — caller must marshal to message thread.
        using PeaksReadyCallback = std::function<void(int64_t fileId, std::vector<float> peaks)>;

        void requestPeaks(int64_t fileId, PeaksReadyCallback onReady);

        /// Build a cache key string for the given file metadata.
        static std::string buildCacheKey(int64_t rootId, const std::string &relPath,
                                         int64_t sizeBytes, int64_t mtime);

    private:
        CatalogDb &catalogDb;
        JobQueue &jobQueue;
        std::string cacheDirectory;
    };

} // namespace sw
