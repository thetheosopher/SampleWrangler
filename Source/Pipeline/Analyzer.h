#pragma once

#include "JobQueue.h"
#include "Catalog/CatalogDb.h"
#include <string>

namespace sw
{

    /// Analyses audio files to extract metadata (duration, sample rate, bpm, key, etc.).
    /// Runs as jobs on the Pipeline JobQueue.
    class Analyzer
    {
    public:
        Analyzer(CatalogDb &db, JobQueue &queue);
        ~Analyzer() = default;

        /// Enqueue analysis for a single file by its DB id.
        void analyzeFile(int64_t fileId);

        /// Enqueue analysis for all un-analyzed files in a source.
        void analyzeRoot(int64_t rootId);

    private:
        CatalogDb &catalogDb;
        JobQueue &jobQueue;
    };

} // namespace sw
