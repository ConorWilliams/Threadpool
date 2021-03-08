#pragma once

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

#include "shared.hpp"

template <typename T> class TaskQueue {
  public:
    template <typename... Args> void emplace(Args&&... args) {
        std::unique_lock<std::mutex> lock(_mutex);
        _tasks.emplace(std::forward<Args>(args)...);
        lock.unlock();
        _cv.notify_one();
    }

    template <typename Predicate> std::optional<T> pop(Predicate pred) {
        // Aquire lock
        std::unique_lock<std::mutex> lock(_mutex);

        // Wait untill there is a task OR predicate returns true
        _cv.wait(lock, [&]() { return !_tasks.empty() || pred(); });

        if (!_tasks.empty()) {
            T tmp = std::move(_tasks.front());
            _tasks.pop();
            lock.unlock();
            return tmp;
        } else {
            return std::nullopt;
        }
    }

    // template <typename... Args> bool try_emplace(Args&&... args) {
    //     if (std::unique_lock<std::mutex> lock{_mutex, std::try_to_lock}) {
    //         _tasks.emplace(std::forward<Args>(args)...);
    //         lock.unlock();
    //         _cv.notify_one();
    //         return true;
    //     } else {
    //         return false;
    //     }
    // }

    // std::optional<T> try_pop() {
    //     if (std::unique_lock<std::mutex> lock{_mutex, std::try_to_lock}) {
    //         T tmp = std::move(_tasks.front());
    //         _tasks.pop();
    //         lock.unlock();
    //         return tmp;
    //     } else {
    //         return std::nullopt;
    //     }
    // }

  private:
    std::queue<T> _tasks;
    std::mutex _mutex;
    std::condition_variable _cv;
};
