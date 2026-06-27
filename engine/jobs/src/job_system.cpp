#include "vortex/jobs/job_system.hpp"

#include "vortex/core/log.hpp"

namespace vortex::jobs {

JobSystem::JobSystem(u32 workerCount) {
    if (workerCount == 0) {
        const u32 hw = std::thread::hardware_concurrency();
        workerCount = hw > 1 ? hw - 1 : 1;   // leave one thread for the caller
    }
    for (u32 i = 0; i < workerCount; ++i)
        m_workers.emplace_back([this] { workerLoop(); });
    VORTEX_INFO("Jobs", "Started %u worker threads", workerCount);
}

JobSystem::~JobSystem() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (std::thread& t : m_workers)
        if (t.joinable()) t.join();
}

bool JobSystem::tryRunOne() {
    Task task;
    {
        std::lock_guard lock(m_mutex);
        if (m_tasks.empty()) return false;
        task = std::move(m_tasks.front());
        m_tasks.pop();
    }
    task.fn();
    return true;
}

void JobSystem::workerLoop() {
    for (;;) {
        Task task;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
            if (m_stop && m_tasks.empty()) return;
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        task.fn();
    }
}

void JobSystem::parallelFor(usize count, const std::function<void(usize)>& body, usize grain) {
    if (count == 0) return;

    // Small ranges (or no workers) run inline — splitting would cost more than it saves.
    if (m_workers.empty() || count <= grain) {
        for (usize i = 0; i < count; ++i) body(i);
        return;
    }

    const usize chunks = (count + grain - 1) / grain;
    std::atomic<usize> done{0};

    {
        std::lock_guard lock(m_mutex);
        for (usize c = 0; c < chunks; ++c) {
            const usize begin = c * grain;
            const usize end   = begin + grain < count ? begin + grain : count;
            m_tasks.push(Task{[&body, &done, begin, end] {
                for (usize i = begin; i < end; ++i) body(i);
                done.fetch_add(1, std::memory_order_release);
            }});
        }
    }
    m_cv.notify_all();

    // The caller participates instead of blocking idle.
    while (done.load(std::memory_order_acquire) < chunks)
        if (!tryRunOne()) std::this_thread::yield();
}

}
