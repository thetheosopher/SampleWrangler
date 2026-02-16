#include "JobQueue.h"

namespace sw
{

    JobQueue::JobQueue(int numThreads)
    {
        workers.reserve(static_cast<size_t>(numThreads));
        for (int i = 0; i < numThreads; ++i)
            workers.emplace_back(&JobQueue::workerLoop, this);
    }

    JobQueue::~JobQueue()
    {
        shutdown();
    }

    void JobQueue::enqueue(Job job)
    {
        {
            std::lock_guard lock(mutex);
            job.generation = cancelGeneration.load(std::memory_order_relaxed);
            jobs.push(std::move(job));
        }
        cv.notify_one();
    }

    void JobQueue::cancelAll()
    {
        cancelGeneration.fetch_add(1, std::memory_order_relaxed);
        // Drain queue
        {
            std::lock_guard lock(mutex);
            while (!jobs.empty())
                jobs.pop();
        }
    }

    void JobQueue::shutdown()
    {
        stopping.store(true);
        cancelGeneration.fetch_add(1, std::memory_order_relaxed);
        cv.notify_all();

        for (auto &t : workers)
        {
            if (t.joinable())
                t.join();
        }
        workers.clear();
    }

    void JobQueue::workerLoop()
    {
        while (!stopping.load())
        {
            Job job;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [this]
                        { return stopping.load() || !jobs.empty(); });

                if (stopping.load())
                    return;
                if (jobs.empty())
                    continue;

                job = std::move(const_cast<Job &>(jobs.top()));
                jobs.pop();
            }

            if (job.work)
                job.work(cancelGeneration, job.generation);
        }
    }

} // namespace sw
