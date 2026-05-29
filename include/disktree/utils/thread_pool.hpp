#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <stdexcept>

namespace disktree::utils {

// ════════════════════════════════════════════════════════════════
// ThreadPool
// A fixed-size pool of worker threads that execute submitted tasks.
//
// Why we write our own instead of using std::async:
//   std::async may spawn a new thread per call (implementation
//   defined). For a scanner that dispatches thousands of directory
//   reads, that means thousands of threads = thrashing.
//   Our pool reuses a fixed set of threads — submit 10,000 tasks,
//   only N threads ever exist at once.
//
// Usage:
//   ThreadPool pool(8);
//   auto future = pool.submit([]{ return do_work(); });
//   auto result = future.get();
//   pool.wait_all();  // drain before reading results
// ════════════════════════════════════════════════════════════════

class ThreadPool {
public:
    explicit ThreadPool(size_t n_threads);
    ~ThreadPool();

    // Submit a callable, returns a future for the result.
    // The callable is executed on one of the worker threads.
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    // Block until all submitted tasks have completed.
    void wait_all();

    size_t thread_count() const { return _workers.size(); }

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    std::vector<std::thread>          _workers;
    std::queue<std::function<void()>> _tasks;
    std::mutex                        _mtx;
    std::condition_variable           _cv;
    std::condition_variable           _cv_done;
    std::atomic<bool>                 _stop{false};
    std::atomic<size_t>               _pending{0};
};

// ── Template implementation ──────────────────────────────────────
// Must be in the header because it's a template.

template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using ReturnType = std::invoke_result_t<F, Args...>;

    // Package the task so we can get a future from it
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<ReturnType> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(_mtx);
        if (_stop.load())
            throw std::runtime_error("ThreadPool: submit after shutdown");

        _pending.fetch_add(1);
        _tasks.push([task]() { (*task)(); });
    }

    _cv.notify_one();
    return result;
}

} // namespace disktree::utils