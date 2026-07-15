#pragma once
#include "vortex/core/types.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace vortex::jobs {

// A ticket for one submitted job. Copyable — several places may care whether the same job
// finished. `done()` is a poll for game loops that check once a frame; blocking belongs to
// JobSystem::wait(), which lends the waiting thread to the pool instead of parking it.
class JobHandle {
public:
    JobHandle() = default;

    // A default-constructed handle is "no job", and no job is always done.
    [[nodiscard]] bool done() const {
        return m_state == nullptr || m_state->load(std::memory_order_acquire);
    }
    [[nodiscard]] bool valid() const { return m_state != nullptr; }

private:
    friend class JobSystem;
    std::shared_ptr<std::atomic<bool>> m_state;
};

class JobSystem {
public:
    explicit JobSystem(u32 workerCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&)            = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] u32 workerCount() const { return static_cast<u32>(m_workers.size()); }

    // Run one job on a worker thread. This is the "offload the expensive thing" call —
    // asset decode, pathfinding, chunk generation — while parallelFor is the "split the
    // hot loop" call. Results come back through a Channel (see channel.hpp), not through
    // shared state the job and the game loop would then have to lock around.
    JobHandle submit(std::function<void()> fn);

    // Block until `job` is done — by WORKING, not idling: the calling thread runs queued
    // jobs while it waits, so waiting on a job can never deadlock the pool that has to
    // run it.
    void wait(const JobHandle& job);

    void parallelFor(usize count, const std::function<void(usize)>& body, usize grain = 1024);

private:
    struct Task { std::function<void()> fn; };

    bool tryRunOne();                 // pop + run one queued task, false if none
    void workerLoop();

    std::vector<std::thread>  m_workers;
    std::queue<Task>          m_tasks;
    std::mutex                m_mutex;
    std::condition_variable   m_cv;
    bool                      m_stop = false;
};

}
