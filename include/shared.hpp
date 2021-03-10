#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <future>
#include <utility>

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

class AtomicCounter {
  public:
    constexpr AtomicCounter() noexcept : _count(0) {}

    // Increase count by 1 and returns the previous value of the count.
    std::int64_t operator++(int) noexcept { return _count.fetch_add(1, std::memory_order_relaxed); }

    // Decrement a reference count, and return whether the result is non-zero.
    // Insert barriers to ensure that state written before the reference count
    // became zero will be visible to a thread that has just made the count zero.
    bool Decrement() noexcept {
        if (_count.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
        // TODO(jbroman): Technically this doesn't need to be an acquire operation
        // unless the result is 1 (i.e., the ref count did indeed reach zero).
        // However, there are toolchain issues that make that not work as well at
        // present (notably TSAN doesn't like it).
        return _count.fetch_sub(1, std::memory_order_acq_rel) != 1;
    }
    // Return whether the reference count is one.  If the reference count is used
    // in the conventional way, a refrerence count of 1 implies that the current
    // thread owns the reference and no other thread shares it.  This call
    // performs the test for a reference count of one, and performs the memory
    // barrier needed for the owning thread to act on the object, knowing that it
    // has exclusive access to the object.

  private:
    std::atomic<std::int64_t> _count;
};