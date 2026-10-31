#pragma once

#include "JobQueue.h"
#include "Catalog/CatalogDb.h"
#include <atomic>
#include <memory>
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

        /// Queued or running analysis jobs (including root scheduling jobs). Thread-safe.
        int pendingJobCount() const noexcept;

    private:
        struct Progress
        {
            std::atomic<int> pendingJobs{0};
        };

        /// Counts a job as pending until the returned token (captured by the job)
        /// is destroyed, which covers completion, early return, and cancelAll() drops.
        std::shared_ptr<void> makePendingJobToken();

        CatalogDb &catalogDb;
        JobQueue &jobQueue;
        // Shared so tokens in jobs that outlive this Analyzer stay valid.
        std::shared_ptr<Progress> progress = std::make_shared<Progress>();
    };

} // namespace sw
