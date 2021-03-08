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

#include "function2/function2.hpp"
#include "shared.hpp"

template <typename T> class TaskQueue {
  public:
    template <typename... Args> void emplace(Args&&... args) {
        std::unique_lock<std::mutex> lock(_mutex);
        _tasks.emplace(std::forward<Args>(args)...);
        lock.unlock();
        _cv.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait(lock, [&]() { return !_tasks.empty(); });
        T tmp = std::move(_tasks.front());
        _tasks.pop();
        lock.unlock();
        return tmp;
    }

    template <typename... Args> bool try_emplace(Args&&... args) {
        if (std::unique_lock<std::mutex> lock{_mutex, std::try_to_lock}) {
            _tasks.emplace(std::forward<Args>(args)...);
            lock.unlock();
            _cv.notify_one();
            return true;
        } else {
            return false;
        }
    }

    std::optional<T> try_pop() {
        if (std::unique_lock<std::mutex> lock{_mutex, std::try_to_lock}) {
            T tmp = std::move(_tasks.front());
            _tasks.pop();
            lock.unlock();
            return tmp;
        } else {
            return std::nullopt;
        }
    }

  private:
    std::queue<fu2::unique_function<void() &&>> _tasks;
    std::mutex _mutex;
    std::condition_variable _cv;
};
