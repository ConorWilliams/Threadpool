#pragma once

#include <concepts>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>  // invoke_result
#include <utility>
#include <vector>

#include "shared.hpp"

class ThreadPool {
  public:
    explicit ThreadPool(std::size_t threads = std::thread::hardware_concurrency()) {
        for (std::size_t i = 0; i < threads; ++i) {
            _threads.emplace_back([&]() {
                //
                std::unique_lock<std::mutex> queue_lock(_mutex, std::defer_lock);

                while (true) {
                    queue_lock.lock();

                    _cv.wait(queue_lock, [&]() { return !_tasks.empty() || _stop_threads; });

                    if (_stop_threads && _tasks.empty()) {
                        return;
                    }

                    auto one_shot = std::move(_tasks.front());

                    _tasks.pop();

                    queue_lock.unlock();

                    std::invoke(std::move(one_shot));
                }
            });
        }
    }

    ~ThreadPool() {
        _stop_threads = true;
        _cv.notify_all();

        for (auto &thread : _threads) {
            thread.join();
        }
    }

    template <typename F, typename... Args> auto execute(F &&f, Args &&...args) {
        //
        auto task = NullaryOneShot(bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        {
            std::unique_lock<std::mutex> queue_lock(_mutex);

            _tasks.emplace(std::move(task));
        }

        _cv.notify_one();

        return future;
    }

  private:
    std::vector<std::thread> _threads;
    std::queue<fu2::unique_function<void() &&>> _tasks;
    std::mutex _mutex;
    std::condition_variable _cv;
    std::atomic<bool> _stop_threads = false;
};
