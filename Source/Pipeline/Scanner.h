#pragma once

#include "JobQueue.h"
#include "Catalog/CatalogDb.h"
#include <string>
#include <functional>

namespace sw
{

    /// Scans source directories, discovers audio files, and upserts them into the catalog.
    /// Runs on the Pipeline JobQueue (background threads).
    class Scanner
    {
    public:
        /// Callback fired on the background thread when a file is scanned.
        using ProgressCallback = std::function<void(const std::string &relativePath)>;
        using CompletionCallback = std::function<void()>;

        Scanner(CatalogDb &db, JobQueue &queue);
        ~Scanner() = default;

        /// Enqueue a full scan of the given source.
        void scanRoot(int64_t rootId,
                      const std::string &rootPath,
                      ProgressCallback onProgress = {},
                      CompletionCallback onCompleted = {});

        /// Set of extensions that are playable in MVP.
        static bool isPlayableExtension(const std::string &ext);

        /// Set of extensions that are index-only (REX, NKI, SFZ).
        static bool isIndexOnlyExtension(const std::string &ext);

    private:
        CatalogDb &catalogDb;
        JobQueue &jobQueue;
    };

} // namespace sw
