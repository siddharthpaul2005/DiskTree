#include "disktree/utils/thread_pool.hpp"

namespace disktree::utils {

ThreadPool::ThreadPool(size_t n_threads) {
    for (size_t i = 0; i < n_threads; ++i) {
        _workers.emplace_back([this] {
            // Each worker runs this loop forever until _stop is set
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(_mtx);

                    // Wait until there's a task OR we're stopping
                    _cv.wait(lock, [this] {
                        return _stop.load() || !_tasks.empty();
                    });

                    // If stopping and no tasks left, exit thread
                    if (_stop.load() && _tasks.empty())
                        return;

                    // Grab the next task
                    task = std::move(_tasks.front());
                    _tasks.pop();
                }

                // Execute outside the lock so other threads can grab tasks
                task();

                // Decrement pending count and notify wait_all().
                // Take the lock before notifying: otherwise wait_all()
                // could check the predicate, get preempted, miss this
                // notification and sleep forever (lost wake-up).
                if (_pending.fetch_sub(1) == 1) {
                    std::lock_guard<std::mutex> lock(_mtx);
                    _cv_done.notify_all();
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        // Set the flag under the lock: a worker that has just checked the
        // wait predicate but not yet gone to sleep would otherwise miss the
        // notification below and block forever, hanging join().
        std::lock_guard<std::mutex> lock(_mtx);
        _stop.store(true);
    }
    _cv.notify_all();      // wake all workers so they can exit
    for (auto& w : _workers)
        if (w.joinable()) w.join();
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(_mtx);
    _cv_done.wait(lock, [this] {
        return _pending.load() == 0;
    });
}

} // namespace disktree::utils