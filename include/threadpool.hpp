#pragma once

#include <bits/c++config.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <future>
#include <ratio>
#include <thread>
#include <utility>

#include "deque.hpp"
#include "function2/function2.hpp"
#include "semaphore.hpp"

namespace riften {

// Bind F and args... into nullary lambda
template <typename F, typename... Args> auto bind(F &&f, Args &&...arg) {
    return [f = std::forward<F>(f), ... arg = std::forward<Args>(arg)]() mutable -> decltype(auto) {
        return std::invoke(std::forward<F>(f), std::forward<Args>(arg)...);
    };
}

// Like std::packaged_task<R() &&>, but garantees no type-erasure.
template <std::invocable F> class NullaryOneShot {
  public:
    using result_type = std::invoke_result_t<F>;

    NullaryOneShot(F &&fn) : _fn(std::forward<F>(fn)) {}

    std::future<result_type> get_future() { return _promise.get_future(); }

    void operator()() && {
        if constexpr (!std::is_same_v<void, result_type>) {
            _promise.set_value(std::invoke(std::forward<F>(_fn)));
        } else {
            std::invoke(std::forward<F>(_fn));
            _promise.set_value();
        }
    }

  private:
    std::promise<result_type> _promise;
    F _fn;
};

template <typename F> NullaryOneShot(F &&) -> NullaryOneShot<F>;

class MonoPool {
  public:
    explicit MonoPool(std::size_t threads = std::thread::hardware_concurrency()) : _sem(0) {
        for (std::size_t i = 0; i < threads; ++i) {
            _threads.emplace_back([&](std::stop_token tok) {
                while (!tok.stop_requested() || _in_flight.load(std::memory_order_acquire) > 0) {
                    // Wait to be signalled
                    _sem.acquire();

                    // Spin until the task are done
                    while (_in_flight.load(std::memory_order_acquire) != 0) {
                        if (std::optional one_shot = _tasks.steal()) {
                            // See: https://www.boost.org/doc/libs/1_75_0/doc/html/atomic/usage_examples.html
                            if (_in_flight.fetch_sub(1, std::memory_order_release) == 1) {
                                // Got the last task
                                std::atomic_thread_fence(std::memory_order_acquire);
                                std::invoke(std::move(*one_shot));
                                break;
                            }
                            std::invoke(std::move(*one_shot));
                        }
                    }
                }
            });
        }
    }

    ~MonoPool() {
        for (auto &thread : _threads) {
            thread.request_stop();
        }
        // Must release any waiting threads
        _sem.release(_threads.size());
    }

    template <typename F, typename... Args> auto enqueue(F &&f, Args &&...args) {
        //
        auto task = NullaryOneShot(bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        _tasks.emplace(std::move(task));

        _in_flight.fetch_add(1, std::memory_order_relaxed);

        _sem.release();

        return future;
    }

  private:
    std::atomic<std::int64_t> _in_flight;
    Semaphore _sem;
    Deque<fu2::unique_function<void() &&>> _tasks;
    std::vector<std::jthread> _threads;
};

class MultiPool {
  public:
    explicit MultiPool(std::size_t threads = std::thread::hardware_concurrency()) : _deques(threads) {
        for (std::size_t i = 0; i < threads; ++i) {
            _threads.emplace_back([&, id = i](std::stop_token tok) {
                while (!tok.stop_requested() || _in_flight.load(std::memory_order_acquire) > 0) {
                    // Wait to be signalled
                    _deques[id].sem.acquire();

                    // Spin until the task are done
                    for (std::size_t j = id;; j = (j + 1) % _deques.size()) {
                        if (std::optional one_shot = _deques[j].tasks.steal()) {
                            // See: https://www.boost.org/doc/libs/1_75_0/doc/html/atomic/usage_examples.html
                            if (_in_flight.fetch_sub(1, std::memory_order_release) == 1) {
                                // Got the last task
                                std::atomic_thread_fence(std::memory_order_acquire);
                                std::invoke(std::move(*one_shot));
                                break;
                            }
                            std::invoke(std::move(*one_shot));
                        }
                        if (_in_flight.load(std::memory_order_acquire) == 0) {
                            break;
                        }
                    }
                }
            });
        }
    }

    ~MultiPool() {
        for (auto &t : _threads) {
            t.request_stop();
        }
        for (auto &d : _deques) {
            d.sem.release();
        }

        // for (auto &d : _deques) {
        //     std::cout << d.tasks.empty() << std::endl;
        // }
    }

    template <typename F, typename... Args> auto enqueue(F &&f, Args &&...args) {
        //
        auto task = NullaryOneShot(bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        _deques[count % _deques.size()].tasks.emplace(std::move(task));

        _in_flight.fetch_add(1, std::memory_order_relaxed);

        _deques[count % _deques.size()].sem.release();

        count++;

        return future;
    }

  private:
    struct NamesPair {
        Semaphore sem{0};
        Deque<fu2::unique_function<void() &&>> tasks;
    };

    std::size_t count = 0;

    std::atomic<std::int64_t> _in_flight;
    std::vector<NamesPair> _deques;
    std::vector<std::jthread> _threads;
};

}  // namespace riften
