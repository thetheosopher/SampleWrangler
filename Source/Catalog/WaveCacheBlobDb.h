#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace sw
{

    class WaveCacheBlobDb
    {
    public:
        WaveCacheBlobDb() = default;
        ~WaveCacheBlobDb();

        WaveCacheBlobDb(const WaveCacheBlobDb &) = delete;
        WaveCacheBlobDb &operator=(const WaveCacheBlobDb &) = delete;

        bool open(const std::string &dbPath);
        void close();
        bool isOpen() const noexcept;

        bool upsertPeaksByKey(const std::string &cacheKey,
                              int64_t fileId,
                              const std::vector<float> &peaks);

        std::optional<std::vector<float>> peaksByKey(const std::string &cacheKey);
        bool removeByFileIds(const std::vector<int64_t> &fileIds);
        bool vacuum();

    private:
        sqlite3 *db = nullptr;
        mutable std::recursive_mutex apiMutex;
    };

} // namespace sw
