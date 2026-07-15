// Async tasks + channels, headless: how work leaves the game loop and how results get back.
//
// The pattern this file exists to show:
//
//   1. The loop hits something expensive (here: counting primes in big ranges).
//   2. It submits the work to the JobSystem and keeps a JobHandle — the frame does NOT wait.
//   3. The job sends its result into a Channel when it finishes, on its own thread.
//   4. Every frame, the loop drains the channel with tryReceive — take what has arrived,
//      never block. Results appear over several frames, and that is the point.
//
// No shared state, no mutex in game code, no "is it done yet" flag anyone forgets to reset:
// the channel is the entire coordination surface. Self-checking, exits non-zero on failure.

#include "vortex/core/log.hpp"
#include "vortex/jobs/channel.hpp"
#include "vortex/jobs/job_system.hpp"

#include <cstdio>
#include <thread>

using namespace vortex;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// Deliberately slow-ish: the work must be real enough that the jobs demonstrably run
// while the "frames" tick.
u32 countPrimes(u32 from, u32 to) {
    u32 count = 0;
    for (u32 n = from; n < to; ++n) {
        if (n < 2) continue;
        bool prime = true;
        for (u32 d = 2; d * d <= n; ++d)
            if (n % d == 0) { prime = false; break; }
        if (prime) ++count;
    }
    return count;
}

struct Result {
    u32 chunk  = 0;
    u32 primes = 0;
};

} // namespace

int main() {
    jobs::JobSystem jobSystem;

    std::printf("-- offload: submit chunks, drain the channel frame by frame --\n");
    constexpr u32 kChunks    = 8;
    constexpr u32 kChunkSize = 200000;

    jobs::Channel<Result> results;
    std::vector<jobs::JobHandle> handles;
    for (u32 c = 0; c < kChunks; ++c) {
        handles.push_back(jobSystem.submit([c, &results] {
            results.send({c, countPrimes(c * kChunkSize, (c + 1) * kChunkSize)});
        }));
    }

    // The "game loop": tick frames, take what has arrived, never wait. A real loop would
    // be rendering here; this one just proves it never had to stop.
    u32  received = 0;
    u32  total    = 0;
    bool sawEmptyFrame = false;   // at least one frame should find nothing yet
    u32  frames = 0;
    bool chunkSeen[kChunks] = {};
    while (received < kChunks) {
        ++frames;
        bool gotAny = false;
        while (auto r = results.tryReceive()) {
            gotAny = true;
            ++received;
            total += r->primes;
            check(r->chunk < kChunks && !chunkSeen[r->chunk],
                  "each chunk reports exactly once");
            chunkSeen[r->chunk] = true;
        }
        if (!gotAny) sawEmptyFrame = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::printf("  %u chunks over %u frames, %u primes below %u\n", received, frames,
                total, kChunks * kChunkSize);

    check(received == kChunks, "every result arrived through the channel");
    // 121127 primes below 1.6M — a fixed answer, so a lost or doubled chunk cannot hide.
    check(total == 121127, "the numbers survived the thread hop intact");
    check(sawEmptyFrame || frames > 1, "the loop kept ticking while jobs ran");

    std::printf("-- wait(): the blocking form, for when the frame genuinely needs it --\n");
    for (const jobs::JobHandle& h : handles) jobSystem.wait(h);
    bool allDone = true;
    for (const jobs::JobHandle& h : handles) allDone = allDone && h.done();
    check(allDone, "wait() returns with the job actually finished");
    check(jobs::JobHandle{}.done(), "a null handle counts as done, not as a hang");

    std::printf("-- close(): how a consumer learns 'never' from 'not yet' --\n");
    jobs::Channel<u32> feed;
    check(feed.send(1) && feed.send(2), "sends are accepted while open");
    feed.close();
    check(!feed.send(3), "sends bounce off a closed channel");
    check(feed.receive().value_or(0) == 1 && feed.receive().value_or(0) == 2,
          "closing does not discard what was already sent");
    check(!feed.receive().has_value(), "a drained, closed channel says so");

    std::printf("-- verdict --\n");
    std::printf(g_failures == 0 ? "  all checks passed\n" : "  %d check(s) FAILED\n",
                g_failures);
    return g_failures;
}
