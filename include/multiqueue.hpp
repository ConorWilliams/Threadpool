#pragma once

#include <bits/c++config.h>

#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>  // invoke_result
#include <utility>
#include <vector>

#include "function2/function2.hpp"
#include "shared.hpp"

template <typename T> class Queue {
  public:
    template <typename... Args> void emplace(Args &&...args) {
        std::unique_lock<std::mutex> lock(_mutex);
        _queue.emplace(std::forward<Args>(args)...);
        lock.unlock();

        _cv.notify_one();
    }

    std::optional<T> try_pop() {
        if (std::unique_lock<std::mutex> lock{_mutex, std::try_to_lock}) {
            T tmp = std::move(_queue.front());
            _queue.pop();
            lock.unlock();
            return tmp;
        } else {
            return std::nullopt;
        }
    }

    // Wait until there is something in the queue
    std::optional<T> wait_pop() {
        std::unique_lock<std::mutex> lock{_mutex};

        _cv.wait(lock, [&] { return !_queue.empty(); });

        if (!_queue.empty()) {
            T tmp = std::move(_queue.front());
            _queue.pop();
            lock.unlock();
            return tmp;
        } else {
            return std::nullopt;
        }
    }

    void release_waiting() { _cv.notify_all(); }

    ~Queue() { release_waiting(); }

  private:
    std::queue<T> _queue;
    std::mutex _mutex;
    std::condition_variable _cv;
};

class ThiefPool {
  public:
    explicit ThiefPool(std::size_t threads = std::thread::hardware_concurrency())
        : _tasks(threads) {
        for (std::size_t i = 0; i < threads; ++i) {
            _threads.emplace_back([&, id = i](std::stop_token tok) {
                //
                while (!tok.stop_requested()) {
                    if (std::optional task = _tasks[id].wait_pop()) {
                        std::invoke(std::move(*task));
                    }
                    for (size_t i = 0; i < _tasks.size(); i++) {
                        if (std::optional task = _tasks[(id + i) % _tasks.size()].try_pop()) {
                            std::invoke(std::move(*task));
                        }
                    }
                }
            });
        }
    }
    template <typename F, typename... Args> auto execute(F &&f, Args &&...args) {
        //
        auto task = NullaryOneShot(bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        std::cout << "emplace at " << _pos % _tasks.size() << std::endl;

        _tasks[_pos % _tasks.size()].emplace(std::move(task));

        return future;
    }

  private:
    std::vector<Queue<fu2::unique_function<void() &&>>> _tasks;
    std::vector<std::jthread> _threads;

    std::size_t _pos = 0;
};
