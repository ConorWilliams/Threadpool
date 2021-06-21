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
#include "xoroshiro128starstar.hpp"

namespace riften {

namespace detail {

// See: https://en.cppreference.com/w/cpp/thread/thread/thread
template <class T> std::decay_t<T> decay_copy(T &&v) { return std::forward<T>(v); }

// Bind F and args... into a nullary one-shot lambda. Lambda captures by value.
template <typename... Args, typename F> auto bind(F &&f, Args &&...args) {
    return [f = decay_copy(std::forward<F>(f)),
            ... args = decay_copy(std::forward<Args>(args))]() mutable -> decltype(auto) {
        return std::invoke(std::move(f), std::move(args)...);
    };
}

// Like std::packaged_task<R() &&>, but guarantees no type-erasure.
template <std::invocable F> class NullaryOneShot {
  public:
    // Stores a copy of the function
    NullaryOneShot(F fn) : _fn(std::move(fn)) {}

    std::future<std::invoke_result_t<F>> get_future() { return _promise.get_future(); }

    void operator()() && {
        try {
            if constexpr (!std::is_same_v<void, std::invoke_result_t<F>>) {
                _promise.set_value(std::invoke(std::move(_fn)));
            } else {
                std::invoke(std::move(_fn));
                _promise.set_value();
            }
        } catch (...) {
            _promise.set_exception(std::current_exception());
        }
    }

  private:
    std::promise<std::invoke_result_t<F>> _promise;
    F _fn;
};

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

                    std::size_t spin = 0;

                    do {
                        // Prioritise our work otherwise steal
                        std::size_t t = spin++ < 100 || !_deques[id].tasks.empty()
                                            ? id
                                            : xoroshiro128() % _deques.size();

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

    // Enqueue callable `f` into the threadpool. Like `std::async`/`std::thread` a copy of `args...` is made,
    // use `std::ref` if you really want a reference. Returns a `std::future<...>` which does not block upon
    // destruction.
    template <typename... Args, typename F>
    [[nodiscard]] std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> enqueue(
        F &&f,
        Args &&...args) {
        //
        auto task = detail::NullaryOneShot(detail::bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        execute(std::move(task));

        return future;
    }

    // Enqueue callable `f` into the threadpool. Like `std::async`/`std::thread` a copy of `args...` is made,
    // use `std::ref` if you really want a reference. This version does *not* return a handle to the called
    // function and thus only accepts functions which return void.
    template <typename... Args, typename F> void enqueue_detach(F &&f, Args &&...args) {
        // Cleaner error message than concept
        static_assert(std::is_same_v<void, std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>,
                      "Function must return void.");

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
