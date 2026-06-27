#pragma once
#include "vortex/core/types.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace vortex::jobs {

class JobSystem {
public:
    explicit JobSystem(u32 workerCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&)            = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] u32 workerCount() const { return static_cast<u32>(m_workers.size()); }

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
