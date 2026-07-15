#pragma once
#include "vortex/core/types.hpp"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace vortex::jobs {

// The way a background job talks to the game loop.
//
// A job that computes something has to put the result SOMEWHERE, and "a member the job
// writes and the loop reads" is a data race with extra steps — it works until the day the
// loop reads a half-written struct. A channel makes the hand-off the explicit, owned
// operation: the job sends, the loop drains, and neither ever holds a reference into the
// other's memory.
//
// Multi-producer, multi-consumer, unbounded. The game-loop side should use tryReceive —
// a frame drains whatever has arrived and moves on; blocking receive() is for dedicated
// consumer threads. close() is how a producer says "no more is coming", which is the only
// way a blocked consumer can ever learn the difference between "not yet" and "never".
template <typename T>
class Channel {
public:
    Channel() = default;
    Channel(const Channel&)            = delete;
    Channel& operator=(const Channel&) = delete;

    // False if the channel is closed — the value is NOT queued. Sending into a closed
    // channel is a normal shutdown race, not an error worth crashing over.
    bool send(T value) {
        {
            std::lock_guard lock(m_mutex);
            if (m_closed) return false;
            m_queue.push(std::move(value));
        }
        m_cv.notify_one();
        return true;
    }

    // The next value if one is already there. This is the game-loop call: check, take,
    // never wait.
    [[nodiscard]] std::optional<T> tryReceive() {
        std::lock_guard lock(m_mutex);
        if (m_queue.empty()) return std::nullopt;
        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }

    // Block until a value arrives or the channel is closed AND drained — closing does not
    // discard what was already sent; a consumer sees every value before it sees the end.
    [[nodiscard]] std::optional<T> receive() {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return m_closed || !m_queue.empty(); });
        if (m_queue.empty()) return std::nullopt;   // closed and drained
        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }

    // No further sends will be accepted; blocked receivers wake. Values already queued
    // remain receivable.
    void close() {
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard lock(m_mutex);
        return m_closed;
    }

    [[nodiscard]] usize size() const {
        std::lock_guard lock(m_mutex);
        return m_queue.size();
    }

private:
    mutable std::mutex      m_mutex;
    std::condition_variable m_cv;
    std::queue<T>           m_queue;
    bool                    m_closed = false;
};

}
