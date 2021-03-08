#include "singlequeue.hpp"

ThreadPool::ThreadPool(size_t threads) {
    for (size_t i = 0; i < threads; ++i) {
        _threads.emplace_back(std::thread([&]() {
            std::unique_lock<std::mutex> queue_lock(_task_mutex, std::defer_lock);

            while (true) {
                queue_lock.lock();

                _task_cv.wait(queue_lock, [&]() { return !_tasks.empty() || _stop_threads; });

                if (_stop_threads && _tasks.empty()) {
                    return;
                }

                auto one_shot = std::move(_tasks.front());

                _tasks.pop();

                queue_lock.unlock();

                std::invoke(std::move(one_shot));
            }
        }));
    }
}

ThreadPool::~ThreadPool() {
    _stop_threads = true;
    _task_cv.notify_all();

    for (auto &thread : _threads) {
        thread.join();
    }
}
