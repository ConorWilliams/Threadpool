#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>  //temp
#include <ratio>
#include <thread>
#include <type_traits>
#include <utility>

#include "function2/function2.hpp"
#include "riften/deque.hpp"
#include "semaphore.hpp"

namespace riften {

namespace detail {

// Bind F and args... into nullary lambda
template <typename... Args, std::invocable<Args...> F> auto bind(F &&f, Args &&...arg) {
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

}  // namespace detail

class Thiefpool {
  public:
    explicit Thiefpool(std::size_t threads = std::thread::hardware_concurrency()) : _deques(threads) {
        for (std::size_t i = 0; i < threads; ++i) {
            _threads.emplace_back([&, id = i](std::stop_token tok) {
                do {
                    // Wait to be signalled
                    // std::cout << "sleep\n";

                    _deques[id].sem.acquire_all();

                    // std::cout << "wake\n";

                    do {
                        // Try and do the work just sent to this deque
                        for (std::size_t i = 0; i < 1'000; i++) {
                            if (std::optional one_shot = _deques[id].tasks.steal()) {
                                fetch_sub();
                                std::invoke(std::move(*one_shot));
                            }
                        }
                        // Try and steal some work
                        for (std::size_t i = id + 1; i < id + _deques.size(); i++) {
                            if (std::optional one_shot = _deques[i % _deques.size()].tasks.steal()) {
                                fetch_sub();
                                std::invoke(std::move(*one_shot));
                            }
                        }

                    } while (_in_flight.load(std::memory_order_acquire) > 0);

                } while (!tok.stop_requested());
            });
        }
    }

    ~Thiefpool() {
        for (auto &t : _threads) {
            t.request_stop();
        }
        for (auto &d : _deques) {
            d.sem.release();
        }
    }

    template <typename... Args, std::invocable<Args...> F> auto enqueue(F &&f, Args &&...args) {
        //
        auto task = detail::NullaryOneShot(detail::bind(std::forward<F>(f), std::forward<Args>(args)...));

        auto future = task.get_future();

        _in_flight.fetch_add(1, std::memory_order_relaxed);

        _deques[count % _deques.size()].tasks.emplace(std::move(task));

        _deques[count % _deques.size()].sem.release();

        count++;

        return future;
    }

  private:
    struct NamesPair {
        Semaphore sem{0};
        Deque<fu2::unique_function<void() &&>> tasks;
    };

    std::atomic<std::int64_t> _in_flight;
    std::size_t count = 0;
    std::vector<NamesPair> _deques;
    std::vector<std::jthread> _threads;

    // Returns true if did last task
    bool fetch_sub() {
        // https://www.boost.org/doc/libs/1_75_0/doc/html/atomic/usage_examples.html
        if (_in_flight.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }
};

}  // namespace riften
