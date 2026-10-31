#pragma once

#include "JobQueue.h"
#include "Catalog/CatalogDb.h"
#include <string>

namespace sw
{

    /// Computes and persists versioned offline analysis features on the Pipeline JobQueue.
    class Analyzer
    {
    public:
        Analyzer(CatalogDb &db, JobQueue &queue);
        ~Analyzer() = default;

        /// Enqueue analysis for a single file by its DB id.
        void analyzeFile(int64_t fileId);

        /// Enqueue analysis for files whose saved analysis is missing or stale.
        void analyzeRoot(int64_t rootId);

    private:
        CatalogDb &catalogDb;
        JobQueue &jobQueue;
    };

} // namespace sw
