// Written in 2020 by Conor Williams (cw648@cam.ac.uk)

#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <future>
#include <ratio>
#include <thread>
#include <type_traits>
#include <utility>

#include "function2/function2.hpp"
#include "riften/deque.hpp"
#include "semaphore.hpp"
#include "xoroshiro128**.hpp"

namespace riften {

namespace detail {

// Bind F and args... into a nullary lambda
template <typename... Args, std::invocable<Args...> F> auto bind(F &&f, Args &&...arg) {
    return [f = std::forward<F>(f), ... arg = std::forward<Args>(arg)]() mutable -> decltype(auto) {
        return std::invoke(std::forward<F>(f), std::forward<Args>(arg)...);
    };
}

// Like std::packaged_task<R() &&>, but guarantees no type-erasure.
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

}  // namespace detail

// Lightweight, fast, work-stealing thread-pool for C++20. Built on the lock-free concurrent `riften::Deque`.
// Upon destruction the threadpool blocks until all tasks have been completed and all threads have joined.
class Thiefpool {
  public:
    // Construct a `Thiefpool` with `num_threads` threads.
    explicit Thiefpool(std::size_t num_threads = std::thread::hardware_concurrency()) : _deques(num_threads) {
        for (std::size_t i = 0; i < num_threads; ++i) {
            _threads.emplace_back([&, id = i](std::stop_token tok) {
                jump(id);  // Get a different random stream
                do {
                    // Wait to be signalled
                    _deques[id].sem.acquire_many();

                    do {
                        // Prioritise our work otherwise steal
                        std::size_t t = _deques[id].tasks.empty() ? xoroshiro128() % _deques.size() : id;

                        if (std::optional one_shot = _deques[t].tasks.steal()) {
                            _in_flight.fetch_sub(1, std::memory_order_release);
                            std::invoke(std::move(*one_shot));
                        }

                        // Loop until all the work is done.
                    } while (_in_flight.load(std::memory_order_acquire) > 0);

                } while (!tok.stop_requested());
            });
        }
    }

    // Enqueue callable `f` into the threadpool. It will be called by perfectly forwarding `args...` (unlike
    // `std::async`/`std::bind`/`std::thread`). Returns a `std::future<...>` which does not block upon
    // destruction.
    template <typename... Args, std::invocable<Args...> F> auto enqueue(F &&f, Args &&...args) {
        //
        auto task = detail::NullaryOneShot(detail::bind(std::forward<F>(f), std::forward<Args>(args)...));
        auto future = task.get_future();

        execute(std::move(task));

        return future;
    }

    // Enqueue callable `f` into the threadpool. It will be called by perfectly forwarded `args...` (unlike
    // `std::async`/`std::bind`/`std::thread`). This version does *not* return a handle to the called function
    // and thus only accepts functions which return void.
    template <typename... Args, std::invocable<Args...> F> void enqueue_detach(F &&f, Args &&...args) {
        // Cleaner error message than concept
        static_assert(std::is_same_v<void, std::invoke_result_t<F, Args...>>, "Function must return void.");

        execute(detail::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }

    ~Thiefpool() {
        for (auto &t : _threads) {
            t.request_stop();
        }
        for (auto &d : _deques) {
            d.sem.release();
        }
    }

  private:
    // Fire and forget interface.
    template <std::invocable F> void execute(F &&f) {
        std::size_t i = count++ % _deques.size();

        _in_flight.fetch_add(1, std::memory_order_relaxed);
        _deques[i].tasks.emplace(std::forward<F>(f));
        _deques[i].sem.release();
    }

    struct named_pair {
        Semaphore sem{0};
        Deque<fu2::unique_function<void() &&>> tasks;
    };

    std::atomic<std::int64_t> _in_flight;
    std::size_t count = 0;
    std::vector<named_pair> _deques;
    std::vector<std::jthread> _threads;
};

}  // namespace riften