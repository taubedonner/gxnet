// SPDX-License-Identifier: MIT
#ifndef GXNET_LINK_WORKER_HPP
#define GXNET_LINK_WORKER_HPP

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "gxnet/link/transport.hpp"

namespace gxnet::link {

/// Drives a transport from one dedicated thread.
///
/// Every BCS call blocks: a read on a busy line takes as long as it takes, and
/// a timeout takes the whole timeout. Doing that on the UI thread would freeze
/// the window for seconds at a stretch, so the transport lives here instead and
/// is only ever touched from this one thread -- which also satisfies COM's
/// apartment rule without any locking around the object itself.
///
/// Results do not come back on that thread. They queue up, and `drain()` runs
/// the completions on whichever thread calls it. Call it once per frame and
/// every callback lands on the UI thread, where it can touch UI state freely:
///
///     worker.request(req, [&](LinkResult<Exchange> result) {
///         last_reply = std::move(result);   // safe: runs inside drain()
///     });
///     ...
///     worker.drain();                       // once per frame
class Worker {
public:
    /// Work to run on the transport thread.
    using Job = std::function<void(Transport&)>;

    explicit Worker(std::unique_ptr<Transport> transport);
    ~Worker();

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    /// Queues arbitrary work against the transport. `on_done` is optional and
    /// runs during `drain()`.
    void post(Job job, std::function<void()> on_done = {});

    /// Queues a request. The result is delivered during `drain()`.
    void request(Request request, std::function<void(LinkResult<Exchange>)> on_done);

    /// Convenience wrappers with the same delivery rule.
    void open(Endpoint endpoint, std::function<void(LinkError)> on_done);
    void close(std::function<void()> on_done = {});

    /// Runs queued completions on the calling thread. Returns how many ran.
    std::size_t drain();

    /// Requests still queued or in flight.
    [[nodiscard]] std::size_t pending() const;
    /// True while a job is actually executing.
    [[nodiscard]] bool busy() const { return busy_.load(); }

    /// Snapshot of the last known connection state. Kept as an atomic copy so
    /// the UI can read it without waiting for the transport thread.
    [[nodiscard]] bool connected() const { return connected_.load(); }

    /// Direct access, valid only from inside a Job.
    [[nodiscard]] Transport& transport() { return *transport_; }

private:
    void run();

    std::unique_ptr<Transport> transport_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    bool stopping_ = false;

    mutable std::mutex done_mutex_;
    std::deque<std::function<void()>> completions_;

    std::atomic<std::size_t> pending_{0};
    std::atomic<bool> busy_{false};
    std::atomic<bool> connected_{false};

    /// Failure of onThreadStart, surfaced on the first job instead of being
    /// swallowed on a thread nobody is watching.
    LinkError thread_error_;

    std::thread thread_;
};

}  // namespace gxnet::link

#endif  // GXNET_LINK_WORKER_HPP
