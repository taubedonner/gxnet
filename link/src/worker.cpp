// SPDX-License-Identifier: MIT
#include "gxnet/link/worker.hpp"

#include <utility>

namespace gxnet::link {

Worker::Worker(std::unique_ptr<Transport> transport) : transport_(std::move(transport)) {
    thread_ = std::thread([this] { run(); });
}

Worker::~Worker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();

    // Anything still queued is dropped rather than run: the destructor is not
    // the place to be calling back into an object that is going away.
}

void Worker::run() {
    thread_error_ = transport_->onThreadStart();

    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) break;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        busy_.store(true);
        job(*transport_);
        connected_.store(transport_->isOpen());
        busy_.store(false);
        pending_.fetch_sub(1);
    }

    transport_->onThreadStop();
}

void Worker::post(Job job, std::function<void()> on_done) {
    if (!job) return;

    Job wrapped = [this, job = std::move(job), on_done = std::move(on_done)](Transport& transport) mutable {
        job(transport);
        if (on_done) {
            std::lock_guard<std::mutex> lock(done_mutex_);
            completions_.push_back(std::move(on_done));
        }
    };

    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(wrapped));
    }
    pending_.fetch_add(1);
    cv_.notify_one();
}

void Worker::request(Request request, std::function<void(LinkResult<Exchange>)> on_done) {
    post([this, request = std::move(request), on_done = std::move(on_done)](Transport& transport) mutable {
        LinkResult<Exchange> result =
            thread_error_ ? LinkResult<Exchange>::fail(thread_error_) : transport.execute(request);
        if (!on_done) return;
        std::lock_guard<std::mutex> lock(done_mutex_);
        completions_.push_back(
            [on_done = std::move(on_done), result = std::move(result)]() mutable { on_done(std::move(result)); });
    });
}

void Worker::open(Endpoint endpoint, std::function<void(LinkError)> on_done) {
    post([this, endpoint = std::move(endpoint), on_done = std::move(on_done)](Transport& transport) mutable {
        LinkError error = thread_error_ ? thread_error_ : transport.open(endpoint);
        if (!on_done) return;
        std::lock_guard<std::mutex> lock(done_mutex_);
        completions_.push_back(
            [on_done = std::move(on_done), error = std::move(error)]() mutable { on_done(std::move(error)); });
    });
}

void Worker::close(std::function<void()> on_done) {
    post([](Transport& transport) { transport.close(); }, std::move(on_done));
}

std::size_t Worker::drain() {
    std::deque<std::function<void()>> ready;
    {
        std::lock_guard<std::mutex> lock(done_mutex_);
        ready.swap(completions_);
    }
    for (auto& completion : ready) completion();
    return ready.size();
}

std::size_t Worker::pending() const { return pending_.load(); }

}  // namespace gxnet::link
