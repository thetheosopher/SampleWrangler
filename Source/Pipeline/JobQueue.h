#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace sw
{

    /// Priority levels for background jobs.
    enum class JobPriority
    {
        Low,    // idle / bulk scanning
        Normal, // analysis
        High    // user-selected item (on-demand)
    };

    /// A cancellable background job.
    struct Job
    {
        std::function<void(const std::atomic<uint64_t> &cancelGeneration, uint64_t jobGeneration)> work;
        JobPriority priority = JobPriority::Normal;
        uint64_t generation = 0;
    };

    /// Simple thread-pool job queue with cancellation support.
    /// Jobs are processed in priority order (High first).
    class JobQueue
    {
    public:
        explicit JobQueue(int numThreads = 2);
        ~JobQueue();

        JobQueue(const JobQueue &) = delete;
        JobQueue &operator=(const JobQueue &) = delete;

        /// Enqueue a job. Thread-safe.
        void enqueue(Job job);

        /// Request cancellation of all pending & running jobs.
        void cancelAll();

        /// Shutdown the queue (blocks until workers finish).
        void shutdown();

        /// Returns the current cancellation generation.
        uint64_t currentCancelGeneration() const noexcept { return cancelGeneration.load(); }

    private:
        void workerLoop();

        struct PriorityCmp
        {
            bool operator()(const Job &a, const Job &b) const
            {
                return static_cast<int>(a.priority) < static_cast<int>(b.priority);
            }
        };

        std::priority_queue<Job, std::vector<Job>, PriorityCmp> jobs;
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<uint64_t> cancelGeneration{0};
        std::atomic<bool> stopping{false};
        std::vector<std::thread> workers;
    };

} // namespace sw
