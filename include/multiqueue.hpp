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
            return tmp;
        } else {
            return std::nullopt;
        }
    }

    // Wait until there is something in the queue or pred returns true
    template <typename Predicate> std::optional<T> pop_wait(Predicate pred) {
        std::unique_lock<std::mutex> lock{_mutex};

        _cv.wait(lock, [&] { return !_queue.empty() || pred(); });

        if (!_queue.empty()) {
            T tmp = std::move(_queue.front());
            _queue.pop();
            return tmp;
        } else {
            return std::nullopt;
        }
    }

    bool empty() {
        std::unique_lock<std::mutex> lock(_mutex);
        return _queue.empty();
    }

    ~Queue() { _cv.notify_all(); }

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
                while (!tok.stop_requested() || !_tasks[id].empty()) {
                    std::optional task = _tasks[id].pop_wait([&] { return tok.stop_requested(); });

                    if (task) {
                        std::invoke(std::move(*task));
                    }

                    for (size_t i = 1; i < _tasks.size(); i++) {
                        std::cout << "stole" << (id + i) % _tasks.size() << std::endl;
                        if (std::optional task = _tasks[(id + i) % _tasks.size()].try_pop()) {
                            //
                            std::invoke(std::move(*task));
                        }
                    }
                }
            });
        }
    }

    ~ThiefPool() {
        for (auto &&thread : _threads) {
            thread.request_stop();
        }
    }

    template <typename F, typename... Args> auto execute(F &&f, Args &&...args) {
        //
        auto task = NullaryOneShot(bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        _tasks[_pos++ % _tasks.size()].emplace(std::move(task));

        return future;
    }

  private:
    std::vector<std::jthread> _threads;

    std::size_t _pos = 0;

    std::vector<Queue<fu2::unique_function<void() &&>>> _tasks;
};
