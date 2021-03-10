#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace cj {

namespace detail {

// Basic wrapper around a c-style array of atomic pointers that provides modulo load/stores
template <typename T> struct RingBuff {
  public:
    explicit RingBuff(std::int64_t cap) : _cap{cap}, _mask{cap - 1}, _data{new std::atomic<T*>[cap]} {}

    std::int64_t capacity() const noexcept { return _cap; }

    // Relaxed store at modulo index
    void store(std::int64_t i, T* x) noexcept { _data[i & _mask].store(x, std::memory_order_relaxed); }

    // Relaxed load at modulo index
    T* load(std::int64_t i) const noexcept { return _data[i & _mask].load(std::memory_order_relaxed); }

    // Allocates and returns a new ring buffer, copies elements in range [b, t) into the new buffer.
    RingBuff<T>* resize(std::int64_t b, std::int64_t t) const;

    // Does not call destructor of items in the ring buffer.
    ~RingBuff() { delete[] _data; }

  private:
    std::int64_t _cap;
    std::int64_t _mask;
    std::atomic<T*>* _data;
};

template <typename T> RingBuff<T>* RingBuff<T>::resize(std::int64_t b, std::int64_t t) const {
    RingBuff<T>* ptr = new RingBuff{2 * _cap};
    for (std::int64_t i = t; i != b; ++i) {
        ptr->store(i, load(i));
    }
    return ptr;
}

}  // namespace detail

// Lock-free single-producer multiple-consumer deque. Only the deque owner can perform pop and push
// operations, while others can steal data from the deque. All threads must have finished using the deque
// before it is destructed. This class implements the deque described in the paper, "Correct and Efficient
// Work-Stealing for Weak Memory Models," available at https://www.di.ens.fr/~zappa/readings/ppopp13.pdf. The
// deque provides the strong exception garantee.
template <typename T> class Deque {
  public:
    // Constructs the queue with a given cap the cap of the queue (must be power of 2)
    explicit Deque(std::int64_t cap = 1024);

    // Destruct the queue, all threads must have finished using the queue.
    ~Deque();

    // Test if empty at instance of call
    bool empty() const noexcept;

    // Emplace an item to the queue Only the owner thread can insert an item to the queue. The operation can
    // trigger the queue to resize its cap if more space is required.
    template <typename... Args> void emplace(Args&&... args);

    // Pops out an item from the queue. Only the owner thread can pop out an item from the queue. The return
    // can be a std::nullopt if this operation failed (empty queue).
    std::optional<T> pop() noexcept(std::is_nothrow_move_constructible_v<T>);

    // Steals an item from the queue Any threads can try to steal an item from the queue. The return can be a
    // std::nullopt if this operation failed (not necessary empty).
    std::optional<T> steal() noexcept(std::is_nothrow_move_constructible_v<T>);

  private:
    std::atomic<std::int64_t> _top;
    std::atomic<std::int64_t> _bottom;

    std::atomic<detail::RingBuff<T>*> _buffer;
    std::vector<detail::RingBuff<T>*> _garbage;

    // Convinience aliases
    static constexpr std::memory_order relaxed = std::memory_order::relaxed;
    static constexpr std::memory_order consume = std::memory_order::consume;
    static constexpr std::memory_order acquire = std::memory_order::acquire;
    static constexpr std::memory_order release = std::memory_order::release;
    static constexpr std::memory_order seq_cst = std::memory_order::seq_cst;
};

template <typename T> Deque<T>::Deque(std::int64_t cap) {
    assert(cap && (!(cap & (cap - 1))));
    _top.store(0, relaxed);
    _bottom.store(0, relaxed);
    _buffer.store(new detail::RingBuff<T>{cap}, relaxed);
    _garbage.reserve(32);
}

template <typename T> Deque<T>::~Deque() {
    for (auto a : _garbage) {
        delete a;
    }
    // Cleans up all remaining items in queue.
    while (pop()) {
    }
    delete _buffer.load();
}

template <typename T> bool Deque<T>::empty() const noexcept {
    int64_t b = _bottom.load(std::memory_order_relaxed);
    int64_t t = _top.load(std::memory_order_relaxed);
    return b <= t;
}

template <typename T> template <typename... Args> void Deque<T>::emplace(Args&&... args) {
    // Construct new object
    T* x = new T(std::forward<Args>(args)...);

    std::int64_t b = _bottom.load(relaxed);
    std::int64_t t = _top.load(acquire);
    detail::RingBuff<T>* a = _buffer.load(relaxed);

    if (a->capacity() - 1 < (b - t)) {
        // Queue is full, build a new one
        try {
            _garbage.push_back(std::exchange(a, a->resize(b, t)));
        } catch (...) {
            // If push_back or resize throws; clean-up and rethrow
            delete x;
            throw;
        }
        _buffer.store(a, relaxed);
    }
    a->store(b, x);

    std::atomic_thread_fence(release);
    _bottom.store(b + 1, relaxed);
}

template <typename T> std::optional<T> Deque<T>::pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
    std::int64_t b = _bottom.load(relaxed) - 1;
    detail::RingBuff<T>* a = _buffer.load(relaxed);

    _bottom.store(b, relaxed);
    std::atomic_thread_fence(seq_cst);
    std::int64_t t = _top.load(relaxed);

    if (t <= b) {
        // Non-empty queue
        T* x = a->load(b);

        if (t == b) {
            // The last item just got stolen
            if (!_top.compare_exchange_strong(t, t + 1, seq_cst, relaxed)) {
                // Failed race.
                _bottom.store(b + 1, relaxed);
                return std::nullopt;
            }
        }
        std::optional<T> tmp{std::move(*x)};
        delete x;
        return tmp;
    } else {
        _bottom.store(b + 1, relaxed);
        return std::nullopt;
    }
}

template <typename T> std::optional<T> Deque<T>::steal() noexcept(std::is_nothrow_move_constructible_v<T>) {
    std::int64_t t = _top.load(acquire);
    std::atomic_thread_fence(seq_cst);
    std::int64_t b = _bottom.load(acquire);

    if (t < b) {
        // Non-empty queue.
        T* x = _buffer.load(consume)->load(t);

        if (!_top.compare_exchange_strong(t, t + 1, seq_cst, relaxed)) {
            // Failed race.
            return std::nullopt;
        }
        std::optional<T> tmp{std::move(*x)};
        delete x;
        return tmp;
    } else {
        return std::nullopt;
    }
}

}  // namespace cj