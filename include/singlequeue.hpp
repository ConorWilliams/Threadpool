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

#include "function2/function2.hpp"

// Bind F and args... into nullary lambda
template <typename F, typename... Args> auto bind(F &&f, Args &&...arg) {
    return [f = std::forward<F>(f), ... arg = std::forward<Args>(arg)]() mutable -> decltype(auto) {
        return std::invoke(std::forward<F>(f), std::forward<Args>(arg)...);
    };
}

// Like std::packaged_task<R() &&>, but garantees no type erasure.
template <std::invocable F> class NullaryOneShot {
  public:
    using invoke_result_t = std::invoke_result_t<F>;

    NullaryOneShot(F &&fn) : _fn(std::forward<F>(fn)) {}

    void operator()() && {
        if constexpr (!std::is_same_v<void, invoke_result_t>) {
            _promise.set_value(std::invoke(std::forward<F>(_fn)));
        } else {
            std::invoke(std::forward<F>(_fn));
            _promise.set_value();
        }
    }

    std::future<invoke_result_t> get_future() { return _promise.get_future(); }

  private:
    F _fn;
    std::promise<invoke_result_t> _promise;
};

template <typename F> NullaryOneShot(F &&) -> NullaryOneShot<F>;

class ThreadPool {
  public:
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency());

    ~ThreadPool();

    template <typename F, typename... Args> auto execute(F &&f, Args &&...args) {
        //
        auto task = NullaryOneShot(bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();
        //
        std::unique_lock<std::mutex> queue_lock(_task_mutex, std::defer_lock);

        queue_lock.lock();

        _tasks.emplace(std::move(task));

        queue_lock.unlock();

        _task_cv.notify_one();

        return future;
    }

  private:
    std::vector<std::thread> _threads;
    std::queue<fu2::unique_function<void() &&>> _tasks;
    std::mutex _task_mutex;
    std::condition_variable _task_cv;
    bool _stop_threads = false;
};
